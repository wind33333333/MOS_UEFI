#include "memory.h"
#include "slub.h"

//初始化伙伴系统
void buddy_system_init(void) {
    for (UINT32 i = 0; i < memory_management.mem_map_count; i++) {
        UINT64 addr, length, order;
        addr = memory_management.mem_map[i].address;
        length = memory_management.mem_map[i].length;
        order = MAX_ORDER;
        while (length >= PAGE_4K_SIZE) {
            //如果地址对齐order地址且长度大于等于order长度等于一个有效块
            if ((addr & (PAGE_4K_SIZE << order) - 1) == 0 && length >= PAGE_4K_SIZE << order) {
                //addr除4096等于page索引，把page索引转成链表地址
                list_head_t *new_node = (list_head_t *) (memory_management.page_table + (addr >> PAGE_4K_SHIFT));
                //添加一个链表节点
                list_add_forward(&memory_management.free_area[order],new_node);
                //设置page的阶数
                ((page_t *) new_node)->order = order;
                addr += PAGE_4K_SIZE << order;
                length -= PAGE_4K_SIZE << order;
                memory_management.free_count[order]++;
                order = MAX_ORDER;
                continue;
            }
            order--;
        }
    }
}

//伙伴系统物理页分配器
page_t *buddy_alloc_pages(UINT32 order) {
    page_t *page;
    UINT32 current_order = order;
    while (TRUE){     //阶链表没有空闲块则分裂
        if (current_order > MAX_ORDER) { //如果阶无效直接返回空指针
            return NULL;
        }else if (memory_management.free_count[current_order] != 0) {
            page = (page_t*)memory_management.free_area[current_order].next;
            list_del((list_head_t*)page);
            memory_management.free_count[current_order]--;
            break;
        }
        current_order++;
    }

    while (current_order > order){//分裂得到的阶块到合适大小
        current_order--;
        list_add_forward( &memory_management.free_area[current_order],(list_head_t*)page);
        page->order = current_order;
        memory_management.free_count[current_order]++;
        page += 1<<current_order;
    }
    return page;
}

//伙伴系统物理页释放器
void buddy_free_pages(page_t *page) {
    if (page == NULL) {        //空指针直接返回
        return;
    }else if (page->refcount > 0) {  //page引用不为空则计数减1
        page->refcount--;
        return;
    }

    while (page->order < MAX_ORDER) {         //当前阶链表有其他page尝试合并伙伴
        //计算伙伴page
        page_t* buddy_page = memory_management.page_table+(page-memory_management.page_table^(1<<page->order));
        if (list_find(&memory_management.free_area[page->order],(list_head_t*)buddy_page) == FALSE)
            break;
        if (page > buddy_page)
            page = buddy_page;
        list_del((list_head_t*)buddy_page);
        memory_management.free_count[page->order]--;
        page->order++;
    }
    list_add_forward(&memory_management.free_area[page->order],(list_head_t*)page);
    memory_management.free_count[page->order]++;
    return;
}