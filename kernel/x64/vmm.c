#include "../include/vmm.h"
#include "../include/buddy_system.h"
#include "../include/errno.h"
#include "../include/printk.h"

//=============================================================== 单个虚拟内存映射接口 ==========================================================================


// 极致性能版：映射虚拟内存 (仅限新映射 P=0 -> P=1)
int32 vmmap(uint64 *pml4t, uint64 va,uint64 pa,  uint64 attr, uint64 page_size) {
    // 🛡️ 架构师防御：在触碰任何物理内存之前，先完成所有参数和对齐校验！(Fail-Fast)
    if (page_size == PAGE_1G_SIZE) {
        if ((va & 0x3FFFFFFF) || (pa & 0x3FFFFFFF)) return -EINVAL;
    } else if (page_size == PAGE_2M_SIZE) {
        if ((va & 0x1FFFFF) || (pa & 0x1FFFFF)) return -EINVAL;
    } else if (page_size == PAGE_4K_SIZE) {
        if ((va & 0xFFF) || (pa & 0xFFF)) return -EINVAL;
    } else {
        return -ENOTSUP; // 防止传入非法的页大小导致灾难
    }

    uint32 idx_pml4 = (va >> 39) & 0x1FF;
    uint32 idx_pdpt = (va >> 30) & 0x1FF;
    uint32 idx_pd   = (va >> 21) & 0x1FF;
    uint32 idx_pt   = (va >> 12) & 0x1FF;

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
int32 unvmmap(uint64 *pml4t, uint64 va) {
    uint32 idx_pml4 = (va >> 39) & 0x1FF;
    uint32 idx_pdpt = (va >> 30) & 0x1FF;
    uint32 idx_pd   = (va >> 21) & 0x1FF;
    uint32 idx_pt   = (va >> 12) & 0x1FF;

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

//=============================================================== ==========================================================================


#define INVALID_PHYS_ADDR (~0ULL) // 定义无效的物理地址 (全 1)

// 获取虚拟地址对应的物理地址 (支持 1G/2M/4K 自动侦测)
uint64 vmm_get_pmm(uint64 *pml4t, void *va) {
    // 1. ILP 提前并发计算所有层级的索引
    uint32 idx_pml4 = ((uint64)va >> 39) & 0x1FF;
    uint32 idx_pdpt = ((uint64)va >> 30) & 0x1FF;
    uint32 idx_pd   = ((uint64)va >> 21) & 0x1FF;
    uint32 idx_pt   = ((uint64)va >> 12) & 0x1FF;

    // 2. 游标变量定义 (复用单指针，降低寄存器压力)
    // 假设传入的 pml4t 是物理基址，转换为内核虚拟地址以便 CPU 访问
    uint64 *cur_table;
    uint64 raw_entry;

    // ===================================================================
    // 【Level 4: PML4】
    // ===================================================================
    raw_entry = pml4t[idx_pml4];
    if (!(raw_entry & PAGE_P)) return INVALID_PHYS_ADDR;

    // ===================================================================
    // 【Level 3: PDPT】
    // ===================================================================
    cur_table = pa_to_va(raw_entry & PAGE_PA_MASK);
    raw_entry = cur_table[idx_pdpt];
    if (!(raw_entry & PAGE_P)) return INVALID_PHYS_ADDR;

    if (raw_entry & PAGE_PS) {
        // 🎯 1GB 巨页命中
        // 计算公式：(去掉属性位后的物理基址 & 屏蔽低30位) | 虚拟地址的低30位偏移
        return (raw_entry & PAGE_PA_MASK & ~0x3FFFFFFFULL) | ((uint64)va & 0x3FFFFFFFULL);
    }

    // ===================================================================
    // 【Level 2: PD】
    // ===================================================================
    cur_table = pa_to_va(raw_entry & PAGE_PA_MASK);
    raw_entry = cur_table[idx_pd];
    if (!(raw_entry & PAGE_P)) return INVALID_PHYS_ADDR;

    if (raw_entry & PAGE_PS) {
        // 🎯 2MB 巨页命中
        // 计算公式：(去掉属性位后的物理基址 & 屏蔽低21位) | 虚拟地址的低21位偏移
        return (raw_entry & PAGE_PA_MASK & ~0x1FFFFFULL) | ((uint64)va & 0x1FFFFFULL);
    }

    // ===================================================================
    // 【Level 1: PT】
    // ===================================================================
    cur_table = pa_to_va(raw_entry & PAGE_PA_MASK);
    raw_entry = cur_table[idx_pt];
    if (!(raw_entry & PAGE_P)) return INVALID_PHYS_ADDR;

    // 🎯 4KB 普通页命中
    // 计算公式：(去掉属性位后的物理基址) | 虚拟地址的低12位偏移
    return (raw_entry & PAGE_PA_MASK) | ((uint64)va & 0xFFFULL);
}


//========================================================== 批量映射虚拟内存接口 =======================================================






// ===================================================================
// 【Level 1: PT 层】 4KB 碎页横向铺砖
// ===================================================================
static inline int32 map_pte_range(uint64 *pde, uint64 *curr_va, uint64 pa, uint64 end, uint64 attr) {
    uint64 va = *curr_va;
    uint64 *ptt;
    page_t *page_pt;
    boolean new_table = FALSE; // 跟踪是否是本轮新建的表

    if (!(*pde & PAGE_P)) {
        page_pt = alloc_pages(0);
        if (!page_pt) { *curr_va = va; return -ENOMEM; }
        uint64 next_pa = page_to_pa(page_pt);
        asm_mem_set(pa_to_va(next_pa), 0, PAGE_4K_SIZE);
        *pde = next_pa | PAGE_P | PAGE_RW | (attr & PAGE_US);
        new_table = TRUE;
    } else {
        page_pt = va_to_page(pa_to_va(*pde & PAGE_PA_MASK));
    }

    ptt = pa_to_va(*pde & PAGE_PA_MASK);
    uint32 idx = get_table_idx(va, PTE_SHIFT);

    do {
        uint64 pte = ptt[idx];
        if (pte & PAGE_P) {
            uint64 old_pa = pte & PAGE_PA_MASK;
            if (old_pa != pa) {
                color_printk(RED, BLACK, "PANIC: VA Collision at PT! VA: %#lx\n", va);
                // 🛡️ 架构师防御：新建了表却寸功未立？当场抹除防泄漏！
                if (new_table && page_pt->refcount == 0) { free_pages(page_pt); *pde = 0; }
                *curr_va = va; return -EEXIST;
            }
            ptt[idx] = pa | attr | PAGE_P; // 幂等更新
        } else {
            ptt[idx] = pa | attr | PAGE_P;
            page_pt->refcount++;           // 账本 +1
        }
    } while (idx++, pa += PAGE_4K_SIZE, va += PAGE_4K_SIZE, *curr_va = va, va != end);

    return 0;
}

// ===================================================================
// 【Level 2: PD 层】 2MB 智能升维与状态机驱动
// ===================================================================
static inline int32 map_pde_range(uint64 *pdpte, uint64 *curr_va, uint64 pa, uint64 end, uint64 attr) {
    uint64 va = *curr_va;
    uint64 *pdt;
    page_t *page_pd;
    boolean new_table = FALSE;

    if (!(*pdpte & PAGE_P)) {
        page_pd = alloc_pages(0);
        if (!page_pd) { *curr_va = va; return -ENOMEM; }
        uint64 next_pa = page_to_pa(page_pd);
        asm_mem_set(pa_to_va(next_pa), 0, PAGE_4K_SIZE);
        *pdpte = next_pa | PAGE_P | PAGE_RW | (attr & PAGE_US);
        new_table = TRUE;
    } else {
        page_pd = va_to_page(pa_to_va(*pdpte & PAGE_PA_MASK));
    }

    pdt = pa_to_va(*pdpte & PAGE_PA_MASK);
    uint32 idx = get_table_idx(va, PDE_SHIFT);
    uint64 next;

    do {
        next = get_addr_end(va, end, PDE_SHIFT);
        uint64 pde = pdt[idx];

        boolean is_present    = (pde & PAGE_P) != 0;
        boolean is_huge       = (pde & PAGE_PS) != 0;
        boolean is_2m_aligned = ((next - va) == PAGE_2M_SIZE && !(va & (PAGE_2M_SIZE - 1)) && !(pa & (PAGE_2M_SIZE - 1)));

        // 🧠 巨页通道 (DRY 优化合并)
        if (is_2m_aligned && (!is_present || is_huge)) {
            if (is_present && (pde & PAGE_PA_MASK) != pa) {
                color_printk(RED, BLACK, "PANIC: VA Collision at 2M PD! VA: %#lx\n", va);
                if (new_table && page_pd->refcount == 0) { free_pages(page_pd); *pdpte = 0; }
                *curr_va = va; return -EEXIST;
            }
            pdt[idx] = pa | adjust_huge_page_attr(attr) | PAGE_P | PAGE_PS;
            if (!is_present) page_pd->refcount++;
        } 
        // 🧠 碎页降维通道
        else {
            if (is_present && is_huge) {
                color_printk(RED, BLACK, "PANIC: Map 4K over 2M HugePage at VA: %#lx\n", va);
                if (new_table && page_pd->refcount == 0) { free_pages(page_pd); *pdpte = 0; }
                *curr_va = va; return -EEXIST;
            }
            
            // 下发任务，探针深入
            int err = map_pte_range(&pdt[idx], curr_va, pa, next, attr);
            
            // 🚨 强制对齐：只要下层成功建了表(哪怕后来报错了)，父节点的账本必须 +1！
            if (!is_present && (pdt[idx] & PAGE_P)) {
                page_pd->refcount++;
            }
            
            if (err) {
                if (new_table && page_pd->refcount == 0) { free_pages(page_pd); *pdpte = 0; }
                return err; // 向上传递错误，触发顶层回滚
            }
        }
    } while (idx++, pa += (next - va), va = next, *curr_va = va, va != end);

    return 0;
}

// ===================================================================
// 【Level 3: PDPT 层】 1GB 智能升维与状态机驱动
// ===================================================================
static inline int32 map_pdpte_range(uint64 *pml4e, uint64 *curr_va, uint64 pa, uint64 end, uint64 attr) {
    uint64 va = *curr_va;
    uint64 *pdptt;
    page_t *page_pdpt;
    boolean new_table = FALSE;

    if (!(*pml4e & PAGE_P)) {
        page_pdpt = alloc_pages(0);
        if (!page_pdpt) { *curr_va = va; return -ENOMEM; }
        uint64 next_pa = page_to_pa(page_pdpt);
        asm_mem_set(pa_to_va(next_pa), 0, PAGE_4K_SIZE);
        *pml4e = next_pa | PAGE_P | PAGE_RW | (attr & PAGE_US);
        new_table = TRUE;
    } else {
        page_pdpt = va_to_page(pa_to_va(*pml4e & PAGE_PA_MASK));
    }

    pdptt = pa_to_va(*pml4e & PAGE_PA_MASK);
    uint32 idx = get_table_idx(va, PDPTE_SHIFT);
    uint64 next;

    do {
        next = get_addr_end(va, end, PDPTE_SHIFT);
        uint64 pdpte = pdptt[idx];

        boolean is_present    = (pdpte & PAGE_P) != 0;
        boolean is_huge       = (pdpte & PAGE_PS) != 0;
        boolean is_1g_aligned = ((next - va) == PAGE_1G_SIZE && !(va & (PAGE_1G_SIZE - 1)) && !(pa & (PAGE_1G_SIZE - 1)));

        if (is_1g_aligned && (!is_present || is_huge)) {
            if (is_present && (pdpte & PAGE_PA_MASK) != pa) {
                color_printk(RED, BLACK, "PANIC: VA Collision at 1G PDPT! VA: %#lx\n", va);
                if (new_table && page_pdpt->refcount == 0) { free_pages(page_pdpt); *pml4e = 0; }
                *curr_va = va; return -EEXIST;
            }
            pdptt[idx] = pa | adjust_huge_page_attr(attr) | PAGE_P | PAGE_PS;
            if (!is_present) page_pdpt->refcount++;
        } else {
            if (is_present && is_huge) {
                color_printk(RED, BLACK, "PANIC: Map 2M/4K over 1G HugePage at VA: %#lx\n", va);
                if (new_table && page_pdpt->refcount == 0) { free_pages(page_pdpt); *pml4e = 0; }
                *curr_va = va; return -EEXIST;
            }
            
            int err = map_pde_range(&pdptt[idx], curr_va, pa, next, attr);
            
            if (!is_present && (pdptt[idx] & PAGE_P)) {
                page_pdpt->refcount++;
            }
            
            if (err) {
                if (new_table && page_pdpt->refcount == 0) { free_pages(page_pdpt); *pml4e = 0; }
                return err;
            }
        }
    } while (idx++, pa += (next - va), va = next, *curr_va = va, va != end);

    return 0;
}

// ===================================================================
// 【主入口】 批量映射引擎 (自动混合 1G/2M/4K 页大小 + 精准回滚)
// ===================================================================
int32 vmmap_range(uint64 *pml4t, uint64 start_va, uint64 pa, uint64 size, uint64 attr) {
    if (size == 0) return 0;

    uint64 curr_va = start_va; // 🎯 探针初始化
    uint64 end = start_va + size;
    uint64 *cur_pml4 = pa_to_va((uint64)pml4t);
    uint32 idx = get_table_idx(curr_va, PML4E_SHIFT);
    uint64 next;
    int err = 0;

    do {
        next = get_addr_end(curr_va, end, PML4E_SHIFT);

        err = map_pdpte_range(&cur_pml4[idx], &curr_va, pa, next, attr);
        
        if (err) {
            // 🚨 精准回滚：利用探针卡住的确切地址，只清理这部分“半成品”
            if (curr_va > start_va) {
                unvmmap_range(pml4t, start_va, curr_va - start_va);
            }
            return err;
        }
    } while (idx++, pa += (next - curr_va), curr_va = next, curr_va != end);

    return 0;
}

//=================================================================================================================


//============================================ 批量卸载虚拟内存映射======================================================

// ===================================================================
// 【Level 1: PT 层】 横向推平 4KB 数据页
// ===================================================================
static inline int32 unmap_pte_range(uint64 *pde, uint64 addr, uint64 end) {
    uint64 *ptt = pa_to_va(*pde & PAGE_PA_MASK);
    uint32 idx = get_table_idx(addr, PTE_SHIFT);
    page_t *page_pt = va_to_page(ptt);

    do {
        if (ptt[idx] & PAGE_P) {
            ptt[idx] = 0;
            asm_invlpg(addr);
            page_pt->refcount--; // 表内有效计数 -1
        }
    } while (idx++, addr += PAGE_4K_SIZE, addr != end);

    // 🧹 级联释放引擎：PT 表空了，自我毁灭并向上级汇报
    if (page_pt->refcount == 0) {
        *pde = 0;               // 关键：彻底切断父级指针
        free_pages(page_pt);    // 回收页表内存
        return 1;               // 向上层发送销毁信号
    }
    return 0;
}

// ===================================================================
// 【Level 2: PD 层】 巨页侦测与级联释放
// ===================================================================
static inline int32 unmap_pde_range(uint64 *pdpte, uint64 addr, uint64 end) {
    uint64 *pdt = pa_to_va(*pdpte & PAGE_PA_MASK);
    uint32 idx = get_table_idx(addr, PDE_SHIFT);
    uint64 next;
    page_t *page_pd = va_to_page(pdt);

    do {
        next = get_addr_end(addr, end, PDE_SHIFT);
        uint64 pde = pdt[idx];

        if (!(pde & PAGE_P)) continue;

        if (pde & PAGE_PS) {
            // 🎯 遭遇 2MB 巨页，严格检查覆盖范围
            if ((next - addr) == PAGE_2M_SIZE && !(addr & (PAGE_2M_SIZE - 1))) {
                pdt[idx] = 0;
                asm_invlpg(addr);
                page_pd->refcount--; // 🛡️ 必须执行：账本 -1
                continue;            // 🛡️ 必须执行：跃过底层操作
            } else {
                color_printk(RED, BLACK, "PANIC: Partial unmap of 2MB Huge Page!\n");
                while(1);
            }
        }

        // 下收到了底层的销毁信号 (1)，自己的账本也减 1
        if (unmap_pte_range(&pdt[idx], addr, next)) {
            page_pd->refcount--;
        }

    } while (idx++, addr = next, addr != end);

    if (page_pd->refcount == 0) {
        *pdpte = 0;
        free_pages(page_pd);
        return 1;
    }
    return 0;
}

// ===================================================================
// 【Level 3: PDPT 层】 巨页侦测与级联释放
// ===================================================================
static inline int32 unmap_pdpte_range(uint64 *pml4e, uint64 addr, uint64 end) {
    uint64 *pdptt = pa_to_va(*pml4e & PAGE_PA_MASK);
    uint32 idx = get_table_idx(addr, PDPTE_SHIFT);
    uint64 next;
    page_t *page_pdpt = va_to_page(pdptt);

    do {
        next = get_addr_end(addr, end, PDPTE_SHIFT);
        uint64 pdpte = pdptt[idx];

        if (!(pdpte & PAGE_P)) continue;

        if (pdpte & PAGE_PS) {
            if ((next - addr) == PAGE_1G_SIZE && !(addr & (PAGE_1G_SIZE - 1))) {
                pdptt[idx] = 0;
                asm_invlpg(addr);
                page_pdpt->refcount--; // 🛡️ 减少引用
                continue;              // 🛡️ 跳出循环
            } else {
                color_printk(RED, BLACK, "PANIC: Partial unmap of 1GB Huge Page!\n");
                while(1);
            }
        }

        if (unmap_pde_range(&pdptt[idx], addr, next)) {
            page_pdpt->refcount--;
        }

    } while (idx++, addr = next, addr != end);

    if (page_pdpt->refcount == 0) {
        *pml4e = 0;
        free_pages(page_pdpt);
        return 1;
    }
    return 0;
}

// ===================================================================
// 【主入口】 批量释放引擎
// ===================================================================
int32 unvmmap_range(uint64 *pml4t, uint64 start_va, uint64 size) {
    if (size == 0) return 0;

    uint64 addr = start_va;
    uint64 end = addr + size;
    uint32 idx = get_table_idx(addr, PML4E_SHIFT);
    uint64 next;
    // 安全起见，如果传来的是物理地址，需要统一转虚拟地址
    uint64 *cur_pml4 = (uint64 *)pa_to_va((uint64)pml4t);

    do {
        next = get_addr_end(addr, end, PML4E_SHIFT);
        uint64 pml4e = cur_pml4[idx];

        if (!(pml4e & PAGE_P)) continue;

        if (unmap_pdpte_range(&cur_pml4[idx], addr, next)) {
            // PML4 作为顶层进程树根，即使被清空也由宿主管理释放
            // 这里只需维护 refcount 的准确性即可（如果有 page_pml4 的话）
        }
    } while (idx++, addr = next, addr != end);

    return 0;
}
//=================================================================================================================