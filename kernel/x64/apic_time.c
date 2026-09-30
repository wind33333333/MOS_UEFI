#include "apic_time.h"

#include "cpu.h"
#include "interrupt.h"
#include "msr.h"


clocksource_t tsc_cs;
clockevent_t tsc_deadline_ce;
clockevent_t apic_oneshot_ce;

apic_timer_ctx_t apic_timer_ctx;

uint8 g_sys_timer_vector;

uint64 tsc_cs_read(clocksource_t *cs) {
    asm_rdtscp();
}

// =========================================================================
// 1. 初始化当前 CPU 核心的 APIC 定时器硬件
//    (BSP 和 所有 AP 核心在绑定闹钟时，都会由子系统自动回调此函数)
// =========================================================================
void apic_oneshot_init_hw(clockevent_t *ce) {

    apic_timer_ctx_t *ctx = (apic_timer_ctx_t *)ce->priv;
    // 1. 写入 BSP 预计算好的全局最优分频配置
    asm_wrmsr(APIC_DIVIDE_CONFIG_MSR, ctx->div_cfg);

    // 2. 设定 LVT 为 One-Shot 模式，解除屏蔽，并映射到 IRQ_VECTOR_TIMER
    asm_wrmsr(APIC_LVT_TIMER_MSR, APIC_ONESHOT | g_sys_timer_vector);

}

// =========================================================================
// 2. 关停当前 CPU 核心的 APIC 定时器硬件
//    (在系统关机、或热切换到其他定时器驱动时被子系统回调)
// =========================================================================
#define APIC_LVT_MASKED           (1U << 16)
void apic_oneshot_stop_hw(clockevent_t *ce) {
    // 1. 将初始倒数值清零，硬件会当场中止当前的倒数倒计时
    asm_wrmsr(APIC_INITIAL_COUNT_MSR, 0);

    // 2. 重新屏蔽 LVT 定时器中断，防止产生幽灵中断
    asm_wrmsr(APIC_LVT_TIMER_MSR, APIC_LVT_MASKED);
}

// =========================================================================
// APIC One-Shot 驱动底层实现 (相对时间硬件的终极抗抖动形态)
// =========================================================================
void apic_oneshot_set_next(clockevent_t *ce, uint64 target_ns, uint64 now_ns) {
    // 1. 🌟 核心防御：无视旧快照，强制二次读取 (Double Fetch)！
    // 既然我是相对倒数硬件，我就必须用最新鲜的时间来算差值。
    // 这次读取紧贴着下方的 asm_wrmsr，把被 NMI 偷袭的物理窗口压缩到了几纳秒。
    uint64 fresh_now_ns = get_uptime_ns();

    uint64 delay_ns;

    // 2. 截止时间校验与补偿
    if (__builtin_expect(target_ns > fresh_now_ns, 1)) {
        // 正常情况：目标时间还在未来，算出被极度压缩、最新鲜的相对差值
        delay_ns = target_ns - fresh_now_ns;
    } else {
        // 💥 异常情况：刚才的二次读取发现，因为各种中断耽误，现在已经【迟到】了！
        // 此时绝不能发生下溢出产生天文数字。直接给一个极小的补偿延时（如 1000 纳秒），
        // 让硬件在接下来的极短时间内立刻“补一枪”中断，将任务拉起。
        delay_ns = 1000ULL;
    }

    // 3. 将通用的纳秒差值 (delay_ns) 换算为 APIC 定时器专属的硬件倒数 Tick 刻度
    uint64 ticks = (uint64)(((__uint128_t)delay_ns * ce->ns_to_dev_mult
                             + ce->ns_to_dev_mask) >> ce->ns_to_dev_shift);

    // 4. 🌟 硬件语义防坑保底
    // 根据 Intel SDM，向 APIC_INITIAL_COUNT 写入 0 的物理语义是【停表(Disarm)】！
    // 如果算出来的 ticks 是 0，不但不会立刻触发中断，反而会把闹钟彻底关掉，
    // 导致这颗 CPU 核心当场永久死锁。所以底线必须是 1。
    if (__builtin_expect(ticks == 0, 0)) {
        ticks = 1;
    }

    // 5. 轰入倒数寄存器，开始相对倒计时
    asm_wrmsr(APIC_INITIAL_COUNT_MSR, ticks);
}


// =========================================================================
// 1. 初始化当前 CPU 核心的 TSC-Deadline 硬件
// =========================================================================
void tsc_deadline_init_hw(clockevent_t *ce) {
    // 清理可能残留的历史记录，防止刚开启就收到幽灵中断
    asm_wrmsr(TSC_DEADLINE_MSR, 0);

    // TSC-Deadline 模式极其纯粹：
    // 它【不需要】配置 APIC_DIVIDE_CONFIG_MSR (分频器被硬件无视)
    // 它【不需要】配置 APIC_INITIAL_COUNT_MSR (倒数器被硬件无视)
    // 只需要把 LVT 设为 DEADLINE 模式，并放行 0x20 向量中断即可！
    asm_wrmsr(APIC_LVT_TIMER_MSR, APIC_TSC_DEADLINE | g_sys_timer_vector);

}

// =========================================================================
// 2. 关停当前 CPU 核心的 TSC-Deadline 定时器硬件
// =========================================================================
void tsc_deadline_stop_hw(clockevent_t *ce) {
    // 1. 根据 Intel SDM 手册，向 MSR 0x6E0 写入 0 即可立刻解除 (Disarm) 定时器
    asm_wrmsr(TSC_DEADLINE_MSR, 0);

    // 2. 屏蔽 LVT 定时器中断，防止切换到其他闹钟时发生冲突
    asm_wrmsr(APIC_LVT_TIMER_MSR, APIC_LVT_MASKED);
}

void tsc_deadline_set_next(clockevent_t *ce, uint64 target_ns, uint64 now_ns) {
    uint64 target_tsc;

    // 1. 尝试使用极致性能的纯数学逆推 (前提: Clocksource 也是 TSC)
    if (time_core_try_abs_ns_to_cycles(target_ns,CLOCKSOURCE_ID_TSC, &target_tsc)) {
        // 完美推导成功，一击入魂！没有任何 NMI 缝隙！
        asm_wrmsr(TSC_DEADLINE_MSR, target_tsc);
        return;
    }

    // 2. 🌟 降级妥协 (Fallback)
    // 走到这里说明系统正在用 HPET/PIT 等作为时钟源。
    // 我们必须老老实实算差值，并读取当前的硬件 TSC 快照。

    // 算出还要等多久 (纳秒)
    uint64 delay_ns = (target_ns > now_ns) ? (target_ns - now_ns) : 1000ULL;

    // 这里的 ns_to_dev_mult 属于 clockevent_t，是开机时专为 TSC 频率校准的
    uint64 delay_tsc = (uint64)(((__uint128_t)delay_ns * ce->ns_to_dev_mult
                                 + ce->ns_to_dev_mask) >> ce->ns_to_dev_shift);
    if (delay_tsc == 0) delay_tsc = 1;

    // 被迫在写入前读取一次当前 TSC (这里会存在微小的 NMI 缝隙)
    target_tsc = asm_rdtscp() + delay_tsc;

    asm_wrmsr(TSC_DEADLINE_MSR, target_tsc);
}
