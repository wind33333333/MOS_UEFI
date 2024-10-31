#include "moslib.h"
#include "printk.h"
#include "ap.h"
#include "acpi.h"
#include "idt.h"
#include "apic.h"
#include "ioapic.h"
#include "memory.h"
#include "gdt.h"
#include "tss.h"
#include "page.h"
#include "cpu.h"
#include "hpet.h"
#include "uefi.h"

__attribute__((section(".init_text"))) void init_kernel(void) {
    UINT32 cpu_id = 0;
    UINT8 bsp_flags = 0;

    init_cpu(&cpu_id, &bsp_flags);                      //获取cpu信息和初始化cpu开启高级功能
    init_output(bsp_flags);                             //初始化输出控制台
    init_memory(bsp_flags);                             //初始化内存管理器
    init_gdt(bsp_flags);                                //初始化GDT
    //init_tss(cpu_id, bsp_flags);                        //初始化TSS
    init_idt(bsp_flags);                                //初始化IDT
    init_acpi(bsp_flags);                               //初始化acpi
    init_hpet(bsp_flags);                               //初始化hpet
    init_ioapic(bsp_flags);                             //初始化ioapic
    init_apic();                                        //初始化apic
    //init_page(bsp_flags);                               //初始化内核页表
    init_ap(cpu_id, bsp_flags);                         //初始化ap核

    //ENABLE_HPET_TIMES(*hpetRegisters.tim0_conf,*hpetRegisters.tim0_comp,0x3000000,HPET_PERIODIC,0);
    //enable_apic_time(0xF000,APIC_TSC_DEADLINE,0x20);

    //STI();
    while (1);
}