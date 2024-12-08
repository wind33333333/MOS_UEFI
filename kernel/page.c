#include "page.h"
#include "printk.h"
#include "memory.h"
#include "hpet.h"
#include "ioapic.h"
#include "acpi.h"
#include "cpu.h"

__attribute__((section(".init_data"))) UINT64 kernel_pml4t_phy_addr;          //正式内核页表

__attribute__((section(".init_text"))) void init_page(void) {
    UINT64 *current_pml4t_virt_addr= vaddr_to_pml4e_vaddr((void*)0);
    UINT64 pml4_backup[256] = {0};
    UINT64 kernel_pml4e_count = calculate_pml4e_count((void*)memory_management.kernel_end_address,memory_management.kernel_end_address - H_BASE_ADDR);

    for (UINT32 i = 0; i < kernel_pml4e_count; i++) {
        pml4_backup[i] = current_pml4t_virt_addr[i];   //备份当前PML4E
        current_pml4t_virt_addr[i] = 0x0UL;            //清除PML4E
    }

    map_pages(0, (void*)0, HADDR_TO_LADDR(memory_management.kernel_end_address) >> PAGE_4K_SHIFT, PAGE_ROOT_RWX);

    kernel_pml4t_phy_addr = alloc_pages(1);
    UINT64 *kernel_pml4t_virt_addr=map_pages(kernel_pml4t_phy_addr,(void*)0x10000000000,1,PAGE_ROOT_RW);
    mem_set(kernel_pml4t_virt_addr,0,4096);

    for (UINT32 i = 0; i < kernel_pml4e_count; i++) {
        kernel_pml4t_virt_addr[i + 256] = current_pml4t_virt_addr[i];        //修改正式内核PML4T 高
        current_pml4t_virt_addr[i] = pml4_backup[i];                         //还原PML4E
    }
    kernel_pml4t_virt_addr[511] = kernel_pml4t_phy_addr|0x3;     //递归映射
    unmap_pages(kernel_pml4t_virt_addr,1);

    set_cr3(kernel_pml4t_phy_addr);

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





