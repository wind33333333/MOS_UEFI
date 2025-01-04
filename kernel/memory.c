#include "memory.h"
#include "printk.h"
#include "uefi.h"

global_memory_descriptor_t memory_management;

list_t free_list[ORDER + 1];
UINT64 free_count[ORDER + 1];

__attribute__((section(".init_text"))) void init_memory(void) {
    //查找memmap中可用物理内存并合并，统计总物理内存容量。
    UINT32 mem_map_index = 0;
    for (UINT32 i = 0; i < (boot_info->mem_map_size / boot_info->mem_descriptor_size); i++) {
        // 使用逻辑或 (||) 来判断内存类型
        if (boot_info->mem_map[i].Type == EFI_LOADER_CODE || boot_info->mem_map[i].Type == EFI_LOADER_DATA || boot_info
            ->mem_map[i].Type == EFI_BOOT_SERVICES_CODE || boot_info->mem_map[i].Type == EFI_BOOT_SERVICES_DATA ||
            boot_info->mem_map[i].Type == EFI_CONVENTIONAL_MEMORY || boot_info->mem_map[i].Type ==
            EFI_ACPI_RECLAIM_MEMORY) {
            if (boot_info->mem_map[i].PhysicalStart == (
                    memory_management.mem_map[mem_map_index].address + memory_management.mem_map[mem_map_index].
                    length)) {
                // 合并相邻的内存块
                memory_management.mem_map[mem_map_index].length += (boot_info->mem_map[i].NumberOfPages << 12);
            } else if (memory_management.mem_map[mem_map_index].length != 0) {
                // 开始记录新的内存块
                mem_map_index++;
                memory_management.mem_map[mem_map_index].address = boot_info->mem_map[i].PhysicalStart;
                memory_management.mem_map[mem_map_index].length = boot_info->mem_map[i].NumberOfPages << 12;
            } else {
                // 初始化新的内存块
                memory_management.mem_map[mem_map_index].address = boot_info->mem_map[i].PhysicalStart;
                memory_management.mem_map[mem_map_index].length = boot_info->mem_map[i].NumberOfPages << 12;
            }
            // 更新可用内存大小
            memory_management.avl_mem_size += (boot_info->mem_map[i].NumberOfPages << 12);
            memory_management.mem_map[mem_map_index].type = 1;
        }
    }
    // 最终更新 mem_map 的数量
    memory_management.mem_map_count = mem_map_index + 1;
    //打印可用物理内存和总物理内存
    for (UINT32 i = 0; i < memory_management.mem_map_count; i++) {
        color_printk(ORANGE, BLACK, "%d  Type:%d    Addr:%#018lX    Length:%#018lX\n", i,
                     memory_management.mem_map[i].type, memory_management.mem_map[i].address,
                     memory_management.mem_map[i].length);
    }
    color_printk(ORANGE, BLACK, "Available RAM:%#lX~%ldMB\n", memory_management.avl_mem_size,
                 memory_management.avl_mem_size / 1024 / 1024);

    //初始化page_table起始地址
    memory_management.page_table = (page_t *) kernel_stack_top;
    //初始化page_size数量
    memory_management.page_size = (memory_management.mem_map[memory_management.mem_map_count - 1].address +
                                   memory_management.mem_map[memory_management.mem_map_count - 1].length) >>
                                  PAGE_4K_SHIFT;
    //初始化page_length
    memory_management.page_length = memory_management.page_size * sizeof(page_t);
    //初始化page_table为0
    mem_set(memory_management.page_table, 0x0, memory_management.page_length);

    //初始化伙伴系统数据结构
    for (UINT32 i = 0; i < memory_management.mem_map_count; i++) {
        UINT64 addr, length, order;
        addr = memory_management.mem_map[i].address;
        length = memory_management.mem_map[i].length;
        order = ORDER;
        while (length >= PAGE_4K_SIZE) {
            //如果地址对齐order地址且长度大于等于order长度等于一个有效块
            if ((addr & (PAGE_4K_SIZE << order) - 1) == 0 && length >= PAGE_4K_SIZE << order) {
                //addr除4096等于page索引，把page索引转成链表地址
                list_t *new_node = (list_t *) (memory_management.page_table + (addr >> PAGE_4K_SHIFT));
                //添加一个链表节点
                list_add_forward(new_node, &free_list[order]);
                //设置page的阶数
                ((page_t *) new_node)->order = order;
                addr += PAGE_4K_SIZE << order;
                length -= PAGE_4K_SIZE << order;
                free_count[order]++;
                order = ORDER;
                continue;
            }
            order--;
        }
    }

    free_list[0].next=NULL;
    free_list[0].prev=NULL;
    free_list[1].next=NULL;
    free_list[1].prev=NULL;
    free_list[2].next=NULL;
    free_list[2].prev=NULL;
    free_list[3].next=NULL;
    free_list[3].prev=NULL;
    free_list[4].next=NULL;
    free_list[4].prev=NULL;
    free_list[5].next=NULL;
    free_list[5].prev=NULL;
    free_list[6].next=NULL;
    free_list[6].prev=NULL;
    free_list[7].next=NULL;
    free_list[7].prev=NULL;
    free_list[8].next=NULL;
    free_list[8].prev=NULL;
    free_list[9].next=NULL;
    free_list[9].prev=NULL;
    free_count[0]=NULL;
    free_count[1]=NULL;
    free_count[2]=NULL;
    free_count[3]=NULL;
    free_count[4]=NULL;
    free_count[5]=NULL;
    free_count[6]=NULL;
    free_count[7]=NULL;
    free_count[8]=NULL;
    free_count[9]=NULL;

    page_t *page = buddy_alloc_pages(0);
    UINT64 addr = page_to_phyaddr(page);
    page_t *page1 = buddy_alloc_pages(2);
    UINT64 addr1 = page_to_phyaddr(page1);

    //kernel_end_address结束地址加上bit map对齐4K边界
    memory_management.kernel_start_address = (UINT64) _start_text;
    memory_management.kernel_end_address = PAGE_4K_ALIGN(
        (UINT64)memory_management.bitmap + memory_management.bitmap_length);

    //通过free_pages函数的异或位操作实现把内核bitmap位图标记为已使用空间。
    free_pages(
        HADDR_TO_LADDR(&_start_init_text),
        memory_management.kernel_end_address - (UINT64) _start_init_text >> PAGE_4K_SHIFT);

    //总物理页
    memory_management.total_pages = memory_management.avl_mem_size >> PAGE_4K_SHIFT;
    //已分配物理页
    memory_management.used_pages = (memory_management.mem_map[0].length + (
                                        memory_management.kernel_end_address - (UINT64) _start_init_text) >>
                                    PAGE_4K_SHIFT);
    //空闲物理页
    memory_management.avl_pages = memory_management.total_pages - memory_management.used_pages;

    color_printk(ORANGE, BLACK,
                 "Bitmap:%#lX \tBitmapSize:%#lX \tBitmapLength:%#lX\n",
                 memory_management.bitmap, memory_management.bitmap_size,
                 memory_management.bitmap_length);
    color_printk(ORANGE, BLACK, "Total 4K PAGEs:%ld \tAlloc:%ld \tFree:%ld\n",
                 memory_management.total_pages, memory_management.used_pages,
                 memory_management.avl_pages);
    color_printk(ORANGE, BLACK, "Init Kernel Start Addr:%#lX Official kernel Start Addr:%lX kernel end addr:%#lX\n",
                 _start_init_text, memory_management.kernel_start_address, memory_management.kernel_end_address);
    return;
}

page_t *buddy_alloc_pages(UINT32 order) {
    page_t *page;
    list_t *node;
    UINT64 addr;
    UINT32 current_order = order;

    //如果对应阶链表内有空闲块则直接分配
    if (free_count[order] != NULL) {
        page = (page_t *) free_list[order].next;
        list_del(free_list[order].next);
        free_count[order]--;
        return page;
    }

    //阶链表没有空闲块则分裂
    while (current_order < ORDER) { //向上查找可用的阶块
        current_order++;
        if (free_count[current_order] != NULL) {
            node = free_list[current_order].next;
            list_del(free_list[current_order].next);
            free_count[current_order]--;
            break;
        }
    }

    while (current_order > order) {//分裂得到的阶块
        current_order--;
        list_add_forward(node, &free_list[current_order]);
        ((page_t*)node)->order = current_order;
        free_count[current_order]++;
        addr=page_to_phyaddr((page_t*)node);
        addr += PAGE_4K_SIZE << current_order;
        node=(list_t *)(memory_management.page_table + (addr >> PAGE_4K_SHIFT));
    }
    return (page_t *) node;
}

//物理页分配器
UINT64 alloc_pages(UINT64 page_count) {
    spin_lock(&memory_management.lock);

    if (memory_management.avl_pages < page_count || page_count == 0) {
        memory_management.lock = 0;
        return -1;
    }

    UINT64 start_idx = 0;
    UINT64 current_length = 0;

    for (UINT64 i = 0; i < (memory_management.bitmap_size / 64); i++) {
        if ((memory_management.bitmap[i] == 0xFFFFFFFFFFFFFFFFUL)) {
            current_length = 0;
            continue;
        }

        for (UINT64 j = 0; j < 64; j++) {
            if (memory_management.bitmap[i] & (1UL << j)) {
                current_length = 0;
                continue;
            }

            if (current_length == 0)
                start_idx = i * 64 + j;

            current_length++;

            if (current_length == page_count) {
                for (UINT64 y = 0; y < page_count; y++) {
                    (memory_management.bitmap[(start_idx + y) / 64] |= (1UL << ((start_idx + y) % 64)));
                }

                memory_management.used_pages += page_count;
                memory_management.avl_pages -= page_count;
                memory_management.lock = 0;
                return (start_idx << PAGE_4K_SHIFT); // 找到连续空闲块，返回结果 物理页地址=起始索引*4096
            }
        }
    }

    memory_management.lock = 0;
    return -1; // 没有找到足够大的连续内存块
}

//物理页释放器
void free_pages(UINT64 phy_addr, UINT64 page_count) {
    spin_lock(&memory_management.lock);
    if ((phy_addr + (page_count << PAGE_4K_SHIFT)) >
        (memory_management.mem_map[memory_management.mem_map_count - 1].address +
         memory_management.mem_map[memory_management.mem_map_count - 1].length)) {
        memory_management.lock = 0;
        return;
    }

    for (UINT64 i = 0; i < page_count; i++) {
        (memory_management.bitmap[((phy_addr >> PAGE_4K_SHIFT) + i) / 64] ^= (
             1UL << ((phy_addr >> PAGE_4K_SHIFT) + i) % 64));
    }
    memory_management.used_pages -= page_count;
    memory_management.avl_pages += page_count;
    memory_management.lock = 0;
    return;
}

//释放物理内存映射虚拟内存
void unmap_pages(void *virt_addr, UINT64 page_count) {
    UINT64 *pte_vaddr = vaddr_to_pte_vaddr(virt_addr);
    UINT64 *pde_vaddr = vaddr_to_pde_vaddr(virt_addr);
    UINT64 *pdpte_vaddr = vaddr_to_pdpte_vaddr(virt_addr);
    UINT64 *pml4e_vaddr = vaddr_to_pml4e_vaddr(virt_addr);
    UINT64 count;

    //取消PTE映射的物理页,刷新 TLB
    for (UINT64 i = 0; i < page_count; i++) {
        pte_vaddr[i] = 0;
        invlpg((void *) ((UINT64) virt_addr + (i << PAGE_4K_SHIFT)));
    }

    //PTT为空则释放物理页
    count = calculate_pde_count(virt_addr, page_count);
    for (INT32 i = 0; i < count; i++) {
        if (forward_find_qword((void *) (((UINT64) pte_vaddr & PAGE_4K_MASK) + (i << PAGE_4K_SHIFT)), 512, 0) == 0) {
            free_pages(pde_vaddr[i] & 0x7FFFFFFFFFFFF000UL, 1);
            pde_vaddr[i] = 0;
        }
    }

    //PDT为空则释放物理页
    count = calculate_pdpte_count(virt_addr, page_count);
    for (INT32 i = 0; i < count; i++) {
        if (forward_find_qword((void *) (((UINT64) pde_vaddr & PAGE_4K_MASK) + (i << PAGE_4K_SHIFT)), 512, 0) == 0) {
            free_pages(pdpte_vaddr[i] & 0x7FFFFFFFFFFFF000UL, 1);
            pdpte_vaddr[i] = 0;
        }
    }

    //PDPTT为空则释放物理页
    count = calculate_pml4e_count(virt_addr, page_count);
    for (INT32 i = 0; i < count; i++) {
        if (forward_find_qword((void *) (((UINT64) pdpte_vaddr & PAGE_4K_MASK) + (i << PAGE_4K_SHIFT)), 512, 0) == 0) {
            free_pages(pml4e_vaddr[i] & 0x7FFFFFFFFFFFF000UL, 1);
            pml4e_vaddr[i] = 0;
        }
    }
    return;
}

//物理内存映射虚拟内存,如果虚拟地址已被占用则从后面的虚拟内存中找一块可用空间挂载物理内存，并返回更新后的虚拟地址。
void *map_pages(UINT64 phy_addr, void *virt_addr, UINT64 page_count, UINT64 attr) {
    while (1) {
        UINT64 *pte_vaddr = vaddr_to_pte_vaddr(virt_addr);
        UINT64 *pde_vaddr = vaddr_to_pde_vaddr(virt_addr);
        UINT64 *pdpte_vaddr = vaddr_to_pdpte_vaddr(virt_addr);
        UINT64 *pml4e_vaddr = vaddr_to_pml4e_vaddr(virt_addr);
        UINT64 count;

        //pml4e为空则挂载物理页
        count = calculate_pml4e_count(virt_addr, page_count);
        for (UINT64 i = 0; i < count; i++) {
            if (pml4e_vaddr[i] == 0) {
                pml4e_vaddr[i] = alloc_pages(1) | (attr & (PAGE_US | PAGE_P | PAGE_RW) | PAGE_RW);
                //pml4e属性设置为可读可写，其余位保持默认。
                mem_set((void *) ((UINT64) pdpte_vaddr & PAGE_4K_MASK) + (i << PAGE_4K_SHIFT), 0x0, PAGE_4K_SIZE);
            }
        }

        //PDPE为空则挂载物理页
        count = calculate_pdpte_count(virt_addr, page_count);
        for (UINT64 i = 0; i < count; i++) {
            if (pdpte_vaddr[i] == 0) {
                pdpte_vaddr[i] = alloc_pages(1) | (attr & (PAGE_US | PAGE_P | PAGE_RW) | PAGE_RW);
                //pdpte属性设置为可读可写，其余位保持默认。
                mem_set((void *) ((UINT64) pde_vaddr & PAGE_4K_MASK) + (i << PAGE_4K_SHIFT), 0x0, PAGE_4K_SIZE);
            }
        }

        //PDE为空则挂载物理页
        count = calculate_pde_count(virt_addr, page_count);
        for (UINT64 i = 0; i < count; i++) {
            if (pde_vaddr[i] == 0) {
                pde_vaddr[i] = alloc_pages(1) | (attr & (PAGE_US | PAGE_P | PAGE_RW) | PAGE_RW); //pde属性设置为可读可写，其余位保持默认。
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

void *kmaollc(UINT64 size) {
    return 0;
}

int kfree(void *virtual_address) {
    return 0;
}
