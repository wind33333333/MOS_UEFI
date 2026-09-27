#include "apic_timer.h"

// =========================================================================
// 3. 全局只读定点数常数 (由 0号核 BSP 在开机时一次性算出，全核共享只读)
// =========================================================================
boolean g_has_tsc_deadline  = FALSE;
uint64  g_apic_hz           = 0;
uint8   g_apic_div_cfg      = 0x03;

// [转换组 A] TSC -> 纳秒 (用于 get_uptime_ns)
uint64  g_tsc_to_ns_mult    = 0;
uint32  g_tsc_to_ns_shift   = 0;

// [转换组 B] 纳秒 -> TSC (用于 sleep_ns / sleep_us 计算目标唤醒 TSC)
uint64  g_ns_to_tsc_mult    = 0;
uint32  g_ns_to_tsc_shift   = 0;
uint64  g_ns_to_tsc_mask    = 0;

// [转换组 C] TSC -> APIC Ticks (用于 AMD / 虚拟机 One-Shot 闹钟装填)
uint64  g_tsc_to_apic_mult  = 0;
uint32  g_tsc_to_apic_shift = 0;
uint64  g_tsc_to_apic_mask  = 0;