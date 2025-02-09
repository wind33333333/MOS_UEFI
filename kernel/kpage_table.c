#include "kpage_table.h"
#include "printk.h"
#include "memblock.h"
#include "memory.h"
#include "hpet.h"
#include "ioapic.h"
#include "acpi.h"
#include "cpu.h"

INIT_DATA UINT64* kplm4t_ptr;          //正式内核页表

INIT_TEXT void init_kpage_table(void) {
    kplm4t_ptr = memblock_alloc(PAGE_4K_SIZE, PAGE_4K_SIZE);
    mem_set(kplm4t_ptr, 0, PAGE_4K_SIZE);
    kplm4t_ptr[0]= tmp_pml4t[0];
    kplm4t_ptr[1]= tmp_pml4t[1];
    kplm4t_ptr[511]= (UINT64)kplm4t_ptr|0x3;  //递归映射

    tmp_pml4t[0] = 0;

    //.init_text-.init_data 可读写执行
    memblock_vmmap(HADDR_TO_LADDR(_start_init_text), (void*)HADDR_TO_LADDR(_start_init_text), _start_text-_start_init_text >> PAGE_4K_SHIFT, PAGE_ROOT_RWX);
    //.text可读执行
    memblock_vmmap(HADDR_TO_LADDR(_start_text), (void*)HADDR_TO_LADDR(_start_text), _start_data-_start_text >> PAGE_4K_SHIFT, PAGE_ROOT_RX);
    //.data-.stack可读写
    memblock_vmmap(HADDR_TO_LADDR(_start_data), (void*)HADDR_TO_LADDR(_start_data), _end_stack-_start_data >> PAGE_4K_SHIFT, PAGE_ROOT_RW);

    ((UINT64*)(LADDR_TO_HADDR(kplm4t_ptr)))[256]=tmp_pml4t[0];
    tmp_pml4t[0] = ((UINT64*)(LADDR_TO_HADDR(kplm4t_ptr)))[0];

    //设置正式内核页表
    set_cr3(kplm4t_ptr);
}





