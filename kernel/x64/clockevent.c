#include "colockevent.h"
#include "msr.h"

static clockevent_t *g_ce_registry[MAX_CLOCKEVENTS];
static uint32        g_ce_count = 0;

// 将纳秒延时统一转换为对应定时器的硬件 Tick 数 (向上取整，防早产)
static inline uint64 ce_ns_to_ticks(clockevent_t *ce, uint64 delay_ns) {
    return (uint64)(((__uint128_t)delay_ns * ce->ns_to_dev_mult + ce->ns_to_dev_mask)
                    >> ce->ns_to_dev_shift);
}

// =========================================================================
// 2. 三个硬件定时器驱动的具体实现 (APIC Deadline / APIC OneShot / HPET)
// =========================================================================

// --- [定时器驱动 A] Local APIC TSC-Deadline 模式 (Intel / KVM) ---
static void ce_deadline_init(clockevent_t *ce) {
    (void)ce;
    asm_wrmsr(APIC_LVT_TIMER_MSR, APIC_TIMER_MODE_DEADLINE | IRQ_VECTOR_TIMER);
    asm_mfence();
}
static void ce_deadline_stop(clockevent_t *ce) {
    (void)ce;
    asm_wrmsr(MSR_IA32_TSC_DEADLINE, 0);
    asm_wrmsr(APIC_LVT_TIMER_MSR, APIC_LVT_MASKED);
}
static void ce_deadline_set_next(clockevent_t *ce, uint64 delay_ns) {
    uint64 delta_tsc = ce_ns_to_ticks(ce, delay_ns);
    asm_wrmsr(MSR_IA32_TSC_DEADLINE, asm_rdtscp() + delta_tsc);
}
static clockevent_t g_ce_lapic_deadline = {
    .name              = "lapic_deadline",
    .rating            = 450,
    .init_hw           = ce_deadline_init,
    .stop_hw           = ce_deadline_stop,
    .set_next_delay_ns = ce_deadline_set_next
};

// --- [定时器驱动 B] Local APIC One-Shot 模式 (AMD / 通用虚拟机) ---
static uint8 g_apic_div_cfg = 0x03;
static void ce_oneshot_init(clockevent_t *ce) {
    (void)ce;
    asm_wrmsr(APIC_DIVIDE_CONFIG_MSR, g_apic_div_cfg);
    asm_wrmsr(APIC_LVT_TIMER_MSR, APIC_TIMER_MODE_ONESHOT | IRQ_VECTOR_TIMER);
}
static void ce_oneshot_stop(clockevent_t *ce) {
    (void)ce;
    asm_wrmsr(APIC_INITIAL_COUNT_MSR, 0);
    asm_wrmsr(APIC_LVT_TIMER_MSR, APIC_LVT_MASKED);
}
static void ce_oneshot_set_next(clockevent_t *ce, uint64 delay_ns) {
    uint64 ticks = ce_ns_to_ticks(ce, delay_ns);
    if (ticks == 0) ticks = 1;
    else if (ticks > 0xFFFFFFFFULL) ticks = 0xFFFFFFFFULL;
    asm_wrmsr(APIC_INITIAL_COUNT_MSR, (uint32)ticks);
}
static clockevent_t g_ce_lapic_oneshot = {
    .name              = "lapic_oneshot",
    .rating            = 350,
    .init_hw           = ce_oneshot_init,
    .stop_hw           = ce_oneshot_stop,
    .set_next_delay_ns = ce_oneshot_set_next
};

// =========================================================================
// 3. 运行时动态切换当前 CPU 核心的硬件定时器 (不断档交接闹钟！)
// =========================================================================
boolean clockevent_switch_this_cpu(clockevent_t *new_ce) {
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
    new_ce->set_next_delay_ns(new_ce, delay_ns);

    local_irq_restore(flags);
    return TRUE;
}

// 注册定时器设备 (自动预计算 ns -> dev_ticks 定点数常数)
void clockevent_register(clockevent_t *ce) {
    calc_mult_shift(1000000000ULL, ce->freq_hz,
                    &ce->ns_to_dev_mult, &ce->ns_to_dev_shift, TRUE);
    ce->ns_to_dev_mask = (1ULL << ce->ns_to_dev_shift) - 1;
    g_ce_registry[g_ce_count++] = ce;
}
