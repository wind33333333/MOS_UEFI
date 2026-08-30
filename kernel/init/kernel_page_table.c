#include "kernel_page_table.h"
#include "buddy_system.h"
#include "memblock.h"
#include "printk.h"
#include "../include/vmalloc.h"

vm_space_t kernel_space;

INIT_TEXT void init_kpage_table(void) {
    kernel_space.cr3_root = memblock_alloc_4k();
    uint64 cr4 = asm_get_cr4();
    kernel_space.paging_level = (cr4 & (1UL<<12)) ? 5 : 4;
    kernel_space.ops.alloc_4k = memblock_alloc_4k;
    kernel_space.ops.free_4k = memblock_free_4k;
    kernel_space.ops.phys_to_virt = pa_to_va;
    kernel_space.ops.virt_to_phys = va_to_pa;

    //直接映射区
    for (uint64 i=0;i < direct_mem_map.count;i++) {
        uint64 start_pa = direct_mem_map.region[i].start_pa;
        uint64 size = direct_mem_map.region[i].size;
        vm_map_range(&kernel_space,(uint64)pa_to_va(start_pa),start_pa,size,PAGE_KERNEL_DATA_RW);
    }

    //初始化page映射区，每个page结构64字节。
    for (uint64 i=0;i < page_mem_map.count;i++) {
        uint64 page_va = (uint64)pa_to_page(page_mem_map.region[i].start_pa);
        uint64 page_size = page_mem_map.region[i].size >> 6;
        uint64 start_pa = memblock_alloc(page_size,4096);
        vm_map_range(&kernel_space,page_va,start_pa,page_size,PAGE_KERNEL_DATA_RW);
        asm_mem_set((void*)page_va,0,page_size);
    }

    //.init_text
    vm_map_range(&kernel_space,(uint64)_start_init_text,(uint64)_start_init_text - KERNEL_VA_START,(uint64)_end_init_text - (uint64)_start_init_text,PAGE_KERNEL_CODE);

    //init_data
    vm_map_range(&kernel_space,(uint64)_start_init_data,(uint64)_start_init_data - KERNEL_VA_START,(uint64)_end_init_data - (uint64)_start_init_data,PAGE_KERNEL_DATA_RW);

    //正式内核 .text可读执行
    vm_map_range(&kernel_space,(uint64)_start_text,(uint64)_start_text - KERNEL_VA_START,(uint64)_end_text - (uint64)_start_text,PAGE_KERNEL_CODE);

    //.data-.stack可读写
    vm_map_range(&kernel_space,(uint64)_start_data,(uint64)_start_data - KERNEL_VA_START,(uint64)_end_stack - (uint64)_start_data,PAGE_KERNEL_DATA_RW);

    //设置正式内核页表
    asm_set_cr3(kernel_space.cr3_root);
}
