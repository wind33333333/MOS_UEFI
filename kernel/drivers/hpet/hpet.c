#include "hpet.h"
#include "printk.h"
#include "../../init/acpi_init.h"
#include "vmalloc.h"
#include "../../init/apic_init.h"
#include "../x64/msr.h"
#include "../x64/cpu.h"

// 假设这是全局的 HPET 设备对象
hpet_device_t hpet_dev;


#define MAX_NMI_SMI_RETRIES  16

// =========================================================================
// 内部辅助函数：单次无边缘等待的干净采样 (Linux tsc_read_refs 思想增强版)
// 作用：直接读取 HPET 与 TSC，仅利用 (tsc2 - tsc1) 拦截 NMI/SMI/VM-Exit
// =========================================================================
static inline uint64 hpet_read_ref_clean(
    volatile uint64 *hpet_counter,
    uint64 max_allowed_span,
    uint64 *out_hpet)
{
    uint64 tsc1, tsc2, hpet_val;

    for (int retry = 0; retry < MAX_NMI_SMI_RETRIES; retry++) {
        // 无需等待 HPET 翻转边缘！直接用序列化指令包夹一次 MMIO 读取
        tsc1     = asm_rdtscp();
        hpet_val = *hpet_counter;
        tsc2     = asm_rdtscp();

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
uint64 hpet_calibrate_tsc_hz(hpet_device_t *hpet_dev, uint32 wait_ms) {
    uint64 target_hpet_delta = (hpet_dev->frequency_hz * wait_ms) / 1000;
    volatile uint64 *hpet_counter = &hpet_dev->hw_regs->main_counter;

    uint64 hpet_start, hpet_end;
    uint64 tsc_start, tsc_end;

    uint64 flags;
    local_irq_save(&flags);

    // 1. 微秒级总线预热 + 探测当前机器单次 MMIO 读取的最小基线开销 (min_span)
    uint64 min_span = 0xFFFFFFFFFFFFFFFFULL;
    for (int i = 0; i < 4; i++) {
        uint64 t1 = asm_rdtscp();
        (void)*hpet_counter;
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
    uint64 delta_tsc  = tsc_end - tsc_start;
    uint64 delta_hpet = hpet_end - hpet_start;

    return asm_mul_div64(delta_tsc, hpet_dev->frequency_hz, delta_hpet);
}


// =================================================================
// 内部单次探测函数：极其精准的边缘对齐读取
// =================================================================
static inline uint64 probe_apic_hpet_hz_once(hpet_device_t *hpet_dev, uint32 wait_ms) {
    uint64 target_hpet_delta = ((uint64)hpet_dev->frequency_hz * wait_ms) / 1000;
    volatile uint64 *hpet_counter = &hpet_dev->hw_regs->main_counter;

    uint64 initial_hpet, hpet_start, hpet_current, hpet_end;
    uint64 apic_start, apic_end;

    // 🛡️ 屏蔽中断，防抖动
    uint64 flags;
    local_irq_save(&flags);

    // 1. 配置 APIC 定时器进入“纯粹倒数、不发中断”的测试模式
    // LVT Timer = 0x10000 (Bit 16 Masked 屏蔽中断, 模式为 00 One-Shot)
    asm_wrmsr(APIC_LVT_TIMER_MSR, 0x10000 | APIC_ONESHOT);
    
    // 分频器 = 0x0B (Divide by 1，不分频，使用最原始的硬件高频)
    asm_wrmsr(APIC_DIVIDE_CONFIG_MSR, 0x0B); 
    
    // 填入 32 位最大值，让它开始倒数
    asm_wrmsr(APIC_INITIAL_COUNT_MSR, 0xFFFFFFFF);

    // =================================================================
    // 2. 起跑线：捕捉 HPET 边缘
    // =================================================================
    initial_hpet = *hpet_counter;
    while ((hpet_start = *hpet_counter) == initial_hpet) {
        asm_pause();
    }
    // 瞬间锁定 APIC 当前倒数值！
    // (在 x2APIC 中，rdmsr 是极速的，不需要双包夹)
    apic_start = asm_rdmsr(APIC_CURRENT_COUNT_MSR);

    // =================================================================
    // 3. 粗略等待
    // =================================================================
    uint64 hpet_target = hpet_start + target_hpet_delta;
    while (*hpet_counter < hpet_target) {
        asm_pause();
    }

    // =================================================================
    // 4. 终点线：捕捉 HPET 边缘
    // =================================================================
    hpet_current = *hpet_counter;
    while ((hpet_end = *hpet_counter) == hpet_current) {
        asm_pause();
    }
    // 瞬间锁定 APIC 倒数终点值！
    apic_end = asm_rdmsr(APIC_CURRENT_COUNT_MSR);

    local_irq_restore(flags);

    // =================================================================
    // 5. 计算逻辑 (注意方向！)
    // =================================================================
    // 🚨 极其关键：因为 APIC 是倒数的，所以经过的 Tick 数 = 起点减去终点！
    uint64 apic_ticks_elapsed = apic_start - apic_end;
    uint64 delta_hpet = hpet_end - hpet_start;

    // APIC_Hz = (经过的 APIC Ticks * HPET_Hz) / 经过的 HPET Ticks
    uint64 apic_hz = (apic_ticks_elapsed * hpet_dev->frequency_hz) / delta_hpet;

    return (uint32)apic_hz;
}

// =================================================================
// 对外的核心 API：带防抖中值滤波的 APIC 测算
// =================================================================
// 校准采样次数
#define APIC_CALIBRATION_SAMPLES 5
uint64 hpet_calibrate_apic_hz(hpet_device_t *hpet_dev, uint32 wait_ms) {
    uint64 samples[APIC_CALIBRATION_SAMPLES];

    // 循环探测 5 次
    for (int i = 0; i < APIC_CALIBRATION_SAMPLES; i++) {
        samples[i] = probe_apic_hpet_hz_once(hpet_dev, wait_ms);
    }

    // 冒泡排序
    for (int i = 0; i < APIC_CALIBRATION_SAMPLES - 1; i++) {
        for (int j = 0; j < APIC_CALIBRATION_SAMPLES - 1 - i; j++) {
            if (samples[j] > samples[j + 1]) {
                uint32 temp = samples[j];
                samples[j] = samples[j + 1];
                samples[j + 1] = temp;
            }
        }
    }

    // 取中位数，完美过滤由于 QEMU/VirtualBox VM-Exit 造成的偶发高延迟突刺！
    uint32 perfect_apic_hz = samples[APIC_CALIBRATION_SAMPLES / 2];
    return perfect_apic_hz;
}

// 定义 1 秒等于 10^15 飞秒
#define FEMTOSECONDS_PER_SECOND 1000000000000000ULL

void hpet_init(void) {
    //hpet初始化
    hpett_t *hpet_table = acpi_get_table(ACPI_SIG_HPET,0);

    // 1. 填充基地址，并获取 MMU 映射后的虚拟地址
    hpet_dev.phys_base_addr = hpet_table->acpi_generic_adderss.address;
    hpet_dev.hw_regs = ioremap(hpet_dev.phys_base_addr,4096);

    // 2. 读取全局能力寄存器并解析
    uint64 cap = hpet_dev.hw_regs->general_cap_id;

    hpet_dev.period_fs = (cap >> 32) & 0xFFFFFFFF;
    hpet_dev.frequency_hz = FEMTOSECONDS_PER_SECOND / hpet_dev.period_fs;
    hpet_dev.num_timers = ((cap >> 8) & 0x1F) + 1;
    hpet_dev.supports_64bit = (cap & (1 << 13)) != 0;
    hpet_dev.legacy_routing = (cap & (1 << 15)) != 0;

    // 3. 解析各个通道的能力
    for (uint8 i = 0; i < hpet_dev.num_timers; i++) {
        uint64 timer_cap = hpet_dev.hw_regs->timers[i].config_cap;
        hpet_dev.hpet_timers[i].id = i;
        hpet_dev.hpet_timers[i].is_present = TRUE;
        hpet_dev.hpet_timers[i].supports_periodic = (timer_cap & (1 << 4)) != 0;
        hpet_dev.hpet_timers[i].supports_64bit = (timer_cap & (1 << 5)) != 0;
        hpet_dev.hpet_timers[i].allowed_irq_bitmap = (timer_cap >> 32) & 0xFFFFFFFF;
    }


    // 4. 停止 HPET，清零主计数器，然后启动！
    hpet_dev.hw_regs->general_config = 0;  // 暂停
    hpet_dev.hw_regs->main_counter = 0;         // 归零
    hpet_dev.hw_regs->general_config = 1;   // 启动 (ENABLE_CNF)
    hpet_dev.is_running = TRUE;

    PR_INFO("HPET Clock Frequency: %dHz  TimerNum:%d PA:%#lx VA:%#lx \n",hpet_dev.frequency_hz,hpet_dev.num_timers,hpet_dev.phys_base_addr,hpet_dev.hw_regs);

}