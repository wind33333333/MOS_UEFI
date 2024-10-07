#include "tss.h"

__attribute__((section(".init_text"))) void tssInit(UINT32 cpuId,UINT8 bspFlags) {
    if (bspFlags) {
        tss_ptr.limit = (cpu_info.cores_num * 104 + 0xFFF) & PAGE_4K_MASK;
        tss_ptr.base = (_tss *)LADDR_TO_HADDR(allocPages(tss_ptr.limit >> PAGE_4K_SHIFT));   //分配tss_tables内存

        for (int i = 0; i < cpu_info.cores_num; i++) {
            tss_ptr.base[i].reserved0 = 0;
            tss_ptr.base[i].rsp0 = (UINT64) LADDR_TO_HADDR(allocPages(4) + PAGE_4K_SIZE * 4);
            tss_ptr.base[i].rsp1 = 0;
            tss_ptr.base[i].rsp2 = 0;
            tss_ptr.base[i].reserved1 = 0;
            tss_ptr.base[i].ist1 = (UINT64) LADDR_TO_HADDR(allocPages(4) + PAGE_4K_SIZE * 4);
            tss_ptr.base[i].ist2 = 0;
            tss_ptr.base[i].ist3 = 0;
            tss_ptr.base[i].ist4 = 0;
            tss_ptr.base[i].ist5 = 0;
            tss_ptr.base[i].ist6 = 0;
            tss_ptr.base[i].ist7 = 0;
            tss_ptr.base[i].reserved2 = 0;
            tss_ptr.base[i].reserved3 = 0;
            tss_ptr.base[i].iomap_base = 0;

            SET_TSS(gdt_ptr.base,TSS_START + i,tss_ptr.base + i);
            memoryManagement.kernelEndAddress = tss_ptr.base[i].ist1;
        }
    }

    __asm__ __volatile__(
            "ltr    %w0 \n\t"
            ::"r"((cpuId << 4) + TSS_START * 8):);

    return;
}
