#include "vmm.h"

#include <stdbool.h>
#include <strings.h>

#include "buddy_system.h"

INT32 vmmap(UINT64 *pml4t, UINT64 pa, void *va, UINT64 attr) {
    UINT64 *pdptt, *pdt, *ptt;
    UINT32 index;
    UINT64 page_size = attr >> 7 & 5; //取attr中的第7位和第9位 0=4K 1=2M 5=1G
    pml4t = pa_to_va(pml4t);

    index = get_pml4e_index(va);
    if (pml4t[index] == 0) {
        pml4t[index] = page_to_pa(alloc_pages(0)) | (attr & (PAGE_US | PAGE_P | PAGE_RW) | PAGE_RW);
        mem_set(pa_to_va(pml4t[index] & 0x7FFFFFFFF000), 0,PAGE_4K_SIZE);
    }

    pdptt = pa_to_va(pml4t[index] & 0x7FFFFFFFF000);
    index = get_pdpte_index(va);
    if (page_size == 5) {//1G页
        if (pdptt[index] == 0) {
            pdptt[index] = pa | attr;
            invlpg(va);
            return 0;    //1G页映射成功
        }
        return -1;   //已被占用
    }

    if (pdptt[index] == 0) {
        pdptt[index] = page_to_pa(alloc_pages(0)) | (attr & (PAGE_US | PAGE_P | PAGE_RW) | PAGE_RW);
        mem_set(pa_to_va(pdptt[index] & 0x7FFFFFFFF000), 0,PAGE_4K_SIZE);
    }

    pdt = pa_to_va(pdptt[index] & 0x7FFFFFFFF000);
    index = get_pde_index(va);
    if (page_size == 1) {//2M页
        if (pdt[index] == 0) {
            pdt[index] = pa | attr;
            invlpg(va);
            return 0;  //2M页映射成功
        }
        return -1;      //以占用
    }

    if (pdt[index] == 0) {
        pdt[index] = page_to_pa(alloc_pages(0)) | (attr & (PAGE_US | PAGE_P | PAGE_RW) | PAGE_RW);
        mem_set(pa_to_va(pdt[index] & 0x7FFFFFFFF000), 0,PAGE_4K_SIZE);
    }

    ptt = pa_to_va(pdt[index] & 0x7FFFFFFFF000);
    index = get_pte_index(va);
    if (ptt[index] == 0) {
        ptt[index] = pa | attr;
        invlpg(va);
        return 0;           //4K页映射成功
    }
    return -1;                //失败
}

INT32 vmunmap(UINT64 *pml4t, void *va) {
    UINT64 *pdptt, *pdt, *ptt;
    UINT32 pml4e_index, pdpte_index, pde_index, pte_index;

    pml4t = pa_to_va(pml4t);
    pml4e_index = get_pml4e_index(va);
    if (pml4t[pml4e_index] == 0) return -1;   //pml4e无效

    pdptt = pa_to_va(pml4t[pml4e_index] & 0x7FFFFFFFF000);
    pdpte_index = get_pdpte_index(va);
    if (pdptt[pdpte_index] == 0) return -1;   //pdpte无效
    if ((pdptt[pdpte_index] >> 7 & 5) == 5) {//如果等于5则表示该页为1G巨页，跳转到巨页释放
        pdptt[pdpte_index] = 0;
        invlpg(va);
        goto huge_page;
    }

    pdt = pa_to_va(pdptt[pdpte_index] & 0x7FFFFFFFF000);
    pde_index = get_pde_index(va);
    if (pdt[pde_index] == 0) return -1;   //pde无效
    if ((pdt[pde_index] >> 7 & 5) == 1) {//如果等于1则表示该页为2M大页，跳转到大页释放
        pdt[pde_index] = 0;
        invlpg(va);
        goto big_page;
    }

    ptt = pa_to_va(pdt[pde_index] & 0x7FFFFFFFF000);        //4K页
    pte_index = get_pte_index(va);
    ptt[pte_index] = 0;
    invlpg(va);


    //ptt为空则释放
    if (forward_find_qword(ptt, 512, 0) == 0) {
        free_pages(va_to_page(ptt));
        pdt[pde_index] = 0;
    }else {
        return 0;
    }

big_page:
    //pde为空则释放
    if (forward_find_qword(pdt, 512, 0) == 0) {
        free_pages(va_to_page(pdt));
        pdptt[pdpte_index] = 0;
    }else {
        return 0;
    }

huge_page:
    //pdpt为空则释放
    if (forward_find_qword(pdptt, 512, 0) == 0) {
        free_pages(va_to_page(pdptt));
        pml4t[pml4e_index] = 0;
    }
    return 0;
}

INT32 vmmap_range(UINT64 *pml4t, UINT64 phy_addr, void *virt_addr, UINT64 length, UINT64 attr) {
    UINT64 page_size, count;
    page_size = attr >> 7 & 5; //取attr中的第7位和第9位 0=4K 1=2M 5=1G
    switch (page_size) {
        case 0:
            page_size = PAGE_4K_SIZE;
        count = PAGE_4K_ALIGN(length) >> PAGE_4K_SHIFT;
        break;
        case 1:
            page_size = PAGE_2M_SIZE;
        count = PAGE_2M_ALIGN(length) >> PAGE_2M_SHIFT;
        break;
        case 5:
            page_size = PAGE_1G_SIZE;
        count = PAGE_1G_ALIGN(length) >> PAGE_1G_SHIFT;
    }

    for (; count > 0; count--) {
        if (vmmap(pml4t, phy_addr, virt_addr, attr) != 0) return -1;
        phy_addr += page_size;
        virt_addr += page_size;
    }
    return 0;
}

INT32 vmunmap_range(UINT64 *pml4t, UINT64 va,UINT64 length,UINT64 page_size) {


}


//释放物理内存映射虚拟内存
void munmap(void *va, UINT64 page_count) {
    UINT64 *pte_vaddr = vaddr_to_pte_vaddr(va);
    UINT64 *pde_vaddr = vaddr_to_pde_vaddr(va);
    UINT64 *pdpte_vaddr = vaddr_to_pdpte_vaddr(va);
    UINT64 *pml4e_vaddr = vaddr_to_pml4e_vaddr(va);
    page_t *page;
    UINT64 count;

    //取消PTE映射的物理页,刷新 TLB
    for (UINT64 i = 0; i < page_count; i++) {
        pte_vaddr[i] = 0;
        invlpg((void *) ((UINT64) va + (i << PAGE_4K_SHIFT)));
    }

    //PTT为空则释放物理页
    count = calculate_pde_count(va, page_count);
    for (INT32 i = 0; i < count; i++) {
        if (forward_find_qword((void *) (((UINT64) pte_vaddr & PAGE_4K_MASK) + (i << PAGE_4K_SHIFT)), 512, 0) == 0) {
            page = pa_to_page(pde_vaddr[i] & 0x7FFFFFFFFFFFF000UL);
            free_pages(page);
            pde_vaddr[i] = 0;
        }
    }

    //PDT为空则释放物理页
    count = calculate_pdpte_count(va, page_count);
    for (INT32 i = 0; i < count; i++) {
        if (forward_find_qword((void *) (((UINT64) pde_vaddr & PAGE_4K_MASK) + (i << PAGE_4K_SHIFT)), 512, 0) == 0) {
            page = pa_to_page(pdpte_vaddr[i] & 0x7FFFFFFFFFFFF000UL);
            free_pages(page);
            pdpte_vaddr[i] = 0;
        }
    }

    //PDPTT为空则释放物理页
    count = calculate_pml4e_count(va, page_count);
    for (INT32 i = 0; i < count; i++) {
        if (forward_find_qword((void *) (((UINT64) pdpte_vaddr & PAGE_4K_MASK) + (i << PAGE_4K_SHIFT)), 512, 0) == 0) {
            page = pa_to_page(pml4e_vaddr[i] & 0x7FFFFFFFFFFFF000UL);
            free_pages(page);
            pml4e_vaddr[i] = 0;
        }
    }
}


//////////////////////////////////////////////////////
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
void bitmap_free_pages(UINT64 pa, UINT64 page_count) {
    spin_lock(&memory_management.lock);
    if ((pa + (page_count << PAGE_4K_SHIFT)) >
        (memory_management.mem_map[memory_management.mem_map_count - 1].address +
         memory_management.mem_map[memory_management.mem_map_count - 1].length)) {
        memory_management.lock = 0;
        return;
    }

    for (UINT64 i = 0; i < page_count; i++) {
        (memory_management.bitmap[((pa >> PAGE_4K_SHIFT) + i) / 64] ^= (
             1UL << ((pa >> PAGE_4K_SHIFT) + i) % 64));
    }
    memory_management.used_pages -= page_count;
    memory_management.avl_pages += page_count;
    memory_management.lock = 0;
    return;
}

//释放物理内存映射虚拟内存
void bitmap_unmap_pages(void *va, UINT64 page_count) {
    UINT64 *pte_vaddr = vaddr_to_pte_vaddr(va);
    UINT64 *pde_vaddr = vaddr_to_pde_vaddr(va);
    UINT64 *pdpte_vaddr = vaddr_to_pdpte_vaddr(va);
    UINT64 *pml4e_vaddr = vaddr_to_pml4e_vaddr(va);
    UINT64 count;

    //取消PTE映射的物理页,刷新 TLB
    for (UINT64 i = 0; i < page_count; i++) {
        pte_vaddr[i] = 0;
        invlpg((void *) ((UINT64) va + (i << PAGE_4K_SHIFT)));
    }

    //PTT为空则释放物理页
    count = calculate_pde_count(va, page_count);
    for (INT32 i = 0; i < count; i++) {
        if (forward_find_qword((void *) (((UINT64) pte_vaddr & PAGE_4K_MASK) + (i << PAGE_4K_SHIFT)), 512, 0) == 0) {
            bitmap_free_pages(pde_vaddr[i] & 0x7FFFFFFFFFFFF000UL, 1);
            pde_vaddr[i] = 0;
        }
    }

    //PDT为空则释放物理页
    count = calculate_pdpte_count(va, page_count);
    for (INT32 i = 0; i < count; i++) {
        if (forward_find_qword((void *) (((UINT64) pde_vaddr & PAGE_4K_MASK) + (i << PAGE_4K_SHIFT)), 512, 0) == 0) {
            bitmap_free_pages(pdpte_vaddr[i] & 0x7FFFFFFFFFFFF000UL, 1);
            pdpte_vaddr[i] = 0;
        }
    }

    //PDPTT为空则释放物理页
    count = calculate_pml4e_count(va, page_count);
    for (INT32 i = 0; i < count; i++) {
        if (forward_find_qword((void *) (((UINT64) pdpte_vaddr & PAGE_4K_MASK) + (i << PAGE_4K_SHIFT)), 512, 0) == 0) {
            bitmap_free_pages(pml4e_vaddr[i] & 0x7FFFFFFFFFFFF000UL, 1);
            pml4e_vaddr[i] = 0;
        }
    }
    return;
}

//物理内存映射虚拟内存,如果虚拟地址已被占用则从后面的虚拟内存中找一块可用空间挂载物理内存，并返回更新后的虚拟地址。
void *bitmap_map_pages(UINT64 pa, void *va, UINT64 page_count, UINT64 attr) {
    while (-1) {
        UINT64 *pte_vaddr = vaddr_to_pte_vaddr(va);
        UINT64 *pde_vaddr = vaddr_to_pde_vaddr(va);
        UINT64 *pdpte_vaddr = vaddr_to_pdpte_vaddr(va);
        UINT64 *pml4e_vaddr = vaddr_to_pml4e_vaddr(va);
        UINT64 count;

        //pml4e为空则挂载物理页
        count = calculate_pml4e_count(va, page_count);
        for (UINT64 i = 0; i < count; i++) {
            if (pml4e_vaddr[i] == 0) {
                pml4e_vaddr[i] = bitmap_alloc_pages(1) | (attr & (PAGE_US | PAGE_P | PAGE_RW) | PAGE_RW);
                //pml4e属性设置为可读可写，其余位保持默认。
                mem_set((void *) ((UINT64) pdpte_vaddr & PAGE_4K_MASK) + (i << PAGE_4K_SHIFT), 0x0, PAGE_4K_SIZE);
            }
        }

        //PDPE为空则挂载物理页
        count = calculate_pdpte_count(va, page_count);
        for (UINT64 i = 0; i < count; i++) {
            if (pdpte_vaddr[i] == 0) {
                pdpte_vaddr[i] = bitmap_alloc_pages(1) | (attr & (PAGE_US | PAGE_P | PAGE_RW) | PAGE_RW);
                //pdpte属性设置为可读可写，其余位保持默认。
                mem_set((void *) ((UINT64) pde_vaddr & PAGE_4K_MASK) + (i << PAGE_4K_SHIFT), 0x0, PAGE_4K_SIZE);
            }
        }

        //PDE为空则挂载物理页
        count = calculate_pde_count(va, page_count);
        for (UINT64 i = 0; i < count; i++) {
            if (pde_vaddr[i] == 0) {
                pde_vaddr[i] = bitmap_alloc_pages(1) | (attr & (PAGE_US | PAGE_P | PAGE_RW) | PAGE_RW);
                //pde属性设置为可读可写，其余位保持默认。
                mem_set((void *) ((UINT64) pte_vaddr & PAGE_4K_MASK) + (i << PAGE_4K_SHIFT), 0x0, PAGE_4K_SIZE);
            }
        }

        //如果虚拟地址被占用，则从后面找一块可用的虚拟地址挂载，并返回更新后的虚拟地址。
        count = reverse_find_qword(pte_vaddr, page_count, 0);
        if (count == 0) {
            //PTE挂载物理页，刷新TLB
            for (UINT64 i = 0; i < page_count; i++) {
                pte_vaddr[i] = pa + (i << PAGE_4K_SHIFT) | attr;
                invlpg((void *) ((UINT64) va & PAGE_4K_MASK) + (i << PAGE_4K_SHIFT));
            }
            return va;
        } else {
            va = (void *) ((UINT64) va + (count << PAGE_4K_SHIFT));
        }
    }
}

/////////////////////////////////////////////////////////////////////////
