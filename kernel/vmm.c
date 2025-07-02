#include "vmm.h"
#include "buddy_system.h"

//映射一个页表
INT32 mmap(UINT64 *pml4t, UINT64 pa, void *va, UINT64 attr, UINT64 page_size) {
    UINT64 *pdptt, *pdt, *ptt;
    UINT32 index;
    pml4t = pa_to_va((UINT64) pml4t);

    index = get_pml4e_index(va);
    if (pml4t[index] == 0) {
        pml4t[index] = page_to_pa(alloc_pages(0)) | (attr & (PAGE_US | PAGE_P | PAGE_RW) | PAGE_RW);
        mem_set(pa_to_va(pml4t[index] & PAGE_PA_MASK), 0,PAGE_4K_SIZE);
    }

    pdptt = pa_to_va(pml4t[index] & PAGE_PA_MASK);
    index = get_pdpte_index(va);
    if (page_size == PAGE_1G_SIZE) {
        //1G页
        if (pdptt[index] == 0) {
            pdptt[index] = pa | attr;
            invlpg(va);
            return 0; //1G页映射成功
        }
        return -1; //已被占用
    }

    if (pdptt[index] == 0) {
        pdptt[index] = page_to_pa(alloc_pages(0)) | (attr & (PAGE_US | PAGE_P | PAGE_RW) | PAGE_RW);
        mem_set(pa_to_va(pdptt[index] & PAGE_PA_MASK), 0,PAGE_4K_SIZE);
    }

    pdt = pa_to_va(pdptt[index] & PAGE_PA_MASK);
    index = get_pde_index(va);
    if (page_size == PAGE_2M_SIZE) {
        //2M页
        if (pdt[index] == 0) {
            pdt[index] = pa | attr;
            invlpg(va);
            return 0; //2M页映射成功
        }
        return -1; //以占用
    }

    if (pdt[index] == 0) {
        pdt[index] = page_to_pa(alloc_pages(0)) | (attr & (PAGE_US | PAGE_P | PAGE_RW) | PAGE_RW);
        mem_set(pa_to_va(pdt[index] & PAGE_PA_MASK), 0,PAGE_4K_SIZE);
    }

    ptt = pa_to_va(pdt[index] & PAGE_PA_MASK);
    index = get_pte_index(va);
    if (ptt[index] == 0) {
        ptt[index] = pa | attr;
        invlpg(va);
        return 0; //4K页映射成功
    }
    return -1; //失败
}

//删除一个页表映射
INT32 unmmap(UINT64 *pml4t, void *va, UINT64 page_size) {
    UINT64 *pdptt, *pdt, *ptt;
    UINT32 pml4e_index, pdpte_index, pde_index, pte_index;

    pml4t = pa_to_va((UINT64) pml4t);
    pml4e_index = get_pml4e_index(va);
    if (pml4t[pml4e_index] == 0) return -1; //pml4e无效

    pdptt = pa_to_va(pml4t[pml4e_index] & PAGE_PA_MASK);
    pdpte_index = get_pdpte_index(va);
    if (pdptt[pdpte_index] == 0) return -1; //pdpte无效
    if (page_size == PAGE_1G_SIZE) {
        //如果为1G巨页，跳转到巨页释放
        pdptt[pdpte_index] = 0;
        invlpg(va);
        goto huge_page;
    }

    pdt = pa_to_va(pdptt[pdpte_index] & PAGE_PA_MASK);
    pde_index = get_pde_index(va);
    if (pdt[pde_index] == 0) return -1; //pde无效
    if (page_size == PAGE_2M_SIZE) {
        //如果等于1则表示该页为2M大页，跳转到大页释放
        pdt[pde_index] = 0;
        invlpg(va);
        goto big_page;
    }

    ptt = pa_to_va(pdt[pde_index] & PAGE_PA_MASK); //4K页
    pte_index = get_pte_index(va);
    ptt[pte_index] = 0;
    invlpg(va);


    //ptt为空则释放
    if (forward_find_qword(ptt, 512, 0) == 0) {
        free_pages(va_to_page(ptt));
        pdt[pde_index] = 0;
    } else {
        return 0;
    }

big_page:
    //pde为空则释放
    if (forward_find_qword(pdt, 512, 0) == 0) {
        free_pages(va_to_page(pdt));
        pdptt[pdpte_index] = 0;
    } else {
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

//批量映射页表
INT32 mmap_range(UINT64 *pml4t, UINT64 pa, void *va, UINT64 size, UINT64 attr, UINT64 page_size) {
    UINT64 page_count = size / page_size;
    while(page_count--) {
        if (mmap(pml4t, pa, va, attr, page_size)) return -1;
        pa += page_size;
        va += page_size;
    }
    return 0;
}

//批量删除页表映射
INT32 unmmap_range(UINT64 *pml4t, void *va, UINT64 size, UINT64 page_size) {
    UINT64 page_count = size / page_size;
    while (page_count--) {
        if (unmmap(pml4t, va, page_size)) return -1;
        va += page_size;
    }
    return 0;
}

//查找页表项
UINT64 find_page_table_entry(UINT64 *pml4t, void *va, page_level_e page_level) {
    UINT64 *pdptt, *pdt, *ptt;
    UINT32 index;
    pml4t = pa_to_va((UINT64) pml4t);
    index = get_pml4e_index(va);
    if (page_level == pml4e_level || pml4t[index] == 0) return pml4t[index];

    pdptt = pa_to_va(pml4t[index] & PAGE_PA_MASK);
    index = get_pdpte_index(va);
    if (page_level == pdpte_level || pdptt[index] == 0) return pdptt[index];

    pdt = pa_to_va(pdptt[index] & PAGE_PA_MASK);
    index = get_pde_index(va);
    if (page_level == pde_level || pdt[index] == 0) return pdt[index];

    ptt = pa_to_va(pdt[index] & PAGE_PA_MASK);
    index = get_pte_index(va);
    return ptt[index];
}

//修改页表项
UINT32 update_page_table_entry(UINT64 *pml4t, void *va, page_level_e page_level, UINT64 entry) {
    UINT64 *pdptt, *pdt, *ptt;
    UINT32 index;
    pml4t = pa_to_va((UINT64) pml4t);
    index = get_pml4e_index(va);
    if (page_level == pml4e_level) {
        pml4t[index] = entry;
        return 0;
    };

    pdptt = pa_to_va(pml4t[index] & PAGE_PA_MASK);
    index = get_pdpte_index(va);
    if (page_level == pdpte_level) {
        pdptt[index] = entry;
        return 0;
    }

    pdt = pa_to_va(pdptt[index] & PAGE_PA_MASK);
    index = get_pde_index(va);
    if (page_level == pde_level) {
        pdt[index] = entry;
        return 0;
    }

    ptt = pa_to_va(pdt[index] & PAGE_PA_MASK);
    index = get_pte_index(va);
    ptt[index] = entry;
    return 0;
}

