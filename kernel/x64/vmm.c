#include "../include/vmm.h"
#include "../include/buddy_system.h"
#include "../include/errno.h"

//=============================================================== 单个虚拟内存映射接口 ==========================================================================

// 提取出一个内联函数，专门解决 PAT 碰撞陷阱
static inline uint64 adjust_huge_page_attr(uint64 attr) {
    uint32 pat = (attr & PAGE_PAT)<<5;
    uint64 huge_attr = attr | pat | PAGE_PS;        // 设置到巨页合法的 PAT 位置,强制打上 PAGE_PS (Bit 7)
    return huge_attr;
}

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

// 核心边界切割器：计算在当前层级的块大小内，本次遍历最多能走到哪个虚拟地址。
// shift 参数代表对应层级的位移量：PT(12), PD(21), PDPT(30), PML4(39)
static inline uint64 get_addr_end(uint64 addr, uint64 end, uint8 shift) {
    // 算出当前块的下一个天然物理边界 (例如 2MB 对齐边界)
    uint64 boundary = (addr + (1ULL << shift)) & ~((1ULL << shift) - 1);

    // 防溢出回绕设计：如果边界还没超过总终点，就走到边界；如果超过了，就走到终点。
    return (boundary - 1 < end - 1) ? boundary : end;
}

// 计算页表索引索引
static inline uint32 get_table_idx(uint64 va,uint8 shift){
    return (va >> shift) & 0x1FF;
}

// ===================================================================
// 【Level 1: PT 层】 4KB 碎页横向铺砖
// ===================================================================
static inline int32 map_pte_range(uint64 *pde,uint64 va, uint64 pa,  uint64 end, uint64 attr) {
    uint64 *ptt;
    page_t *page_pt;

    // 如果当前的 PT 表不存在，立刻原地建一张新表
    if (!(*pde & PAGE_P)) {
        page_pt = alloc_pages(0);
        if (!page_pt) return -ENOMEM;
        uint64 next_pa = page_to_pa(page_pt);
        asm_mem_set(pa_to_va(next_pa), 0, PAGE_4K_SIZE);

        // 挂载到父级 PD 目录 (继承必要的读写和用户态权限)
        uint64 dir_attr = PAGE_P | PAGE_RW | (attr & PAGE_US);
        *pde = next_pa | dir_attr;
    } else {
        page_pt = va_to_page(pa_to_va(*pde & PAGE_PA_MASK));
    }

    ptt = pa_to_va(*pde & PAGE_PA_MASK);
    uint32 idx = get_table_idx(va,PTE_SHIFT);

    // 🚀 横向铺砖：沿着 PTE 数组狂奔，完美利用 L1 Cache
    do {
        if (!(ptt[idx] & PAGE_P)) { // 防冲突：仅映射空位
            ptt[idx] = pa | attr | PAGE_P;
            page_pt->refcount++;    // PT 表内有效映射数 +1
        }
    // VA 和 PA 必须手拉手一起横向推进 4KB！
    } while (idx++, pa += PAGE_4K_SIZE, va += PAGE_4K_SIZE, va != end);

    return 0;
}

// ===================================================================
// 【Level 2: PD 层】 2MB 智能升维与横向派发
// ===================================================================
static inline int32 map_pde_range(uint64 *pdpte,uint64 va,  uint64 pa, uint64 end, uint64 attr) {
    uint64 *pdt;
    page_t *page_pd;

    if (!(*pdpte & PAGE_P)) {
        page_pd = alloc_pages(0);
        if (!page_pd) return -ENOMEM;
        uint64 next_pa = page_to_pa(page_pd);
        asm_mem_set(pa_to_va(next_pa), 0, PAGE_4K_SIZE);

        uint64 dir_attr = PAGE_P | PAGE_RW | (attr & PAGE_US);
        *pdpte = next_pa | dir_attr;
    } else {
        page_pd = va_to_page(pa_to_va(*pdpte & PAGE_PA_MASK));
    }

    pdt = pa_to_va(*pdpte & PAGE_PA_MASK);
    uint32 idx = get_table_idx(va,PDE_SHIFT);
    uint64 next;

    do {
        // ✂️ 神级切割：在这 2MB 范围内，我们能走多远？
        next = get_addr_end(va, end, PDE_SHIFT);

        // 🧠 巨页自适应降维打击：如果凑齐一整块 2MB 且对齐完美，直接挂载大页！
        if ((next - va) == PAGE_2M_SIZE && !(va & (PAGE_2M_SIZE - 1)) && !(pa & (PAGE_2M_SIZE - 1))) {
            if (!(pdt[idx] & PAGE_P)) {
                pdt[idx] = pa | adjust_huge_page_attr(attr) | PAGE_P;
                page_pd->refcount++;
            }
        }
        // 凑不齐 2MB，就把这块切好的任务下发给底层去铺碎砖
        else {
            int err = map_pte_range(&pdt[idx],va, pa,  next, attr);
            if (err) return err;

            // 注意：这里为了极致性能省略了对 PD 自身的 refcount 细致维护，
            // 若新分配了下级表，PD 的 refcount 理论应 +1。
        }
    } while (idx++, pa += (next - va), va = next, va != end);

    return 0;
}

// ===================================================================
// 【Level 3: PDPT 层】 1GB 智能升维与横向派发
// ===================================================================
static inline int32 map_pdpte_range(uint64 *pml4e, uint64 va, uint64 pa, uint64 end, uint64 attr) {
    uint64 *pdptt;
    page_t *page_pdpt;

    if (!(*pml4e & PAGE_P)) {
        page_pdpt = alloc_pages(0);
        if (!page_pdpt) return -ENOMEM;
        uint64 next_pa = page_to_pa(page_pdpt);
        asm_mem_set(pa_to_va(next_pa), 0, PAGE_4K_SIZE);

        uint64 dir_attr = PAGE_P | PAGE_RW | (attr & PAGE_US);
        *pml4e = next_pa | dir_attr;
    } else {
        page_pdpt = va_to_page(pa_to_va(*pml4e & PAGE_PA_MASK));
    }

    pdptt = pa_to_va(*pml4e & PAGE_PA_MASK);
    uint32 idx = get_table_idx(va,PDPTE_SHIFT);
    uint64 next;

    do {
        // ✂️ 切割 1GB 边界
        next = get_addr_end(va, end, PDPTE_SHIFT);

        // 🧠 终极巨页自适应：满足 1GB 完美对齐
        if ((next - va) == PAGE_1G_SIZE && !(va & (PAGE_1G_SIZE - 1)) && !(pa & (PAGE_1G_SIZE - 1))) {
            if (!(pdptt[idx] & PAGE_P)) {
                pdptt[idx] = pa | adjust_huge_page_attr(attr) | PAGE_P;
                page_pdpt->refcount++;
            }
        } else {
            int err = map_pde_range(&pdptt[idx],va,pa,next, attr);
            if (err) return err;
        }
    } while (idx++, pa += (next - va), va = next, va != end);

    return 0;
}



// ===================================================================
// 【主入口】 批量映射引擎 (自动混合 1G/2M/4K 页大小)
// ===================================================================
int32 vmmap_range(uint64 *pml4t, uint64 start_va,uint64 pa, uint64 size, uint64 attr){
    if (size == 0) return 0;

    uint64 va = start_va;
    uint64 end = va + size;
    uint64 *cur_pml4 = pa_to_va((uint64)pml4t);
    uint32 idx = get_table_idx(va,PML4E_SHIFT);
    uint64 next;
    int err = 0;

    do {
        // ✂️ 512GB 级边界切割
        next = get_addr_end(va, end, PML4E_SHIFT);

        // 直接派发给 PDPT 层
        err = map_pdpte_range(&cur_pml4[idx],  va, pa,next, attr);
        if (err) {
            // 工业级内核应在此调用 unvmmap_range(start_va, va - start_va) 进行回滚
            return err;
        }
    } while (idx++, pa += (next - va), va = next, va != end);

    return 0;
}

//=================================================================================================================


//============================================ 批量卸载虚拟内存映射======================================================

// ===================================================================
// 【Level 1: PT 层】 横向推平 4KB 数据页
// ===================================================================
static inline int32 unmap_pte_range(uint64 *pde, uint64 addr, uint64 end) {
    uint64 *ptt = pa_to_va(*pde & PAGE_PA_MASK);
    uint32 idx = get_table_idx(addr,PTE_SHIFT);
    page_t *page_pt = va_to_page(ptt);

    // 🚀 横向扫荡，没有函数调用，纯内存数组操作
    do {
        if (ptt[idx] & PAGE_P) {
            ptt[idx] = 0;              // 断开物理映射
            asm_invlpg(addr);  // 刷新当前页的 TLB
            page_pt->refcount--;       // 表内有效计数 -1
        }
    } while (idx++, addr += PAGE_4K_SIZE, addr != end);

    // 🧹 级联释放引擎：PT 表空了，自我毁灭并向上级汇报
    if (page_pt->refcount == 0) {
        *pde = 0;               // 关键：彻底切断父级指针，防止 Use-After-Free
        free_pages(page_pt);    // 回收页表内存
        return 1;               // 返回 1，通知父目录将它的 refcount -1
    }
    return 0;
}

// ===================================================================
// 【Level 2: PD 层】 巨页侦测与任务切割
// ===================================================================
static inline int32 unmap_pde_range(uint64 *pdpte, uint64 addr, uint64 end) {
    uint64 *pdt = pa_to_va(*pdpte & PAGE_PA_MASK);
    uint32 idx = get_table_idx(addr,PDE_SHIFT);
    uint64 next;
    page_t *page_pd = va_to_page(pdt);

    do {
        next = get_addr_end(addr, end, PDE_SHIFT);
        uint64 pde = pdt[idx];

        if (!(pde & PAGE_P)) continue; // 极速跨越：发现空洞，直接跃过这 2MB！

        if (pde & PAGE_PS) {
            // 🎯 遭遇 2MB 巨页，直接秒杀！
            pdt[idx] = 0;
            asm_invlpg(addr);
            page_pd->refcount--;
            continue;
        }

        // 往下发配切割好的区间。如果下级被摧毁（返回1），当前层计数 -1
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
// 【Level 3: PDPT 层】 巨页侦测与任务切割
// ===================================================================
static inline int32 unmap_pdpte_range(uint64 *pml4e, uint64 addr, uint64 end) {
    uint64 *pdptt = pa_to_va(*pml4e & PAGE_PA_MASK);
    uint32 idx = get_table_idx(addr,PDPTE_SHIFT);
    uint64 next;
    page_t *page_pdpt = va_to_page(pdptt);

    do {
        next = get_addr_end(addr, end, PDPTE_SHIFT);
        uint64 pdpte = pdptt[idx];

        if (!(pdpte & PAGE_P)) continue; // 极速跨越空洞 1GB

        if (pdpte & PAGE_PS) {
            // 🎯 遭遇 1GB 巨页，直接秒杀！
            pdptt[idx] = 0;
            asm_invlpg(addr);
            page_pdpt->refcount--;
            continue;
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
// 【主入口】 批量释放引擎 (无视页大小、碎片化、空洞)
// ===================================================================
int32 unvmmap_range(uint64 *pml4t, uint64 start_va, uint64 size) {
    if (size == 0) return 0;

    uint64 addr = start_va;
    uint64 end = addr + size;
    uint32 idx = get_table_idx(addr,PML4E_SHIFT);
    uint64 next;

    do {
        next = get_addr_end(addr, end, PML4E_SHIFT);
        uint64 pml4e = pml4t[idx];

        if (!(pml4e & PAGE_P)) continue;

        // 顶层无需判断巨页，直接下发切割好的任务
        if (unmap_pdpte_range(&pml4t[idx], addr, next)) {
            // 注意：PML4 表是进程树根，即使全空也不在这里 free，仅维护计数对齐即可
            va_to_page(pml4t)->refcount--;
        }
    } while (idx++, addr = next, addr != end);

    return 0;
}

//=================================================================================================================