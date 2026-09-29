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
    return timekeeping_measure_freq_hz(asm_rdtscp,FALSE,10);
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
    // 方式2：用当前时钟校准apic
    // =====================================================================
    asm_wrmsr(APIC_LVT_TIMER_MSR, APIC_LVT_MASKED | APIC_ONESHOT);
    asm_wrmsr(APIC_DIVIDE_CONFIG_MSR, APIC_DIV_BY_1);
    asm_wrmsr(APIC_INITIAL_COUNT_MSR, 0xFFFFFFFFULL);
    return timekeeping_measure_freq_hz(apic_count_read,TRUE,10);
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




uint64 tsc_cs_read(clocksource_t *cs) {
    asm_rdtscp();
}

uint8 g_apic_oneshot_div_cfg;

// =========================================================================
// 1. 初始化当前 CPU 核心的 APIC 定时器硬件
//    (BSP 和 所有 AP 核心在绑定闹钟时，都会由子系统自动回调此函数)
// =========================================================================
static void apic_oneshot_init_hw(clockevent_t *ce) {

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
static void apic_oneshot_stop_hw(clockevent_t *ce) {
    // 1. 将初始倒数值清零，硬件会当场中止当前的倒数倒计时
    asm_wrmsr(APIC_INITIAL_COUNT_MSR, 0);

    // 2. 重新屏蔽 LVT 定时器中断，防止产生幽灵中断
    asm_wrmsr(APIC_LVT_TIMER_MSR, APIC_LVT_MASKED);
}

static void apic_oneshot_set_next(clockevent_t *ce, uint64 delay_ns) {

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

clocksource_t tsc_cs;
clockevent_t tsc_deadline_ce;
clockevent_t apic_oneshot_ce;

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
        clockevent_register(&tsc_deadline_ce);
    }
}


