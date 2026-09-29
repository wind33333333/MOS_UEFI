#include "apic_time_init.h"
#include "../x64/cpu.h"
#include "../x64/msr.h"
#include "../x64/apic_time.h"
#include "../time/time_core.h"
#include "../x64/interrupt.h"

// =========================================================================
// 终极精简版 TSC 频率探测：Intel 0x15 直读 -> 全平台 HPET 硬件实测
// =========================================================================
uint64 detect_tsc_hz() {
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
    return clocksource_calibrate_hz(asm_rdtscp,FALSE,10);
}


//读取当前apic计数数值
static uint64 apic_count_read() {
    asm_rdmsr(APIC_CURRENT_COUNT_MSR );
}


#define APIC_LVT_MASKED           (1U << 16)
// =========================================================================
// 3. [核心 API] 以已校准的 tsc_hz 为黄金标尺，单次极速测算 APIC 原始总线频率
//    推荐参数：wait_ms = 5 (仅耗时 5 毫秒，零 HPET 访问)
// =========================================================================
uint64 detect_apic_hz() {
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
    // 步骤 1：启动 APIC 定时器 (屏蔽中断 + 1分频最高精度 + 0xFFFFFFFF 满格起步)
    // =====================================================================
    asm_wrmsr(APIC_LVT_TIMER_MSR, APIC_LVT_MASKED | APIC_ONESHOT);
    asm_wrmsr(APIC_DIVIDE_CONFIG_MSR, APIC_DIV_BY_1);
    asm_wrmsr(APIC_INITIAL_COUNT_MSR, 0xFFFFFFFFULL);
    return clocksource_calibrate_hz(apic_count_read,TRUE,10);
}

// =========================================================================
// 2. 通用定点数参数计算器 (仅在开机时调用，自动寻找精度最高的 mult 和 shift)
// =========================================================================
static inline void calc_mult_shift(
    uint64 from_hz,
    uint64 to_hz,
    uint64 *out_mult,
    uint32 *out_shift,
    boolean round_up) {
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


// CPUID(0x01).ECX 特性位定义
#define CPUID_FEAT_ECX_X2APIC        (1U << 21) // Bit 21: 支持 x2APIC (MSR 0x800~0x83F)
#define CPUID_FEAT_ECX_TSC_DEADLINE  (1U << 24) // Bit 24: 支持 APIC TSC-Deadline 模式 (MSR 0x6E0)
#define CPUID_FEAT_ECX_HYPERVISOR    (1U << 31) // Bit 31: 当前运行在虚拟机 (Hypervisor) 中

// =========================================================================
// 检测当前 CPU 是否支持硬件级 Local APIC TSC-Deadline 模式
// =========================================================================
static inline boolean cpu_has_tsc_deadline(void) {
    uint32 eax, ebx, ecx, edx;

    // 查询基础功能叶 0x01
    asm_cpuid(0x01, &eax, &ebx, &ecx, &edx);

    // 检查 ECX 的第 24 位 (0x01000000)
    return (ecx & CPUID_FEAT_ECX_TSC_DEADLINE) != 0;
}

clocksource_t tsc_cs;
clockevent_t tsc_deadline_ce;
clockevent_t apic_oneshot_ce;



uint64 tsc_cs_read(clocksource_t *cs) {
    asm_rdtscp();
}

//初始化tsc始终和定时器
void apic_time_init() {
    //全局tsc时钟
    tsc_cs.freq_hz = detect_tsc_hz();
    tsc_cs.name = "tsc";
    tsc_cs.rating = 400;
    tsc_cs.mask = CS_MASK_64BIT;
    tsc_cs.is_tsc = TRUE;
    tsc_cs.read = tsc_cs_read;
    tsc_cs.priv = NULL;
    clocksource_register(&tsc_cs);


    //全局apic定时器，tsc-deadline或oneshot模式
    g_apic_timer.has_tsc_deadline = cpu_has_tsc_deadline();
    if (!g_apic_timer.has_tsc_deadline) {
        uint64 raw_bus_hz = detect_apic_hz();
        static const uint8 k_div_table[8] = {0x0B, 0x00, 0x01, 0x02, 0x03, 0x08, 0x09, 0x0A};
        uint32 shift = 0;
        while (shift < 7 && (raw_bus_hz >> shift) > 10000000ULL) {
            shift++;
        }

        apic_oneshot_ce.freq_hz = raw_bus_hz >> shift;
        apic_oneshot_ce.name = "apic-oneshot";
        apic_oneshot_ce.rating = 350;
    }
}


