#pragma once
#include "moslib.h"
#include "../include/vmm.h"
#include "list.h"
#include "../init/memblock_init.h"
#include "../x64/numa_node.h"

// 伙伴系统支持的最大阶数 (例如 10 阶对应 4KB * 2^10 = 4MB 的连续大块)
#define MAX_ORDER 10

// -----------------------------------------------------------------------------
// 页标志位枚举 (对应 (1ULL << 标志位))
// -----------------------------------------------------------------------------
#define PG_LOCKED   0      /* 页面被内核强锁，禁止换出 */
#define PG_SLUB     1      /* 页面已交由 SLUB/SLAB 分配器管理，kmalloc 专用 */
#define PG_HEAD     2      /* 复合页面 (Compound Page) 的首节点 */
#define PG_BUDDY    3      /* 页面当前处于闲置状态，归属伙伴系统空闲链表 */

// NUMA Node ID 占据 flags 的最顶端 8 位 (Bit 56 ~ 63)
#define PG_NID_SHIFT    56
#define PG_NID_MASK     0xFFULL

typedef struct kmem_cache_t kmem_cache_t;

/**
 * @brief 核心物理页描述符 (Page Descriptor)
 * @note  大小必须严格对齐到 64 字节，完美适配 CPU Cache Line，拒绝伪共享！
 */
typedef struct page_t {
    uint64       flags;                // 页状态标志位集合
    uint32       order;                // 当前页块的阶数 (2^order 个连续 4K 页)
    uint32       refcount;             // 引用计数 (多进程共享、COW 写时复制核心)
    uint32       using_count;          // SLUB: 当前 slab 节点已用对象数量
    uint32       free_count;           // SLUB: 当前 slab 节点空闲对象数量
    void         *free_list;           // SLUB: 下一个空闲对象指针
    kmem_cache_t *slub_cache;          // SLUB: 指向所属 kmem_cache 根节点
    union {
        list_head_t  list;             // 伙伴系统空闲链表挂载点
        uint64       compound_head;    // 复合页尾部节点的父级指针 (最低位为 1 表示它是尾节点)
    };
} __attribute__((aligned(64))) page_t;


typedef struct {
    list_head_t list;
    uint32 count;
} free_area_t;

typedef struct {
    uint32      lock;                             // 🌟 SMP + IRQ 并发保护锁
    free_area_t free_area[MAX_ORDER + 1];         // 0 ~ MAX_ORDER 阶空闲链表
    uint64      free_pages_count;                 // 实时空闲 4KB 页统计 (便于监控与水位线判断)

    // 🌟 预计算的按物理距离排序的降级路由表 (彻底消灭热路径排序)
    uint8       fallback_nids[CONFIG_NUMA_NODES]; // [0] 恒为本节点，后续按 SLIT 距离升序
    uint8       fallback_count;                   // 有效备选节点总数
} numa_zone_t;

typedef struct {
    page_t     *vmemmap;
    uint8       active_nodes;                     // 系统实际激活的 NUMA 节点数 (至少为 1)
    numa_zone_t zones[CONFIG_NUMA_NODES];
} buddy_system_t;


// 在 page->flags 标志位定义中增加 PCP 专属状态位
#define PG_PCP           3       // 该页当前寄存在某个 CPU 的 PCP 本地缓存中 (此时 PG_BUDDY 必为 0)

// 默认水位线参数 (可在 pcp_init_cpu 中按节点总内存动态微调)
#define PCP_BATCH_MIN    8       // 极小内存节点下的最小批处理数
#define PCP_BATCH_MAX    64      // 大内存服务器上的最大批处理数
#define PCP_HIGH_FACTOR  6       // 高水位 = batch * 6 (典型值: batch=32, high=192)

// =========================================================================
// Per-CPU 单页缓存结构 (64B 缓存行对齐，彻底杜绝 SMP 伪共享)
// =========================================================================
typedef struct{
    list_head_t list;            // 本核私有 Order 0 空闲页双向链表 (头热尾冷)
    uint32      count;           // 当前缓存的 4KB 页数量
    uint32      high;            // 高水位阈值：count 达到 high 时触发批量回流；0 表示本核 PCP 未启用
    uint32      batch;           // 单次填充 / 回流的批量页数
    uint8       nid;             // 缓存所属的本地 NUMA Node ID (初始化时固化，避免热路径反复推导)
    uint8       _pad[3];
    uint64      alloc_hits;      // 统计：快路径命中次数 (便于性能调优观测)
    uint64      free_hits;       // 统计：快路径回收次数
} pcp_pageset_t;


extern buddy_system_t buddy_system;

/**
 * @brief 为 page_t 安全地烙印 NUMA Node ID
 * @note  必须先清空旧数据，再写入新数据，防止直接按位或 (|) 导致脏数据叠加
 */
static inline void set_page_nid(page_t *page, uint8 nid) {
    // 1. 清理：用反向掩码 ~(0xFF << 56) 将高 8 位挖空置 0，同时绝对保留低 56 位原有标志
    page->flags &= ~(PG_NID_MASK << PG_NID_SHIFT);

    // 2. 写入：将传入的 nid 转换为 64 位并左移到顶端，合并进去
    page->flags |= ((uint64)nid << PG_NID_SHIFT);
}

/**
 * @brief 从 page_t 中快速提取所属的 NUMA Node ID
 */
static inline uint8 page_to_nid(page_t *page) {
    // 右移 56 位，并与 0xFF 掩码做与运算，截取出干净的 Node ID
    return (uint8)((page->flags >> PG_NID_SHIFT) & PG_NID_MASK);
}

// =========================================================================
// 内部辅助：重置页标志位，仅安全保留物理 NUMA Node ID
// =========================================================================
static inline void page_reset_flags_keep_nid(page_t *page, uint8 nid) {
    page->flags = 0;
    set_page_nid(page, nid);
}

// -----------------------------------------------------------------------------
// 高频内联寻址转换引擎
// -----------------------------------------------------------------------------

// page_t 对象地址 转换为 真实物理裸地址
static inline uint64 page_to_pa(page_t *page) {
    return (uint64)(page - (page_t*)vm_layout.vmemmap_start) << 12;
}

// 真实物理裸地址 转换为 page_t 对象地址
static inline page_t* pa_to_page(uint64 pa) {
    return (page_t*)vm_layout.vmemmap_start + (pa >> 12);
}



// 获取复合大页的头部 page_t (如果传入的是尾部子节点，自动寻根)
static inline page_t *compound_head(page_t *page) {
    uint64 head = page->compound_head;
    if (head & 1) return (page_t*)(head - 1);
    return page;
}

page_t *alloc_pages_node(uint32 order, uint8 target_nid);
// 兼容接口：暴露给外界的无状态调用
page_t *alloc_pages(uint32 order) {
    return alloc_pages_node(order, NUMA_NO_NODE);
};
void __buddy_free_locked(numa_zone_t *zone, page_t *page, uint32 order, uint8 nid);
void free_pages(page_t *page);