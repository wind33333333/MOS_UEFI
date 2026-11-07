#include "../include/pmm.h"
#include "../init/memblock_init.h"
#include "slub.h"
#include "../x64/numa_node.h" // 必须引入之前写的 NUMA 拓扑矩阵

buddy_system_t buddy_system;


// =========================================================================
// 分配器：底层 NUMA 感知分裂引擎
// =========================================================================
page_t *alloc_pages_node(uint32 order, uint8 target_nid) {
    if (order > MAX_ORDER) return NULL;

    // 1. 自动感知：如果调用者传 NUMA_NO_NODE，自动推导当前 CPU 所属 Node
    if (target_nid == NUMA_NO_NODE) {
        // 使用汇编宏直接获取当前 CPU 核心的控制块中的 numa_node 属性
        target_nid = THIS_CCB->numa_node;
        // 如果系统还处于超早期，连 CPU 映射都没有，回退到 Node 0
        if (target_nid == 0xFF) target_nid = 0;
    }

    page_t *page = NULL;
    uint8 fallback_nids[CONFIG_NUMA_NODES];
    uint8 fallback_count = 0;

    // 2. 构建基于物理距离的 Fallback（降级）访问队列
    // 首选节点（本地节点）永远拥有最高优先级，强制排在第一位
    fallback_nids[fallback_count++] = target_nid;

    // 收集系统中所有其他的可用节点
    uint8 sort_start_idx = fallback_count; // 记录需要排序的起始下标
    for (uint8 i = 0; i < numa_info.numa_nodes_count; i++) {
        if (i != target_nid) {
            fallback_nids[fallback_count++] = i;
        }
    }

    // 🌟 核心优化：按 NUMA 距离矩阵从近到远排序 (就地冒泡排序)
    // 保护第 0 位 (target_nid) 不参与排序，仅对远端节点按距离重排
    for (uint8 i = sort_start_idx; i < fallback_count; i++) {
        for (uint8 j = i + 1; j < fallback_count; j++) {
            uint8 node_a = fallback_nids[i];
            uint8 node_b = fallback_nids[j];

            // 查表获取这两个备选节点到目标节点的物理距离
            uint8 dist_a = numa_info.numa_distance[target_nid][node_a];
            uint8 dist_b = numa_info.numa_distance[target_nid][node_b];

            // 距离值越小说明物理总线越近
            // 如果节点 B 比节点 A 更近，或者距离相同但 Node ID 更小（维持稳定排序），则交换
            if (dist_b < dist_a || (dist_b == dist_a && node_b < node_a)) {
                fallback_nids[i] = node_b;
                fallback_nids[j] = node_a;
            }
        }
    }

    // 3. 开始分配：优先本地 Node，枯竭则向周边的 Node 借用
    uint32 current_order = order;
    uint8 alloc_nid = target_nid;

    for (uint8 idx = 0; idx < fallback_count; idx++) {
        alloc_nid = fallback_nids[idx];
        numa_zone_t *zone = &buddy_system.zones[alloc_nid];

        current_order = order;
        // 向上攀爬寻找可用高阶块
        while (current_order <= MAX_ORDER) {
            if (zone->free_area[current_order].count > 0) {
                // 找到了！从该节点的该阶链表中摘除
                page = CONTAINER_OF(zone->free_area[current_order].list.next, page_t, list);
                list_del(&page->list);
                zone->free_area[current_order].count--;
                page->flags &= ~(1ULL << PG_BUDDY);
                break;
            }
            current_order++;
        }

        if (page) break; // 如果在这个 Node 借到了，立刻停止去其他 Node 寻找
    }

    if (!page) return NULL; // 整个系统的内存彻底枯竭 (OOM)

    // 4. 向下劈裂 (Split)：如果拿到的是高阶块，切开并把多余的退还给【刚刚借出内存的那个 Node】
    numa_zone_t *alloc_zone = &buddy_system.zones[alloc_nid];
    while (current_order > order) {
        current_order--;

        page_t *buddy_half = page + (1ULL << current_order);
        buddy_half->order = current_order;
        buddy_half->flags |= (1ULL << PG_BUDDY);

        // 🌟 退还给借出者的 Zone
        list_add(&buddy_half->list, &alloc_zone->free_area[current_order].list);
        alloc_zone->free_area[current_order].count++;
    }

    // 5. 构建复合页的头尾机制，并打上物理 Node 烙印
    page->order = order;
    page->flags = 0;

    // 🌟 修复 Bug 2：使用内联掩码函数强制烙印，防止脏数据叠加
    set_page_nid(page, alloc_nid);

    if (order > 0) {
        page->flags |= (1ULL << PG_HEAD);
        for (uint32 i = 1; i < (1ULL << order); i++) {
            page[i].compound_head = (uint64)page | 1;
            page[i].flags = 0;
            // 尾随的子页同样使用掩码函数安全打上归属烙印
            set_page_nid(&page[i], alloc_nid);
        }
    }

    return page;
}



// =========================================================================
// 回收器：核心融合引擎 (严禁跨 Node 融合)
// =========================================================================
void free_pages(page_t *page) {
    if (page == NULL || page->refcount > 0) return;
    if (page->compound_head & 1) return;

    uint32 order = page->order;
    page->flags &= ~(1ULL << PG_HEAD);

    // 🌟 自动寻祖：从 page 自身的 flag 中提取出它当初属于哪个物理 Node
    uint8 nid = (page->flags >> PG_NID_SHIFT) & 0xFF;
    numa_zone_t *zone = &buddy_system.zones[nid];

    // 1. 尝试与周边的伙伴同化并向上攀爬融合
    while (order < MAX_ORDER) {
        uint64 page_idx  = page - buddy_system.page_table;
        uint64 buddy_idx = page_idx ^ (1ULL << order);
        page_t *buddy_page = buddy_system.page_table + buddy_idx;

        // 伙伴不在空闲链表、阶数不匹配、或者【属于不同的 NUMA Node】，直接终止融合！
        uint8 buddy_nid = page_to_nid(buddy_page);
        if (!(buddy_page->flags & (1ULL << PG_BUDDY)) ||
            buddy_page->order != order ||
            buddy_nid != nid) {
            break;
        }

        // 从该 Node 的空闲区中把它硬拽出来合并
        list_del(&buddy_page->list);
        zone->free_area[order].count--;
        buddy_page->flags &= ~(1ULL << PG_BUDDY);

        if (buddy_idx < page_idx) {
            page = buddy_page;
        }
        order++;
    }

    // 2. 融合终了，将最终的超级块打上印记，挂入对应 Node 的圣殿
    page->order = order;
    page->flags |= (1ULL << PG_BUDDY);
    list_add(&page->list, &zone->free_area[order].list);
    zone->free_area[order].count++;
}