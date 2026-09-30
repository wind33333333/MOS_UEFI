// ========================= [ time_core.c ] =========================
#include "time_core.h"
#include "../x64/interrupt.h"
#include "printk.h"
#include "../x64/cpu.h"

timekeeper_t   g_timekeeper;
static clocksource_t *g_cs_list[8];
static uint32         g_cs_count = 0;

static clockevent_t  *g_ce_list[8];
static uint32         g_ce_count = 0;

uint8 g_sys_timer_vector = 0; // 全局统一定时器中断号


// 内部辅助：读取指定时钟源的当前计数值
static inline uint64 cs_read_cycles(clocksource_t *cs) {
    return (cs->is_tsc == TRUE) ? asm_rdtscp() : cs->read(cs);
}

// =========================================================================
// 获取系统单调运行时间 (纳秒) —— 直接通过局部快照 cs 访问静态属性
// =========================================================================
uint64 get_uptime_ns(void) {
    uint32 seq;
    uint64 base, last, now_cycles;
    clocksource_t *cs;

    do {
        seq = g_timekeeper.seq;
        base = g_timekeeper.base_ns;
        last = g_timekeeper.cycle_last;
        cs   = g_timekeeper.active_cs;
        now_cycles = cs_read_cycles(cs);
    } while (__builtin_expect((seq & 1U) || (g_timekeeper.seq != seq), 0));

    // 循环退出后，cs 指针已确保与 base、last 严格对应，直接解引用 cs 的只读常数即可！
    uint64 delta_cycles = (now_cycles - last) & cs->mask;
    uint64 delta_ns     = (uint64)(((__uint128_t)delta_cycles * cs->mult) >> cs->shift);

    return base + delta_ns;
}

// =========================================================================
//泛化的绝对时间逆推引擎
// =========================================================================
boolean time_core_try_abs_ns_to_cycles(uint64 target_ns, uint32 required_cs_id, uint64 *out_target_cycles) {
    uint32 seq;
    uint64 base_ns, last_cycles;
    clocksource_t *cs;

    do {
        seq = g_timekeeper.seq;
        base_ns = g_timekeeper.base_ns;
        last_cycles = g_timekeeper.cycle_last;
        cs = g_timekeeper.active_cs;
    } while (__builtin_expect((seq & 1U) || (g_timekeeper.seq != seq), 0));

    // 🌟 异构系统拦截：如果全局时钟源不是你这个驱动期望的硬件，拒绝转换！
    if (cs->id != required_cs_id) {
        return FALSE;
    }

    // --- 同构系统纯数学魔法 ---
    if (__builtin_expect(target_ns <= base_ns, 0)) {
        *out_target_cycles = last_cycles;
        return TRUE;
    }

    uint64 delta_ns = target_ns - base_ns;

    // 逆向公式：这里用的 cs->shift 和 cs->mult
    // 会自动适配当前激活的是 TSC 的参数，还是 HPET 的参数！
    uint64 delta_cycles = (uint64)(((__uint128_t)delta_ns << cs->shift) / cs->mult);

    *out_target_cycles = last_cycles + delta_cycles;
    return TRUE;
}

// =========================================================================
// 通用定点数参数计算器 (仅在开机时调用，自动寻找精度最高的 mult 和 shift)
// =========================================================================
static void calc_mult_shift(uint64 from_hz,uint64 to_hz,uint64 *out_mult,uint32 *out_shift,boolean round_up) {
    uint32 shift = 62;
    uint64 mult = 0;

    // 从高到低搜索最大的 shift，使 mult 恰好落入 32 位上限 (<= 0xFFFFFFFF)
    while (shift > 0) {
        // 防溢出检查：确保 (to_hz << shift) 的高 64 位严格小于除数 from_hz
        if ((to_hz >> (64 - shift)) < from_hz) {
            if (round_up) {
                mult = asm_mul_div64_ceil(to_hz, 1ULL << shift, from_hz);
            } else {
                mult = asm_mul_div64(to_hz, 1ULL << shift, from_hz);
            }

            if (mult <= 0xFFFFFFFFULL && mult > 0) {
                break;
            }
        }
        shift--;
    }

    *out_mult = mult;
    *out_shift = shift;
}

static boolean clocksource_switch(clocksource_t *new_cs) {
    if (new_cs == NULL || new_cs == g_timekeeper.active_cs) {
        return FALSE;
    }

    uint64 flags;
    local_irq_save(&flags);

    // 1. 用旧时钟源结算到当前时刻的累积纳秒数
    uint64 current_ns = 0;
    clocksource_t *old_cs = g_timekeeper.active_cs;
    if (old_cs != NULL) {
        uint64 old_now   = cs_read_cycles(old_cs);
        uint64 old_delta = (old_now - g_timekeeper.cycle_last) & old_cs->mask;
        current_ns = g_timekeeper.base_ns +
                     (uint64)(((__uint128_t)old_delta * old_cs->mult) >> old_cs->shift);
    }

    // 2. 采样新时钟源的起点计数值
    uint64 new_cycle_start = cs_read_cycles(new_cs);

    // 3. 顺序锁保护下，仅需更新 3 个核心锚点字段！
    g_timekeeper.seq++;

    g_timekeeper.base_ns    = current_ns;
    g_timekeeper.cycle_last = new_cycle_start;
    g_timekeeper.active_cs  = new_cs;

    g_timekeeper.seq++;

    local_irq_restore(flags);
    PR_INFO("System Switch Clock:%s\n",new_cs->name);
    return TRUE;
}

// 驱动调用此函数主动注册时钟源
void clocksource_register(clocksource_t *cs) {
    // 子系统统一为其预计算定点数 mult 和 shift
    calc_mult_shift(cs->freq_hz, 1000000000ULL, &cs->mult, &cs->shift, FALSE);
    g_cs_list[g_cs_count++] = cs;

    PR_OK("%s Clock freq:%ldhz register success.\n",cs->name,cs->freq_hz);

    // 若当前无时钟源，或新注册的驱动评分更高，自动无缝热切换！
    if (g_timekeeper.active_cs == NULL || cs->rating > g_timekeeper.active_cs->rating) {
        clocksource_switch(cs);
    }
}

uint64 clocksource_get_active_freq(void) {
    return (g_timekeeper.active_cs != NULL) ? g_timekeeper.active_cs->freq_hz : 0;
}


// =========================================================================
// 3. 运行时动态切换当前 CPU 核心的硬件定时器 (不断档交接闹钟！)
// =========================================================================
boolean clockevent_switch(clockevent_t *new_ce) {
    cpu_core_t *core = &cpu_cores[THIS_CPU->logical_id];
    if (new_ce == NULL || new_ce == core->active_ce) {
        return FALSE;
    }

    uint64 flags;
    local_irq_save(&flags);

    // 1. 先关停旧定时器的硬件计数器与中断掩码，防止切完后产生幽灵中断
    if (core->active_ce != NULL) {
        core->active_ce->stop_hw(core->active_ce);
    }

    // 2. 切换指针并初始化新定时器的硬件寄存器
    core->active_ce = new_ce;
    new_ce->init_hw(new_ce);

    // 3. 🌟 立即将当前核心正在等待的 next_deadline_ns 重新装填进新定时器！
    //    哪怕在睡眠中途切换定时器，任务依然会在原定的纳秒时刻准时醒来！
    uint64 now_ns = get_uptime_ns();
    uint64 delay_ns = (core->next_deadline_ns > now_ns) ? (core->next_deadline_ns - now_ns) : 1000ULL;
    new_ce->set_next_event(new_ce, delay_ns);

    local_irq_restore(flags);
    PR_INFO("Coer:%d Switch Timer:%s\n",core->logical_id,new_ce->name);
    return TRUE;
}

// 驱动调用此函数主动注册定时器
void clockevent_register(clockevent_t *ce) {
    calc_mult_shift(1000000000ULL, ce->freq_hz,
                    &ce->ns_to_dev_mult, &ce->ns_to_dev_shift, TRUE);
    ce->ns_to_dev_mask = (1ULL << ce->ns_to_dev_shift) - 1;
    g_ce_list[g_ce_count++] = ce;
    PR_OK("%s Timer freq:%ldhz register success.\n",ce->name,ce->freq_hz);
}

static inline void reprogram_clockevent(uint64 now_ns) {
    uint64 target_ns = THIS_CPU->next_deadline_ns;
    // 🌟 上层把 now_ns 传给底层驱动，底层就不用自己去读了
    THIS_CPU->active_ce->set_next_event_ns(THIS_CPU->active_ce, target_ns, now_ns);
}

void sleep_ns(uint64 delay_ns) {
    uint64 now_ns = get_uptime_ns();
    THIS_CPU->next_deadline_ns = now_ns + delay_ns;
    reprogram_clockevent(now_ns);
}


// =========================================================================
// 🌟 子系统通用标尺服务：利用当前已激活的 active_cs，为任何未知频率的硬件测算频率
//    自带微秒级基线预热 + 单次等待 + NMI/SMI 包夹验毒 + 实测分母结算
// =========================================================================
uint64 timekeeping_measure_freq_hz(uint64 (*target_read)(void), boolean is_down_counter, uint32 wait_ms) {
    clocksource_t *ref_cs = g_timekeeper.active_cs;
    if (ref_cs == NULL || ref_cs->freq_hz == 0) {
        return 0; // 尚未有任何基准时钟源注册
    }

    uint64 target_ref_delta = (ref_cs->freq_hz / 1000ULL) * wait_ms;
    uint64 flags;
    local_irq_save(&flags);

    // 1. 微秒级预热：探测“读一次参考时钟 + 读一次被测计数器”的最小包夹基线
    uint64 min_span = 0xFFFFFFFFFFFFFFFFULL;
    for (int i = 0; i < 4; i++) {
        uint64 r1 = cs_read_cycles(ref_cs);
        (void)target_read();
        uint64 r2 = cs_read_cycles(ref_cs);
        uint64 span = (r2 - r1) & ref_cs->mask;
        if (span < min_span) min_span = span;
    }
    // 允许 2 倍总线抖动 + 约 1 微秒的参考时钟余量
    uint64 margin = (ref_cs->freq_hz / 1000000ULL) + 2;
    uint64 max_span = (min_span << 1) + margin;

    // 2. 起跑线干净采样 (拦截 NMI / SMI)
    uint64 ref_start = 0, tgt_start = 0;
    for (int retry = 0; retry < 16; retry++) {
        uint64 r1 = cs_read_cycles(ref_cs);
        tgt_start = target_read();
        uint64 r2 = cs_read_cycles(ref_cs);
        if (((r2 - r1) & ref_cs->mask) <= max_span) {
            ref_start = r2;
            break;
        }
        ref_start = r2;
    }

    // 3. 单次等待 wait_ms
    while (((cs_read_cycles(ref_cs) - ref_start) & ref_cs->mask) < target_ref_delta) {
        asm_pause();
    }

    // 4. 终点线干净采样 (拦截 NMI / SMI)
    uint64 ref_end = 0, tgt_end = 0;
    for (int retry = 0; retry < 16; retry++) {
        uint64 r1 = cs_read_cycles(ref_cs);
        tgt_end   = target_read();
        uint64 r2 = cs_read_cycles(ref_cs);
        if (((r2 - r1) & ref_cs->mask) <= max_span) {
            ref_end = r2;
            break;
        }
        ref_end = r2;
    }

    local_irq_restore(flags);

    // 5. 实测分母结算 (支持向上递增的 TSC，也支持向下递减的 APIC！)
    uint64 delta_ref = (ref_end - ref_start) & ref_cs->mask;
    uint64 delta_tgt = is_down_counter ? (tgt_start - tgt_end) : (tgt_end - tgt_start);

    return asm_mul_div64(delta_tgt, ref_cs->freq_hz, delta_ref);
}


// 每个 CPU 核心从已注册的定时器池中自动绑定评分最高的一个
void clockevent_init_per_cpu(void) {
    clockevent_t *best = NULL;
    for (uint32 i = 0; i < g_ce_count; i++) {
        if (best == NULL || g_ce_list[i]->rating > best->rating) {
            best = g_ce_list[i];
        }
    }
    THIS_CPU->next_deadline_ns = get_uptime_ns() + 1000000ULL;
    clockevent_switch(best);
}

uint64 s=0;
int32 timer_irq_handler (cpu_registers_t *regs,void *dev_id) {
    sleep_ns(1000000000UL);
    color_printk(ORANGE,BLACK,"%lds ",s++);
}


void time_core_init(void) {
    // 1. 子系统统一申请，终生不释放
    g_sys_timer_vector = alloc_irq();

    // 2. 注册统一的中断处理入口
    register_isr(g_sys_timer_vector, timer_irq_handler,NULL,"sys-timer-irq");
}
