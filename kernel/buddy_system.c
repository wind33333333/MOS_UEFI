#include "buddy_system.h"

#include "kpage_table.h"
#include "memblock.h"

buddy_system_t buddy_system;

INIT_TEXT static inline UINT32 get_trailing_zeros(UINT64 page_index) {
    if (page_index == 0) return 64;
    return __builtin_ctzll(page_index);
}

INIT_TEXT static inline UINT32 get_max_order_for_size(UINT64 num_pages) {
    UINT32 k = 0;
    while ((1ULL << k) <= num_pages && k <= MAX_ORDER) k++;
    return k - 1;
}

//初始化伙伴系统
INIT_TEXT void init_buddy_system(void) {
    //初始化page_table指针
    buddy_system.page_table = (page_t*)VMEMMAP_START;

    //初始化空闲链表
    for (UINT64 i = 0; i<=MAX_ORDER; i++) {
        list_head_init(&buddy_system.free_area[i].list);
    }

    //初始化vmemmap区为2M页表
    for (UINT32 i = 0; i < memblock.memory.count; i++) {
        UINT64 pa = memblock.memory.region[i].base;
        UINT64 size = memblock.memory.region[i].size;
        UINT64 vmemmap_va = (UINT64)pa_to_page(pa)&PAGE_2M_MASK;
        UINT64 pdte_count = PAGE_2M_ALIGN((size >> PAGE_4K_SHIFT)*sizeof(page_t))>>PAGE_2M_SHIFT;
        for (UINT64 i = 0; i < pdte_count; i++) {
            if (find_page_table_entry(kpml4t_ptr, vmemmap_va, pde_level)) {
                vmemmap_va += PAGE_2M_SIZE;
                continue;
            }
            UINT64 pa = (UINT64)memblock_alloc(PAGE_2M_SIZE,PAGE_2M_SIZE);
            memblock_mmap(kpml4t_ptr, pa, vmemmap_va,PAGE_ROOT_RW_2M1G, PAGE_2M_SIZE);
            mem_set((void*)vmemmap_va, 0, PAGE_2M_SIZE);
            vmemmap_va += PAGE_2M_SIZE;
        }
    }

    //把memblock中的memory内存移交给伙伴系统管理，memblock_alloc内存分配器退出，由伙伴系统接管物理内存管理。
    for (UINT32 i = 0; i < memblock.memory.count; i++) {
        UINT64 pa = memblock.memory.region[i].base;
        UINT64 size = memblock.memory.region[i].size;
        while (size >= PAGE_4K_SIZE) {
            UINT64 page_index = pa >> PAGE_4K_SHIFT;
            UINT64 num_pages = size >> PAGE_4K_SHIFT;
            UINT32 k_alignment = get_trailing_zeros(page_index);
            UINT32 k_size = get_max_order_for_size(num_pages);
            UINT32 order = (k_alignment < k_size) ? k_alignment : k_size;
            page_t *page = &buddy_system.page_table[page_index];
            list_add_head(&buddy_system.free_area[order].list, &page->list);
            page->order = order;
            UINT64 block_size = PAGE_4K_SIZE << order;
            pa += block_size;
            size -= block_size;
            buddy_system.free_area[order].count++;
        }
    }
}

//伙伴系统物理页分配器
page_t *alloc_pages(UINT32 order) {
    page_t *page;
    UINT32 current_order = order;
    //阶链表没有空闲块则分裂
    while (TRUE){
        if (current_order > MAX_ORDER) return NULL;        //如果阶无效直接返回空指针
        if (buddy_system.free_area[current_order].count) {
            page = CONTAINER_OF(buddy_system.free_area[current_order].list.next,page_t,list);
            list_del(buddy_system.free_area[current_order].list.next);
            buddy_system.free_area[current_order].count--;
            break;
        }
        current_order++;
    }

    //分裂得到的阶块到合适大小
    while (current_order > order){
        current_order--;
        list_add_head(&buddy_system.free_area[current_order].list,&page->list);
        page->order = current_order;
        buddy_system.free_area[current_order].count++;
        page += 1<<current_order;
        page->order = current_order;
    }

    //如果是复合也则标记填充符合页page
    for (UINT32 i = 1; i < (1 << current_order); i++) {
        page[i].compound_head = (UINT64)page | 1;
    }

    return page;
}

//伙伴系统物理页释放器
void free_pages(page_t *page) {
    //空指针或者被引用了直接返回
    if (page == NULL || page->refcount > 0) return;

    while (page->order < MAX_ORDER) {         //当前阶链表有其他page尝试合并伙伴
        //计算伙伴page
        page_t* buddy_page = buddy_system.page_table+(page-buddy_system.page_table^(1<<page->order));
        if (list_find(&buddy_system.free_area[page->order].list,&buddy_page->list) == FALSE) break;
        if (page > buddy_page) page = buddy_page;
        list_del(&buddy_page->list);
        buddy_system.free_area[page->order].count--;
        page->order++;
    }
    list_add_head(&buddy_system.free_area[page->order].list,&page->list);
    buddy_system.free_area[page->order].count++;
}
