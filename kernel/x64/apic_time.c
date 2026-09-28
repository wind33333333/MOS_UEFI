#include "apic_time.h"

#include "cpu.h"
#include "interrupt.h"
#include "msr.h"


tsc_clock_t       g_tsc_clock;
apic_timer_cfg_t  g_apic_timer;

// -------------------------------------------------------------------------
// 1. 时钟 API (只依赖 g_tsc_clock，开机极早期即可调用)
// -------------------------------------------------------------------------
uint64 get_uptime_ns(void) {
    uint64 tsc = asm_rdtscp();
    return (uint64)(((__uint128_t)tsc * g_tsc_clock.tsc_to_ns_mult) >> g_tsc_clock.tsc_to_ns_shift);
}

uint64 ns_to_tsc_delta(uint64 delay_ns) {
    return (uint64)(((__uint128_t)delay_ns * g_tsc_clock.ns_to_tsc_mult + g_tsc_clock.ns_to_tsc_mask)
                    >> g_tsc_clock.ns_to_tsc_shift);
}

// -------------------------------------------------------------------------
// 2. 定时器 API (只依赖 g_lapic_timer，供中断与调度器调用)
// -------------------------------------------------------------------------
void apic_timer_set_deadline_tsc(uint64 now_tsc, uint64 target_tsc) {
    if (g_apic_timer.has_tsc_deadline) {
        asm_wrmsr(TSC_DEADLINE_MSR, target_tsc);
        return;
    }

    if (target_tsc <= now_tsc) {
        asm_wrmsr(APIC_INITIAL_COUNT_MSR, 1);
        return;
    }

    uint64 delta_tsc  = target_tsc - now_tsc;
    uint64 apic_ticks = (uint64)(((__uint128_t)delta_tsc * g_apic_timer.tsc_to_apic_mult
                                  + g_apic_timer.tsc_to_apic_mask) >> g_apic_timer.tsc_to_apic_shift);

    if (apic_ticks > 0xFFFFFFFFULL) {
        apic_ticks = 0xFFFFFFFFULL;
    }

    asm_wrmsr(APIC_INITIAL_COUNT_MSR, (uint32)apic_ticks);
}

void apic_timer_enable(void) {
    // 配置当前核心的 APIC 定时器硬件寄存器
    uint8 irq = alloc_irq();
    if (g_apic_timer.has_tsc_deadline) {
        asm_wrmsr(APIC_LVT_TIMER_MSR, APIC_TSC_DEADLINE | irq);
        asm_mfence();
    } else {
        asm_wrmsr(APIC_DIVIDE_CONFIG_MSR, g_apic_timer.apic_div_cfg);
        asm_wrmsr(APIC_LVT_TIMER_MSR, APIC_ONESHOT | irq);
    }

    // 启动当前核心的第 1 个定时中断
    cpu_core_t *core = &cpu_cores[THIS_CPU->logical_id];
    uint64 now = asm_rdtscp();
    core->next_deadline_ns = now + g_apic_timer.tsc_step_per_tick;
    apic_timer_set_deadline_tsc(now, core->next_deadline_ns);
}