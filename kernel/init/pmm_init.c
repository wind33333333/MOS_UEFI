#include "pmm.h"
#include "pmm_init.h"
#include "memblock_init.h"
#include "slub.h"
#include "printk.h"

extern vm_space_t kernel_space;

// =========================================================================
// 伙伴系统初始化 (极致降维 O(1) 加速版)
// =========================================================================
void buddy_system_init(void) {
    // 1. 定位 page_map 的虚拟基址
    buddy_system.page_table = (page_t *)vm_layout.page_map_start;

    // 2. 初始化各阶空闲链表
    for (uint64 i = 0; i <= MAX_ORDER; i++) {
        list_head_init(&buddy_system.free_area[i].list);
        buddy_system.free_area[i].count = 0;
    }

    // 3. 将 memblock 的连续物理大块，以最大对齐阶数高效切片，喂给伙伴系统
    for (uint32 i = 0; i < memblock.free.count; i++) {
        uint64 start_pa = memblock.free.region[i].start_pa;
        uint64 count    = memblock.free.region[i].size >> 12;

        uint64 start_idx = pa_to_page(start_pa) - buddy_system.page_table;
        uint64 end_idx   = start_idx + count;

        while (start_idx < end_idx) {
            uint32 order = MAX_ORDER;

            // 智能嗅探：寻找当前游标能满足的、且不越界的最高对齐阶数！
            while (order > 0) {
                if ((start_idx & ((1ULL << order) - 1)) == 0 && (start_idx + (1ULL << order) <= end_idx)) {
                    break;
                }
                order--;
            }

            // 获取该超级块的首个 page_t
            page_t *p = buddy_system.page_table + start_idx;

            // 彻底清洗该大块的数据，并将它假装成一个刚刚回收的块扔给 free_pages
            p->order = order;
            p->flags = 0;
            p->refcount = 0;

            free_pages(p); // 这里的释放会极为迅速地挂载到对应阶的 free_area 中

            start_idx += (1ULL << order);
        }
    }

    // 4. 将伙伴系统正式绑定到虚拟内存 VMM 回调接口，接管天下
    kernel_space.ops.alloc_pages = alloc_pages;
    kernel_space.ops.free_pages  = free_pages;
    kernel_space.ops.page_to_phys = page_to_pa;
    kernel_space.ops.phys_to_page = pa_to_page;
    kernel_space.ops.phys_to_virt = pa_to_va;
    kernel_space.ops.virt_to_phys = va_to_pa;

    PR_OK("Buddy System Memory success.\n");
}