#include "kpage.h"
#include "printk.h"
#include "memblock.h"
#include "memory.h"
#include "hpet.h"
#include "ioapic.h"
#include "acpi.h"
#include "cpu.h"

INIT_DATA UINT64* kernel_pml4t_phy_addr;          //正式内核页表

INIT_TEXT void init_kernel_page(void) {
    kernel_pml4t_phy_addr = memblock_alloc(PAGE_4K_SIZE, PAGE_4K_SIZE);
    mem_set(kernel_pml4t_phy_addr, 0, PAGE_4K_SIZE);
    kernel_pml4t_phy_addr[0]= tmp_pml4t[0];
    kernel_pml4t_phy_addr[1]= tmp_pml4t[1];
    kernel_pml4t_phy_addr[511]= (UINT64)kernel_pml4t_phy_addr|0x3;

    tmp_pml4t[0] = 0;

    //同过memblock_mmap函数创建一个新页表，做为正式内核页表,把_start-_end物理页挂载到新页表
    UINT64 x= _end-_start;
    memblock_mmap(HADDR_TO_LADDR(_start), HADDR_TO_LADDR(_start), _end-_start >> PAGE_4K_SHIFT, PAGE_ROOT_RWX);

    kernel_pml4t_phy_addr[256]=tmp_pml4t[0];
    tmp_pml4t[0] = kernel_pml4t_phy_addr[0];

}





