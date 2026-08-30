/**
 * @file vmm_page.c
 * @brief MOS_UEFI 虚拟内存页操作子系统实现
 *
 * 核心算法逻辑：
 * 1. 统一树漫游 (vmm_walk)：基于层级泛化索引公式向下遍历，支持缺页动态分配与单步分配日志记录。
 * 2. 事务回滚引擎：在批量映射出现异常时，基于已映射长度与分配日志执行无损逆向清理。
 * 3. 区间裁剪遍历 (Interval Tree Walk)：解映射与权限修改单次遍历树结构，自动完成大页局部拆分与整页快速覆盖。
 * 4. 树自修剪 (Tree Pruning)：解映射回溯时自动检测空表，逐级释放无用物理帧。
 * 5. TLB 延迟批处理：收集修改地址段，在顶层操作完成时统一执行精准刷新或 CR3 重载。
 */

#include "vmm_page.h"

#define PAGE_SIZE_4KB      0x1000ULL              ///< 4 KiB 标准页字节大小
#define PTE_ADDR_MASK      0x000FFFFFFFFFF000ULL  ///< 提取页表项中物理地址的掩码 (Bits 12..51)
#define PTE_WRITE_MASK  (~(SW_FLAG_OVERWRITE | SW_FLAG_NO_HUGE)) // 掩码逻辑：除了 API 控制符，其他所有的硬件标志和“状态类”软件标志，统统放行！

/**
 * @brief 单次漫游分配日志（用于单步操作失败时的即时回滚）
 */
typedef struct {
    uint64 allocated_tables[8]; ///< 记录单次 vmm_walk 过程中新创建的中间页表物理地址
    uint64  allocated_count;     ///< 新分配的页表数量
} vm_alloc_log_t;

/* ========================================================================== */
/*                         内部基础辅助函数                                    */
/* ========================================================================== */

/**
 * @brief 计算给定虚拟地址在指定分页层级中的 9-bit 索引值
 * @note 公式: Index = (vaddr >> (12 + 9 * (level - 1))) & 0x1FF
 *       Level 1 (PT):   Bits 12..20
 *       Level 2 (PD):   Bits 21..29
 *       Level 3 (PDPT): Bits 30..38
 *       Level 4 (PML4): Bits 39..47
 *       Level 5 (PML5): Bits 48..56
 */
static inline uint64 vmm_get_index(uint64 vaddr, uint8 level) {
    return (vaddr >> (12 + 9 * (level - 1))) & 0x1FF;
}

/**
 * @brief 检查虚拟地址是否符合 x86_64 规范地址要求 (Canonical Address Check)
 * @details 48 位虚拟地址要求 Bits 47..63 必须完全与 Bit 47 相同（符号位扩展）；
 *          57 位虚拟地址要求 Bits 56..63 必须完全与 Bit 56 相同。
 */
static boolean vmm_is_canonical(uint64 vaddr, uint8 paging_level) {
    if (paging_level == 4) {
        int64 sign_extended = ((int64)vaddr << 16) >> 16;
        return (uint64)sign_extended == vaddr;
    } else if (paging_level == 5) {
        int64 sign_extended = ((int64)vaddr << 7) >> 7;
        return (uint64)sign_extended == vaddr;
    }
    return FALSE;
}


/**
 * @brief 初始化 TLB 刷新收集器
 */
static inline void tlb_batch_init(vm_tlb_batch_t *batch) {
    batch->count = 0;
    batch->flush_all = FALSE;
}

/**
 * @brief 向批处理器添加一个需要刷新的虚拟内存区间
 */
static void tlb_batch_add(vm_tlb_batch_t *batch, uint64 vaddr, uint64 size) {
    if (batch->flush_all) return;
    if (batch->count < VM_TLB_MAX_RANGES) {
        batch->ranges[batch->count].start = vaddr;
        batch->ranges[batch->count].size = size;
        batch->count++;
    } else {
        // 收集区间过多，标记为全量刷新以降低后续开销
        batch->flush_all = TRUE;
    }
}

/**
 * @brief 提交 TLB 刷新操作
 * @note 若标记为全量刷新则重载 CR3；否则对每个区间循环调用 invlpg 指令
 */
static void tlb_batch_commit(const vm_tlb_batch_t *batch) {
    if (batch->flush_all) {
        // 通过重新加载当前 CR3 寄存器强制刷新非全局页 TLB
        uint64 cr3;
        __asm__ volatile("mov %%cr3, %0; mov %0, %%cr3" : "=r"(cr3) :: "memory");
        return;
    }
    for (uint64 i = 0; i < batch->count; i++) {
        uint64 cur = batch->ranges[i].start;
        uint64 end = cur + batch->ranges[i].size;
        while (cur < end) {
            __asm__ volatile("invlpg (%0)" :: "r"(cur) : "memory");
            cur += PAGE_SIZE_4KB;
        }
    }
}

/* ========================================================================== */
/*                         页表通用遍历 (Walk)                                 */
/* ========================================================================== */

/**
 * @brief 页表树自顶向下遍历函数 (通用漫游引擎)
 * @param space          目标地址空间
 * @param vaddr          待查找的虚拟地址
 * @param target_level   遍历终止的目标层级 (1 = PT, 2 = PD, 3 = PDPT)
 * @param create_missing 路径中遇到缺失页表时是否自动分配创建
 * @param out_entry      输出目标层级页表项的虚拟地址指针
 * @param alloc_log      分配日志指针（用于记录新创建的中间页表以便回滚）
 * @return vm_status_t   执行状态
 */
static vm_status_e vmm_walk(vm_space_t *space, uint64 vaddr, uint8 target_level,
                            boolean create_missing, uint64 **out_entry, vm_alloc_log_t *alloc_log) {
    if (!vmm_is_canonical(vaddr, space->paging_level)) {
        return VM_ERR_CANONICAL;
    }

    uint64 cur_table_pa = space->cr3_root;

    // 从顶层根页表逐级向下漫游至 target_level 的上一级
    for (uint8 lvl = space->paging_level; lvl > target_level; lvl--) {
        uint64 *table_va = (uint64 *)space->ops.phys_to_virt(cur_table_pa);
        uint64 idx = vmm_get_index(vaddr, lvl);
        uint64 entry = table_va[idx];

        if (entry & HW_PAGE_P) {
            // 中间路径遇到大页标志（说明此处已存在未拆分的大页，无法继续深入）
            if (entry & HW_PAGE_PS) {
                return VM_ERR_ALREADY_MAPPED;
            }
            cur_table_pa = entry & PTE_ADDR_MASK;
        } else {
            // 节点不存在且不要求创建
            if (!create_missing) {
                return VM_ERR_NOT_MAPPED;
            }

            // 分配新的 4KB 物理页作为下级页表
            uint64 new_table_pa = space->ops.alloc_4k();
            if (!new_table_pa) {
                return VM_ERR_NOMEM;
            }

            // 记录到单步事务分配日志中
            if (alloc_log && alloc_log->allocated_count < 8) {
                alloc_log->allocated_tables[alloc_log->allocated_count++] = new_table_pa;
            }

            // 中间目录项默认赋予全权限 (Present | Writable | User)，最终权限由末级叶子项收敛控制
            table_va[idx] = new_table_pa | HW_PAGE_P | HW_PAGE_RW | HW_PAGE_US;
            cur_table_pa = new_table_pa;
        }
    }

    // 获取目标层级页表的虚拟地址，并返回对应条目的指针
    uint64 *final_table_va = (uint64 *)space->ops.phys_to_virt(cur_table_pa);
    *out_entry = &final_table_va[vmm_get_index(vaddr, target_level)];
    return VM_SUCCESS;
}

/* ========================================================================== */
/*                          大页拆分引擎 (Huge Page Splitter)                  */
/* ========================================================================== */

/**
 * @brief 将指定虚拟地址所在的大页(2MB/1GB)炸开(Split)，拆分为 512 个下级页(4KB/2MB)
 *
 * @param space     目标虚拟地址空间
 * @param vaddr     落入目标大页范围内的任意虚拟地址
 * @param from_size 当前大页的层级级别 (2: 2MB PDE, 3: 1GB PDPT)
 * @return vm_status_t 执行状态
 */
vm_status_e vm_split_huge_page(vm_space_t *space, uint64 vaddr, vm_page_size_e from_size) {
    uint8 cur_lvl = (uint8)from_size;

    // 硬件限制：仅支持将 Level 2 (2MB) 拆分为 4KB，或将 Level 3 (1GB) 拆分为 2MB
    if (cur_lvl != 2 && cur_lvl != 3) {
        return VM_ERR_INVALID_ARGS;
    }

    // 计算当前大页的总容量大小，并将 vaddr 严格向下对齐至该大页的起始边界
    uint64 huge_size = 1ULL << (12 + 9 * (cur_lvl - 1));
    vaddr &= ~(huge_size - 1);

    uint64 *entry = NULL;
    // 顺藤摸瓜，拿到当前层级的大页目录项
    if (vmm_walk(space, vaddr, cur_lvl, FALSE, &entry, NULL) != VM_SUCCESS || !entry) {
        return VM_ERR_NOT_MAPPED;
    }

    // 防御性校验：必须存在，且必须是真正的大页
    if (!(*entry & HW_PAGE_P) || !(*entry & HW_PAGE_PS)) {
        return VM_ERR_INVALID_ARGS;
    }

    // 向 PMM 申请一个全 0 的物理帧，作为下一级子页表
    uint64 sub_table_pa = space->ops.alloc_4k();
    if (!sub_table_pa) {
        return VM_ERR_NOMEM;
    }

    uint64 *sub_table_va = (uint64 *)space->ops.phys_to_virt(sub_table_pa);

    // =========================================================================
    // 优化 1：统一掩码提取物理基址 (避开 PAT 位与保留位陷阱)
    // 2MB 大页有效位 [21:51]，1GB 大页有效位 [30:51]。
    // 根据 Intel 规范，1GB 的 [21:29] 必为 0。因此使用截断低 21 位的掩码可同时兼容两者。
    // =========================================================================
    uint64 base_pa = *entry & 0x000FFFFFFFE00000ULL;

    // 提取原大页所有的基础权限位与缓存控制位
    uint64 child_flags = *entry & (HW_PAGE_P | HW_PAGE_G | HW_PAGE_NX | HW_PAGE_RW | HW_PAGE_US |
                                   HW_PAGE_HUGE_PAT | HW_PAGE_PWT | HW_PAGE_PCD| HW_PAGE_PS );

    if (cur_lvl == 2) {
        // 只要是拆成 4KB，必须无条件抹杀 HUGE (PS) 标志！
        child_flags &= ~HW_PAGE_PS;

        // 【大快人心】：以前在这里专门写的处理 PAT 漂移的 4 行 if 逻辑，全部删光！
    }

    // 计算拆分后，每个下级子页的物理基址步进跨度 (2MB拆为4KB, 1GB拆为2MB)
    uint64 sub_step = 1ULL << (12 + 9 * (cur_lvl - 2));

    // =========================================================================
    // 极致极简循环：没有任何分支判断，利用标量指令并行打满
    // =========================================================================
    for (uint64 i = 0; i < 512; i++) {
        sub_table_va[i] = (base_pa + i * sub_step) | child_flags;
    }

    // =========================================================================
    // 安全策略落实：更新原目录项
    // 1. 挂载子页表物理地址
    // 2. 移除 HUGE 标志 (它现在是一个纯粹的指向下级的目录了)
    // 3. 强制 WRITABLE (防 Upgrade 陷阱，将控制权下放)
    // 4. 动态继承 USER (保障 Meltdown 等推测执行安全)
    // =========================================================================
    uint64 dir_user_flag = *entry & HW_PAGE_US;
    *entry = sub_table_pa | HW_PAGE_P | HW_PAGE_RW | dir_user_flag;

    // 局部刷新该虚拟地址的 TLB，丢弃旧的大页缓存
    __asm__ volatile("invlpg (%0)" :: "r"(vaddr) : "memory");

    return VM_SUCCESS;
}

/* ========================================================================== */
/*                         高内聚区间映射与事务回滚引擎                       */
/* ========================================================================== */

/**
 * @brief 将虚拟地址区间映射到指定的物理地址区间 (支持贪心大页与安全覆写)
 *
 * @param space  虚拟地址空间上下文 (包含根页表、PMM 分配回调及锁等)
 * @param vaddr  起始虚拟地址 (必须 4KB 对齐)
 * @param paddr  起始物理地址 (必须 4KB 对齐)
 * @param size   映射总长度 (必须 4KB 对齐)
 * @param flags  内存属性与修饰符组合 (如 PAGE_USER_DATA_RW | SW_PAGE_OVERWRITE)
 * @return vm_status_e 执行状态
 */
vm_status_e vm_map_range(vm_space_t *space, uint64 vaddr, uint64 paddr, uint64 size, uint64 flags) {
    // 1. 基础参数合法性与 4KB 对齐校验
    if (!size || (vaddr & 0xFFF) || (paddr & 0xFFF) || (size & 0xFFF)) {
        return VM_ERR_INVALID_ARGS;
    }

    // 2. 虚拟地址区间规范性校验 (防止在 x86_64 留下 canonical 漏洞)
    if (!vmm_is_canonical(vaddr, space->paging_level) ||
        !vmm_is_canonical(vaddr + size - 1, space->paging_level)) {
        return VM_ERR_CANONICAL;
    }

    // 初始化 TLB 批量刷新器
    vm_tlb_batch_t tlb_batch;
    tlb_batch_init(&tlb_batch);

    uint64 curr_va = vaddr;
    uint64 curr_pa = paddr;
    uint64 mapped_bytes = 0;

    vm_status_e status;

    while (mapped_bytes < size) {
        uint64 remaining = size - mapped_bytes;
        uint8 target_level = 1;     // 默认使用 Level 1 (4KB 页面)
        uint64 step_bytes = 0x1000;
        uint64 new_flags = flags;

        // =====================================================================
        // 【贪心算法】：在满足对齐、且长度充足时，狂暴升级为大页
        // =====================================================================
        if (!(new_flags & SW_FLAG_NO_HUGE)) {
            // 尝试 1GB 巨页 (Level 3)
            if (remaining >= 0x40000000 && !(curr_va & 0x3FFFFFFF) && !(curr_pa & 0x3FFFFFFF)) {
                target_level = 3;
                step_bytes = 0x40000000;
                // 【PAT 终极优化】：再也不需要 PAT(Bit 7) 到 (Bit 12) 的恶心漂移！
                // 因为 PWT 和 PCD 的位置永远固定，我们只需轻轻打上大页标志 (PS)！
                new_flags |= HW_PAGE_PS;
            }
            // 尝试 2MB 大页 (Level 2)
            else if (remaining >= 0x200000 && !(curr_va & 0x1FFFFF) && !(curr_pa & 0x1FFFFF)) {
                target_level = 2;
                step_bytes = 0x200000;
                // 【PAT 终极优化】：再也不需要 PAT(Bit 7) 到 (Bit 12) 的恶心漂移！
                // 因为 PWT 和 PCD 的位置永远固定，我们只需轻轻打上大页标志 (PS)！
                new_flags |= HW_PAGE_PS;
            }
        }

        // 记录单步寻址过程中临时分配的中间目录 (用于 OOM 及冲突时的精准回滚)
        vm_alloc_log_t log = { .allocated_count = 0 };
        uint64 *entry = NULL;

        // 向下漫游页表树，按需造桥 (分配中间目录)
        status = vmm_walk(space, curr_va, target_level, TRUE, &entry, &log);

        // 【防泄漏补丁 1 - 孤儿页表清理】：若 walk 走到一半物理内存耗尽，必须超度刚才临时造的桥梁
        if (status != VM_SUCCESS) {
            for (uint64 i = 0; i < log.allocated_count; i++) {
                space->ops.free_4k(log.allocated_tables[i]);
            }
            goto rollback;
        }

        // =====================================================================
        // 冲突拦截、目录强拆重建 (Unmap-then-Map) 与旧页安全回收
        // =====================================================================
        if (*entry & HW_PAGE_P) {

            // 1. 权限拦截：若用户未授权覆盖 (OVERWRITE)，则清理临时目录，立刻报错撤销
            if (!(new_flags & SW_FLAG_OVERWRITE)) {
                for (uint64 i = 0; i < log.allocated_count; i++) {
                    space->ops.free_4k(log.allocated_tables[i]);
                }
                status = VM_ERR_ALREADY_MAPPED;
                goto rollback;
            }

            // 2. 【防泄漏补丁 2 - 大页强拆重建】：
            // 如果我们准备盖大页 (target_level > 1)，却发现脚下是一栋结构复杂的“旧楼”(无 PS 标志的目录)，
            // 绝不能强行覆盖指针！采用 Unmap-then-Map 策略，将旧楼彻底夷为平地后再盖大页。
            if (target_level > 1 && !(*entry & HW_PAGE_PS)) {
                // 动作 A：放弃本次注入，清理刚为了大页漫游而临时分配的桥梁
                for (uint64 i = 0; i < log.allocated_count; i++) {
                    space->ops.free_4k(log.allocated_tables[i]);
                }

                // 动作 B：调用现成的高级解映射接口，利用自修剪算法，递归摧毁这片区域内的所有旧映射及页表
                // (注: 若架构采用细粒度锁，需确保 vm_unmap_range 内部调用不会产生死锁)
                vm_unmap_range(space, curr_va, step_bytes);

                // 动作 C：虚拟地址不推进一步，原地 continue 重试。
                // 下一次回来时，旧楼已空，大页将完美落成！
                continue;
            }

            // 3. 【防泄漏补丁 3 - 旧数据页完美回收】：
            // 能安稳走到这里的，一定是准备被同级覆盖的叶子节点 (4K 盖 4K，或 2M 盖 2M)。
            // 在填入新物理地址前，提取旧物理地址并通知 PMM 回收，做到真正的 0 数据页泄漏。
            uint64 old_pa = *entry & PTE_ADDR_MASK;
            if (space->ops.free_4k && old_pa != 0) {
                // 注：若未来底层引入了写时复制 (COW)，这里的 free 需变更为 ref_count--
                space->ops.free_4k(old_pa);
            }
        }

        // =====================================================================
        // 执行物理映射注入
        // =====================================================================
        uint64 pte_flags = new_flags & PTE_WRITE_MASK;
        uint64 new_entry = (curr_pa & PTE_ADDR_MASK) | pte_flags;
        *entry = new_entry;

        // 将被修改的地址加入 TLB 待刷新队列
        tlb_batch_add(&tlb_batch, curr_va, step_bytes);

        // 成功映射，游标推进
        curr_va += step_bytes;
        curr_pa += step_bytes;
        mapped_bytes += step_bytes;
    } // end while

    // 整个范围映射事务成功完成，一次性批量下发 TLB Shootdown
    tlb_batch_commit(&tlb_batch);
    return VM_SUCCESS;

rollback:
    // =====================================================================
    // 宏观事务失败：全自动大回滚机制
    // =====================================================================
    // 如果本次映射操作中途暴毙（如 OOM），利用 vm_unmap_range 的“自修剪”能力，
    // 将之前成功映射的前半段范围彻底摧毁，保持内存状态原子性。
    if (mapped_bytes > 0) {
        vm_unmap_range(space, vaddr, mapped_bytes);
    }
    return status;
}

/* ========================================================================== */
/*                         范围解映射与自动修剪                                */
/* ========================================================================== */

/**
 * @brief 递归区间裁剪解映射与自修剪核心函数
 * @param space     目标地址空间
 * @param table_pa  当前处理页表的物理地址
 * @param lvl       当前页表层级 (5..1)
 * @param start     当前层需处理的起始虚拟地址
 * @param end       当前层需处理的结束虚拟地址 (开区间)
 * @param tlb_batch TLB 收集器
 * @return TRUE 表示当前页表已完全变为空表，通知上级父节点释放该条目并回收物理帧
 */
static boolean vmm_unmap_tree_range(vm_space_t *space, uint64 table_pa, uint8 lvl,
                                 uint64 start, uint64 end, vm_tlb_batch_t *tlb_batch) {
    uint64 *table_va = (uint64 *)space->ops.phys_to_virt(table_pa);
    uint64 step = 1ULL << (12 + 9 * (lvl - 1)); // 当前层单个条目覆盖的虚拟内存范围
    uint64 cur = start;

    while (cur < end) {
        uint64 idx = vmm_get_index(cur, lvl);
        uint64 entry_base = cur & ~(step - 1);
        uint64 next_boundary = entry_base + step;
        uint64 sub_end = (end < next_boundary) ? end : next_boundary; // 裁剪出当前条目重叠的区间

        uint64 entry = table_va[idx];

        if (entry & HW_PAGE_P) {
            if (lvl == 1) {
                // 1. 标准 4KB 叶子节点：直接清除条目
                table_va[idx] = 0;
                tlb_batch_add(tlb_batch, cur, step);
            } else if (entry & HW_PAGE_PS) {
                // 2. 大页节点
                if (cur == entry_base && sub_end == next_boundary) {
                    // 解映射范围完整覆盖整个大页：直接清除大页条目
                    table_va[idx] = 0;
                    tlb_batch_add(tlb_batch, cur, step);
                } else {
                    // 解映射范围仅覆盖大页的一部分：先拆分大页，再递归解映射局部
                    if (vm_split_huge_page(space, entry_base, (vm_page_size_e)lvl) == VM_SUCCESS) {
                        entry = table_va[idx];
                        uint64 child_pa = entry & PTE_ADDR_MASK;
                        if (vmm_unmap_tree_range(space, child_pa, lvl - 1, cur, sub_end, tlb_batch)) {
                            table_va[idx] = 0;
                        }
                    }
                }
            } else {
                // 3. 中间目录节点：递归向下处理子树
                uint64 child_pa = entry & PTE_ADDR_MASK;
                if (vmm_unmap_tree_range(space, child_pa, lvl - 1, cur, sub_end, tlb_batch)) {
                    table_va[idx] = 0;
                }
            }
        }

        cur = sub_end;
    }

    // 4. 自底向上树修剪 (Tree Pruning): 扫描当前页表是否全空
    if (lvl != space->paging_level) { // 绝不释放根页表 (CR3)
        for (uint64 i = 0; i < 512; i++) {
            if (table_va[i] & HW_PAGE_P) {
                return FALSE; // 仍有条目在使用，不能释放
            }
        }
        // 512 个条目全为 0，归还该页表物理帧给分配器
        space->ops.free_4k(table_pa);
        return TRUE; // 返回 TRUE 通知父节点将对应条目清零
    }

    return FALSE;
}

vm_status_e vm_unmap_range(vm_space_t *space, uint64 vaddr, uint64 size) {
    if (!size || (vaddr & 0xFFF) || (size & 0xFFF)) {
        return VM_ERR_INVALID_ARGS;
    }
    if (!vmm_is_canonical(vaddr, space->paging_level) ||
        !vmm_is_canonical(vaddr + size - 1, space->paging_level)) {
        return VM_ERR_CANONICAL;
    }

    vm_tlb_batch_t tlb_batch;
    tlb_batch_init(&tlb_batch);

    // 单次递归调用，完成整个区间的页表销毁与空节点修剪
    vmm_unmap_tree_range(space, space->cr3_root, space->paging_level, vaddr, vaddr + size, &tlb_batch);

    // 统一提交 TLB 刷新
    tlb_batch_commit(&tlb_batch);
    return VM_SUCCESS;
}

/* ========================================================================== */
/*                         范围权限修改与树裁剪                                */
/* ========================================================================== */

/**
 * @brief 递归区间裁剪权限修改核心函数
 */
static vm_status_e vmm_protect_tree_range(vm_space_t *space, uint64 table_pa, uint8 lvl,
                                          uint64 start, uint64 end, uint64 new_flags,
                                          vm_tlb_batch_t *tlb_batch) {
    uint64 *table_va = (uint64 *)space->ops.phys_to_virt(table_pa);
    uint64 step = 1ULL << (12 + 9 * (lvl - 1));
    uint64 cur = start;

    while (cur < end) {
        uint64 idx = vmm_get_index(cur, lvl);
        uint64 entry_base = cur & ~(step - 1);
        uint64 next_boundary = entry_base + step;
        uint64 sub_end = (end < next_boundary) ? end : next_boundary;

        uint64 entry = table_va[idx];

        if (entry & HW_PAGE_P) {
            uint64 pa = entry & PTE_ADDR_MASK;

            if (lvl == 1) {
                // 1. 叶子节点 (4KB 标准页)：保留物理地址，更新属性标志
                table_va[idx] = pa | (new_flags & PTE_WRITE_MASK) | HW_PAGE_P;
                tlb_batch_add(tlb_batch, cur, step);
            } else if (entry & HW_PAGE_PS) {
                // 2. 大页节点
                if (cur == entry_base && sub_end == next_boundary) {
                    // 权限修改范围完整覆盖整个大页：直接就地更新大页条目属性
                    table_va[idx] = pa | (new_flags & PTE_WRITE_MASK) | HW_PAGE_P | HW_PAGE_PS;
                    tlb_batch_add(tlb_batch, cur, step);
                } else {
                    // 范围仅涉及大页的一部分：先拆分大页为子页表，再向下递归修改
                    vm_status_e split_status = vm_split_huge_page(space, entry_base, (vm_page_size_e)lvl);
                    if (split_status != VM_SUCCESS) {
                        return split_status;
                    }

                    entry = table_va[idx];
                    uint64 child_pa = entry & PTE_ADDR_MASK;
                    vm_status_e status = vmm_protect_tree_range(space, child_pa, lvl - 1, cur, sub_end, new_flags, tlb_batch);
                    if (status != VM_SUCCESS) {
                        return status;
                    }
                }
            } else {
                // 3. 中间目录节点：向下递归处理子树
                uint64 child_pa = entry & PTE_ADDR_MASK;
                vm_status_e status = vmm_protect_tree_range(space, child_pa, lvl - 1, cur, sub_end, new_flags, tlb_batch);
                if (status != VM_SUCCESS) {
                    return status;
                }
            }
        }

        cur = sub_end;
    }

    return VM_SUCCESS;
}

vm_status_e vm_protect_range(vm_space_t *space, uint64 vaddr, uint64 size, uint64 new_flags) {
    if (!size || (vaddr & 0xFFF) || (size & 0xFFF)) {
        return VM_ERR_INVALID_ARGS;
    }
    if (!vmm_is_canonical(vaddr, space->paging_level) ||
        !vmm_is_canonical(vaddr + size - 1, space->paging_level)) {
        return VM_ERR_CANONICAL;
    }

    vm_tlb_batch_t tlb_batch;
    tlb_batch_init(&tlb_batch);

    // 单次递归树遍历，自动处理全量大页更新与局部大页拆分修改
    vm_status_e status = vmm_protect_tree_range(space, space->cr3_root, space->paging_level,
                                                vaddr, vaddr + size, new_flags, &tlb_batch);
    tlb_batch_commit(&tlb_batch);
    return status;
}

/* ========================================================================== */
/*                         查询与地址空间生命周期                              */
/* ========================================================================== */

vm_status_e vm_query(const vm_space_t *space, uint64 vaddr, uint64 *out_paddr,
                     uint64 *out_flags, vm_page_size_e *out_size) {
    if (!vmm_is_canonical(vaddr, space->paging_level)) {
        return VM_ERR_CANONICAL;
    }

    uint64 cur_table_pa = space->cr3_root;

    // 自顶向下逐级漫游查询
    for (uint8 lvl = space->paging_level; lvl >= 1; lvl--) {
        uint64 *table_va = (uint64 *)space->ops.phys_to_virt(cur_table_pa);
        uint64 idx = vmm_get_index(vaddr, lvl);
        uint64 entry = table_va[idx];

        // 遇到未映射项
        if (!(entry & HW_PAGE_P)) {
            return VM_ERR_NOT_MAPPED;
        }

        // 遇到叶子节点（大页或到达 Level 1 终点）
        if ((entry & HW_PAGE_PS) || lvl == 1) {
            uint64 offset_mask = (1ULL << (12 + 9 * (lvl - 1))) - 1;
            if (out_paddr) *out_paddr = (entry & PTE_ADDR_MASK) | (vaddr & offset_mask);
            if (out_flags) *out_flags = (uint64)(entry & PTE_WRITE_MASK);
            if (out_size)  *out_size  = (vm_page_size_e)lvl;
            return VM_SUCCESS;
        }

        cur_table_pa = entry & PTE_ADDR_MASK;
    }

    return VM_ERR_NOT_MAPPED;
}

vm_status_e vm_space_init(vm_space_t *space, uint8 level, vm_allocator_ops_t ops, boolean clone_kernel) {
    if (level != 4 && level != 5) {
        return VM_ERR_INVALID_ARGS;
    }

    // 分配并清零顶层根页表
    uint64 root_pa = ops.alloc_4k();
    if (!root_pa) {
        return VM_ERR_NOMEM;
    }

    space->cr3_root = root_pa;
    space->paging_level = level;
    space->ops = ops;
    space->lock = NULL;

    if (clone_kernel) {
        // 读取当前活动的 CR3 根页表物理地址
        uint64 active_cr3;
        __asm__ volatile("mov %%cr3, %0" : "=r"(active_cr3));
        uint64 active_root_pa = active_cr3 & PTE_ADDR_MASK;

        uint64 *active_root_va = (uint64 *)ops.phys_to_virt(active_root_pa);
        uint64 *new_root_va = (uint64 *)ops.phys_to_virt(root_pa);

        // 复制高半核共享条目 (索引 256..511 覆盖高半区虚拟地址空间)
        for (uint64 i = 256; i < 512; i++) {
            new_root_va[i] = active_root_va[i];
        }
    }

    return VM_SUCCESS;
}

vm_status_e vm_space_destroy(vm_space_t *space) {
    if (!space || !space->cr3_root) {
        return VM_ERR_INVALID_ARGS;
    }

    // 计算用户态低半区 (索引 0..255) 覆盖的虚拟地址上限并执行范围解映射
    uint64 user_limit = 1ULL << (12 + 9 * (space->paging_level - 1) + 8);
    vm_unmap_range(space, 0, user_limit);

    // 释放根页表物理帧
    space->ops.free_4k(space->cr3_root);
    space->cr3_root = 0;
    return VM_SUCCESS;
}

void vm_space_switch(const vm_space_t *space) {
    // 加载根页表物理地址到 CR3 寄存器，完成硬件 MMU 上下文切换
    __asm__ volatile("mov %0, %%cr3" :: "r"(space->cr3_root) : "memory");
}