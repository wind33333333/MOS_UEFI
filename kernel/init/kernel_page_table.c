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
        vm_map_range(&kernel_space,(uint64)pa_to_va(start_pa),start_pa,size,PAGE_KERNEL_DATA_RW | SW_FLAG_MAX_1G );
    }

    // 初始化 page 映射区，每个 page 结构 64 字节
    for (uint64 i = 0; i < page_mem_map.count; i++) {
        uint64 page_va = (uint64)pa_to_page(page_mem_map.region[i].start_pa);

        // 1. 计算所需结构体总大小
        uint64 raw_page_size = page_mem_map.region[i].size >> 6;

        // 2. 【核心修复 1】向上对齐到 4KB，满足 vm_map_range 的严苛参数要求
        // 假设 PAGE_4K_ALIGN 宏的作用是 (size + 0xFFF) & ~0xFFF
        uint64 page_size = PAGE_4K_ALIGN(raw_page_size);

        // 3. 【核心修复 2】动态嗅探最佳物理分配对齐 (Smart Alignment)
        // 逻辑：只有当我们需要大页，且虚拟地址(VA)已经满足大页对齐时，
        // 我们才让物理地址(PA)也去对齐大页。否则强求 PA 对齐只是浪费内存。
        uint64 pa_align = PAGE_4K_SIZE; // 兜底 4K 对齐

        if (page_size >= PAGE_1G_SIZE && !(page_va & PAGE_1G_OFFSET_MASK)) {
            pa_align = PAGE_1G_SIZE; // VA 是 1G 对齐的，并且尺寸足够，申请 1G 对齐物理页！
        }
        else if (page_size >= PAGE_2M_SIZE && !(page_va & PAGE_2M_OFFSET_MASK)) {
            pa_align = PAGE_2M_SIZE;   // VA 是 2M 对齐的，并且尺寸足够，申请 2M 对齐物理页！
        }

        // 4. 根据计算出的最佳对齐，向 memblock 索要物理内存
        uint64 start_pa = memblock_alloc(page_size, pa_align);
        if (!start_pa) {
            // 异常处理：内存不足 (Panic)
            color_printk(RED, BLACK, "FATAL: memblock_alloc failed for page_t array!\n");
            while(1);
        }

        // 5. 清零物理内存
        asm_mem_set(pa_to_va(start_pa), 0, page_size);

        // 6. 注入页表，完美利用贪心大页
        vm_map_range(&kernel_space, page_va, start_pa, page_size, PAGE_KERNEL_DATA_RW | SW_FLAG_MAX_1G);
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
