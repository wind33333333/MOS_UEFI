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

UINT32 *apic_id_table;   //apic_id_table
cpu_info_t cpu_info;

__attribute__((section(".init_text"))) void init_kernel(void) {
    apic_id_table=(UINT32*)0;
    mem_set((void*)&cpu_info,0,sizeof(cpu_info_t));
    UINT32 apic_id,cpu_id;
    mem_set(&cpu_info,0,sizeof(cpu_info_t));    //初始化全局变量
    init_output();                             //初始化输出控制台
    init_memory();                             //初始化内存管理器
    init_acpi();                               //初始化acpi
    init_cpu_mode();                           //初始化cpu开启高级功能
    get_cpu_info();                            //获取cpu信息
    GET_APICID(apic_id);                       //获取apic_ia
    cpu_id = apicid_to_cpuid(apic_id);         //获取cpu_id
    init_gdt();                                //初始化GDT
    init_tss();                                //初始化TSS
    init_idt();                                //初始化IDT
    init_hpet();                               //初始化hpet
    init_ioapic();                             //初始化ioapic
    init_apic();                               //初始化apic
    init_page();                               //初始化内核页表
    init_ap();                                 //初始化ap核

    color_printk(GREEN, BLACK, "CPUID:%d APICID:%d init successful\n", cpu_id,apic_id);

    //ENABLE_HPET_TIMES(*hpetRegisters.tim0_conf,*hpetRegisters.tim0_comp,0x3000000,HPET_PERIODIC,0);
    //enable_apic_time(0xF000,APIC_TSC_DEADLINE,0x20);

    //STI();
    while (1);
}