/**
 * @file vmm_page.c
 * @brief MOS_UEFI 虚拟内存页操作子系统实现
 *
 * 核心算法逻辑：
 * 1. 统一树漫游 (vmm_walk)：基于层级泛化索引公式向下遍历，4 参数极限寄存器优化。
 * 2. 事务回滚引擎：在批量映射出现异常时，基于已映射长度与分配日志执行无损逆向清理。
 * 3. 区间裁剪遍历 (Interval Tree Walk)：解映射与权限修改单次遍历树结构，自动完成大页局部拆分。
 * 4. 树自修剪 (Tree Pruning)：解映射回溯时自动检测空表，逐级释放无用物理帧。
 * 5. TLB 延迟批处理：收集修改地址段，在顶层操作完成时统一执行精准刷新或 CR3 重载。
 */

#include "vmm.h"
#include "tlb.h"
#include "printk.h"


//全局内存布局图
vm_layout_t vm_layout;


/* ========================================================================== */
/*                         内部基础辅助函数                                    */
/* ========================================================================== */

/**
 * @brief 计算给定虚拟地址在指定分页层级中的 9-bit 索引值
 */
static inline uint64 vmm_get_index(uint64 vaddr, uint8 level) {
    return (vaddr >> (12 + 9 * (level - 1))) & 0x1FF;
}

/**
 * @brief 检查虚拟地址是否符合 x86_64 规范地址要求 (Canonical Address Check)
 */
static boolean vmm_is_canonical(uint64 vaddr, uint8 paging_level) {
    if (paging_level == 4) {
        int64 sign_extended = ((int64)(vaddr << 16)) >> 16;
        return (uint64)sign_extended == vaddr;
    } else if (paging_level == 5) {
        int64 sign_extended = ((int64)(vaddr << 7)) >> 7;
        return (uint64)sign_extended == vaddr;
    }
    return FALSE;
}

/* ========================================================================== */
/*                         TLB 批处理引擎实现                                  */
/* ========================================================================== */

// 智能刷新阈值：超过 32 页 (128KB)，直接清空整个 PCID 比逐页刷更快
#define TLB_SMART_FLUSH_THRESHOLD 32
// 绝对代价阈值：如果操作跨度太大，为了防止占用过多 CPU 时间，强制全刷
#define TLB_COST_THRESHOLD 64

typedef struct {
    struct {
        uint64 start;
        uint64 size;
        uint64 stride;
    } ranges[TLB_SMART_FLUSH_THRESHOLD];
    uint64 count;
    uint64 total_invlpg_cost;
    boolean flush_all;
    boolean is_global;
} vm_tlb_batch_t;


/**
 * @brief TLB 批处理控制块初始化
 */
static inline void tlb_batch_init(vm_tlb_batch_t *batch) {
    batch->count = 0;
    batch->total_invlpg_cost = 0;
    batch->flush_all = FALSE;
    batch->is_global = FALSE;
}

/**
 * @brief 向批处理引擎中添加一条作废记录
 * @param batch     批处理控制块
 * @param vaddr     目标虚拟地址
 * @param size      作废的内存区间大小
 * @param page_size 页粒度 (4K, 2M, 1G)，用于计算真实步长
 * @param is_global 是否包含内核全局映射页 (HW_PAGE_G)
 */
static void tlb_batch_add(vm_tlb_batch_t *batch, uint64 vaddr, uint64 size, uint64 page_size, boolean is_global) {
    // 如果已经被标记为“焦土全刷”模式，直接返回，不再浪费时间记录碎片
    if (batch->flush_all) return;

    // 计算当前区间需要执行单页刷新的物理次数 (代价评估)
    uint64 cost = size / page_size;
    if (cost == 0) cost = 1;

    // 🌟 核心修复 1：累加全局污染标志 (使用按位或，防止状态被后续的普通页覆盖)
    batch->is_global |= is_global;
    batch->total_invlpg_cost += cost;

    // 如果累积的总刷新次数超过了绝对阈值，立刻切换为“全刷”模式
    if (batch->total_invlpg_cost > TLB_COST_THRESHOLD) {
        batch->flush_all = TRUE;
        return;
    }

    // 🌟 智能合并 (大页友好)：如果地址连续，且步长(页大小)完全一致，只需拉长上一个区间的 size
    if (batch->count > 0) {
        uint64 last_idx = batch->count - 1;
        if (batch->ranges[last_idx].start + batch->ranges[last_idx].size == vaddr &&
            batch->ranges[last_idx].stride == page_size) {
            batch->ranges[last_idx].size += size;
            return;
        }
    }

    // 申请新的槽位记录
    if (batch->count < TLB_SMART_FLUSH_THRESHOLD) {
        batch->ranges[batch->count].start = vaddr;
        batch->ranges[batch->count].size = size;
        batch->ranges[batch->count].stride = page_size;
        batch->count++;
    } else {
        // 槽位已满，被迫退化为全刷模式
        batch->flush_all = TRUE;
    }
}


/* ========================================================================== */
/*                         VMM 高级接口实现 (屏蔽底层指令)                      */
/* ========================================================================== */

/**
 * @brief 智能区间刷新 (自动决策: 狙击 vs 灭门)
 */
void vmm_tlb_flush_range(vm_space_t *space, uint64 start_va, uint64 size) {
    if (size == 0) return;

    // 软件状态级优化：如果进程 PCID 为 0，说明它从未被调度过，CPU 根本没有它的缓存！直接返回！
    if (space->pcid == 0) return;

    uint64 page_count = (size + PAGE_4K_SIZE - 1) / PAGE_4K_SIZE;

    // 策略 A：区间过大，直接切换到“单进程灭门模式”
    if (page_count > TLB_SMART_FLUSH_THRESHOLD) {
        tlb_flush_pcid_all(space->pcid); // 底层 ALT 宏自动处理新老硬件兼容
        return;
    }

    // 策略 B：区间较小，进入“逐页精准狙击模式”
    for (uint64 i = 0; i < page_count; i++) {
        uint64 va = start_va + i * PAGE_4K_SIZE;
        // 🌟 核心清理：不再判断 pcid != 0，直接调底层！老硬件会自动退化为 invlpg！
        tlb_flush_pcid_page(space->pcid, va);
    }
}

/**
 * @brief 整个地址空间无差别销毁 (用于进程退出或 exec)
 */
static inline void vmm_tlb_flush_space(vm_space_t *space) {
    // 软件状态防御：未分配/未运行的进程，无需刷新物理硬件
    if (space->pcid == 0) return;

    // 硬件降级全权交给底层的 ALT_INSTR 自动解决
    tlb_flush_pcid_all(space->pcid);
}

void vmm_tlb_batch_commit(vm_space_t *space, vm_tlb_batch_t *batch) {
    if (batch->count == 0 && !batch->flush_all) return;

    // 🌟 策略 1：灭门模式 (Nuke Mode)
    // 只有当积累的修改量极大，或者空间槽位已满时，才启动面杀伤
    if (batch->flush_all) {
        if (batch->is_global) {
            // 只有当【大面积】修改内核全局空间时 (极其罕见，如海量 vmalloc)，才核平全系统
            tlb_flush_all();
        } else {
            // 用户态空间的大规模修改，只灭门单一进程
            vmm_tlb_flush_space(space);
        }
        goto clear_batch;
    }

    // 🌟 软件状态防御：进程如果没分配过 PCID (没运行过)
    // - 若修改的是普通页：CPU 里根本没它的缓存，直接跳过！
    // - 若修改的是全局页：全局页是所有进程共享的，必须往下走，强行狙击！
    if (space->pcid == 0 && !batch->is_global) {
        goto clear_batch;
    }

    // 🌟 策略 2：精准狙击模式 (Sniper Mode)
    // 包含【少量普通页】或【少量全局页】
    uint64 pcid = space->pcid;
    for (uint64 i = 0; i < batch->count; i++) {
        uint64 start  = batch->ranges[i].start;
        uint64 size   = batch->ranges[i].size;
        uint64 stride = batch->ranges[i].stride;
        uint64 count  = size / stride;

        for (uint64 j = 0; j < count; j++) {
            uint64 va = start + j * stride;

            // 🌟 硬件级真理修正：
            // 1. 如果修改了全局页，INVPCID Type 0 是无效的！必须退化使用 INVLPG！
            // 2. 如果进程从未运行过 (pcid == 0)，对于全局页也必须强刷 INVLPG。
            if (batch->is_global || pcid == 0) {
                tlb_flush_page(va); // 纯正的 INVLPG，无视 PCID 精准爆破全局页
            } else {
                tlb_flush_pcid_page(pcid, va); // INVPCID Type 0，狙击普通进程页
            }
        }
    }

    clear_batch:
        tlb_batch_init(batch);
}


/* ========================================================================== */
/*                         内部宏与核心状态机声明                               */
/* ========================================================================== */

// PTE 属性保留掩码 (增加 HW_PAGE_HUGE_PAT 修复大页 PAT 丢失漏洞)
#define PTE_PRESERVE_MASK (HW_PAGE_P | HW_PAGE_US | HW_PAGE_G | HW_PAGE_PWT | HW_PAGE_PCD | HW_PAGE_A | HW_PAGE_D | HW_PAGE_4K_PAT | HW_PAGE_HUGE_PAT)
#define PTE_MODIFY_MASK   (HW_PAGE_RW | HW_PAGE_NX)

// VMM 漫游内部控制位压缩 (避开 SW_FLAG 分配的 52~61 位，使用绝对安全的 48~51 位段)
#define VMM_WALK_CREATE      (1ULL << 62)
#define VMM_WALK_LVL_SHIFT   48
#define VMM_WALK_LVL_MASK    (0xFULL << VMM_WALK_LVL_SHIFT)

/**
 * @brief 页表漫游状态机 (Walk State)
 * @note 承载漫游结果与新分配节点回滚日志。分配在栈上以避免双重指针传递。
 */
typedef struct {
    uint64 *entry;          ///< 目标层级页表项的虚拟地址指针
    uint64 *parent_ptes[5]; ///< 新分配子目录对应的父级 PTE 地址 (用于失败时解除链接)
    page_t *pages[5];       ///< 新分配的中间目录物理页对象 (用于失败时归还 PMM)
    uint8  count;           ///< 沿途分配的中间目录层数
} vm_walk_state_t;

/**
 * @brief 页表递归操作通用上下文 (Operation Context)
 * @note 规范化参数传递，确保 x86_64 寄存器充足，避免递归深度引发的性能衰退与栈溢出。
 */
typedef struct {
    vm_space_t      *space;
    vm_tlb_batch_t  *tlb_batch;
    uint64          flags;
} vm_op_ctx_t;


/* ========================================================================== */
/*                          页表通用遍历 (Walk)                               */
/* ========================================================================== */

/**
 * @brief 页表树自顶向下遍历核心引擎
 */
static vm_status_e vmm_walk(vm_space_t *space, uint64 vaddr, uint64 flags, vm_walk_state_t *state) {
    if (!vmm_is_canonical(vaddr, space->paging_level)) return VM_ERR_CANONICAL;

    // 解析嵌入在 flags 高位的层级要求与创建指令
    uint8 target_level = (flags >> VMM_WALK_LVL_SHIFT) & 0xF;
    boolean create_missing = (flags & VMM_WALK_CREATE) != 0;

    uint64 us_flag = flags & HW_PAGE_US;
    uint64 cur_table_pa = space->cr3_root;

    state->count = 0;

    for (uint8 lvl = space->paging_level; lvl > target_level; lvl--) {
        uint64 *table_va = (uint64 *)space->ops.phys_to_virt(cur_table_pa);
        uint64 idx = vmm_get_index(vaddr, lvl);
        uint64 entry = table_va[idx];

        if (entry & HW_PAGE_P) {
            if (entry & HW_PAGE_PS) return VM_ERR_ALREADY_MAPPED; // 遇大页直接拦截
            cur_table_pa = entry & PTE_ADDR_MASK;
        } else {
            if (!create_missing) return VM_ERR_NOT_MAPPED;

            // 按需动态分配子目录
            page_t *new_page = space->ops.alloc_pages(0);

            // 若发生 OOM，内部即刻执行原子回滚，斩断残脉
            if (!new_page) {
                for (uint8 i = 0; i < state->count; i++) {
                    *(state->parent_ptes[i]) = 0;
                    space->ops.free_pages(state->pages[i]);
                }
                state->count = 0;
                return VM_ERR_NOMEM;
            }

            uint64 new_table_pa = space->ops.page_to_phys(new_page);
            uint64 *new_table_va = (uint64 *)space->ops.phys_to_virt(new_table_pa);

            // 强制抹除可能残留的脏内存数据
            asm_mem_set(new_table_va,0,PAGE_4K_SIZE);

            // 存入操作日志以便支持高级回滚策略
            if (state->count < 5) {
                state->parent_ptes[state->count] = &table_va[idx];
                state->pages[state->count++] = new_page;
            }

            table_va[idx] = new_table_pa | us_flag | HW_PAGE_P | HW_PAGE_RW;
            cur_table_pa = new_table_pa;
        }
    }

    uint64 *final_table_va = (uint64 *)space->ops.phys_to_virt(cur_table_pa);
    state->entry = &final_table_va[vmm_get_index(vaddr, target_level)];
    return VM_SUCCESS;
}

/* ========================================================================== */
/*                          大页拆分引擎 (Huge Page Splitter)                 */
/* ========================================================================== */

vm_status_e vm_split_huge_page(vm_space_t *space, uint64 vaddr, vm_page_lvl_e from_lvl) {
    uint8 cur_lvl = (uint8)from_lvl;
    if (cur_lvl != 2 && cur_lvl != 3) return VM_ERR_INVALID_ARGS;

    uint64 huge_size = 1ULL << (12 + 9 * (cur_lvl - 1));
    vaddr &= ~(huge_size - 1);

    vm_walk_state_t state = {0};
    uint64 walk_flags = ((uint64)cur_lvl << VMM_WALK_LVL_SHIFT);

    if (vmm_walk(space, vaddr, walk_flags, &state) != VM_SUCCESS || !state.entry) {
        return VM_ERR_NOT_MAPPED;
    }

    uint64 *entry = state.entry;
    if (!(*entry & HW_PAGE_P) || !(*entry & HW_PAGE_PS)) return VM_ERR_INVALID_ARGS;

    page_t *sub_page = space->ops.alloc_pages(0);
    if (!sub_page) return VM_ERR_NOMEM;

    uint64 sub_table_pa = space->ops.page_to_phys(sub_page);
    uint64 *sub_table_va = (uint64 *)space->ops.phys_to_virt(sub_table_pa);

    uint64 base_pa = *entry & 0x000FFFFFFFE00000ULL;
    uint64 child_flags = *entry & (HW_PAGE_P | HW_PAGE_G | HW_PAGE_NX | HW_PAGE_RW |
                                   HW_PAGE_US | HW_PAGE_HUGE_PAT | HW_PAGE_PWT | HW_PAGE_PCD | HW_PAGE_PS);

    // 针对大页转小页的 PAT 迁移修复
    if (cur_lvl == 2) {
        child_flags &= ~HW_PAGE_PS;
        if (child_flags & HW_PAGE_HUGE_PAT) {
            child_flags &= ~HW_PAGE_HUGE_PAT; // 清除第 12 位的大页 PAT
            child_flags |= HW_PAGE_4K_PAT;    // 迁移至第 7 位的小页 PAT
        }
    }

    uint64 sub_step = 1ULL << (12 + 9 * (cur_lvl - 2));

    // 并行映射子页表槽位
    for (uint64 i = 0; i < 512; i++) {
        sub_table_va[i] = (base_pa + i * sub_step) | child_flags;
    }

    uint64 dir_user_flag = *entry & HW_PAGE_US;
    *entry = sub_table_pa | HW_PAGE_P | HW_PAGE_RW | dir_user_flag;

    tlb_flush_page(vaddr); // 清除 TLB 旧大页缓存
    return VM_SUCCESS;
}


/* ========================================================================== */
/*                          高内聚区间映射与事务回滚引擎                      */
/* ========================================================================== */

vm_status_e vm_map_range(vm_space_t *space, uint64 vaddr, uint64 paddr, uint64 size, uint64 flags) {
    if (!size || (vaddr & PAGE_4K_OFFSET_MASK) || (paddr & PAGE_4K_OFFSET_MASK) || (size & PAGE_4K_OFFSET_MASK))
        return VM_ERR_INVALID_ARGS;
    if (!vmm_is_canonical(vaddr, space->paging_level) || !vmm_is_canonical(vaddr + size - 1, space->paging_level))
        return VM_ERR_CANONICAL;

    vm_tlb_batch_t tlb_batch;
    tlb_batch_init(&tlb_batch);

    uint64 curr_va = vaddr, curr_pa = paddr, mapped_bytes = 0;
    vm_status_e status;
    uint8 max_allowed_level = GET_MAX_LEVEL(flags);

    while (mapped_bytes < size) {
        uint64 remaining = size - mapped_bytes;
        uint8 target_level = PAGE_LVL_4K;
        uint64 step_bytes = PAGE_4K_SIZE;
        uint64 new_flags = flags;

        // 大页贪心升阶判定
        if (max_allowed_level >= PAGE_LVL_1G && remaining >= PAGE_1G_SIZE &&
            !(curr_va & PAGE_1G_OFFSET_MASK) && !(curr_pa & PAGE_1G_OFFSET_MASK)) {
            target_level = PAGE_LVL_1G; step_bytes = PAGE_1G_SIZE; new_flags |= HW_PAGE_PS;
        } else if (max_allowed_level >= PAGE_LVL_2M && remaining >= PAGE_2M_SIZE &&
                   !(curr_va & PAGE_2M_OFFSET_MASK) && !(curr_pa & PAGE_2M_OFFSET_MASK)) {
            target_level = PAGE_LVL_2M; step_bytes = PAGE_2M_SIZE; new_flags |= HW_PAGE_PS;
        }

        if ((new_flags & SW_FLAG_STRICT_HUGE) && (target_level != max_allowed_level)) {
            status = VM_ERR_INVALID_ARGS; goto rollback;
        }

        vm_walk_state_t state = {0};
        uint64 walk_flags = new_flags | VMM_WALK_CREATE | ((uint64)target_level << VMM_WALK_LVL_SHIFT);

        status = vmm_walk(space, curr_va, walk_flags, &state);
        if (status != VM_SUCCESS) goto rollback;

        uint64 *entry = state.entry;

        // 冲突覆盖及向下强拆逻辑
        if (*entry & HW_PAGE_P) {
            if (!(new_flags & SW_FLAG_OVERWRITE)) {
                for (uint8 i = 0; i < state.count; i++) {
                    *(state.parent_ptes[i]) = 0;
                    space->ops.free_pages(state.pages[i]);
                }
                status = VM_ERR_ALREADY_MAPPED; goto rollback;
            }

            if (target_level > PAGE_LVL_4K && !(*entry & HW_PAGE_PS)) {
                for (uint8 i = 0; i < state.count; i++) {
                    *(state.parent_ptes[i]) = 0;
                    space->ops.free_pages(state.pages[i]);
                }
                vm_unmap_range(space, curr_va, step_bytes, UNMAP_FLAG_NONE);
                continue; // 原地复试
            }

            uint64 old_pa = *entry & PTE_ADDR_MASK;
            if ((new_flags & SW_FLAG_FREE_OLD_PHYS) && old_pa != 0) {
                space->ops.free_pages(space->ops.phys_to_page(old_pa));
            }

            tlb_batch_add(&tlb_batch, curr_va, step_bytes, step_bytes, (new_flags & HW_PAGE_G) ? TRUE : FALSE);
        }

        uint64 pte_flags = new_flags & PTE_WRITE_MASK;
        *entry = (curr_pa & PTE_ADDR_MASK) | pte_flags;

        curr_va += step_bytes;
        curr_pa += step_bytes;
        mapped_bytes += step_bytes;
    }

    vmm_tlb_batch_commit(space,&tlb_batch);
    return VM_SUCCESS;

rollback:
    // 回滚当前映射事务
    if (mapped_bytes > 0) vm_unmap_range(space, vaddr, mapped_bytes, UNMAP_FLAG_NONE);
    return status;
}



/* ========================================================================== */
/*                          范围解映射与自动修剪                              */
/* ========================================================================== */

/**
 * @brief 递归区间裁剪解映射引擎
 */
static boolean vmm_unmap_tree_range(vm_op_ctx_t *ctx, uint64 table_pa, uint8 lvl,
                                    uint64 start, uint64 end) {
    // 变量提升：压榨硬件寄存器，消除内存访存周期
    vm_space_t     *space = ctx->space;
    uint32         flags  = (uint32)ctx->flags;
    vm_tlb_batch_t *batch = ctx->tlb_batch;

    uint64 *table_va = (uint64 *)space->ops.phys_to_virt(table_pa);
    uint64 step = 1ULL << (12 + 9 * (lvl - 1));
    uint64 cur = start;

    while (cur < end) {
        uint64 idx = vmm_get_index(cur, lvl);
        uint64 entry_base = cur & ~(step - 1);
        uint64 next_boundary = entry_base + step;

        // 边界防溢出，避免解映射 0xFFFFFFFFFFFFFFFF 时发生回绕
        uint64 sub_end = (next_boundary == 0 || end < next_boundary) ? end : next_boundary;
        uint64 entry = table_va[idx];

        if (entry & HW_PAGE_P) {
            boolean is_global = (entry & HW_PAGE_G) ? TRUE : FALSE;
            uint64 old_pa = entry & PTE_ADDR_MASK;

            if (lvl == PAGE_LVL_4K || ((entry & HW_PAGE_PS) && cur == entry_base && sub_end == next_boundary)) {
                // 完全覆盖：执行 O(1) 对象级清理
                table_va[idx] = 0;

                if ((flags & UNMAP_FLAG_FREE_PHYS) && space->ops.free_pages && old_pa != 0) {
                    space->ops.free_pages(space->ops.phys_to_page(old_pa));
                }
                tlb_batch_add(batch, cur, step, step, is_global);

            } else if (entry & HW_PAGE_PS) {
                // 局部破坏：向下敲碎大页
                if (vm_split_huge_page(space, entry_base, (vm_page_lvl_e)lvl) == VM_SUCCESS) {
                    entry = table_va[idx];
                    uint64 next_pa = entry & PTE_ADDR_MASK;
                    if (vmm_unmap_tree_range(ctx, next_pa, lvl - 1, cur, sub_end)) {
                        table_va[idx] = 0;
                    }
                }
            } else {
                // 标准递归
                uint64 next_pa = entry & PTE_ADDR_MASK;
                if (vmm_unmap_tree_range(ctx, next_pa, lvl - 1, cur, sub_end)) {
                    table_va[idx] = 0;
                }
            }
        }
        cur = sub_end;
    }

    // 后序遍历自底向上修剪闲置树枝
    if (lvl != space->paging_level) {
        for (uint64 i = 0; i < 512; i++) {
            if (table_va[i] & HW_PAGE_P) return FALSE;
        }
        space->ops.free_pages(space->ops.phys_to_page(table_pa));
        return TRUE;
    }
    return FALSE;
}

vm_status_e vm_unmap_range(vm_space_t *space, uint64 vaddr, uint64 size, uint32 flags) {
    if (!size || (vaddr & PAGE_4K_OFFSET_MASK) || (size & PAGE_4K_OFFSET_MASK)) return VM_ERR_INVALID_ARGS;
    if (!vmm_is_canonical(vaddr, space->paging_level) || !vmm_is_canonical(vaddr + size - 1, space->paging_level))
        return VM_ERR_CANONICAL;

    vm_tlb_batch_t tlb_batch;
    tlb_batch_init(&tlb_batch);

    vm_op_ctx_t ctx = {
        .space = space,
        .tlb_batch = &tlb_batch,
        .flags = flags
    };

    vmm_unmap_tree_range(&ctx, space->cr3_root, space->paging_level, vaddr, vaddr + size);

    vmm_tlb_batch_commit(space,&tlb_batch);
    return VM_SUCCESS;
}

/* ========================================================================== */
/*                          范围权限修改与树裁剪                              */
/* ========================================================================== */

/**
 * @brief 递归区间裁剪权限修改核心引擎
 */
static vm_status_e vmm_protect_tree_range(vm_op_ctx_t *ctx, uint64 table_pa, uint8 lvl,
                                          uint64 start, uint64 end) {
    vm_space_t     *space     = ctx->space;
    uint64         new_flags  = ctx->flags;
    vm_tlb_batch_t *batch     = ctx->tlb_batch;

    uint64 *table_va = (uint64 *)space->ops.phys_to_virt(table_pa);
    uint64 step = 1ULL << (12 + 9 * (lvl - 1));
    uint64 cur = start;

    while (cur < end) {
        uint64 idx = vmm_get_index(cur, lvl);
        uint64 entry_base = cur & ~(step - 1);
        uint64 next_boundary = entry_base + step;
        uint64 sub_end = (next_boundary == 0 || end < next_boundary) ? end : next_boundary;
        uint64 entry = table_va[idx];

        if (!(entry & HW_PAGE_P)) return VM_ERR_NOT_MAPPED;

        if (lvl == 1) {
            uint64 pa = entry & PTE_ADDR_MASK;
            table_va[idx] = pa | (entry & PTE_PRESERVE_MASK) | (new_flags & PTE_MODIFY_MASK);
            tlb_batch_add(batch, cur, step, step, (entry & HW_PAGE_G)?TRUE:FALSE);

        } else if (entry & HW_PAGE_PS) {
            if (cur == entry_base && sub_end == next_boundary) {
                uint64 huge_pa = entry & ~(step - 1);
                table_va[idx] = huge_pa | (entry & PTE_PRESERVE_MASK) | (new_flags & PTE_MODIFY_MASK) | HW_PAGE_PS;
                tlb_batch_add(batch, cur, step, step, (entry & HW_PAGE_G)?TRUE:FALSE);
            } else {
                vm_status_e split_status = vm_split_huge_page(space, entry_base, (vm_page_lvl_e)lvl);
                if (split_status != VM_SUCCESS) return split_status;

                entry = table_va[idx];
                vm_status_e status = vmm_protect_tree_range(ctx, entry & PTE_ADDR_MASK, lvl - 1, cur, sub_end);
                if (status != VM_SUCCESS) return status;
            }
        } else {
            vm_status_e status = vmm_protect_tree_range(ctx, entry & PTE_ADDR_MASK, lvl - 1, cur, sub_end);
            if (status != VM_SUCCESS) return status;
        }
        cur = sub_end;
    }
    return VM_SUCCESS;
}

vm_status_e vm_protect_range(vm_space_t *space, uint64 vaddr, uint64 size, uint64 new_flags) {
    if (!size || (vaddr & PAGE_4K_OFFSET_MASK) || (size & PAGE_4K_OFFSET_MASK)) return VM_ERR_INVALID_ARGS;
    if (!vmm_is_canonical(vaddr, space->paging_level) || !vmm_is_canonical(vaddr + size - 1, space->paging_level))
        return VM_ERR_CANONICAL;

    vm_tlb_batch_t tlb_batch;
    tlb_batch_init(&tlb_batch);

    vm_op_ctx_t ctx = {
        .space = space,
        .tlb_batch = &tlb_batch,
        .flags = new_flags
    };

    vm_status_e status = vmm_protect_tree_range(&ctx, space->cr3_root, space->paging_level, vaddr, vaddr + size);
    vmm_tlb_batch_commit(space,&tlb_batch);
    return status;
}

/* ========================================================================== */
/*                          查询与地址空间生命周期                             */
/* ========================================================================== */

vm_status_e vm_query(const vm_space_t *space, uint64 vaddr, uint64 *out_paddr,
                     uint64 *out_flags, vm_page_lvl_e *out_size) {
    if (!vmm_is_canonical(vaddr, space->paging_level)) return VM_ERR_CANONICAL;

    uint64 cur_table_pa = space->cr3_root;
    for (uint8 lvl = space->paging_level; lvl >= PAGE_LVL_4K; lvl--) {
        uint64 *table_va = (uint64 *)space->ops.phys_to_virt(cur_table_pa);
        uint64 idx = vmm_get_index(vaddr, lvl);
        uint64 entry = table_va[idx];

        if (!(entry & HW_PAGE_P)) return VM_ERR_NOT_MAPPED;

        if ((entry & HW_PAGE_PS) || lvl == PAGE_LVL_4K) {
            uint64 offset_mask = (1ULL << (12 + 9 * (lvl - 1))) - 1;
            if (out_paddr) *out_paddr = (entry & PTE_ADDR_MASK) | (vaddr & offset_mask);
            if (out_flags) *out_flags = (uint64)(entry & PTE_WRITE_MASK);
            if (out_size)  *out_size  = (vm_page_lvl_e)lvl;
            return VM_SUCCESS;
        }
        cur_table_pa = entry & PTE_ADDR_MASK;
    }
    return VM_ERR_NOT_MAPPED;
}

vm_status_e vm_space_init(vm_space_t *space, uint8 level, const vm_allocator_ops_t *ops, boolean clone_kernel) {
    if (((level != 4) && (level != 5)) || !ops) return VM_ERR_INVALID_ARGS;

    space->ops = *ops;
    space->lock = NULL;

    page_t *root_page = space->ops.alloc_pages(0);
    if (!root_page) return VM_ERR_NOMEM;
    uint64 root_pa = space->ops.page_to_phys(root_page);

    space->cr3_root = root_pa;
    space->paging_level = level;

    uint64 *new_root_va = (uint64 *)space->ops.phys_to_virt(root_pa);
    asm_mem_set(new_root_va,0,PAGE_4K_SIZE);

    if (clone_kernel) {
        uint64 active_cr3 = asm_get_cr3();
        uint64 *active_root_va = (uint64 *)space->ops.phys_to_virt(active_cr3 & PTE_ADDR_MASK);

        // 初始化时拷贝共享的内核高地址空间映射
        asm_mem_cpy(active_root_va+256,new_root_va+256,256);
    }
    return VM_SUCCESS;
}

vm_status_e vm_space_destroy(vm_space_t *space) {
    if (!space || !space->cr3_root) return VM_ERR_INVALID_ARGS;

    // 清理非内核空间的用户映射区域，并释放物理空间支持
    uint64 user_limit = 1ULL << (12 + 9 * (space->paging_level - 1) + 8);
    vm_unmap_range(space, 0, user_limit, UNMAP_FLAG_FREE_PHYS);

    space->ops.free_pages(space->ops.phys_to_page(space->cr3_root));
    space->cr3_root = 0;

    return VM_SUCCESS;
}

void vm_space_switch(const vm_space_t *space) {
    __asm__ volatile("mov %0, %%cr3" :: "r"(space->cr3_root) : "memory");
}



