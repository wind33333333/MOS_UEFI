#include "tss.h"
#include "memory.h"
#include "gdt.h"
#include "cpu.h"

__attribute__((section(".init_text"))) void init_tss(void) {
    //分配tss内存，每个cpu核心需要一个tss 对齐4k,每个tss占104字节
    tss_t *tss_ptr = (tss_t *)LADDR_TO_HADDR(alloc_pages(PAGE_4K_ALIGN(cpu_info.logical_processors_number * 104) >> PAGE_4K_SHIFT));
    mem_set((void*)tss_ptr,0,PAGE_4K_ALIGN(cpu_info.logical_processors_number * 104));

    //循环初始化tss,每个tss.rsp0和ist1分配16K栈空间
    for (int i = 0; i < cpu_info.logical_processors_number; i++) {
        tss_ptr[i].reserved0 = 0;
        tss_ptr[i].rsp0 = (UINT64) LADDR_TO_HADDR(alloc_pages(4) + PAGE_4K_SIZE * 4);
        tss_ptr[i].rsp1 = 0;
        tss_ptr[i].rsp2 = 0;
        tss_ptr[i].reserved1 = 0;
        tss_ptr[i].ist1 = (UINT64) LADDR_TO_HADDR(alloc_pages(4) + PAGE_4K_SIZE * 4);
        tss_ptr[i].ist2 = 0;
        tss_ptr[i].ist3 = 0;
        tss_ptr[i].ist4 = 0;
        tss_ptr[i].ist5 = 0;
        tss_ptr[i].ist6 = 0;
        tss_ptr[i].ist7 = 0;
        tss_ptr[i].reserved2 = 0;
        tss_ptr[i].reserved3 = 0;
        tss_ptr[i].iomap_base = 0;

        //设置gdt tss描述符
        SET_TSS(gdt_ptr.base,TSS_DESCRIPTOR_START_INDEX+ i,tss_ptr + i);
        memory_management.kernel_end_address = tss_ptr[i].ist1;
    }
    LTR(TSS_DESCRIPTOR_START_INDEX * 8);
    return;
}
