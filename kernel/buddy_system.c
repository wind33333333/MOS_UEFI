#include "buddy_system.h"
#include "kernel_page_table.h"
#include "memblock.h"
#include "printk.h"

buddy_system_t buddy_system;

//初始化伙伴系统
INIT_TEXT void init_buddy_system(void) {
    //初始化page_table指针
    buddy_system.page_table = (page_t *) VMEMMAP_START;
    //初始化空闲链表
    for (uint64 i = 0; i <= MAX_ORDER; i++) {
        list_head_init(&buddy_system.free_area[i].list);
    }

    //把memblock中的memory内存移交给伙伴系统管理，memblock_alloc内存分配器退出，由伙伴系统接管物理内存管理。
    for (uint32 i = 0; i < memblock.memory.count; i++) {
        uint64 pa = memblock.memory.region[i].base;
        uint64 count = memblock.memory.region[i].size >> PAGE_4K_SHIFT;
        while (count--) {
            free_pages(pa_to_page(pa));
            pa += PAGE_4K_SIZE;
        }
    }
}

//伙伴系统物理页分配器
page_t *alloc_pages(uint32 order) {
    page_t *page;
    uint32 current_order = order;
    //阶链表没有空闲块则分裂
    while (TRUE) {
        if (current_order > MAX_ORDER) return NULL; //如果阶无效直接返回空指针
        if (buddy_system.free_area[current_order].count) {
            page = CONTAINER_OF(buddy_system.free_area[current_order].list.next, page_t, list);
            list_del(buddy_system.free_area[current_order].list.next);
            buddy_system.free_area[current_order].count--;
            break;
        }
        current_order++;
    }

    //分裂得到的阶块到合适大小
    while (current_order > order) {
        current_order--;
        list_add_head(&buddy_system.free_area[current_order].list, &page->list);
        page->order = current_order;
        buddy_system.free_area[current_order].count++;
        page += 1 << current_order;
        page->order = current_order;
    }

    //如果是复合也则标记头并填充符合页page
    page->flags = 0;
    if (order) page->flags = asm_bts(page->flags,PG_HEAD);
    for (uint32 i = 1; i < (1 << current_order); i++) {
        page[i].compound_head = (uint64) page | 1;
    }

    return page;
}

//伙伴系统物理页释放器
void free_pages(page_t *page) {
    //空指针或者被引用了直接返回
    if (page == NULL || page->refcount > 0) return;

    while (page->order < MAX_ORDER) {
        //当前阶链表有其他page尝试合并伙伴
        //计算伙伴page
        page_t *buddy_page = buddy_system.page_table + (page - buddy_system.page_table ^ (1UL << page->order));
        if (!asm_bt(buddy_page->flags,PG_BUDDY) || buddy_page->order != page->order) break;
        if (page > buddy_page) page = buddy_page;
        list_del(&buddy_page->list);
        buddy_system.free_area[page->order].count--;
        page->order++;
    }
    page->flags = 0;
    page->flags = asm_bts(page->flags,PG_BUDDY);
    list_add_head(&buddy_system.free_area[page->order].list, &page->list);
    buddy_system.free_area[page->order].count++;
}
