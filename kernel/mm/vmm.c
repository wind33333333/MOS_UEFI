#include "../include/vmm.h"
#include "../include/buddy_system.h"
#include "../include/errno.h"


// 提取出一个内联函数，专门解决 PAT 碰撞陷阱
static inline uint64 adjust_huge_page_attr(uint64 attr) {
    uint32 pat = (attr & PAGE_PAT)<<5;
    uint64 huge_attr = attr | pat | PAGE_PS;        // 设置到巨页合法的 PAT 位置,强制打上 PAGE_PS (Bit 7)
    return huge_attr;
}

// 极致性能版：映射虚拟内存 (仅限新映射 P=0 -> P=1)
int32 vmmap(uint64 *pml4t, uint64 pa, void *va, uint64 attr, uint64 page_size) {
    // 🚀 ILP 优化：提前并发计算所有层级的索引
    uint32 idx_pml4 = ((uint64)va >> 39) & 0x1FF;
    uint32 idx_pdpt = ((uint64)va >> 30) & 0x1FF;
    uint32 idx_pd   = ((uint64)va >> 21) & 0x1FF;
    uint32 idx_pt   = ((uint64)va >> 12) & 0x1FF;

    // 中间层目录必备权限：存在 (P)、可写 (RW)、继承最终页的用户态权限 (US)
    uint64 dir_attr = PAGE_P | PAGE_RW | (attr & PAGE_US);

    // 游标变量定义 (Pointer Chasing)
    uint64 *cur_table = pa_to_va((uint64)pml4t); // 当前正在操作的页表虚拟地址
    uint64 raw_entry;                            // 从页表中读出的包含属性的原始值
    uint64 next_pa;                              // 剥离了属性的、纯净的下一级物理基址

    // ===================================================================
    // 【Level 4: PML4 (Page Map Level 4)】
    // ===================================================================
    raw_entry = cur_table[idx_pml4];
    if (!(raw_entry & PAGE_P)) {
        void *new_page = alloc_pages(0);
        if (!new_page) return -ENOMEM;

        // 完美保留你的优化：新分配的页地址本身就是纯净的，直接用，无需按位与
        next_pa = page_to_pa(new_page);
        asm_mem_set(pa_to_va(next_pa), 0, PAGE_4K_SIZE);
        cur_table[idx_pml4] = next_pa | dir_attr;
    } else {
        // 【致命 Bug 修复】：如果目录已存在，必须剥离下方的权限位，还原出纯净物理基址！
        next_pa = raw_entry & PAGE_PA_MASK;
    }

    // ===================================================================
    // 【Level 3: PDPT (Page Directory Pointer Table)】
    // ===================================================================
    cur_table = pa_to_va(next_pa);

    // 1G 巨页拦截
    if (page_size == PAGE_1G_SIZE) {
        if (((uint64)va & 0x3FFFFFFF) || (pa & 0x3FFFFFFF)) return -EINVAL;
        if (cur_table[idx_pdpt] & PAGE_P) return -EEXIST;

        cur_table[idx_pdpt] = pa | adjust_huge_page_attr(attr);
        return 0; // 映射成功
    }

    // 继续向下钻取
    raw_entry = cur_table[idx_pdpt];
    if (!(raw_entry & PAGE_P)) {
        void *new_page = alloc_pages(0);
        if (!new_page) return -ENOMEM;

        next_pa = page_to_pa(new_page);
        asm_mem_set(pa_to_va(next_pa), 0, PAGE_4K_SIZE);
        cur_table[idx_pdpt] = next_pa | dir_attr;
    } else {
        next_pa = raw_entry & PAGE_PA_MASK;
    }

    // ===================================================================
    // 【Level 2: PD (Page Directory)】
    // ===================================================================
    cur_table = pa_to_va(next_pa);

    // 2M 巨页拦截
    if (page_size == PAGE_2M_SIZE) {
        if (((uint64)va & 0x1FFFFF) || (pa & 0x1FFFFF)) return -EINVAL;
        if (cur_table[idx_pd] & PAGE_P) return -EEXIST;

        cur_table[idx_pd] = pa | adjust_huge_page_attr(attr);
        return 0; // 映射成功
    }

    // 继续向下钻取
    raw_entry = cur_table[idx_pd];
    if (!(raw_entry & PAGE_P)) {
        void *new_page = alloc_pages(0);
        if (!new_page) return -ENOMEM;

        next_pa = page_to_pa(new_page);
        asm_mem_set(pa_to_va(next_pa), 0, PAGE_4K_SIZE);
        cur_table[idx_pd] = next_pa | dir_attr;
    } else {
        next_pa = raw_entry & PAGE_PA_MASK;
    }

    // ===================================================================
    // 【Level 1: PT (Page Table)】
    // ===================================================================
    cur_table = pa_to_va(next_pa);

    // 4K 普通页拦截
    if (page_size == PAGE_4K_SIZE) {
        if (((uint64)va & 0xFFF) || (pa & 0xFFF)) return -EINVAL;
        if (cur_table[idx_pt] & PAGE_P) return -EEXIST;
        cur_table[idx_pt] = pa | attr;
        return 0; // 映射成功
    }

    return -ENOTSUP; // 不支持的页大小
}



//删除一个页表映射
int32 unvmmap(uint64 *pml4t, void *va, uint64 page_size) {
    uint64 *pdptt, *pdt, *ptt;
    uint32 pml4e_index, pdpte_index, pde_index, pte_index;

    pml4t = pa_to_va((uint64) pml4t);
    pml4e_index = get_pml4e_index(va);
    if (pml4t[pml4e_index] == 0) return -1; //pml4e无效

    pdptt = pa_to_va(pml4t[pml4e_index] & PAGE_PA_MASK);
    pdpte_index = get_pdpte_index(va);
    if (pdptt[pdpte_index] == 0) return -1; //pdpte无效
    if (page_size == PAGE_1G_SIZE) {
        //如果为1G巨页，跳转到巨页释放
        pdptt[pdpte_index] = 0;
        asm_invlpg(va);
        goto huge_page;
    }

    pdt = pa_to_va(pdptt[pdpte_index] & PAGE_PA_MASK);
    pde_index = get_pde_index(va);
    if (pdt[pde_index] == 0) return -1; //pde无效
    if (page_size == PAGE_2M_SIZE) {
        //如果等于1则表示该页为2M大页，跳转到大页释放
        pdt[pde_index] = 0;
        asm_invlpg(va);
        goto big_page;
    }

    ptt = pa_to_va(pdt[pde_index] & PAGE_PA_MASK); //4K页
    pte_index = get_pte_index(va);
    ptt[pte_index] = 0;
    asm_invlpg(va);


    //ptt为空则释放
    if (asm_forward_find_qword(ptt, 512, 0) == 0) {
        free_pages(va_to_page(ptt));
        pdt[pde_index] = 0;
    } else {
        return 0;
    }

big_page:
    //pde为空则释放
    if (asm_forward_find_qword(pdt, 512, 0) == 0) {
        free_pages(va_to_page(pdt));
        pdptt[pdpte_index] = 0;
    } else {
        return 0;
    }

huge_page:
    //pdpt为空则释放
    if (asm_forward_find_qword(pdptt, 512, 0) == 0) {
        free_pages(va_to_page(pdptt));
        pml4t[pml4e_index] = 0;
    }
    return 0;
}


uint64 vmm_get_phys(uint64 *pml4t, void *va) {
    uint64 *pdptt, *pdt, *ptt;
    uint32 index;

    pml4t = pa_to_va((uint64) pml4t);
    index = get_pml4e_index(va);
    if (!(pml4t[index] & PAGE_P)) return -1UL;

    pdptt = pa_to_va(pml4t[index] & PAGE_PA_MASK);
    index = get_pdpte_index(va);
    uint64 pdpte = pdptt[index];
    if (!(pdpte&PAGE_P)) return -1;
    if (pdpte & PAGE_PS) {// 这是个 1GB 的巨型页！
        return (pdpte & 0x000FFFFFC0000000ULL) | ((uint64)va & 0x3FFFFFFFULL);
    }

    pdt = pa_to_va(pdpte & PAGE_PA_MASK);
    index = get_pde_index(va);
    uint64 pde = pdt[index];
    if (!(pde&PAGE_P)) return -1;
    if (pde & PAGE_PS) {// 这是个 2M 的大页！
        return (pde & 0x000FFFFFFFE00000ULL) | ((uint64)va & 0x1FFFFFULL);
    }

    ptt = pa_to_va(pde & PAGE_PA_MASK);
    index = get_pte_index(va);
    uint64 pte = ptt[index];
    if (!(pte&PAGE_P)) return -1;
    return (pte & PAGE_PA_MASK) | ((uint64)va & 0xFFFULL);

}

//批量映射页表
int32 mmap_range(uint64 *pml4t, uint64 pa, void *va, uint64 size, uint64 attr, uint64 page_size) {
    uint64 page_count = size / page_size;
    while(page_count--) {
        if (vmmap(pml4t, pa, va, attr, page_size)) return -1;
        pa += page_size;
        va += page_size;
    }
    return 0;
}

//批量删除页表映射
int32 unmmap_range(uint64 *pml4t, void *va, uint64 size, uint64 page_size) {
    uint64 page_count = size / page_size;
    while (page_count--) {
        if (unvmmap(pml4t, va, page_size)) return -1;
        va += page_size;
    }
    return 0;
}

//查找页表项
uint64 find_page_table_entry(uint64 *pml4t, void *va, page_level_e page_level) {
    uint64 *pdptt, *pdt, *ptt;
    uint32 index;
    pml4t = pa_to_va((uint64) pml4t);
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
uint32 update_page_table_entry(uint64 *pml4t, void *va, page_level_e page_level, uint64 entry) {
    uint64 *pdptt, *pdt, *ptt;
    uint32 index;
    pml4t = pa_to_va((uint64) pml4t);
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

