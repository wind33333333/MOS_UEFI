#include "buddy_system.h"

#include "kpage_table.h"
#include "memblock.h"

buddy_system_t buddy_system;

//初始化伙伴系统
INIT_TEXT void init_buddy_system(void) {
    buddy_system.page_table = (page_t*)VMEMMAP_START;
    //初始化空闲链表
    for (UINT64 i = 0; i<=MAX_ORDER; i++) {
        list_head_init(&buddy_system.free_area[i]);
    }

    //初始化vmemmap区为2M页表
    for (UINT32 i = 0; i < memblock.memory.count; i++) {
        UINT64 pa = memblock.memory.region[i].base;
        UINT64 size = memblock.memory.region[i].size;
        UINT64 vmemmap_va = (UINT64)pa_to_page(pa)&PAGE_2M_MASK;
        UINT64 page_count = PAGE_2M_ALIGN((size >> PAGE_4K_SHIFT)*sizeof(page_t))>>PAGE_2M_SHIFT;
        for (UINT64 i = 0; i < page_count; i++) {
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
        UINT64 order = MAX_ORDER;
        while (size >= PAGE_4K_SIZE) {
            //如果地址对齐order地址且长度大于等于order长度等于一个有效块
            if ((pa & (PAGE_4K_SIZE << order) - 1) == 0 && size >= PAGE_4K_SIZE << order) {
                //addr除4096等于page索引，把page索引转成链表地址
                page_t *page = &buddy_system.page_table[pa>>PAGE_4K_SHIFT];
                //添加一个链表节点
                list_add_head(&buddy_system.free_area[order],&page->block);
                //设置page的阶数
                page->order = order;
                pa += PAGE_4K_SIZE << order;
                size -= PAGE_4K_SIZE << order;
                buddy_system.free_count[order]++;
                order = MAX_ORDER;
                continue;
            }
            order--;
        }
    }
}

//伙伴系统物理页分配器
page_t *alloc_pages(UINT32 order) {
    page_t *page;
    UINT32 current_order = order;
    while (TRUE){     //阶链表没有空闲块则分裂
        //如果阶无效直接返回空指针
        if (current_order > MAX_ORDER) return NULL;
        if (buddy_system.free_count[current_order] != 0) {
            page = CONTAINER_OF(buddy_system.free_area[current_order].next,page_t,block);
            list_del(buddy_system.free_area[current_order].next);
            buddy_system.free_count[current_order]--;
            break;
        }
        current_order++;
    }

    while (current_order > order){//分裂得到的阶块到合适大小
        current_order--;
        list_add_head(&buddy_system.free_area[current_order],&page->block);
        page->order = current_order;
        buddy_system.free_count[current_order]++;
        page += 1<<current_order;
        page->order = current_order;
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
        if (list_find(&buddy_system.free_area[page->order],&buddy_page->block) == FALSE) break;
        if (page > buddy_page) page = buddy_page;
        list_del(&buddy_page->block);
        buddy_system.free_count[page->order]--;
        page->order++;
    }
    list_add_head(&buddy_system.free_area[page->order],&page->block);
    buddy_system.free_count[page->order]++;
}
