#include "page.h"
#include "printk.h"
#include "memory.h"
#include "hpet.h"
#include "ioapic.h"
#include "acpi.h"
#include "cpu.h"

__attribute__((section(".init_data"))) UINT64 *pml4t;          //正式内核页表

__attribute__((section(".init_text"))) void init_page(void) {
    pml4t = LADDR_TO_HADDR(alloc_pages(1));
    mem_set((void*)pml4t,0,4096);

    UINT64 pml4_bak[256] = {0};
    UINT64 pml4e_num = (((memory_management.kernel_end_address >> 12) - ((memory_management.kernel_end_address >> 12) & ~(512UL * 512 * 512 - 1))) +
                        (512UL * 512 * 512 - 1)) / (512UL * 512 * 512);

    for (UINT32 i = 0; i < pml4e_num; i++) {
        pml4_bak[i] = pml4t_vbase[i];  //备份原PML4E
        pml4t_vbase[i] = 0x0UL;        //清除PML4E
    }

    map_pages(0, 0, HADDR_TO_LADDR(memory_management.kernel_end_address) / 4096, PAGE_ROOT_RWX);

    for (UINT32 i = 0; i < pml4e_num; i++) {
        //pml4t[i] = pml4t_vbase[i];            //修改正式内核PML4T 低
        pml4t[i + 256] = pml4t_vbase[i];        //修改正式内核PML4T 高
        pml4t_vbase[i] = pml4_bak[i];           //还原PML4E
    }
    pml4t[511] = HADDR_TO_LADDR((UINT64)pml4t|0x3);     //递归映射

    SET_CR3(HADDR_TO_LADDR(pml4t));

    map_pages(HADDR_TO_LADDR(Pos.FB_addr), (UINT64)Pos.FB_addr, Pos.FB_length / 4096, PAGE_ROOT_RW_WC);
    map_pages(HADDR_TO_LADDR(ioapic_baseaddr), (UINT64) ioapic_baseaddr, 1,PAGE_ROOT_RW_UC);
    map_pages(HADDR_TO_LADDR(hpet.address), hpet.address, 1, PAGE_ROOT_RW_UC);
    map_pages(HADDR_TO_LADDR(apic_id_table),(UINT64)apic_id_table,PAGE_4K_ALIGN(cpu_info.logical_processors_number<<2)>>PAGE_4K_SHIFT,PAGE_ROOT_RW_UC);

    return;
}





