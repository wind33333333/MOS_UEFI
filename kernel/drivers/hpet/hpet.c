#include "hpet.h"
#include "printk.h"
#include "vmm.h"
#include "acpi.h"
#include "vmalloc.h"

// 假设这是全局的 HPET 设备对象
hpet_device_t hpet_dev;


// 定义 1 秒等于 10^15 飞秒
#define FEMTOSECONDS_PER_SECOND 1000000000000000ULL

// 假设这些宏是你 HPET 的映射基址
#define HPET_CAP_REG   (HPET_BASE_VADDR + 0x00) // 能力寄存器
#define HPET_COUNT_REG (HPET_BASE_VADDR + 0xF0) // 主计数器寄存器

/**
 * @brief 使用 HPET 精确测算 CPU TSC 频率 (极限边缘对齐版)
 * @param wait_ms 期望的校准时长 (建议传 10 毫秒)
 * @return TSC 频率 (Hz)
 */
uint64 hpet_calibrate_tsc_hz(hpet_device_t *hpet_dev, uint32 wait_ms) {
    uint64 target_hpet_delta = (hpet_dev->frequency_hz * wait_ms) / 1000;

    // =================================================================
    // 0. 预热 (Warm-up)
    // =================================================================
    // 把读取指令和 MMIO 内存页加载到 CPU L1 Cache 和 TLB 中，
    // 防止起跑时的 Cache Miss 导致几百纳秒的延迟干扰。
    hpet_hw_regs_t *hw_regs = hpet_dev->hw_regs;
    volatile uint64 dummy_hpet = hw_regs->main_counter;
    volatile uint64 dummy_tsc  = asm_rdtsc_start();
    (void)dummy_hpet; (void)dummy_tsc;

    // =================================================================
    // 1. 捕捉起跑线“边缘” (Start Edge Alignment)
    // =================================================================
    uint64 hpet_prev = hw_regs->main_counter;
    uint64 hpet_start;
    uint64 tsc_start;

    // 疯狂轮询，直到 HPET 发生跳变的那一瞬间！
    while ((hpet_start = hw_regs->main_counter) == hpet_prev) {
        asm_pause();
    }
    // 🌟 HPET 刚刚跳变！现在的物理时间精确卡在 HPET 的边界上！
    // 瞬间锁死 TSC！
    tsc_start = asm_rdtsc_start();


    // =================================================================
    // 2. 粗略等待大段时间流逝 (Bulk Wait)
    // =================================================================
    uint64 hpet_target = hpet_start + target_hpet_delta;
    uint64 hpet_current;

    while ((hpet_current = hw_regs->main_counter) < hpet_target) {
        asm_pause();
    }


    // =================================================================
    // 3. 捕捉终点线“边缘” (End Edge Alignment)
    // =================================================================
    // 注意：上面的循环跳出时，hpet_current 可能卡在某个阶梯的中间。
    // 为了彻底消除误差，我们再等它翻转一次，抓取下一个完美的边界！
    hpet_prev = hpet_current;
    uint64 hpet_end;
    uint64 tsc_end;

    while ((hpet_end = hw_regs->main_counter) == hpet_prev) {
        asm_pause();
    }
    // 🌟 HPET 再次跳变！瞬间锁死终点 TSC！
    tsc_end = asm_rdtsc_end(0);


    // =================================================================
    // 4. 数学计算
    // =================================================================
    uint64 delta_tsc  = tsc_end - tsc_start;
    uint64 delta_hpet = hpet_end - hpet_start;

    // TSC_Hz = (Delta_TSC * HPET_Hz) / Delta_HPET
    uint64 tsc_hz = (delta_tsc * hpet_dev->frequency_hz) / delta_hpet;

    return tsc_hz;
}

void init_hpet(void) {
    //hpet初始化
    hpett_t *hpet_table = acpi_get_table('TEPH');

    // 1. 填充基地址，并获取 MMU 映射后的虚拟地址
    hpet_dev.phys_base_addr = hpet_table->acpi_generic_adderss.address;
    hpet_dev.hw_regs = iomap(hpet_dev.phys_base_addr,4096,PAGE_4K_SIZE,PAGE_ROOT_RW_UC_4K );

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
        hpet_dev.channels[i].id = i;
        hpet_dev.channels[i].is_present = TRUE;
        hpet_dev.channels[i].supports_periodic = (timer_cap & (1 << 4)) != 0;
        hpet_dev.channels[i].supports_64bit = (timer_cap & (1 << 5)) != 0;
        hpet_dev.channels[i].allowed_irq_bitmap = (timer_cap >> 32) & 0xFFFFFFFF;
    }

    color_printk(YELLOW, BLACK, "HPET Clock Frequency: %dHz  TimerNum:%d PA:%#lx VA:%#lx \n",hpet_dev.frequency_hz,hpet_dev.num_timers,hpet_dev.phys_base_addr,hpet_dev.hw_regs);

    // 4. 停止 HPET，清零主计数器，然后启动！
    hpet_dev.hw_regs->general_config = 0;  // 暂停
    hpet_dev.hw_regs->main_counter = 0;         // 归零
    hpet_dev.hw_regs->general_config = 1;   // 启动 (ENABLE_CNF)
    hpet_dev.is_running = TRUE;


}