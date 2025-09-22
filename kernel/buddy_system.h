#pragma once
#include "moslib.h"
#include "slub.h"
#include "vmm.h"

#define MAX_ORDER 10

#define VMEMMAP_START 0xFFFFFC0000000000UL

#define     PG_LOCKED 0      /* 页面锁定 */
#define     PG_SLUB   1      /* SLUB/SLAB 页面，kmalloc 使用 */
#define     PG_HEAD   2      /* 复合页面头部 */
#define     PG_BUDDY  3      /* 伙伴系统空闲页面 */

typedef struct{
    uint64       flags;                // 类型
    uint32       order;                // 阶数
    uint32       refcount;             // 引用此处
    uint32       using_count;          // 当前slab节点已用对象数量
    uint32       free_count;           // 当前slab节点空闲对象数量
    void         *free_list;           // 下一个空闲对象指针
    kmem_cache_t *slub_cache;          // 指向所属kmem_cache
    union {
        list_head_t  list;             // 链表
        uint64       compound_head;    // 复合页头指针 位0为1表示页尾，为0表示页头
    };
}__attribute__((aligned(64))) page_t;


typedef struct {
    uint64 count;
    list_head_t list;
}free_area_t;

typedef struct buddy_system_t {
    page_t* page_table;
    free_area_t free_area[MAX_ORDER + 1];
}buddy_system_t;

extern buddy_system_t buddy_system;

//page地址转换物理地址
static inline uint64 page_to_pa(page_t *page) {
    return (uint64)(page - (page_t*)VMEMMAP_START) << PAGE_4K_SHIFT;
}

//物理地址转换page地址
static inline page_t* pa_to_page(uint64 pa) {
    return (page_t*)VMEMMAP_START+(pa >> PAGE_4K_SHIFT);
}

//page地址转虚拟地址
static inline void *page_to_va(page_t *page) {
    return pa_to_va(page_to_pa(page));
}

//虚拟地址转page地址
static inline page_t *va_to_page(void *va) {
    return pa_to_page(va_to_pa(va));
}

//复合页转页头
static inline struct page_t *compound_head(page_t *page){
    uint64 head = page->compound_head;
    if (head & 1)
        return (page_t*)(head - 1);
    return (page_t*)page;
}


void init_buddy_system(void);
page_t* alloc_pages(uint32 order);
void free_pages(page_t *page);

