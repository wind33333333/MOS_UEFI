#include "lib.h"
#include "printk.h"
//#include "ioapic.h"
#include "ap.h"
//#include "acpi.h"
#include "idt.h"
//#include "apic.h"
#include "memory.h"
#include "gdt.h"
#include "tss.h"
#include "page.h"
#include "cpu.h"
#include "hpet.h"

__attribute__((section(".init_text"))) void Kernel_init(void) {
    unsigned int cpu_id = 0;
    unsigned char bsp_flags = 0;

    cpu_init(&cpu_id, &bsp_flags);                    //获取cpu信息
    pos_init(bsp_flags);                                //初始化输出控制台
    memory_init(bsp_flags);                             //初始化内存管理器
    gdt_init(bsp_flags);                                //初始化GDT
    tss_init(cpu_id, bsp_flags);                         //初始化TSS
    idt_init(bsp_flags);                                //初始化IDT
    //acpi_init(bsp_flags);                               //初始化acpi
    hpet_init(bsp_flags);                               //初始化hpet
    //ioapic_init(bsp_flags);                             //初始化ioapic
    //apic_init();                                        //初始化apic
    page_init(bsp_flags);                               //初始化内核页表
    ap_init(cpu_id, bsp_flags);                          //初始化ap核


    //ENABLE_HPET_TIMES(*hpetRegisters.TIM0_CONF,*hpetRegisters.TIM0_COMP,0x3000000,HPET_PERIODIC,0);
    //enable_apic_time(0xF000,APIC_TSC_DEADLINE,0x20);


    sti();
    while (1);
}