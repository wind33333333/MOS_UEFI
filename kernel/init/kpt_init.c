#include "kpt_init.h"
#include "slub.h"
#include "memblock_init.h"
#include "printk.h"
#include "../include/vmm.h"
#include "pmm.h"
#include "../mm/tlb.h"
#include "video_init.h"

vm_space_t kernel_space;

// 定义最大合并区域，32 绝对够用，因为这代表物理内存有 32 个超级大断层
#define MAX_PAGEMAP_REGIONS 32

typedef struct {
    uint64 va_start;
    uint64 va_end;
} merged_page_map_t;

static inline void page_map_init() {
    // -------------------------------------------------------------------------
    // 初始化 page_t 映射区 (Vmemmap) - 【扫描合并 + 步进式流式映射架构】
    // -------------------------------------------------------------------------

    merged_page_map_t merged_page_map[MAX_PAGEMAP_REGIONS];
    uint64 merged_count = 0;

    // =========================================================================
    // 阶段一：纯数学推演，扫描、2M 对齐并合并区间 (无物理副作用)
    // =========================================================================
    // （这部分代码你的逻辑堪称完美，一行都不用改，完美解决了碰撞与重叠！）
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
                    PR_ERROR("Page map regions exceeded MAX_PAGEMAP_REGIONS!\n");
                    while(1);
                }
                merged_page_map[merged_count].va_start = va_start;
                merged_page_map[merged_count].va_end   = va_end;
                merged_count++;
            }
        }
    }

    // =========================================================================
    // 阶段二：流式物理映射 (Streaming Execution)
    // 🌟 核心进化：抛弃庞大的执行计划表，每次严格按 1GB 或 2MB 的离散物理页进行分配！
    // =========================================================================
    for (uint64 i = 0; i < merged_count; i++) {
        uint64 va_curr = merged_page_map[i].va_start;
        uint64 va_end  = merged_page_map[i].va_end;

        while (va_curr < va_end) {
            uint64 remaining = va_end - va_curr;
            uint64 step_size;
            uint64 map_flags;

            // 智能嗅探：当前虚拟游标已踩中 1GB 边界，且剩余容量至少有 1GB
            if ((va_curr & PAGE_1G_OFFSET_MASK) == 0 && remaining >= PAGE_1G_SIZE) {
                // 🚨 绝对核心修复：不再一口吞下 remaining，而是严格只切下 1 个巨页！
                step_size = PAGE_1G_SIZE;
                map_flags = PAGE_KERNEL_DATA_RW | SW_FLAG_MAX_1G;
            } else {
                // 不满足 1GB 条件，或者处于尾部碎片区，每次严格只切下 1 个 2MB 大页！
                step_size = PAGE_2M_SIZE;
                map_flags = PAGE_KERNEL_DATA_RW | SW_FLAG_MAX_2M;
            }

            // 1. 无脑向 memblock 索要：由于 step_size 永远被锁死在 1G 或 2M，
            // 物理分配器查找如此小且规整的连续块易如反掌，彻底消灭物理内存碎片化导致的宕机！
            uint64 pa = memblock_alloc(step_size, step_size);
            if (!pa) {
                PR_ERROR("memblock_alloc failed! Step Size: %#lx\n", step_size);
                while(1);
            }

            // 2. 清洗数据
            asm_mem_set(pa_to_va(pa), 0, step_size);

            // 3. 呼叫底层的 VMM 引擎，精准铺下一块大砖！
            vm_map_range(&kernel_space, va_curr, pa, step_size, map_flags);

            // 游标推进
            va_curr += step_size;
        }
    }
}


void kpage_table_init(void) {
    kernel_space.cr3_root = memblock_alloc(4096, PAGE_4K_SIZE);
    kernel_space.paging_level = tmp_paging_level;

    // 临时显存对等映射
    tmp_video_mem_map();

    //直接映射区

    for (uint64 i=0;i < direct_mem_map.count;i++) {
        uint64 start_pa = direct_mem_map.region[i].start_pa;
        uint64 size = direct_mem_map.region[i].size;
        vm_map_range(&kernel_space,(uint64)pa_to_va(start_pa),start_pa,size,PAGE_KERNEL_DATA_RW | SW_FLAG_MAX_1G );

    }

    // page映射区
    page_map_init();

    // .init_text
    uint64 init_text_va = (uint64)_start_init_text;
    uint64 init_text_pa = init_text_va - vm_layout.kernel_start;
    uint64 init_text_sz = (uint64)_end_init_text - init_text_va;
    PR_INFO("  -> .init_text    : VA %#lx -> PA %#lx, Size: %#lx\n", init_text_va, init_text_pa, init_text_sz);
    vm_map_range(&kernel_space, init_text_va, init_text_pa, init_text_sz, PAGE_KERNEL_CODE);

    // .init_rodata
    uint64 init_rodata_va = (uint64)_start_init_rodata;
    uint64 init_rodata_pa = init_rodata_va - vm_layout.kernel_start;
    uint64 init_rodata_sz = (uint64)_end_init_rodata - init_rodata_va;
    PR_INFO("  -> .init_rodata  : VA %#lx -> PA %#lx, Size: 0x%lx\n", init_rodata_va, init_rodata_pa, init_rodata_sz);
    vm_map_range(&kernel_space, init_rodata_va, init_rodata_pa, init_rodata_sz, PAGE_KERNEL_DATA_RW);

    // .init_data
    uint64 init_data_va = (uint64)_start_init_data;
    uint64 init_data_pa = init_data_va - vm_layout.kernel_start;
    uint64 init_data_sz = (uint64)_end_init_bss - init_data_va;
    PR_INFO("  -> .init_data/bss: VA %#lx -> PA %#lx, Size: 0x%lx\n", init_data_va, init_data_pa, init_data_sz);
    vm_map_range(&kernel_space, init_data_va, init_data_pa, init_data_sz, PAGE_KERNEL_DATA_RW);

    // 正式内核 .text可读执行
    uint64 text_va = (uint64)_start_text;
    uint64 text_pa = text_va - vm_layout.kernel_start;
    uint64 text_sz = (uint64)_end_text - text_va;
    PR_INFO("  -> .text         : VA %#lx -> PA %#lx, Size: 0x%lx\n", text_va, text_pa, text_sz);
    vm_map_range(&kernel_space, text_va, text_pa, text_sz, PAGE_KERNEL_CODE);

    // .rodata
    uint64 rodata_va = (uint64)_start_rodata;
    uint64 rodata_pa = rodata_va - vm_layout.kernel_start;
    uint64 rodata_sz = (uint64)_end_rodata - rodata_va;
    PR_INFO("  -> .rodata       : VA %#lx -> PA %#lx, Size: 0x%lx\n", rodata_va, rodata_pa, rodata_sz);
    vm_map_range(&kernel_space, rodata_va, rodata_pa, rodata_sz, PAGE_KERNEL_DATA_RO);

    // .data .bss
    uint64 data_va = (uint64)_start_data;
    uint64 data_pa = data_va - vm_layout.kernel_start;
    uint64 data_sz = (uint64)_end_bss - data_va;
    PR_INFO("  -> .data/bss     : VA %#lx -> PA %#lx, Size: 0x%lx\n", data_va, data_pa, data_sz);
    vm_map_range(&kernel_space, data_va, data_pa, data_sz, PAGE_KERNEL_DATA_RW);

    asm_set_cr3(kernel_space.cr3_root);
    tlb_flush_all();

    // 设置正式内核页表,并刷新tlb
    PR_OK("Kernel page table switch successful, %d level paging enabled,CR3:%lx \n",kernel_space.paging_level, kernel_space.cr3_root);

}
