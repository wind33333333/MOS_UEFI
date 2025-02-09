#ifndef BUDDY_SYSTEM_H
#define BUDDY_SYSTEM_H
#include "moslib.h"
#include "memory.h"

#define MAX_ORDER 10

typedef struct{
    list_head_t block;
    UINT32 falgs;
    UINT32 order;
    UINT32 refcount;
}page_t;

typedef struct buddy_system_t {
    page_t* page_table;
    UINT64 page_size;
    UINT64 page_length;

    list_head_t free_area[MAX_ORDER + 1];
    UINT64 free_count[MAX_ORDER + 1];
}buddy_system_t;

extern buddy_system_t buddy_system;

//page转换物理地址
static inline UINT64 page_to_phyaddr(page_t *page) {
    return (UINT64)(page - buddy_system.page_table) << PAGE_4K_SHIFT;
}

//物理地址转换page
static inline page_t* phyaddr_to_page(UINT64 phyaddr) {
    return buddy_system.page_table+(phyaddr >> PAGE_4K_SHIFT);
}

void init_buddy_system(void);
page_t* alloc_pages(UINT32 order);
void free_pages(page_t *page);
void buddy_unmap_pages(void *virt_addr);
void *buddy_map_pages(page_t *page, void *virt_addr, UINT64 attr);

#endif
