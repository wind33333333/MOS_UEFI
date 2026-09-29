#include "../x64/cpu.h"
#include "../x64/msr.h"
#include "../x64/apic_time.h"
#include "../time/time_core.h"
#include "../x64/interrupt.h"

extern clocksource_t tsc_cs;
extern clockevent_t tsc_deadline_ce;
extern clockevent_t apic_oneshot_ce;
uint64 tsc_cs_read(clocksource_t *cs);
void apic_oneshot_init_hw(clockevent_t *ce);
void apic_oneshot_stop_hw(clockevent_t *ce);
void apic_oneshot_set_next(clockevent_t *ce, uint64 delay_ns);

// =========================================================================
// 终极精简版 TSC 频率探测：Intel 0x15 直读 -> timekeeping测算
// =========================================================================
static uint64 detect_tsc_hz() {
    // [通道 1] 现代 Intel 真机 (或开启 CPU 直通的虚拟机)：CPUID(0x15) 0ms 精确计算
    uint32 eax, ebx, ecx, edx;
    asm_cpuid(0, &eax, &ebx, &ecx, &edx);
    if (eax >= 0x15) {
        asm_cpuid(0x15, &eax, &ebx, &ecx, &edx);
        if (eax != 0 && ebx != 0 && ecx != 0) {
            return ((uint64) ecx * (uint64) ebx) / (uint64) eax;
        }
    }

    // [通道 2] QEMU / VMware / VirtualBox 虚拟机 + AMD 全系真机 + 老旧 Intel：
    return timekeeping_measure_freq_hz(asm_rdtscp,FALSE,10);
}


//读取当前apic计数数值
static uint64 apic_count_read() {
    asm_rdmsr(APIC_CURRENT_COUNT_MSR );
}


// =========================================================================
// apic频率探测：Intel 0x15 直读 -> timekeeping测算
// =========================================================================
#define APIC_LVT_MASKED           (1U << 16)
static uint64 detect_apic_hz() {
    // [通道 1] 现代 Intel 真机 (或开启 CPU 直通的虚拟机)：CPUID(0x15) 0ms 精确计算
    uint32 eax, ebx, ecx, edx;
    asm_cpuid(0, &eax, &ebx, &ecx, &edx);
    if (eax >= 0x15) {
        asm_cpuid(0x15, &eax, &ebx, &ecx, &edx);
        if (eax != 0 && ebx != 0 && ecx != 0) {
            return ecx;
        }
    }

    // =====================================================================
    // 方式2：用当前时钟校准apic
    // =====================================================================
    asm_wrmsr(APIC_LVT_TIMER_MSR, APIC_LVT_MASKED | APIC_ONESHOT);
    asm_wrmsr(APIC_DIVIDE_CONFIG_MSR, APIC_DIV_BY_1);
    asm_wrmsr(APIC_INITIAL_COUNT_MSR, 0xFFFFFFFFULL);
    return timekeeping_measure_freq_hz(apic_count_read,TRUE,10);
}


// =========================================================================
// 检测当前 CPU 是否支持硬件级 Local APIC TSC-Deadline 模式
// =========================================================================
// CPUID(0x01).ECX 特性位定义
#define CPUID_FEAT_ECX_X2APIC        (1U << 21) // Bit 21: 支持 x2APIC (MSR 0x800~0x83F)
#define CPUID_FEAT_ECX_TSC_DEADLINE  (1U << 24) // Bit 24: 支持 APIC TSC-Deadline 模式 (MSR 0x6E0)
#define CPUID_FEAT_ECX_HYPERVISOR    (1U << 31) // Bit 31: 当前运行在虚拟机 (Hypervisor) 中
static inline boolean cpu_has_tsc_deadline(void) {
    uint32 eax, ebx, ecx, edx;

    // 查询基础功能叶 0x01
    asm_cpuid(0x01, &eax, &ebx, &ecx, &edx);

    // 检查 ECX 的第 24 位 (0x01000000)
    return (ecx & CPUID_FEAT_ECX_TSC_DEADLINE) != 0;
}


uint8 g_apic_oneshot_div_cfg;



//初始化tsc始终和定时器
void apic_time_init() {
    //tsc时钟注册
    tsc_cs.freq_hz = detect_tsc_hz();
    tsc_cs.name = "tsc";
    tsc_cs.rating = 400;
    tsc_cs.mask = CS_MASK_64BIT;
    tsc_cs.is_tsc = TRUE;
    tsc_cs.read = tsc_cs_read;
    tsc_cs.priv = NULL;
    clocksource_register(&tsc_cs);

    //apic定时器注册
    uint64 apic_hz = detect_apic_hz();
    static const uint8 k_div_table[8] = {0x0B, 0x00, 0x01, 0x02, 0x03, 0x08, 0x09, 0x0A};
    uint32 shift = 0;
    while (shift < 7 && (apic_hz >> shift) > 10000000ULL) {
        shift++;
    }
    g_apic_oneshot_div_cfg = k_div_table[shift];
    apic_oneshot_ce.freq_hz = apic_hz >> shift;
    apic_oneshot_ce.name = "apic-oneshot";
    apic_oneshot_ce.rating = 350;
    apic_oneshot_ce.init_hw = apic_oneshot_init_hw;
    apic_oneshot_ce.stop_hw = apic_oneshot_stop_hw;
    apic_oneshot_ce.set_next_delay_ns = apic_oneshot_set_next;
    apic_oneshot_ce.priv = NULL;
    clockevent_register(&apic_oneshot_ce);

    //tsc-deadline定时器注册
    if (cpu_has_tsc_deadline()) {
        tsc_deadline_ce.freq_hz = tsc_cs.freq_hz;
        tsc_deadline_ce.name = "tsc-deadline";
        tsc_deadline_ce.rating = 400;
        tsc_deadline_ce.init_hw = NULL;
        tsc_deadline_ce.stop_hw = NULL;
        tsc_deadline_ce.set_next_delay_ns = NULL;
        clockevent_register(&tsc_deadline_ce);
    }
}


