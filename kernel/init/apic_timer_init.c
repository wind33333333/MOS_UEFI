#include "apic_timer_init.h"
#include "../x64/cpu.h"
#include "../x64/msr.h"
#include "../drivers/hpet/hpet.h"
#include "../x64/apic_timer.h"
#include "../x64/interrupt.h"

#define MAX_NMI_SMI_RETRIES  16

// =========================================================================
// 内部辅助函数：单次无边缘等待的干净采样 (Linux tsc_read_refs 思想增强版)
// 作用：直接读取 HPET 与 TSC，仅利用 (tsc2 - tsc1) 拦截 NMI/SMI/VM-Exit
// =========================================================================
static inline uint64 hpet_read_ref_clean(
    volatile uint64 *hpet_counter,
    uint64 max_allowed_span,
    uint64 *out_hpet) {
    uint64 tsc1, tsc2, hpet_val;

    for (int retry = 0; retry < MAX_NMI_SMI_RETRIES; retry++) {
        // 无需等待 HPET 翻转边缘！直接用序列化指令包夹一次 MMIO 读取
        tsc1 = asm_rdtscp();
        hpet_val = *hpet_counter;
        tsc2 = asm_rdtscp();

        // 🌟 唯一保留双包夹的理由：验证本次读取期间没有发生 NMI / SMI / VM-Exit！
        // 只要包夹宽度正常，首尾两次读取的 MMIO 延迟就会在最终相减时自动 100% 抵消！
        if ((tsc2 - tsc1) <= max_allowed_span) {
            *out_hpet = hpet_val;
            return tsc2; // 无需算中点，直接返回 tsc2，与终点线的 tsc2 共模抵消！
        }
    }

    // 极端兜底
    *out_hpet = hpet_val;
    return tsc2;
}

// =========================================================================
// 终极精简版：单次 10ms 极速 TSC 频率校准器
// =========================================================================
uint64 hpet_calibrate_tsc_hz(uint32 wait_ms) {
    uint64 target_hpet_delta = (hpet_dev.frequency_hz * wait_ms) / 1000;
    volatile uint64 *hpet_counter = &hpet_dev.hw_regs->main_counter;

    uint64 hpet_start, hpet_end;
    uint64 tsc_start, tsc_end;

    uint64 flags;
    local_irq_save(&flags);

    // 1. 微秒级总线预热 + 探测当前机器单次 MMIO 读取的最小基线开销 (min_span)
    uint64 min_span = 0xFFFFFFFFFFFFFFFFULL;
    for (int i = 0; i < 4; i++) {
        uint64 t1 = asm_rdtscp();
        (void) *hpet_counter;
        uint64 t2 = asm_rdtscp();
        if ((t2 - t1) < min_span) {
            min_span = t2 - t1;
        }
    }
    uint64 max_allowed_span = (min_span << 1) + 1000;

    // 2. 起跑线直接采样 (无边缘等待，耗时仅 ~1us)
    tsc_start = hpet_read_ref_clean(hpet_counter, max_allowed_span, &hpet_start);

    // 3. 等待 10ms 窗口
    uint64 hpet_target = hpet_start + target_hpet_delta;
    while (*hpet_counter < hpet_target) {
        asm_pause();
    }

    // 4. 终点线直接采样 (无边缘等待，MMIO 延迟与起跑线自动抵消)
    tsc_end = hpet_read_ref_clean(hpet_counter, max_allowed_span, &hpet_end);

    local_irq_restore(flags);

    // 5. 128 位防溢出比例换算
    uint64 delta_tsc = tsc_end - tsc_start;
    uint64 delta_hpet = hpet_end - hpet_start;

    return asm_mul_div64(delta_tsc, hpet_dev.frequency_hz, delta_hpet);
}

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
    // 100% 默认支持 HPET，无需配置任何冷门虚拟机参数，直接实测 10ms！
    return hpet_calibrate_tsc_hz(10);
}


#define APIC_LVT_MASKED           (1U << 16)
// =========================================================================
// 2. 内部辅助函数：捕获一次无 NMI / SMI / VM-Exit 污染的 APIC 与 TSC 同步快照
// =========================================================================
static inline uint64 apic_read_ref_clean(
    uint64 max_allowed_span,
    uint64 *out_apic_ccr) {
    uint64 tsc1, tsc2, apic_val;

    for (int retry = 0; retry < MAX_NMI_SMI_RETRIES; retry++) {
        tsc1 = asm_rdtscp();
        apic_val = asm_rdmsr(APIC_CURRENT_COUNT_MSR); // 读取 32 位当前倒数值 (0x839)
        tsc2 = asm_rdtscp();

        // 🌟 当场验毒：检查读取 MSR 的瞬间是否遭遇了 NMI / SMI 或宿主机调度抢占
        // 只要包夹宽度在正常基线阈值内，首尾两次 rdmsr 的固定延迟会在相减时 100% 共模抵消！
        if ((tsc2 - tsc1) <= max_allowed_span) {
            *out_apic_ccr = apic_val;
            return tsc2; // 直接返回下边界 tsc2，无需计算中点
        }
    }

    // 极端兜底：保证内核在任何极端虚拟化负载下绝不死锁
    *out_apic_ccr = apic_val;
    return tsc2;
}

// =========================================================================
// 3. [核心 API] 以已校准的 tsc_hz 为黄金标尺，单次极速测算 APIC 原始总线频率
//    推荐参数：wait_ms = 5 (仅耗时 5 毫秒，零 HPET 访问)
// =========================================================================
uint64 tsc_calibrate_apic_hz() {
    // 计算粗略等待窗口对应的 TSC 增量
    uint64 tsc_hz = g_tsc_clock.tsc_hz;
    uint32 wait_ms = 5;
    uint64 target_tsc_delta = (tsc_hz / 1000ULL) * wait_ms;

    uint64 apic_start, apic_end;
    uint64 tsc_start, tsc_end;

    uint64 flags;
    local_irq_save(&flags);

    // =====================================================================
    // 步骤 1：启动 APIC 定时器 (屏蔽中断 + 1分频最高精度 + 0xFFFFFFFF 满格起步)
    // =====================================================================
    asm_wrmsr(APIC_LVT_TIMER_MSR, APIC_LVT_MASKED | APIC_ONESHOT);
    asm_wrmsr(APIC_DIVIDE_CONFIG_MSR, APIC_DIV_BY_1);
    asm_wrmsr(APIC_INITIAL_COUNT_MSR, 0xFFFFFFFFULL);

    // =====================================================================
    // 步骤 2：微秒级预热 + 探测当前环境读一次 APIC MSR 的最小基线开销 (min_span)
    //        (物理真机通常仅 ~40 周期；虚拟机 MSR Trap 通常为 ~2000 周期)
    // =====================================================================
    uint64 min_span = 0xFFFFFFFFFFFFFFFFULL;
    for (int i = 0; i < 4; i++) {
        uint64 t1 = asm_rdtscp();
        asm_rdmsr(APIC_CURRENT_COUNT_MSR);
        uint64 t2 = asm_rdtscp();
        if ((t2 - t1) < min_span) {
            min_span = t2 - t1;
        }
    }
    uint64 max_allowed_span = (min_span << 1) + 1000;

    // =====================================================================
    // 步骤 3：起跑线直接采样 (耗时小于 1 微秒，自带抗 NMI/SMI 过滤)
    // =====================================================================
    tsc_start = apic_read_ref_clean(max_allowed_span, &apic_start);

    // =====================================================================
    // 步骤 4：纯核内单次等待 (仅轮询 TSC，零外部总线流量)
    // =====================================================================
    uint64 tsc_target = tsc_start + target_tsc_delta;
    while (asm_rdtscp() < tsc_target) {
        asm_pause();
    }

    // =====================================================================
    // 步骤 5：终点线直接采样 (哪怕因重试超过 5ms，实测分母也会自动同步补偿)
    // =====================================================================
    tsc_end = apic_read_ref_clean(max_allowed_span, &apic_end);

    // 校准完毕，顺手写 0 停掉 APIC 计数器，保持硬件状态干净
    asm_wrmsr(APIC_INITIAL_COUNT_MSR, 0);

    local_irq_restore(flags);

    // =====================================================================
    // 步骤 6：按实测分母 delta_tsc 精确结算
    //        注意：APIC 是向下递减计数器，所以差值是 (apic_start - apic_end)！
    // =====================================================================
    uint64 delta_apic = apic_start - apic_end;
    uint64 delta_tsc = tsc_end - tsc_start;

    // 公式：APIC_Hz = (delta_apic * tsc_hz) / delta_tsc
    return asm_mul_div64(delta_apic, tsc_hz, delta_tsc);
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


// -------------------------------------------------------------------------
// 3. 第一阶段初始化：开机极早期初始化全局“表盘” (仅在 BSP 调用 1 次)
// -------------------------------------------------------------------------
void tsc_clock_init_global(hpet_device_t *hpet_dev, uint32 max_basic_leaf) {
    uint64 tsc_hz = detect_tsc_hz(hpet_dev, max_basic_leaf);
    g_tsc_clock.tsc_hz = tsc_hz;

    // 预计算 TSC <-> 纳秒 双向定点数常数
    calc_mult_shift(tsc_hz, 1000000000ULL, &g_tsc_clock.tsc_to_ns_mult, &g_tsc_clock.tsc_to_ns_shift, FALSE);
    calc_mult_shift(1000000000ULL, tsc_hz, &g_tsc_clock.ns_to_tsc_mult, &g_tsc_clock.ns_to_tsc_shift, TRUE);
    g_tsc_clock.ns_to_tsc_mask = (1ULL << g_tsc_clock.ns_to_tsc_shift) - 1;
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

// -------------------------------------------------------------------------
// 4. 第二阶段初始化：初始化硬件“闹钟” (BSP 算一次参数，所有核心各自挂起中断)
// -------------------------------------------------------------------------
void apic_timer_init() {
    uint64 tsc_hz = detect_tsc_hz();
    g_tsc_clock.tsc_hz = tsc_hz;
    g_apic_timer.tsc_step_per_tick = tsc_hz / 1000;
    g_apic_timer.has_tsc_deadline = cpu_has_tsc_deadline();
    if (!g_apic_timer.has_tsc_deadline) {
        uint64 raw_bus_hz = tsc_calibrate_apic_hz();
        static const uint8 k_div_table[8] = {0x0B, 0x00, 0x01, 0x02, 0x03, 0x08, 0x09, 0x0A};
        uint32 shift = 0;
        while (shift < 7 && (raw_bus_hz >> shift) > 10000000ULL) {
            shift++;
        }
        g_apic_timer.apic_hz = raw_bus_hz >> shift;
        g_apic_timer.apic_div_cfg = k_div_table[shift];

        calc_mult_shift(tsc_hz, g_apic_timer.apic_hz,
                        &g_apic_timer.tsc_to_apic_mult, &g_apic_timer.tsc_to_apic_shift, TRUE);
        g_apic_timer.tsc_to_apic_mask = (1ULL << g_apic_timer.tsc_to_apic_shift) - 1;
    }
}


