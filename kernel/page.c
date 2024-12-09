#include "page.h"
#include "printk.h"
#include "memory.h"
#include "hpet.h"
#include "ioapic.h"
#include "acpi.h"
#include "cpu.h"

__attribute__((section(".init_data"))) UINT64 kernel_pml4t_phy_addr;          //正式内核页表

__attribute__((section(".init_text"))) void init_kernel_page(void) {
    UINT64 pml4e_backup;     //缓存pml4e

    //计算虚拟地址0当前pml4t的虚拟地址
    UINT64 *current_pml4t_virt_addr= vaddr_to_pml4e_vaddr((void*)0);

    //分配一个4K页做为正式内核pml4t
    kernel_pml4t_phy_addr = alloc_pages(1);

    pml4e_backup = *current_pml4t_virt_addr;   //备份当前PML4E
    *current_pml4t_virt_addr = 0x0UL;          //清除PML4E

    //同过map_pages函数创建一个新页表，做为正式内核页表,把0-kernel_end_address物理页挂载到新页表
    map_pages(0, (void*)0, HADDR_TO_LADDR(memory_management.kernel_end_address) >> PAGE_4K_SHIFT, PAGE_ROOT_RWX);

    //把正式内核pml4t挂载到虚拟地址0起始的一块可用虚拟内存，并把pml4t初始化为0。
    UINT64 *kernel_pml4t_virt_addr=map_pages(kernel_pml4t_phy_addr,(void*)0,1,PAGE_ROOT_RW);
    mem_set(kernel_pml4t_virt_addr,0,4096);

    kernel_pml4t_virt_addr[256] = *current_pml4t_virt_addr;        //修改正式内核PML4T 高
    kernel_pml4t_virt_addr[511] = kernel_pml4t_phy_addr|0x3;           //递归映射
    unmap_pages(kernel_pml4t_virt_addr,1);

    *current_pml4t_virt_addr = pml4e_backup;                           //还原PML4E

    set_cr3(kernel_pml4t_phy_addr);     //设置加载正式内核页表

    map_pages(HADDR_TO_LADDR(Pos.FB_addr), (void*)(UINT64)Pos.FB_addr, Pos.FB_length / 4096, PAGE_ROOT_RW_WC);
    map_pages(HADDR_TO_LADDR((UINT64)ioapic_address.ioregsel), ioapic_address.ioregsel, 1,PAGE_ROOT_RW_UC);
    map_pages(HADDR_TO_LADDR(hpet.address), (void*)hpet.address, 1, PAGE_ROOT_RW_UC);
    map_pages(HADDR_TO_LADDR(apic_id_table),apic_id_table,PAGE_4K_ALIGN(cpu_info.logical_processors_number<<2)>>PAGE_4K_SHIFT,PAGE_ROOT_RW_UC);

    return;
}

//UINT64 create_page_table(UINT64 virt_addr,UINT64 page_count){
//    UINT64 pml4t_backup[256] = {0};
//    UINT64 pml4t_addr = alloc_pages(1);
//    UINT64 *pte_vaddr = (UINT64*)vaddr_to_pte_vaddr(virt_addr&0x7FFFFFFFFFFFUL);
//    UINT64 *pde_vaddr = (UINT64*) vaddr_to_pde_vaddr(virt_addr&0x7FFFFFFFFFFFUL);
//    UINT64 *pdpte_vaddr = (UINT64*) vaddr_to_pdpte_vaddr(virt_addr&0x7FFFFFFFFFFFUL);
//    UINT64 *pml4e_vaddr = (UINT64*) vaddr_to_pml4e_vaddr(virt_addr&0x7FFFFFFFFFFFUL);
//    UINT64 pde_count = calculate_pde_count(virt_addr,page_count);
//    UINT64 pdpte_count = calculate_pdpte_count(virt_addr,page_count);
//    UINT64 pml4e_count = calculate_pml4e_count(virt_addr,page_count);
//
//    map_pages(pml4e_count,0,page_count,)
//
//}





