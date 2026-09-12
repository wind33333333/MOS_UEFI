#pragma once
#include "moslib.h"
#include "../include/vmm.h"

// 伙伴系统支持的最大阶数 (例如 10 阶对应 4KB * 2^10 = 4MB 的连续大块)
#define MAX_ORDER 10

// -----------------------------------------------------------------------------
// 页标志位枚举 (对应 (1ULL << 标志位))
// -----------------------------------------------------------------------------
#define PG_LOCKED   0      /* 页面被内核强锁，禁止换出 */
#define PG_SLUB     1      /* 页面已交由 SLUB/SLAB 分配器管理，kmalloc 专用 */
#define PG_HEAD     2      /* 复合页面 (Compound Page) 的首节点 */
#define PG_BUDDY    3      /* 页面当前处于闲置状态，归属伙伴系统空闲链表 */

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

/**
 * @brief 某阶伙伴链表的管理头
 */
typedef struct {
    uint64      count;                 // 当前阶的空闲块总数
    list_head_t list;                  // 双向链表头
} free_area_t;

/**
 * @brief 伙伴系统全局状态管理器
 */
typedef struct buddy_system_t {
    page_t*     page_table;            // 指向 Vmemmap (page_t 元数据大数组) 的首地址
    free_area_t free_area[MAX_ORDER + 1]; // 0~10 阶的空闲链表数组
} buddy_system_t;

extern buddy_system_t buddy_system;

// -----------------------------------------------------------------------------
// 高频内联寻址转换引擎
// -----------------------------------------------------------------------------

// page_t 对象地址 转换为 真实物理裸地址
static inline uint64 page_to_pa(page_t *page) {
    return (uint64)(page - (page_t*)vm_layout.page_map_start) << 12;
}

// 真实物理裸地址 转换为 page_t 对象地址
static inline page_t* pa_to_page(uint64 pa) {
    return (page_t*)vm_layout.page_map_start + (pa >> 12);
}



// 获取复合大页的头部 page_t (如果传入的是尾部子节点，自动寻根)
static inline page_t *compound_head(page_t *page) {
    uint64 head = page->compound_head;
    if (head & 1) return (page_t*)(head - 1);
    return page;
}

void buddy_system_init(void);
page_t* alloc_pages(uint32 order);
void free_pages(page_t *page);