#include "apic_time.h"

#include "cpu.h"
#include "interrupt.h"
#include "msr.h"


clocksource_t tsc_cs;
clockevent_t tsc_deadline_ce;
clockevent_t apic_oneshot_ce;

uint64 tsc_cs_read(clocksource_t *cs) {
    asm_rdtscp();
}

// =========================================================================
// 1. 初始化当前 CPU 核心的 APIC 定时器硬件
//    (BSP 和 所有 AP 核心在绑定闹钟时，都会由子系统自动回调此函数)
// =========================================================================
void apic_oneshot_init_hw(clockevent_t *ce) {

    // 1. 写入 BSP 预计算好的全局最优分频配置
    asm_wrmsr(APIC_DIVIDE_CONFIG_MSR, g_apic_oneshot_div_cfg);

    // 2. 设定 LVT 为 One-Shot 模式，解除屏蔽，并映射到 IRQ_VECTOR_TIMER
    uint8 irq = alloc_irq();
    asm_wrmsr(APIC_LVT_TIMER_MSR, APIC_ONESHOT | irq);

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

void apic_oneshot_set_next(clockevent_t *ce, uint64 delay_ns) {

    // 🌟 核心逆向换算：把通用的纳秒，折算成 APIC Timer 自己的原始 Tick 数！
    // 比如 delay_ns = 5,000,000 (5毫秒)
    // 经过 APIC 自己专属的 mult 和 shift 换算，变成了 31,250 个 APIC Tick
    uint64 apic_ticks = (uint64)(((__uint128_t)delay_ns * ce->ns_to_dev_mult
                                  + ce->ns_to_dev_mask) >> ce->ns_to_dev_shift);

    if (apic_ticks == 0) apic_ticks = 1;
    if (apic_ticks > 0xFFFFFFFFULL) apic_ticks = 0xFFFFFFFFULL;

    // 最后把这 31,250 个原始 Tick 写入 APIC 硬件计数器！
    asm_wrmsr(APIC_INITIAL_COUNT_MSR, (uint32)apic_ticks);
}


// =========================================================================
// 1. 初始化当前 CPU 核心的 TSC-Deadline 硬件
// =========================================================================
static void tsc_deadline_init_hw(clockevent_t *ce) {
    // 清理可能残留的历史记录，防止刚开启就收到幽灵中断
    asm_wrmsr(TSC_DEADLINE_MSR, 0);

    // TSC-Deadline 模式极其纯粹：
    // 它【不需要】配置 APIC_DIVIDE_CONFIG_MSR (分频器被硬件无视)
    // 它【不需要】配置 APIC_INITIAL_COUNT_MSR (倒数器被硬件无视)
    // 只需要把 LVT 设为 DEADLINE 模式，并放行 0x20 向量中断即可！
    asm_wrmsr(APIC_LVT_TIMER_MSR, APIC_TSC_DEADLINE | IRQ_VECTOR_TIMER);

}

// =========================================================================
// 2. 关停当前 CPU 核心的 TSC-Deadline 定时器硬件
// =========================================================================
static void tsc_deadline_stop_hw(clockevent_t *ce) {
    // 1. 根据 Intel SDM 手册，向 MSR 0x6E0 写入 0 即可立刻解除 (Disarm) 定时器
    asm_wrmsr(TSC_DEADLINE_MSR, 0);

    // 2. 屏蔽 LVT 定时器中断，防止切换到其他闹钟时发生冲突
    asm_wrmsr(APIC_LVT_TIMER_MSR, APIC_LVT_MASKED);
}

// =========================================================================
// 3. 设置下一次唤醒时间 (最极简、最安全的定闹钟逻辑)
// =========================================================================
static void tsc_deadline_set_next_delay_ns(clockevent_t *ce, uint64 delay_ns) {
    // 1. 将通用的纳秒 (ns) 换算成 TSC 的 Tick 周期数
    //    由于 TSC-Deadline 的频率就是 CPU TSC 的频率，所以直接用预计算好的多项式转换
    uint64 delay_tsc = (uint64)(((__uint128_t)delay_ns * ce->ns_to_dev_mult
                                 + ce->ns_to_dev_mask) >> ce->ns_to_dev_shift);

    // 至少等待 1 个时钟周期，防止 0 引起硬件停表 (0 的语义是 Disarm)
    if (delay_tsc == 0) {
        delay_tsc = 1;
    }

    // 2. 🌟 绝对时间装填：当前 TSC 快照 + 等待的 TSC ticks
    uint64 now_tsc = asm_rdtscp();
    uint64 target_tsc = now_tsc + delay_tsc;

    // 3. 写入目标寄存器
    // 【硬件兜底神技】：如果你算完 target_tsc 后，进程被耽误了 1 微秒，
    // 导致当前真实的 TSC 已经超过了 target_tsc，再执行下面这行 wrmsr 会发生什么？
    // 答案是：硬件检测到 (current_tsc >= target_tsc)，会【立刻产生中断】，绝不死锁！
    asm_wrmsr(TSC_DEADLINE_MSR, target_tsc);
}

