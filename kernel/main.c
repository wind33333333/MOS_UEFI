#include "moslib.h"
#include "printk.h"
#include "acpi.h"
#include "ioapic.h"
#include "buddy_system.h"
#include "slub.h"
#include "cpu.h"
#include "hpet.h"
#include "kpage_table.h"
#include "memblock.h"
#include "vmm.h"

INIT_TEXT void init_kernel(void) {
    mem_set(_start_bss,0x0,_end_bss-_start_bss);    //初始化bss段
    init_memblock();                           //初始化启动内存分配器
    init_kpage_table();                        //初始化正式内核页表
    init_output();                             //初始化输出控制台
    init_buddy_system();                       //初始化伙伴系统
    init_slub();                               //初始化slub内存分配器


    UINT64 *va = (UINT64*)0xFFFF808000000000;
    UINT64 *va1 = (UINT64*)0xFFFF808080000000;
    mmap_range(kpml4t_ptr,0x0,va,0x80000000,PAGE_ROOT_RW_4K,PAGE_4K_SIZE);
    mmap_range(kpml4t_ptr,0x80000000,va1,0x80000000,PAGE_ROOT_RW_4K,PAGE_4K_SIZE);

    munmap_range(kpml4t_ptr,va,0x80000000,PAGE_4K_SIZE);
    munmap_range(kpml4t_ptr,va1,0x80000000,PAGE_4K_SIZE);




    while (TRUE);
    //////////////////
    // init_acpi();                               //初始化acpi
    // init_ioapic();                             //初始化ioapic
    // init_hpet();                               //初始化hpet
    // init_cpu();                                //初始化CPU

    //ENABLE_HPET_TIMES(*hpetRegisters.tim0_conf,*hpetRegisters.tim0_comp,0x3000000,HPET_PERIODIC,0);
    //enable_apic_time(0xF000,APIC_TSC_DEADLINE,0x20);

    //STI();
    while (1);
}
