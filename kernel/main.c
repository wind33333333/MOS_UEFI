#include "moslib.h"
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
    UINT32 cpuId = 0;
    UINT8 bspFlags = 0;

    cpuInit(&cpuId, &bspFlags);                    //获取cpu信息和初始化cpu开启高级功能
    posInit(bspFlags);                                //初始化输出控制台
    memoryInit(bspFlags);                             //初始化内存管理器
    gdtInit(bspFlags);                                //初始化GDT
    tssInit(cpuId, bspFlags);                         //初始化TSS
    idtInit(bspFlags);                                //初始化IDT
    //acpiInit(bspFlags);                               //初始化acpi
    hpetInit(bspFlags);                               //初始化hpet
    //ioapicInit(bspFlags);                             //初始化ioapic
    //apicInit();                                        //初始化apic
    pageInit(bspFlags);                               //初始化内核页表
    apInit(cpuId, bspFlags);                          //初始化ap核


    //ENABLE_HPET_TIMES(*hpetRegisters.TIM0_CONF,*hpetRegisters.TIM0_COMP,0x3000000,HPET_PERIODIC,0);
    //enable_apic_time(0xF000,APIC_TSC_DEADLINE,0x20);


    sti();
    while (1);
}