#include "../init/acpi_init.h"
#include "moslib.h"
#include "../drivers/hpet/hpet.h"
#include "printk.h"
#include "../init/apic_init.h"
#include "vmalloc.h"

extern clockevent_t hpet_ce0;
extern clocksource_t hpet_cs;
extern hpet_device_t hpet_dev;
uint64 hpet_cs_read(clocksource_t *cs);
void hpet_ce_init_hw(clockevent_t *ce);
void hpet_ce_stop_hw(clockevent_t *ce);
void hpet_ce_set_next_delay_ns(clockevent_t *ce, uint64 delay_ns);

void hpet_init(void) {
    //hpet初始化
    hpett_t *hpet_table = acpi_get_table(ACPI_SIG_HPET,0);

    // 1. 填充基地址，并获取 MMU 映射后的虚拟地址
    hpet_dev.phys_base_addr = hpet_table->acpi_generic_adderss.address;
    hpet_dev.hw_regs = ioremap(hpet_dev.phys_base_addr,4096);

    // 2. 读取全局能力寄存器并解析
    uint64 cap = hpet_dev.hw_regs->general_cap_id;

    uint32 period_fs = (uint32)(cap >> 32);
    hpet_dev.freq_hz = 1000000000000000ULL / period_fs;
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
    PR_OK("HPET TimerNum:%d PA:%#lx VA:%#lx \n",hpet_dev.num_timers,hpet_dev.phys_base_addr,hpet_dev.hw_regs);

    // 4. 停止 HPET，清零主计数器，然后启动！
    hpet_dev.hw_regs->general_config = 0;  // 暂停
    hpet_dev.hw_regs->main_counter = 0;    // 归零
    hpet_dev.hw_regs->general_config = 1;   // 启动 (ENABLE_CNF)

    //5.注册hpet时钟
    hpet_cs.name = "hpet";
    hpet_cs.rating = 250;
    hpet_cs.freq_hz = hpet_dev.freq_hz;
    hpet_cs.mask = hpet_dev.supports_64bit ? CS_MASK_64BIT : CS_MASK_32BIT;
    hpet_cs.is_tsc =FALSE;
    hpet_cs.read = hpet_cs_read;
    hpet_cs.priv = &hpet_dev;
    clocksource_register(&hpet_cs);

    //6.注册hpet定时器
    hpet_ce0.name = "hpet0";
    hpet_ce0.rating = 250;
    hpet_ce0.freq_hz = hpet_dev.freq_hz;
    hpet_ce0.init_hw = hpet_ce_init_hw;
    hpet_ce0.stop_hw = hpet_ce_stop_hw;
    hpet_ce0.set_next_delay_ns = hpet_ce_set_next_delay_ns;
    hpet_ce0.priv = &hpet_dev;
    clockevent_register(&hpet_ce0);
}