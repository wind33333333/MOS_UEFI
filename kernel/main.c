#include "moslib.h"
#include "printk.h"
#include "acpi.h"
#include "ioapic.h"
#include "memory.h"
#include "cpu.h"
#include "hpet.h"
#include "gdt.h"

__attribute__((section(".init_text"))) void init_kernel(void) {
    mem_set(&_start_bss,0x0,(UINT64)&_end_bss-(UINT64)&_start_bss);    //初始化bss段
    init_output();                             //初始化输出控制台
    init_memory();                             //初始化内存管理器
    init_acpi();                               //初始化acpi
    init_ioapic();                             //初始化ioapic
    init_hpet();                               //初始化hpet
    init_cpu();                                //初始化CPU

    //ENABLE_HPET_TIMES(*hpetRegisters.tim0_conf,*hpetRegisters.tim0_comp,0x3000000,HPET_PERIODIC,0);
    //enable_apic_time(0xF000,APIC_TSC_DEADLINE,0x20);

    //STI();
    while (1);
}
