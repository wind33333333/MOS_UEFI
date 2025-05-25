#include <time.h>

#include "moslib.h"
#include "printk.h"
#include "acpi.h"
#include "ioapic.h"
#include "buddy_system.h"
#include "slub.h"
#include "cpu.h"
#include "hpet.h"
#include "kernel_page_table.h"
#include "memblock.h"
#include "vmm.h"
#include "vmalloc.h"
#include "rbtree.h"
#include "uefi.h"

INIT_TEXT void init_kernel(void) {
    mem_set(_start_bss,0x0,_end_bss-_start_bss);    //初始化bss段
    init_memblock();                           //初始化启动内存分配器

    //memblock.memory.region[0].size = 0xDD000;
    UINT64 i = memblock_alloc(0x1000,0x200000);

    init_kpage_table();                        //初始化正式内核页表
    init_buddy_system();                       //初始化伙伴系统
    init_slub();                               //初始化slub内存分配器
    init_rbtree_empty_augment_callbacks();     //初始化红黑树空回调函数
    init_vmalloc();                            //初始化vmalloc
    init_efi_runtime_service();                //初始化efi运行时服务
    init_output();                             //初始化输出控制台

    init_acpi();                               //初始化acpi
    // init_ioapic();                             //初始化ioapic
    // init_hpet();                               //初始化hpet
    // init_cpu();                                //初始化CPU

    //ENABLE_HPET_TIMES(*hpetRegisters.tim0_conf,*hpetRegisters.tim0_comp,0x3000000,HPET_PERIODIC,0);
    //enable_apic_time(0xF000,APIC_TSC_DEADLINE,0x20);

    //STI();
    while (1);
}
