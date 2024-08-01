#include "page.h"

__attribute__((section(".init_text"))) void page_init(unsigned char bsp_flags) {

    if (bsp_flags) {
        unsigned long pml4_bak[256] = {0};
        unsigned long pml4e_num = (((memory_management_struct.kernel_end >> 12) - ((memory_management_struct.kernel_end >> 12) & ~(512UL * 512 * 512 - 1))) +
                                   (512UL * 512 * 512 - 1)) / (512UL * 512 * 512);

        for (unsigned int i = 0; i < pml4e_num; i++) {
            pml4_bak[i] = pml4t_vbase[i];  //备份原PML4E
            pml4t_vbase[i] = 0x0UL;        //清除PML4E
        }

        map_pages(0, 0, HADDR_TO_LADDR(memory_management_struct.kernel_end) / 4096, PAGE_ROOT_RWX);

        for (unsigned int i = 0; i < pml4e_num; i++) {
            //__PML4T[i] = pml4t_vbase[i];            //修改正式内核PML4T 低
            __PML4T[i + 256] = pml4t_vbase[i];        //修改正式内核PML4T 高
            pml4t_vbase[i] = pml4_bak[i];             //还原PML4E
        }

        color_printk(ORANGE, BLACK, "OS Can Used Total 4K PAGEs: %ld \tAlloc: %ld \tFree: %ld\n",
                     memory_management_struct.total_pages, memory_management_struct.alloc_pages,
                     memory_management_struct.free_pages);
        color_printk(ORANGE, BLACK, "OS StartAddr: %#018lX \tEndAddr: %#018lX \n",
                     memory_management_struct.kernel_start, memory_management_struct.kernel_end);

        SET_CR3(HADDR_TO_LADDR(&__PML4T));

        map_pages(HADDR_TO_LADDR(Pos.FB_addr), Pos.FB_addr, Pos.FB_length / 4096, PAGE_ROOT_RW);
        map_pages(HADDR_TO_LADDR(ioapic_baseaddr), (unsigned long) ioapic_baseaddr, 1,
                  PAGE_UC);
        map_pages(HADDR_TO_LADDR(hpet_attr.baseaddr), hpet_attr.baseaddr, 1, PAGE_UC);
    }

    SET_CR3(HADDR_TO_LADDR(&__PML4T));
    return;
}





