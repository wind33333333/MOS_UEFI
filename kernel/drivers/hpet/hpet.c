#include "hpet.h"
#include "printk.h"
#include "../../init/acpi_init.h"
#include "vmalloc.h"
#include "../../init/apic_init.h"
#include "../x64/msr.h"
#include "../x64/cpu.h"

// 假设这是全局的 HPET 设备对象
hpet_device_t hpet_dev;

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