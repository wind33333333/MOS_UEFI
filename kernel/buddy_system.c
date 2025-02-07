#include "buddy_system.h"
#include "memblock.h"

buddy_system_t buddy_system;

//初始化伙伴系统
INIT_TEXT void init_buddy_system(void) {
    //初始化page_size数量
    buddy_system.page_size = memblock.memory.region[memblock.memory.count-1].base+memblock.memory.region[memblock.memory.count-1].size>>PAGE_4K_SHIFT;
    //初始化page_length长度
    buddy_system.page_length = buddy_system.page_size * sizeof(page_t);
    //page_table分配内存
    buddy_system.page_table = (page_t*)LADDR_TO_HADDR(memblock_alloc(buddy_system.page_length,8));
    //初始化page_table为0
    mem_set(buddy_system.page_table, 0x0, buddy_system.page_length);

    //把memblock中的memory内存移交给伙伴系统管理，memblock_alloc内存分配器退出，由伙伴系统接管物理内存管理。
    for (UINT32 i = 0; i < memblock.memory.count; i++) {
        if (memblock.memory.region[i].size<PAGE_4K_SIZE) continue;
        UINT64 addr = align_up(memblock.memory.region[i].base,PAGE_4K_SIZE);
        UINT64 length = (memblock.memory.region[i].size-(addr-memblock.memory.region[i].base))&PAGE_4K_MASK;
        UINT64 order = MAX_ORDER;
        while (length >= PAGE_4K_SIZE) {
            //如果地址对齐order地址且长度大于等于order长度等于一个有效块
            if ((addr & (PAGE_4K_SIZE << order) - 1) == 0 && length >= PAGE_4K_SIZE << order) {
                //addr除4096等于page索引，把page索引转成链表地址
                list_head_t *new_node = (list_head_t *) (buddy_system.page_table + (addr >> PAGE_4K_SHIFT));
                //添加一个链表节点
                list_add_forward(&buddy_system.free_area[order],new_node);
                //设置page的阶数
                ((page_t *) new_node)->order = order;
                addr += PAGE_4K_SIZE << order;
                length -= PAGE_4K_SIZE << order;
                buddy_system.free_count[order]++;
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
        }else if (buddy_system.free_count[current_order] != 0) {
            page = (page_t*)buddy_system.free_area[current_order].next;
            list_del((list_head_t*)page);
            buddy_system.free_count[current_order]--;
            break;
        }
        current_order++;
    }

    while (current_order > order){//分裂得到的阶块到合适大小
        current_order--;
        list_add_forward( &buddy_system.free_area[current_order],(list_head_t*)page);
        page->order = current_order;
        buddy_system.free_count[current_order]++;
        page += 1<<current_order;
        page->order = current_order;
    }
    return page;
}

//伙伴系统物理页释放器
void buddy_free_pages(page_t *page) {
    //空指针或者被引用了直接返回
    if (page == NULL || page->refcount > 0) return;

    while (page->order < MAX_ORDER) {         //当前阶链表有其他page尝试合并伙伴
        //计算伙伴page
        page_t* buddy_page = buddy_system.page_table+(page-buddy_system.page_table^(1<<page->order));
        if (list_find(&buddy_system.free_area[page->order],(list_head_t*)buddy_page) == FALSE) break;
        if (page > buddy_page) page = buddy_page;
        list_del((list_head_t*)buddy_page);
        buddy_system.free_count[page->order]--;
        page->order++;
    }
    list_add_forward(&buddy_system.free_area[page->order],(list_head_t*)page);
    buddy_system.free_count[page->order]++;
}

//释放物理内存映射虚拟内存
void buddy_unmap_pages(void *virt_addr) {
    UINT64 *pte_vaddr = vaddr_to_pte_vaddr(virt_addr);
    UINT64 *pde_vaddr = vaddr_to_pde_vaddr(virt_addr);
    UINT64 *pdpte_vaddr = vaddr_to_pdpte_vaddr(virt_addr);
    UINT64 *pml4e_vaddr = vaddr_to_pml4e_vaddr(virt_addr);
    page_t *page = phyaddr_to_page(*pte_vaddr & 0x7FFFFFFFFFFFF000UL);
    UINT64 page_count = 1UL << page->order;
    UINT64 count;

    //释放物理页
    buddy_free_pages(page);
    //取消PTE映射的物理页,刷新 TLB
    for (UINT64 i = 0; i < page_count; i++) {
        pte_vaddr[i] = 0;
        invlpg((void *) ((UINT64) virt_addr + (i << PAGE_4K_SHIFT)));
    }

    //PTT为空则释放物理页
    count = calculate_pde_count(virt_addr, page_count);
    for (INT32 i = 0; i < count; i++) {
        if (forward_find_qword((void *) (((UINT64) pte_vaddr & PAGE_4K_MASK) + (i << PAGE_4K_SHIFT)), 512, 0) == 0) {
            page = phyaddr_to_page(pde_vaddr[i] & 0x7FFFFFFFFFFFF000UL);
            buddy_free_pages(page);
            pde_vaddr[i] = 0;
        }
    }

    //PDT为空则释放物理页
    count = calculate_pdpte_count(virt_addr, page_count);
    for (INT32 i = 0; i < count; i++) {
        if (forward_find_qword((void *) (((UINT64) pde_vaddr & PAGE_4K_MASK) + (i << PAGE_4K_SHIFT)), 512, 0) == 0) {
            page = phyaddr_to_page(pdpte_vaddr[i] & 0x7FFFFFFFFFFFF000UL);
            buddy_free_pages(page);
            pdpte_vaddr[i] = 0;
        }
    }

    //PDPTT为空则释放物理页
    count = calculate_pml4e_count(virt_addr, page_count);
    for (INT32 i = 0; i < count; i++) {
        if (forward_find_qword((void *) (((UINT64) pdpte_vaddr & PAGE_4K_MASK) + (i << PAGE_4K_SHIFT)), 512, 0) == 0) {
            page = phyaddr_to_page(pml4e_vaddr[i] & 0x7FFFFFFFFFFFF000UL);
            buddy_free_pages(page);
            pml4e_vaddr[i] = 0;
        }
    }
}

//物理内存映射虚拟内存,如果虚拟地址已被占用则从后面的虚拟内存中找一块可用空间挂载物理内存，并返回更新后的虚拟地址。
void *buddy_map_pages(page_t *page, void *virt_addr, UINT64 attr) {
    UINT64 page_count = 1UL << page->order;
    UINT64 phy_addr = page_to_phyaddr(page);
    UINT64 count;
    while (TRUE) {
        UINT64 *pte_vaddr = vaddr_to_pte_vaddr(virt_addr);
        UINT64 *pde_vaddr = vaddr_to_pde_vaddr(virt_addr);
        UINT64 *pdpte_vaddr = vaddr_to_pdpte_vaddr(virt_addr);
        UINT64 *pml4e_vaddr = vaddr_to_pml4e_vaddr(virt_addr);

        //pml4e为空则挂载物理页
        count = calculate_pml4e_count(virt_addr, page_count);
        for (UINT64 i = 0; i < count; i++) {
            if (pml4e_vaddr[i] == 0) {
                pml4e_vaddr[i] = page_to_phyaddr(buddy_alloc_pages(0)) | (attr & (PAGE_US | PAGE_P | PAGE_RW) | PAGE_RW);
                mem_set((void *) ((UINT64) pdpte_vaddr & PAGE_4K_MASK) + (i << PAGE_4K_SHIFT), 0x0, PAGE_4K_SIZE);
            }
        }

        //PDPE为空则挂载物理页
        count = calculate_pdpte_count(virt_addr, page_count);
        for (UINT64 i = 0; i < count; i++) {
            if (pdpte_vaddr[i] == 0) {
                pdpte_vaddr[i] = page_to_phyaddr(buddy_alloc_pages(0)) | (attr & (PAGE_US | PAGE_P | PAGE_RW) | PAGE_RW);
                mem_set((void *) ((UINT64) pde_vaddr & PAGE_4K_MASK) + (i << PAGE_4K_SHIFT), 0x0, PAGE_4K_SIZE);
            }
        }

        //PDE为空则挂载物理页
        count = calculate_pde_count(virt_addr, page_count);
        for (UINT64 i = 0; i < count; i++) {
            if (pde_vaddr[i] == 0) {
                pde_vaddr[i] = page_to_phyaddr(buddy_alloc_pages(0)) | (attr & (PAGE_US | PAGE_P | PAGE_RW) | PAGE_RW);
                mem_set((void *) ((UINT64) pte_vaddr & PAGE_4K_MASK) + (i << PAGE_4K_SHIFT), 0x0, PAGE_4K_SIZE);
            }
        }

        //如果虚拟地址被占用，则从后面找一块可用的虚拟地址挂载，并返回更新后的虚拟地址。
        count = reverse_find_qword(pte_vaddr, page_count, 0);
        if (count == 0) {
            //PTE挂载物理页，刷新TLB
            for (UINT64 i = 0; i < page_count; i++) {
                pte_vaddr[i] = phy_addr + (i << PAGE_4K_SHIFT) | attr;
                invlpg((void *) ((UINT64) virt_addr & PAGE_4K_MASK) + (i << PAGE_4K_SHIFT));
            }
            return virt_addr;
        } else {
            virt_addr = (void *) ((UINT64) virt_addr + (count << PAGE_4K_SHIFT));
        }
    }
}