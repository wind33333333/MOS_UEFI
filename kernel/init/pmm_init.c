#include "pmm.h"
#include "pmm_init.h"
#include "memblock_init.h"
#include "slub.h"
#include "printk.h"

extern vm_space_t kernel_space;

// =========================================================================
// 伙伴系统 NUMA 感知初始化 (极致降维 O(1) 自动分拣版)
// =========================================================================
void buddy_system_init(void) {
    // 1. 定位 page_map 的虚拟基址
    buddy_system.page_table = (page_t *)vm_layout.page_map_start;

    // 2. 遍历初始化所有 Node 的所有阶空闲链表
    for (uint8 n = 0; n < CONFIG_NUMA_NODES; n++) {
        for (uint64 i = 0; i <= MAX_ORDER; i++) {
            list_head_init(&buddy_system.zones[n].free_area[i].list);
            buddy_system.zones[n].free_area[i].count = 0;
        }
    }

    // 3. 遍历已经被 SRAT 完美盖过戳的 memblock 资产簿
    for (uint32 i = 0; i < memblock.memory.count; i++) {
        uint64 start_pa = memblock.memory.region[i].base;
        uint64 count    = memblock.memory.region[i].size >> 12;
        // 🌟 提取 Memblock 在早期阶段千辛万苦洗出来的物理 Node 归属
        uint8 region_nid = memblock.memory.region[i].nid;

        // 容错：如果有漏网之鱼，强行划归 Node 0 避免游离
        if (region_nid == NUMA_NO_NODE) region_nid = 0;

        uint64 start_idx = pa_to_page(start_pa) - buddy_system.page_table;
        uint64 end_idx   = start_idx + count;

        while (start_idx < end_idx) {
            uint32 order = MAX_ORDER;

            // 智能嗅探当前游标能满足的最高阶数
            while (order > 0) {
                if ((start_idx & ((1ULL << order) - 1)) == 0 && (start_idx + (1ULL << order) <= end_idx)) {
                    break;
                }
                order--;
            }

            page_t *p = buddy_system.page_table + start_idx;

            // 彻底清洗该大块的数据
            p->order = order;
            p->flags = 0;
            p->refcount = 0;

            // 🌟 核心注入：在交接给伙伴系统前，把 Node ID 烙印在 Page 的标志位里！
            // 这样稍后的 free_pages 就会通过这个标志，自动把它挂接到 buddy_system.zones[region_nid] 里！
            p->flags |= ((uint64)region_nid << PG_NID_SHIFT);

            // 顺带把尾部连续页也打上同样的印记 (用于后续分离合并时的识别)
            for (uint32 j = 1; j < (1ULL << order); j++) {
                (p + j)->flags = 0;
                (p + j)->flags |= ((uint64)region_nid << PG_NID_SHIFT);
            }

            // 扔进回收引擎，实现全自动分拣归位！
            free_pages(p);

            start_idx += (1ULL << order);
        }
    }

    // 4. 将支持 NUMA 的伙伴系统正式绑定到虚拟内存 VMM 回调接口，接管天下
    kernel_space.ops.alloc_pages = alloc_pages;
    kernel_space.ops.free_pages  = free_pages;
    kernel_space.ops.page_to_phys = page_to_pa;
    kernel_space.ops.phys_to_page = pa_to_page;
    kernel_space.ops.phys_to_virt = pa_to_va;
    kernel_space.ops.virt_to_phys = va_to_pa;

    PR_OK("NUMA-Aware Buddy System init success.\n");
}