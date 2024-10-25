#include "page.h"
#include "printk.h"
#include "memory.h"
#include "cpu.h"
#include "hpet.h"
#include "ioapic.h"

__attribute__((section(".init_text"))) void init_page(UINT8 bsp_flags) {

    if (bsp_flags) {
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
            pml4t_vbase[i] = pml4_bak[i];             //还原PML4E
        }

        color_printk(ORANGE, BLACK, "OS Can Used Total 4K PAGEs: %ld \tAlloc: %ld \tFree: %ld\n",
                     memory_management.total_pages, memory_management.used_pages,
                     memory_management.avl_pages);
        color_printk(ORANGE, BLACK, "OS StartAddr: %#018lX \tEndAddr: %#018lX \n",
                     memory_management.kernel_start_address, memory_management.kernel_end_address);

        SET_CR3(HADDR_TO_LADDR(&pml4t));

        map_pages(HADDR_TO_LADDR(Pos.FB_addr), (UINT64)Pos.FB_addr, Pos.FB_length / 4096, PAGE_ROOT_RW);
        map_pages(HADDR_TO_LADDR(ioapic_baseaddr), (UINT64) ioapic_baseaddr, 1,
                  PAGE_ROOT_UC);
        map_pages(HADDR_TO_LADDR(hpet.address), hpet.address, 1, PAGE_ROOT_UC);
    }

    SET_CR3(HADDR_TO_LADDR(&pml4t));
    return;
}





