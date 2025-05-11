#ifndef BUDDY_SYSTEM_H
#define BUDDY_SYSTEM_H
#include "moslib.h"
#include "vmm.h"

#define MAX_ORDER 10

#define VMEMMAP_START 0xFFFFFC0000000000UL

typedef struct{
    UINT32 flags;
    UINT32 order;
    UINT32 refcount;
    UINT32 registers[8];
    list_head_t block;
}__attribute__((aligned(64))) page_t;

typedef struct buddy_system_t {
    page_t* page_table;
    UINT64 page_size;
    UINT64 page_length;

    list_head_t free_area[MAX_ORDER + 1];
    UINT64 free_count[MAX_ORDER + 1];
}buddy_system_t;

extern buddy_system_t buddy_system;

//page地址转换物理地址
static inline UINT64 page_to_pa(page_t *page) {
    return (UINT64)(page - buddy_system.page_table) << PAGE_4K_SHIFT;
}

//物理地址转换page地址
static inline page_t* pa_to_page(UINT64 pa) {
    return buddy_system.page_table+(pa >> PAGE_4K_SHIFT);
}

static inline page_t* _pa_to_page(UINT64 pa) {
    return (page_t*)(VMEMMAP_START+(pa >> PAGE_4K_SHIFT)*sizeof(page_t));
}

//page地址转虚拟地址
static inline void *page_to_va(page_t *page) {
    return pa_to_va(page_to_pa(page));
}

//虚拟地址转page地址
static inline page_t *va_to_page(void *va) {
    return pa_to_page(va_to_pa(va));
}

void init_buddy_system(void);
page_t* alloc_pages(UINT32 order);
void free_pages(page_t *page);


#endif
