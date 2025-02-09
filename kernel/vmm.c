#include "vmm.h"
#include "printk.h"

global_memory_descriptor_t memory_management;

//物理页分配器
UINT64 bitmap_alloc_pages(UINT64 page_count) {
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
void bitmap_free_pages(UINT64 phy_addr, UINT64 page_count) {
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
            bitmap_free_pages(pde_vaddr[i] & 0x7FFFFFFFFFFFF000UL, 1);
            pde_vaddr[i] = 0;
        }
    }

    //PDT为空则释放物理页
    count = calculate_pdpte_count(virt_addr, page_count);
    for (INT32 i = 0; i < count; i++) {
        if (forward_find_qword((void *) (((UINT64) pde_vaddr & PAGE_4K_MASK) + (i << PAGE_4K_SHIFT)), 512, 0) == 0) {
            bitmap_free_pages(pdpte_vaddr[i] & 0x7FFFFFFFFFFFF000UL, 1);
            pdpte_vaddr[i] = 0;
        }
    }

    //PDPTT为空则释放物理页
    count = calculate_pml4e_count(virt_addr, page_count);
    for (INT32 i = 0; i < count; i++) {
        if (forward_find_qword((void *) (((UINT64) pdpte_vaddr & PAGE_4K_MASK) + (i << PAGE_4K_SHIFT)), 512, 0) == 0) {
            bitmap_free_pages(pml4e_vaddr[i] & 0x7FFFFFFFFFFFF000UL, 1);
            pml4e_vaddr[i] = 0;
        }
    }
    return;
}

//物理内存映射虚拟内存,如果虚拟地址已被占用则从后面的虚拟内存中找一块可用空间挂载物理内存，并返回更新后的虚拟地址。
void *map_pages(UINT64 phy_addr, void *virt_addr, UINT64 page_count, UINT64 attr) {
    while (TRUE) {
        UINT64 *pte_vaddr = vaddr_to_pte_vaddr(virt_addr);
        UINT64 *pde_vaddr = vaddr_to_pde_vaddr(virt_addr);
        UINT64 *pdpte_vaddr = vaddr_to_pdpte_vaddr(virt_addr);
        UINT64 *pml4e_vaddr = vaddr_to_pml4e_vaddr(virt_addr);
        UINT64 count;

        //pml4e为空则挂载物理页
        count = calculate_pml4e_count(virt_addr, page_count);
        for (UINT64 i = 0; i < count; i++) {
            if (pml4e_vaddr[i] == 0) {
                pml4e_vaddr[i] = bitmap_alloc_pages(1) | (attr & (PAGE_US | PAGE_P | PAGE_RW) | PAGE_RW);
                //pml4e属性设置为可读可写，其余位保持默认。
                mem_set((void *) ((UINT64) pdpte_vaddr & PAGE_4K_MASK) + (i << PAGE_4K_SHIFT), 0x0, PAGE_4K_SIZE);
            }
        }

        //PDPE为空则挂载物理页
        count = calculate_pdpte_count(virt_addr, page_count);
        for (UINT64 i = 0; i < count; i++) {
            if (pdpte_vaddr[i] == 0) {
                pdpte_vaddr[i] = bitmap_alloc_pages(1) | (attr & (PAGE_US | PAGE_P | PAGE_RW) | PAGE_RW);
                //pdpte属性设置为可读可写，其余位保持默认。
                mem_set((void *) ((UINT64) pde_vaddr & PAGE_4K_MASK) + (i << PAGE_4K_SHIFT), 0x0, PAGE_4K_SIZE);
            }
        }

        //PDE为空则挂载物理页
        count = calculate_pde_count(virt_addr, page_count);
        for (UINT64 i = 0; i < count; i++) {
            if (pde_vaddr[i] == 0) {
                pde_vaddr[i] = bitmap_alloc_pages(1) | (attr & (PAGE_US | PAGE_P | PAGE_RW) | PAGE_RW); //pde属性设置为可读可写，其余位保持默认。
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
