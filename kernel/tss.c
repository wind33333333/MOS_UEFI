#include "tss.h"
#include "vmm.h"
#include "gdt.h"
#include "cpu.h"

INIT_TEXT void init_tss(void) {
    //分配tss内存，每个cpu核心需要一个tss 对齐4k,每个tss占104字节
    tss_t *tss_ptr = (tss_t *)pa_to_va(bitmap_alloc_pages(PAGE_4K_ALIGN(cpu_info.logical_processors_number * sizeof(tss_t)) >> PAGE_4K_SHIFT));
    bitmap_map_pages(va_to_pa(tss_ptr),tss_ptr,PAGE_4K_ALIGN(cpu_info.logical_processors_number * sizeof(tss_t)) >> PAGE_4K_SHIFT,PAGE_ROOT_RW_4K);
    mem_set((void*)tss_ptr,0,PAGE_4K_ALIGN(cpu_info.logical_processors_number * sizeof(tss_t)));

    //循环初始化tss,每个tss.rsp0和ist1分配16K栈空间
    for (int i = 0; i < cpu_info.logical_processors_number; i++) {
        tss_ptr[i].reserved0 = 0;
        tss_ptr[i].rsp0 = (UINT64) pa_to_va(bitmap_alloc_pages(4) + PAGE_4K_SIZE * 4);
        bitmap_map_pages(va_to_pa(tss_ptr[i].rsp0)-PAGE_4K_SIZE * 4,(void*)(tss_ptr[i].rsp0-PAGE_4K_SIZE * 4),4,PAGE_ROOT_RW_4K);
        tss_ptr[i].rsp1 = 0;
        tss_ptr[i].rsp2 = 0;
        tss_ptr[i].reserved1 = 0;
        tss_ptr[i].ist1 = (UINT64) pa_to_va(bitmap_alloc_pages(4) + PAGE_4K_SIZE * 4);
        bitmap_map_pages(va_to_pa(tss_ptr[i].ist1)-PAGE_4K_SIZE * 4,(void*)(tss_ptr[i].ist1-PAGE_4K_SIZE * 4),4,PAGE_ROOT_RW_4K);
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
        set_tss_descriptor(TSS_DESCRIPTOR_START_INDEX + i,(UINT64)tss_ptr+i);
    }
    ltr(TSS_DESCRIPTOR_START_INDEX * 16);
}

// 设置 TSS 描述符
void set_tss_descriptor(UINT32 index,UINT64 tss_address) {
    // 低 64 位的描述符
    gdt_ptr.base[index*2] = TSS_TYPE|P|TSS_LIMIT|DPL_0|((tss_address&0xFFFF)<<16)|(((tss_address>>16)&0xFF)<<32)|(((tss_address>>24)&0xFF)<<56);
    // 高 64 位的描述符
    gdt_ptr.base[index*2+1] = tss_address >> 32;                   // BASE 的高 32 位
}
