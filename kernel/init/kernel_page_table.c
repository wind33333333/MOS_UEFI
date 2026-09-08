#include "kernel_page_table.h"
#include "buddy_system.h"
#include "memblock.h"
#include "printk.h"
#include "../include/vmalloc.h"

vm_space_t kernel_space;

// 定义最大合并区域和最大切片计划的数量
#define MAX_PAGEMAP_REGIONS 32
typedef struct {
    uint64 va_start;
    uint64 va_end;
} merged_page_map_t;

#define MAX_PAGEMAP_CHUNKS  128
//映射执行计划表结构体
typedef struct {
    uint64 va_start;
    uint64 size;
    uint64 pa_align;
    uint64 map_flags;
} vmemmap_chunk_t;

INIT_TEXT static inline void init_vmemmap() {
    // -------------------------------------------------------------------------
    // 初始化 page_t 映射区 (Vmemmap) - 【顶级三段式流水线架构】
    // -------------------------------------------------------------------------

    // 阶段一与阶段二的本地沙盘数据结构
    merged_page_map_t merged_page_map[MAX_PAGEMAP_REGIONS];
    uint64 merged_count = 0;

    vmemmap_chunk_t chunk_plan[MAX_PAGEMAP_CHUNKS];
    uint64 chunk_count = 0;

    // =========================================================================
    // 阶段一：纯数学推演，扫描、2M 对齐并合并区间 (无物理副作用)
    // =========================================================================
    for (uint64 i = 0; i < page_mem_map.count; i++) {
        uint64 p_start = page_mem_map.region[i].start_pa;
        uint64 p_end   = p_start + page_mem_map.region[i].size;

        uint64 va_start = (uint64)pa_to_page(p_start);
        uint64 va_end   = (uint64)pa_to_page(p_end);

        va_start = PAGE_2M_ALIGN_DOWN(va_start);
        va_end   = PAGE_2M_ALIGN(va_end);

        if (merged_count == 0) {
            merged_page_map[merged_count].va_start = va_start;
            merged_page_map[merged_count].va_end   = va_end;
            merged_count++;
        } else {
            uint64 last = merged_count - 1;
            if (va_start <= merged_page_map[last].va_end) {
                if (va_end > merged_page_map[last].va_end) {
                    merged_page_map[last].va_end = va_end;
                }
            } else {
                if (merged_count >= MAX_PAGEMAP_REGIONS) {
                    color_printk(RED, BLACK, "FATAL: Vmemmap regions exceeded MAX_VMEMMAP_REGIONS!\n");
                    while(1);
                }
                merged_page_map[merged_count].va_start = va_start;
                merged_page_map[merged_count].va_end   = va_end;
                merged_count++;
            }
        }
    }

    // =========================================================================
    // 阶段二：制定执行计划 (Execution Plan)，滑动切割 1G/2M 块 (无物理副作用)
    // =========================================================================
    for (uint64 i = 0; i < merged_count; i++) {
        uint64 va_curr = merged_page_map[i].va_start;
        uint64 va_end  = merged_page_map[i].va_end;

        while (va_curr < va_end) {
            if (chunk_count >= MAX_PAGEMAP_CHUNKS) {
                color_printk(RED, BLACK, "FATAL: Vmemmap chunks exceeded MAX_VMEMMAP_CHUNKS!\n");
                while(1); // 🌟 防御性前置：如果越界，在触碰物理内存前直接死机，绝不污染系统！
            }

            uint64 remaining = va_end - va_curr;
            vmemmap_chunk_t *chunk = &chunk_plan[chunk_count];
            chunk->va_start = va_curr;

            // 智能嗅探：1GB 黄金躯干 vs 2MB 零碎头尾
            if ((va_curr & PAGE_1G_OFFSET_MASK) == 0 && remaining >= PAGE_1G_SIZE) {
                chunk->size      = remaining & PAGE_1G_MASK; // 提取完整的 1GB 倍数
                chunk->pa_align  = PAGE_1G_SIZE;
                chunk->map_flags = PAGE_KERNEL_DATA_RW | SW_FLAG_MAX_1G;
            } else {
                uint64 next_1g_boundary = (va_curr + PAGE_1G_SIZE) & PAGE_1G_MASK;
                uint64 size_to_boundary = next_1g_boundary - va_curr;

                chunk->size      = (size_to_boundary < remaining) ? size_to_boundary : remaining;
                chunk->pa_align  = PAGE_2M_SIZE;
                chunk->map_flags = PAGE_KERNEL_DATA_RW | SW_FLAG_MAX_2M;
            }

            va_curr += chunk->size;
            chunk_count++;
        }
    }

    // 🌟 内核装X时刻：在物理分配前，清晰地打印出整个内存规划蓝图
    color_printk(BLUE, BLACK, "--- Vmemmap Execution Plan Generated (%d Chunks) ---\n", chunk_count);
    for (uint64 i = 0; i < chunk_count; i++) {
        color_printk(BLUE, BLACK, "[Plan %2d] VA: %#018lx, Size: %4ld MB, Align: %s\n",
                     i, chunk_plan[i].va_start, chunk_plan[i].size >> 20,
                     chunk_plan[i].pa_align == PAGE_1G_SIZE ? "1GB" : "2MB");
    }

    // =========================================================================
    // 阶段三：物理执行 (Physical Execution)，集中分配与映射
    // =========================================================================
    for (uint64 i = 0; i < chunk_count; i++) {
        vmemmap_chunk_t *chunk = &chunk_plan[i];

        // 1. 无脑向 memblock 索要完全符合计划的物理内存
        uint64 pa = memblock_alloc(chunk->size, chunk->pa_align);
        if (!pa) {
            // 虽然有了计划，但如果物理内存真的不够了，依然要拦截
            color_printk(RED, BLACK, "FATAL: memblock_alloc failed for chunk %d! Size: %#lx\n", i, chunk->size);
            while(1);
        }

        // 2. 清洗幽灵数据
        asm_mem_set(pa_to_va(pa), 0, chunk->size);

        // 3. 呼叫底层的 VMM 引擎！此时参数已是完美的形态
        vm_map_range(&kernel_space, chunk->va_start, pa, chunk->size, chunk->map_flags);

        color_printk(GREEN, BLACK, "[Vmemmap] Mapped: VA:%#lx -> PA:%#lx\n", chunk->va_start, pa);
    }
}


INIT_TEXT void kpage_table_init(void) {
    kernel_space.cr3_root = memblock_alloc(4096,PAGE_4K_SIZE);
    kernel_space.paging_level = tmp_paging_level;

    //直接映射区
    for (uint64 i=0;i < direct_mem_map.count;i++) {
        uint64 start_pa = direct_mem_map.region[i].start_pa;
        uint64 size = direct_mem_map.region[i].size;
        vm_map_range(&kernel_space,(uint64)pa_to_va(start_pa),start_pa,size,PAGE_KERNEL_DATA_RW | SW_FLAG_MAX_1G );
        color_printk(GREEN,BLACK,"%d dircect_mem_map start_pa:%#lx size:%#lx \n",i,start_pa,size);
    }

    //page映射区
    init_vmemmap();

    //.init_text
    vm_map_range(&kernel_space,(uint64)_start_init_text,(uint64)_start_init_text - KERNEL_VA_START,(uint64)_end_init_text - (uint64)_start_init_text,PAGE_KERNEL_CODE);

    //init_data
    vm_map_range(&kernel_space,(uint64)_start_init_data,(uint64)_start_init_data - KERNEL_VA_START,(uint64)_end_init_data - (uint64)_start_init_data,PAGE_KERNEL_DATA_RW);

    //正式内核 .text可读执行
    vm_map_range(&kernel_space,(uint64)_start_text,(uint64)_start_text - KERNEL_VA_START,(uint64)_end_text - (uint64)_start_text,PAGE_KERNEL_CODE);

    //.data .bss
    vm_map_range(&kernel_space,(uint64)_start_data,(uint64)_start_data - KERNEL_VA_START,(uint64)_end_bss - (uint64)_start_data,PAGE_KERNEL_DATA_RW);

    //.rodata
    vm_map_range(&kernel_space,(uint64)_start_rodata,(uint64)_start_rodata - KERNEL_VA_START,(uint64)_end_rodata - (uint64)_start_rodata,PAGE_KERNEL_DATA_RO);

    //.stack
    vm_map_range(&kernel_space,(uint64)_start_stack,(uint64)_start_stack - KERNEL_VA_START,(uint64)_end_stack - (uint64)_start_stack,PAGE_KERNEL_DATA_RW);

    //设置正式内核页表,并刷新tlb
    asm_set_cr3(kernel_space.cr3_root);
    uint64 cr4 = asm_get_cr4();
    asm_set_cr4(cr4 & ~(1ULL << 7)); // 翻转 CR4.PGE 刷新全局页
    asm_set_cr4(cr4);
}
