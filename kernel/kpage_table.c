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
    kplm4t_ptr[511]= (UINT64)kplm4t_ptr|0x3;

    tmp_pml4t[0] = 0;

    //同过memblock_mmap函数创建一个新页表，做为正式内核页表,把_start-_end物理页挂载到新页表
    memblock_mmap(HADDR_TO_LADDR(_start), HADDR_TO_LADDR(_start), _end-_start >> PAGE_4K_SHIFT, PAGE_ROOT_RWX);

    ((UINT64*)(LADDR_TO_HADDR(kplm4t_ptr)))[256]=tmp_pml4t[0];
    tmp_pml4t[0] = ((UINT64*)(LADDR_TO_HADDR(kplm4t_ptr)))[0];

    set_cr3(kplm4t_ptr);
}





