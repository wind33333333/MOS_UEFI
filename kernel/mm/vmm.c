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
    // 🛡️ 架构师防御：在触碰任何物理内存之前，先完成所有参数和对齐校验！(Fail-Fast)
    if (page_size == PAGE_1G_SIZE) {
        if (((uint64)va & 0x3FFFFFFF) || (pa & 0x3FFFFFFF)) return -EINVAL;
    } else if (page_size == PAGE_2M_SIZE) {
        if (((uint64)va & 0x1FFFFF) || (pa & 0x1FFFFF)) return -EINVAL;
    } else if (page_size == PAGE_4K_SIZE) {
        if (((uint64)va & 0xFFF) || (pa & 0xFFF)) return -EINVAL;
    } else {
        return -ENOTSUP; // 防止传入非法的页大小导致灾难
    }

    uint32 idx_pml4 = ((uint64)va >> 39) & 0x1FF;
    uint32 idx_pdpt = ((uint64)va >> 30) & 0x1FF;
    uint32 idx_pd   = ((uint64)va >> 21) & 0x1FF;
    uint32 idx_pt   = ((uint64)va >> 12) & 0x1FF;

    uint64 *cur_table = pml4t;
    uint64 raw_entry, next_pa;
    page_t *new_page;

    uint64 dir_attr = PAGE_P | PAGE_RW | (attr & PAGE_US);

    // ===================================================================
    // 【Level 4: PML4】
    // ===================================================================
    raw_entry = cur_table[idx_pml4];
    if (raw_entry & PAGE_P) {
        next_pa = raw_entry & PAGE_PA_MASK;
    } else {
        new_page = alloc_pages(0);
        if (!new_page) return -ENOMEM;
        next_pa = page_to_pa(new_page);
        asm_mem_set(pa_to_va(next_pa), 0, PAGE_4K_SIZE);
        va_to_page(cur_table)->refcount++; // PML4 引用计数 +1
        cur_table[idx_pml4] = next_pa | dir_attr;
    }

    // ===================================================================
    // 【Level 3: PDPT】
    // ===================================================================
    cur_table = pa_to_va(next_pa);
    if (page_size == PAGE_1G_SIZE) {
        if (cur_table[idx_pdpt] & PAGE_P) return -EEXIST;
        va_to_page(cur_table)->refcount++; // PDPT 引用计数 +1
        cur_table[idx_pdpt] = pa | adjust_huge_page_attr(attr);
        return 0;
    }

    raw_entry = cur_table[idx_pdpt];
    if (raw_entry & PAGE_P) {
        next_pa = raw_entry & PAGE_PA_MASK;
    } else {
        new_page = alloc_pages(0);
        if (!new_page) return -ENOMEM;
        next_pa = page_to_pa(new_page);
        asm_mem_set(pa_to_va(next_pa), 0, PAGE_4K_SIZE);
        va_to_page(cur_table)->refcount++; // PDPT 引用计数 +1
        cur_table[idx_pdpt] = next_pa | dir_attr;
    }

    // ===================================================================
    // 【Level 2: PD】
    // ===================================================================
    cur_table = pa_to_va(next_pa);
    if (page_size == PAGE_2M_SIZE) {
        if (cur_table[idx_pd] & PAGE_P) return -EEXIST;
        va_to_page(cur_table)->refcount++; // PD 引用计数 +1
        cur_table[idx_pd] = pa | adjust_huge_page_attr(attr);
        return 0;
    }

    raw_entry = cur_table[idx_pd];
    if (raw_entry & PAGE_P) {
        next_pa = raw_entry & PAGE_PA_MASK;
    } else {
        new_page = alloc_pages(0);
        if (!new_page) return -ENOMEM;
        next_pa = page_to_pa(new_page);
        asm_mem_set(pa_to_va(next_pa), 0, PAGE_4K_SIZE);
        va_to_page(cur_table)->refcount++; // PD 引用计数 +1
        cur_table[idx_pd] = next_pa | dir_attr;
    }

    // ===================================================================
    // 【Level 1: PT】
    // ===================================================================
    cur_table = pa_to_va(next_pa);
    if (cur_table[idx_pt] & PAGE_P) return -EEXIST;
    va_to_page(cur_table)->refcount++; // PT 引用计数 +1
    cur_table[idx_pt] = pa | attr;

    return 0;
}


// 智能级联删除页表映射 (无参数侦测版)
int32 unvmmap(uint64 *pml4t, void *va) {
    uint32 idx_pml4 = ((uint64)va >> 39) & 0x1FF;
    uint32 idx_pdpt = ((uint64)va >> 30) & 0x1FF;
    uint32 idx_pd   = ((uint64)va >> 21) & 0x1FF;
    uint32 idx_pt   = ((uint64)va >> 12) & 0x1FF;

    uint64 *pdptt, *pdt, *ptt;
    uint64 raw_entry;
    page_t *page;

    // 【Level 4: PML4】
    raw_entry = pml4t[idx_pml4];
    if (!(raw_entry & PAGE_P)) return -ENOENT;

    // 【Level 3: PDPT】 自动侦测 1GB
    pdptt = pa_to_va(raw_entry & PAGE_PA_MASK);
    raw_entry = pdptt[idx_pdpt];
    if (!(raw_entry & PAGE_P)) return -ENOENT;

    if (raw_entry & PAGE_PS) { // 1GB 叶子
        pdptt[idx_pdpt] = 0;
        asm_invlpg(va);
        goto cleanup_pdpt;
    }

    // 【Level 2: PD】 自动侦测 2MB
    pdt = pa_to_va(raw_entry & PAGE_PA_MASK);
    raw_entry = pdt[idx_pd];
    if (!(raw_entry & PAGE_P)) return -ENOENT;

    if (raw_entry & PAGE_PS) { // 2MB 叶子
        pdt[idx_pd] = 0;
        asm_invlpg(va);
        goto cleanup_pd;
    }

    // 【Level 1: PT】 4KB 碎页
    ptt = pa_to_va(raw_entry & PAGE_PA_MASK);
    raw_entry = ptt[idx_pt];
    if (!(raw_entry & PAGE_P)) return -ENOENT;

    ptt[idx_pt] = 0;
    asm_invlpg(va);


// ===================================================================
// 🧹 完美对称级联回收引擎 (Bottom-Up)
// ===================================================================
cleanup_pt:
    page = va_to_page(ptt);
    if (--page->refcount != 0) return 0;
    pdt[idx_pd] = 0; // 物理页回收，父级指针断开
    free_pages(page);

cleanup_pd:
    page = va_to_page(pdt);
    if (--page->refcount != 0) return 0;
    pdptt[idx_pdpt] = 0;
    free_pages(page);

cleanup_pdpt:
    page = va_to_page(pdptt);
    if (--page->refcount != 0) return 0;
    pml4t[idx_pml4] = 0;
    free_pages(page);

cleanup_pml4:
    // 【修复 Bug 2】保持引用计数绝对对称！
    va_to_page(pml4t)->refcount--;
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

