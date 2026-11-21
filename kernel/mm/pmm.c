#include "../include/pmm.h"
#include "../x64/cpu.h"
#include "../include/printk.h"
#include "../init/memblock_init.h"
#include "../x64/numa_node.h"

buddy_system_t buddy_system;

// =========================================================================
// 内部辅助：原子引用计数递减 & 标志位安全重置
// =========================================================================
static inline int32 page_ref_dec_and_check(page_t *page) {
    uint32 ref = __atomic_load_n(&page->refcount, __ATOMIC_RELAXED);
    while (1) {
        if (ref == 0) return -1;
        if (__atomic_compare_exchange_n(&page->refcount, &ref, ref - 1,
                                        FALSE, __ATOMIC_ACQ_REL, __ATOMIC_RELAXED)) {
            return (int32)(ref - 1);
        }
    }
}


// =========================================================================
// 底层无锁内核原语 1：从指定 Zone 摘除并劈裂出一个 2^order 块
// 前置条件：调用者必须已经持有 zone->lock 且已关本地中断
// =========================================================================
static page_t *__buddy_alloc_locked(numa_zone_t *zone, uint32 order, uint8 alloc_nid) {
    page_t *page = NULL;
    uint32 current_order = order;

    while (current_order <= MAX_ORDER) {
        if (zone->free_area[current_order].count > 0) {
            page = CONTAINER_OF(zone->free_area[current_order].list.next, page_t, list);
            list_del(&page->list);
            zone->free_area[current_order].count--;
            page->flags &= ~(1ULL << PG_BUDDY);
            break;
        }
        current_order++;
    }

    if (!page) return NULL;

    // 向下劈裂，将右半块退回本 Zone
    while (current_order > order) {
        current_order--;

        page_t *buddy_half = page + (1ULL << current_order);
        page_reset_flags_keep_nid(buddy_half, alloc_nid);
        buddy_half->flags |= (1ULL << PG_BUDDY);
        buddy_half->order = current_order;
        buddy_half->refcount = 0;
        buddy_half->compound_head = 0;

        list_add(&buddy_half->list, &zone->free_area[current_order].list);
        zone->free_area[current_order].count++;
    }

    zone->free_pages_count -= (1ULL << order);
    return page;
}

// =========================================================================
// 底层无锁内核原语 2：将一个 2^order 块融合归还至指定 Zone
// 前置条件：调用者必须已经持有 zone->lock 且已关本地中断，且页面非 PG_BUDDY/PG_PCP
// =========================================================================
void __buddy_free_locked(numa_zone_t *zone, page_t *page, uint32 order, uint8 nid) {
    if (page->flags & ((1ULL << PG_BUDDY) | (1ULL << PG_PCP))) {
        PR_ERROR("PMM PANIC: Double free detected in buddy core! page=%p\n", page);
        return;
    }

    zone->free_pages_count += (1ULL << order);
    uint64 page_idx = (uint64)(page - buddy_system.vmemmap);

    // 向上融合同阶同 Node 伙伴 (vmemmap 按 MAX_ORDER 外扩对齐保证无越界缺页)
    while (order < MAX_ORDER) {
        uint64 buddy_idx = page_idx ^ (1ULL << order);
        page_t *buddy_page = buddy_system.vmemmap + buddy_idx;

        uint8 buddy_nid = page_to_nid(buddy_page);
        if (!(buddy_page->flags & (1ULL << PG_BUDDY)) ||
            buddy_page->order != order ||
            buddy_nid != nid) {
            break;
        }

        list_del(&buddy_page->list);
        zone->free_area[order].count--;
        buddy_page->flags &= ~(1ULL << PG_BUDDY);
        buddy_page->order = 0;

        if (buddy_idx < page_idx) {
            page = buddy_page;
            page_idx = buddy_idx;
        }
        order++;
    }

    page_reset_flags_keep_nid(page, nid);
    page->order = order;
    page->compound_head = 0;
    page->flags |= (1ULL << PG_BUDDY);
    list_add(&page->list, &zone->free_area[order].list);
    zone->free_area[order].count++;
}

// =========================================================================
// PCP 批量填充引擎 (低水位 count == 0 时触发)
// 核心价值：仅获取 1 次 zone->lock，一次性批发 batch 个单页装入本核链表
// 前置条件：本地中断已关闭
// =========================================================================
static uint32 pcp_refill_batch(pcp_pageset_t *pcp) {
    uint8 nid = pcp->nid;
    numa_zone_t *zone = &buddy_system.zones[nid];
    uint32 target = pcp->batch;
    uint32 harvested = 0;

    // 仅加普通自旋锁 (调用方已关本地中断，省去重复 pushfq/popfq)
    spin_lock(&zone->lock);

    while (harvested < target) {
        page_t *page = __buddy_alloc_locked(zone, 0, nid);
        if (!page) {
            break; // 本地 Node 内存已空，有多少拿多少
        }

        // 剥离 Buddy 身份，打上 PG_PCP 印记并挂入本核链表
        // 注意：先清 compound_head 再执行 list_add，兼容 union 内存布局
        page_reset_flags_keep_nid(page, nid);
        page->flags |= (1ULL << PG_PCP);
        page->order = 0;
        page->refcount = 0;
        page->compound_head = 0;

        list_add_tail(&page->list, &pcp->list);
        harvested++;
    }

    spin_unlock(&zone->lock);

    pcp->count += harvested;
    return harvested;
}

// =========================================================================
// PCP 批量回流引擎 (高水位 count 达到 high，或主动排空时触发)
// 核心价值：从链表尾部摘取最“冷”的 count_to_drain 页，仅持锁 1 次批量归还融合
// 前置条件：本地中断已关闭
// =========================================================================
static void pcp_drain_batch(pcp_pageset_t *pcp, uint32 count_to_drain) {
    if (count_to_drain == 0 || pcp->count == 0) return;
    if (count_to_drain > pcp->count) {
        count_to_drain = pcp->count;
    }

    uint8 nid = pcp->nid;
    numa_zone_t *zone = &buddy_system.zones[nid];

    spin_lock(&zone->lock);

    while (count_to_drain > 0 && pcp->count > 0) {
        // 🌟 冷尾淘汰策略：从链表尾部 (list.prev) 摘取最久未使用的冷页归还 Buddy，
        //    把链表头部的 L1/L2 热页留在 PCP 里服务即将到来的分配请求！
        page_t *page = CONTAINER_OF(pcp->list.prev, page_t, list);
        list_del(&page->list);
        pcp->count--;
        count_to_drain--;

        // 清除 PG_PCP 标志，正式交回 Buddy 核心进行伙伴融合
        page->flags &= ~(1ULL << PG_PCP);
        __buddy_free_locked(zone, page, 0, nid);
    }

    spin_unlock(&zone->lock);
}

// =========================================================================
// Per-CPU 初始化接口：BSP 在 buddy_system_init 末尾调用，各 AP 在初始化 CCB 后调用
// =========================================================================
void pcp_init_cpu(void) {
    uint64 irq_flags;
    local_irq_save(&irq_flags);

    pcp_pageset_t *pcp = &this_ccb()->pcp;
    uint8 local_nid = THIS_CCB->numa_node;
    if (local_nid >= buddy_system.active_nodes) {
        local_nid = 0;
    }

    list_head_init(&pcp->list);
    pcp->count      = 0;
    pcp->nid        = local_nid;
    pcp->alloc_hits = 0;
    pcp->free_hits  = 0;

    // 根据本地 NUMA 节点的物理内存规模自适应计算 batch 水位：
    // 经验公式：每 128MB 内存分配 1 个 batch 额度，钳位在 [8, 64] 之间
    // 例：4GB 节点 -> batch = 32, high = 192 (约 768KB/核)
    uint64 node_pages = buddy_system.zones[local_nid].free_pages_count;
    uint32 batch = (uint32)(node_pages >> 15); // node_pages / 32768 (每 128MB 计 1)
    if (batch < PCP_BATCH_MIN) batch = PCP_BATCH_MIN;
    if (batch > PCP_BATCH_MAX) batch = PCP_BATCH_MAX;

    pcp->batch = batch;
    // 最后写入 high (大于 0 即标志本核 PCP 正式激活上线)
    pcp->high  = batch * PCP_HIGH_FACTOR;

    local_irq_restore(irq_flags);
}

// =========================================================================
// 主动排空接口：在 CPU 下线、休眠或严重内存紧张 (OOM 抢救) 时调用
// =========================================================================
void pcp_drain_local(void) {
    uint64 irq_flags;
    local_irq_save(&irq_flags);

    pcp_pageset_t *pcp = &this_ccb()->pcp;
    if (pcp->high > 0 && pcp->count > 0) {
        pcp_drain_batch(pcp, pcp->count);
    }
    local_irq_restore(irq_flags);
}

// =========================================================================
// 分配器：集成 PCP 无锁单页快路径 + NUMA Buddy 慢路径
// =========================================================================
page_t *alloc_pages_node(uint32 order, uint8 target_nid) {
    if (order > MAX_ORDER) return NULL;

    // 1. 推导并归一化目标 NUMA 节点
    uint8 local_nid = THIS_CCB->numa_node;
    if (local_nid >= buddy_system.active_nodes) {
        local_nid = 0;
    }

    if (target_nid == NUMA_NO_NODE) {
        target_nid = local_nid;
    } else if (target_nid >= buddy_system.active_nodes) {
        target_nid = 0;
    }

    // =====================================================================
    // 🚀 快路径：Order 0 且请求本 CPU 本地 NUMA 节点 -> 无锁直取 PCP！
    // =====================================================================
    if (order == 0 && target_nid == local_nid) {
        uint64 irq_flags;
        local_irq_save(&irq_flags);

        pcp_pageset_t *pcp = &this_ccb()->pcp;

        if (pcp->high > 0) {
            // 低水位检测：缓存已空，持锁 1 次批量进货 batch 个页
            if (pcp->count == 0) {
                pcp_refill_batch(pcp);
            }

            if (pcp->count > 0) {
                // 🌟 LIFO 热头弹出：拿取最近刚释放的缓存热页
                page_t *page = CONTAINER_OF(pcp->list.next, page_t, list);
                list_del(&page->list);
                pcp->count--;
                pcp->alloc_hits++;

                local_irq_restore(irq_flags);

                // 锁外初始化页面归属状态 (注意：先 list_del 后清 compound_head)
                page_reset_flags_keep_nid(page, local_nid);
                page->order = 0;
                page->compound_head = 0;
                __atomic_store_n(&page->refcount, 1, __ATOMIC_RELEASE);
                return page;
            }
        }

        // 本核 PCP 未启用，或本地 Node 已彻底枯竭：恢复中断，落入跨节点慢路径借页
        local_irq_restore(irq_flags);
    }

    // =====================================================================
    // 🐢 慢路径：高阶块 (order 大于 0)、显式跨节点分配、或本地节点 OOM 降级借页
    // =====================================================================
    numa_zone_t *local_zone = &buddy_system.zones[target_nid];
    page_t *page = NULL;
    uint8 alloc_nid = target_nid;

    for (uint8 idx = 0; idx < local_zone->fallback_count; idx++) {
        alloc_nid = local_zone->fallback_nids[idx];
        numa_zone_t *zone = &buddy_system.zones[alloc_nid];

        uint64 irq_flags;
        spin_lock_irqsave(&zone->lock, &irq_flags);
        page = __buddy_alloc_locked(zone, order, alloc_nid);
        spin_unlock_irqrestore(&zone->lock, irq_flags);

        if (page) break;
    }

    if (!page) return NULL;

    // 锁外初始化复合页头尾元数据
    page_reset_flags_keep_nid(page, alloc_nid);
    page->order = order;
    page->compound_head = 0;

    if (order > 0) {
        page->flags |= (1ULL << PG_HEAD);
        uint64 num_pages = 1ULL << order;
        for (uint64 i = 1; i < num_pages; i++) {
            page_reset_flags_keep_nid(&page[i], alloc_nid);
            page[i].order = 0;
            page[i].refcount = 0;
            page[i].compound_head = ((uint64)page) | 1ULL;
        }
    }

    __atomic_store_n(&page->refcount, 1, __ATOMIC_RELEASE);
    return page;
}


// =========================================================================
// 回收器：集成 PCP 无锁单页快路径 + 高水位批量回流 + NUMA Buddy 慢路径
// =========================================================================
void free_pages(page_t *page) {
    if (!page) return;

    // 1. 拦截非法传入的复合页尾页
    if (page->compound_head & 1ULL) {
        PR_WARN("PMM WARN: Attempt to free a compound tail page %p, ignored.\n", page);
        return;
    }

    // 2. 原子递减引用计数，统一捕获 Double-Free (包括对 PCP/Buddy 中页面的重复释放)
    int32 remain_ref = page_ref_dec_and_check(page);
    if (remain_ref < 0) {
        PR_ERROR("PMM ERROR: Double free (refcount == 0) on page %p!\n", page);
        return;
    }
    if (remain_ref > 0) {
        return;
    }

    uint32 order = page->order;
    if (order > MAX_ORDER) {
        PR_ERROR("PMM ERROR: Corrupted page order %u on page %p!\n", order, page);
        return;
    }

    uint8 nid = page_to_nid(page);
    if (nid >= buddy_system.active_nodes) {
        PR_ERROR("PMM ERROR: Corrupted page nid %u on page %p!\n", nid, page);
        return;
    }

    // =====================================================================
    // 🚀 快路径：Order 0 且属于当前 CPU 本地 NUMA 节点 -> 无锁压入 PCP！
    //    (若跨节点释放，严禁压入本核 PCP，防止跨 Node 污染本地缓存)
    // =====================================================================
    if (order == 0) {
        uint64 irq_flags;
        local_irq_save(&irq_flags);

        pcp_pageset_t *pcp = &this_ccb()->pcp;

        if (pcp->high > 0 && pcp->nid == nid) {
            if (page->flags & ((1ULL << PG_BUDDY) | (1ULL << PG_PCP))) {
                local_irq_restore(irq_flags);
                PR_ERROR("PMM PANIC: Double free detected in PCP! page=%p\n", page);
                return;
            }

            // 清洗状态并压入链表头部 (保持 Cache 热度)
            page_reset_flags_keep_nid(page, nid);
            page->flags |= (1ULL << PG_PCP);
            page->order = 0;
            page->compound_head = 0;

            list_add(&page->list, &pcp->list);
            pcp->count++;
            pcp->free_hits++;

            // 高水位检查：缓存已满，持锁 1 次将尾部最冷的 batch 个页批量退回 Buddy
            if (pcp->count >= pcp->high) {
                pcp_drain_batch(pcp, pcp->batch);
            }

            local_irq_restore(irq_flags);
            return;
        }

        local_irq_restore(irq_flags);
    }

    // =====================================================================
    // 🐢 慢路径：高阶复合页、跨节点释放、或本核 PCP 未激活 -> 直接归还目标 Zone
    // =====================================================================
    if (order > 0) {
        uint64 num_pages = 1ULL << order;
        for (uint64 i = 1; i < num_pages; i++) {
            page[i].compound_head = 0;
            page[i].refcount = 0;
            page[i].order = 0;
            page_reset_flags_keep_nid(&page[i], nid);
        }
    }

    numa_zone_t *zone = &buddy_system.zones[nid];
    uint64 irq_flags;
    spin_lock_irqsave(&zone->lock,&irq_flags);
    __buddy_free_locked(zone, page, order, nid);
    spin_unlock_irqrestore(&zone->lock, irq_flags);
}

// =========================================================================
// 启动期专用交接接口 (供 buddy_system_init 调用，直接入库 Buddy)
// =========================================================================
void buddy_init_free_block(page_t *page, uint32 order, uint8 nid) {
    if (!page || order > MAX_ORDER) return;
    if (nid >= buddy_system.active_nodes) nid = 0;

    uint64 num_pages = 1ULL << order;
    for (uint64 i = 0; i < num_pages; i++) {
        page[i].refcount = 0;
        page[i].compound_head = 0;
        page[i].order = 0;
        page_reset_flags_keep_nid(&page[i], nid);
    }

    numa_zone_t *zone = &buddy_system.zones[nid];
    uint64 irq_flags;
    spin_lock_irqsave(&zone->lock,&irq_flags);
    __buddy_free_locked(zone, page, order, nid);
    spin_unlock_irqrestore(&zone->lock, irq_flags);
}