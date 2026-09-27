#pragma once
#include "moslib.h"

// =========================================================================
// [第 1 层：表盘] 全局只读时钟源 (Clocksource) —— 仅 40 字节，独占 1 条 64B 缓存行
// 负责：get_uptime_ns()、ns_to_tsc_delta()、udelay()
// =========================================================================
typedef struct {
    uint64  tsc_hz;             // [8B] 全系统统一的恒定 TSC 频率 (Hz)

    uint64  tsc_to_ns_mult;     // [8B] TSC -> 纳秒 乘法系数
    uint64  ns_to_tsc_mult;     // [8B] 纳秒 -> TSC 乘法系数
    uint64  ns_to_tsc_mask;     // [8B] 纳秒 -> TSC 向上取整掩码 ((1ULL << shift) - 1)

    uint32  tsc_to_ns_shift;    // [4B] TSC -> 纳秒 右移位数
    uint32  ns_to_tsc_shift;    // [4B] 纳秒 -> TSC 右移位数
} __attribute__((aligned(64))) tsc_clock_t;

extern tsc_clock_t g_tsc_clock;


// =========================================================================
// [第 2 层：闹钟规格] 全局只读硬件定时器配置 (Clockevent Config) —— 仅 38 字节，独占 1 条 64B 缓存行
// 负责：lapic_timer_set_deadline_tsc() 与默认调度时间片步长
// =========================================================================
typedef struct {
    uint64  tsc_step_per_tick;  // [8B] 默认调度时间片对应的 TSC 步长 (如 tsc_hz / 1000)
    uint64  apic_hz;            // [8B] 分频后的 APIC 实际计数频率 (仅 One-Shot 模式有效)

    uint64  tsc_to_apic_mult;   // [8B] TSC -> APIC Tick 乘法系数
    uint64  tsc_to_apic_mask;   // [8B] TSC -> APIC Tick 向上取整掩码

    uint32  tsc_to_apic_shift;  // [4B] TSC -> APIC Tick 右移位数
    boolean has_tsc_deadline;   // [1B] TRUE: 走 Intel 0x6E0; FALSE: 走 AMD/VM 0x838 One-Shot
    uint8   apic_div_cfg;       // [1B] APIC 分频器寄存器编码 (0x83E)
} __attribute__((aligned(64))) apic_timer_cfg_t;

extern apic_timer_cfg_t g_apic_timer;

void apic_timer_set_deadline_tsc(uint64 now_tsc, uint64 target_tsc);
uint64 get_uptime_ns(void);