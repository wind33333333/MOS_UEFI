#include "../include/memblock.h"
#include "../include/vmm.h"
#include "../include/printk.h"
#include "../include/errno.h"

INIT_DATA memblock_alloc_t memblock; //临时内存器内存地图，后面buddy system需要用到空闲地图。
INIT_DATA mem_arr_t page_mem_map; //page页映射区内存地图
INIT_DATA efi_runtime_memmap_t efi_runtime_memmap; //uefi运行时的数据和代码地图
mem_arr_t direct_mem_map; //直接映射区内存地图


//物理内存区域添加到 memblock 的列表中
INIT_TEXT void memblock_add(mem_arr_t *memblock_type, uint64 pa_start, uint64 size) {
    if (memblock_type->count == 0) {
        memblock_type->region[0].start_pa = pa_start;
        memblock_type->region[0].size = size;
        memblock_type->count++;
    } else if (memblock_type->region[memblock_type->count - 1].start_pa + memblock_type->region[
                   memblock_type->count - 1].
               size == pa_start) {
        memblock_type->region[memblock_type->count - 1].size += size;
    } else {
        memblock_type->region[memblock_type->count].start_pa = pa_start;
        memblock_type->region[memblock_type->count].size = size;
        memblock_type->count++;
    }
}


#define MEM_1MB (0x100000ULL) // 1MB 物理地址边界
INIT_TEXT void init_memblock(void) {
    uint64 phy_mem_size = 0;
    uint64 kernel_pa_start = (uint64) _start - KERNEL_VA_START;
    uint64 kernel_pa_end = (uint64) _end - KERNEL_VA_START;

    uint32 count = boot_info->mem_map_size / boot_info->mem_descriptor_size;
    for (uint32 i = 0; i < count; i++) {
        EFI_MEMORY_DESCRIPTOR *mem_des = (EFI_MEMORY_DESCRIPTOR *) (
            (uint8 *) boot_info->mem_map + i * boot_info->mem_descriptor_size);
        if (mem_des->NumberOfPages == 0) continue;

        uint64 pa_start = mem_des->PhysicalStart;
        uint64 size = mem_des->NumberOfPages << PAGE_4K_SHIFT;
        uint64 pa_end = pa_start + size;

        uint32 type = mem_des->Type;
        // 1. 处理普通可用内存 (核心逻辑：映射 + 截断 + 切割 + 释放)
        if (type == EFI_LOADER_DATA ||
            type == EFI_LOADER_CODE ||
            type == EFI_BOOT_SERVICES_CODE ||
            type == EFI_BOOT_SERVICES_DATA ||
            type == EFI_CONVENTIONAL_MEMORY) {
            phy_mem_size += size; // 计算总物理内存

            // 物理地址空间加入直接映射区 & page映射区
            memblock_add(&direct_mem_map, pa_start, size);
            memblock_add(&page_mem_map, pa_start, size);

            // 🛡️ 1MB 截断处理逻辑
            if (pa_start < MEM_1MB) {
                if (pa_end > MEM_1MB) {
                    size -= MEM_1MB - pa_start;
                    pa_start = MEM_1MB;
                } else {
                    continue; // 1M以内的全部丢弃
                }
            }

            // 🔪 把内核切出来
            if (pa_start < kernel_pa_end && pa_end > kernel_pa_start) {
                if (pa_start < kernel_pa_start) {
                    memblock_add(&memblock.free, pa_start, kernel_pa_start - pa_start);
                }
                if (pa_end > kernel_pa_end) {
                    memblock_add(&memblock.free, kernel_pa_end, pa_end - kernel_pa_end);
                }
            } else {
                // 完全没有交集，整块存到 free 中
                memblock_add(&memblock.free, pa_start, size);
            }
        } else if (type == EFI_ACPI_RECLAIM_MEMORY || type == EFI_ACPI_MEMORY_NVS) {
            phy_mem_size += size; // ACPI 也是真实插在主板上的物理内存，计入总数

            // 仅仅加入直接映射区，保证内核后续能读到 ACPI 表即可
            memblock_add(&direct_mem_map, pa_start, size);

            // 剥离的好处：到这里直接结束！不参与后面的 page 映射和 free 分配。
        } else if (type == EFI_RUNTIME_SERVICES_DATA || type == EFI_RUNTIME_SERVICES_CODE) {
            efi_runtime_memmap.mem_map[efi_runtime_memmap.count] = *mem_des;
            efi_runtime_memmap.count++;
        }
    }

    color_printk(GREEN, BLACK, "Total Physics Memory:%dMB\n", phy_mem_size / 1024 / 1024);
}


INIT_TEXT uint64 memblock_alloc(uint64 size, uint64 align) {
    if (!size) return EINVAL;
    uint64 align_base, align_size;
    uint32 index = 0;

    // 寻找合适的空闲块
    for (index = 0; index < memblock.free.count; index++) {
        uint64 base = memblock.free.region[index].start_pa;
        align_base = align_up(base, align);
        align_size = align_base - base + size;

        if (align_size <= memblock.free.region[index].size) {
            break; // 找到了！
        }
    }

    if (index >= memblock.free.count) return ENOMEM; // OOM: 内存耗尽

    // 🛡️ 架构级补全：将分配出去的内存记录到 used 池中
    memblock_add(&memblock.used, align_base, size);

    // 开始执行切割逻辑
    if (align_base == memblock.free.region[index].start_pa && size == memblock.free.region[index].size) {
        // 全等：完美切除
        for (uint32 j = index; j < memblock.free.count - 1; j++) {
            memblock.free.region[j] = memblock.free.region[j + 1];
        }
        memblock.free.count--;

    } else if (align_base == memblock.free.region[index].start_pa) {
        // 切头
        memblock.free.region[index].start_pa += size;
        memblock.free.region[index].size -= size;

    } else if (align_size == memblock.free.region[index].size) {
        // 切尾
        memblock.free.region[index].size -= align_size; // ⚠️ 注意：切尾时，剩下的是前面的部分，减去的是 align_size！

    } else {
        // 中间切 (一分为二)
        if (memblock.free.count >= MAX_MEMBLOCK) {
            color_printk(RED, BLACK, "PANIC: memblock free array overflow in alloc!\n");
            while(1); // 必须死机保护
        }

        // 把后面的数据全部向右挪一位，腾出空间
        for (uint32 j = memblock.free.count; j > index + 1; j--) {
            memblock.free.region[j] = memblock.free.region[j - 1];
        }

        // 设置右侧的新碎块
        memblock.free.region[index + 1].start_pa = align_base + size;
        memblock.free.region[index + 1].size = memblock.free.region[index].size - align_size;

        // 修正左侧的旧碎块
        memblock.free.region[index].size = align_base - memblock.free.region[index].start_pa;

        memblock.free.count++;
    }
    return align_base;
}

INIT_TEXT int32 memblock_free(uint64 ptr, uint64 size) {
    if (!size) return EINVAL;

    uint32 insert_idx = 0;

    // 1. 寻找插入点：保证整个数组物理地址严格递增
    while (insert_idx < memblock.free.count && memblock.free.region[insert_idx].start_pa < ptr) {
        insert_idx++;
    }

    boolean merged_left = FALSE;
    boolean merged_right = FALSE;

    // 2. 尝试向左合并 (看它是否能拼在左侧邻居的尾巴上)
    if (insert_idx > 0) {
        uint32 left = insert_idx - 1;
        if (memblock.free.region[left].start_pa + memblock.free.region[left].size == ptr) {
            memblock.free.region[left].size += size; // 吞并！
            merged_left = TRUE;
        }
    }

    // 3. 尝试向右合并 (看它是否能拼在右侧邻居的头上)
    if (insert_idx < memblock.free.count) {
        if (ptr + size == memblock.free.region[insert_idx].start_pa) {
            if (merged_left) {
                // 🌟 终极事件：双向合并！(左侧已经吞并了它，现在左侧的尾巴碰到了右侧的头)
                uint32 left = insert_idx - 1;
                // 让左侧直接吃掉右侧
                memblock.free.region[left].size += memblock.free.region[insert_idx].size;

                // 因为右侧被吃掉了，数组整体向左平移一格，消灭空洞
                for (uint32 j = insert_idx; j < memblock.free.count - 1; j++) {
                    memblock.free.region[j] = memblock.free.region[j + 1];
                }
                memblock.free.count--;
            } else {
                // 只向右合并
                memblock.free.region[insert_idx].start_pa = ptr;
                memblock.free.region[insert_idx].size += size;
            }
            merged_right = TRUE;
        }
    }

    // 4. 如果两边都不接壤（孤岛），老老实实插入新块
    if (!merged_left && !merged_right) {
        if (memblock.free.count >= MAX_MEMBLOCK) {
            color_printk(RED, BLACK, "PANIC: memblock free array overflow in free!\n");
            while(1);
        }
        // 数组向右平移，腾出坑位
        for (uint32 j = memblock.free.count; j > insert_idx; j--) {
            memblock.free.region[j] = memblock.free.region[j - 1];
        }
        memblock.free.region[insert_idx].start_pa = ptr;
        memblock.free.region[insert_idx].size = size;
        memblock.free.count++;
    }

    // 🛡️ 架构级补全：最好在此处写一个逻辑，将其从 memblock.used 数组中剔除 (同理)

    return 0;
}


//========================================================== 批量映射虚拟内存接口 =======================================================


// ===================================================================
// 【Level 1: PT 层】 4KB 碎页横向铺砖
// ===================================================================
static inline int32 memblock_map_pte_range(uint64 *pde,uint64 va, uint64 pa,  uint64 end, uint64 attr) {
    uint64 *ptt;
    uint64 table_pa;

    // 如果当前的 PT 表不存在，立刻原地建一张新表
    if (!(*pde & PAGE_P)) {
        table_pa = memblock_alloc(PAGE_4K_SIZE,PAGE_4K_SIZE);
        if (!table_pa) return -ENOMEM;
        uint64 next_pa = table_pa;
        asm_mem_set(pa_to_va(next_pa), 0, PAGE_4K_SIZE);

        // 挂载到父级 PD 目录 (继承必要的读写和用户态权限)
        uint64 dir_attr = PAGE_P | PAGE_RW | (attr & PAGE_US);
        *pde = next_pa | dir_attr;
    }

    ptt = pa_to_va(*pde & PAGE_PA_MASK);
    uint32 idx = get_table_idx(va,PTE_SHIFT);

    // 🚀 横向铺砖：沿着 PTE 数组狂奔，完美利用 L1 Cache
    do {
        if (!(ptt[idx] & PAGE_P)) { // 防冲突：仅映射空位
            ptt[idx] = pa | attr | PAGE_P;
        }
    // VA 和 PA 必须手拉手一起横向推进 4KB！
    } while (idx++, pa += PAGE_4K_SIZE, va += PAGE_4K_SIZE, va != end);

    return 0;
}

// ===================================================================
// 【Level 2: PD 层】 2MB 智能升维与横向派发
// ===================================================================
static inline int32 memblock_map_pde_range(uint64 *pdpte,uint64 va,  uint64 pa, uint64 end, uint64 attr) {
    uint64 *pdt;
    uint64 table_pa;

    if (!(*pdpte & PAGE_P)) {
        table_pa = memblock_alloc(PAGE_4K_SIZE,PAGE_4K_SIZE);
        if (!table_pa) return -ENOMEM;
        uint64 next_pa = table_pa;
        asm_mem_set(pa_to_va(next_pa), 0, PAGE_4K_SIZE);

        uint64 dir_attr = PAGE_P | PAGE_RW | (attr & PAGE_US);
        *pdpte = next_pa | dir_attr;
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
            }
        }
        // 凑不齐 2MB，就把这块切好的任务下发给底层去铺碎砖
        else {
            int err = memblock_map_pte_range(&pdt[idx],va, pa,  next, attr);
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
static inline int32 memblock_map_pdpte_range(uint64 *pml4e, uint64 va, uint64 pa, uint64 end, uint64 attr) {
    uint64 *pdptt;
    uint64 table_pa;

    if (!(*pml4e & PAGE_P)) {
        table_pa = memblock_alloc(PAGE_4K_SIZE,PAGE_4K_SIZE);
        if (!table_pa) return -ENOMEM;
        uint64 next_pa = table_pa;
        asm_mem_set(pa_to_va(next_pa), 0, PAGE_4K_SIZE);

        uint64 dir_attr = PAGE_P | PAGE_RW | (attr & PAGE_US);
        *pml4e = next_pa | dir_attr;
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
            }
        } else {
            int err = memblock_map_pde_range(&pdptt[idx],va,pa,next, attr);
            if (err) return err;
        }
    } while (idx++, pa += (next - va), va = next, va != end);

    return 0;
}



// ===================================================================
// 【主入口】 批量映射引擎 (自动混合 1G/2M/4K 页大小)
// ===================================================================
int32 memblock_vmmap_range(uint64 *pml4t, uint64 start_va,uint64 pa, uint64 size, uint64 attr){
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
        err = memblock_map_pdpte_range(&cur_pml4[idx],  va, pa,next, attr);
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
static inline int32 memblock_unmap_pte_range(uint64 *pde, uint64 addr, uint64 end) {
    uint64 *ptt = pa_to_va(*pde & PAGE_PA_MASK);
    uint32 idx = get_table_idx(addr, PTE_SHIFT);

    // 🚀 横向扫荡，断开物理映射并刷新 TLB
    do {
        if (ptt[idx] & PAGE_P) {
            ptt[idx] = 0;
            asm_invlpg(addr);
        }
    } while (idx++, addr += PAGE_4K_SIZE, addr != end);

    // 🧹 级联释放：极速扫描 512 项，如果全 0，说明整张表已空，触发自我销毁
    if (asm_forward_find_qword(ptt, 512, 0) == 0) {
        *pde = 0;                                   // 必须先切断父级指针
        memblock_free(va_to_pa(ptt), PAGE_4K_SIZE); // 交还给物理内存池
        return 1;                                   // 向上级汇报已销毁
    }
    return 0;
}

// ===================================================================
// 【Level 2: PD 层】 2MB 巨页侦测与任务发配
// ===================================================================
static inline int32 memblock_unmap_pde_range(uint64 *pdpte, uint64 addr, uint64 end) {
    uint64 *pdt = pa_to_va(*pdpte & PAGE_PA_MASK);
    uint32 idx = get_table_idx(addr, PDE_SHIFT);
    uint64 next;

    do {
        next = get_addr_end(addr, end, PDE_SHIFT);
        uint64 pde = pdt[idx];

        if (!(pde & PAGE_P)) continue; // 空洞，直接跃过这 2MB！

        if (pde & PAGE_PS) {
            // 🎯 遭遇 2MB 巨页，直接秒杀！
            pdt[idx] = 0;
            asm_invlpg(addr);
            continue;
        }

        // 往下发配切割好的区间 (返回值我们可以忽略，因为最后会统一扫表)
        memblock_unmap_pte_range(&pdt[idx], addr, next);

    } while (idx++, addr = next, addr != end);

    // 🧹 级联释放：当前 PD 表是否全空？
    if (asm_forward_find_qword(pdt, 512, 0) == 0) {
        *pdpte = 0;
        memblock_free(va_to_pa(pdt), PAGE_4K_SIZE);
        return 1;
    }
    return 0;
}

// ===================================================================
// 【Level 3: PDPT 层】 1GB 巨页侦测与任务发配
// ===================================================================
static inline int32 memblock_unmap_pdpte_range(uint64 *pml4e, uint64 addr, uint64 end) {
    uint64 *pdptt = pa_to_va(*pml4e & PAGE_PA_MASK);
    uint32 idx = get_table_idx(addr, PDPTE_SHIFT);
    uint64 next;

    do {
        next = get_addr_end(addr, end, PDPTE_SHIFT);
        uint64 pdpte = pdptt[idx];

        if (!(pdpte & PAGE_P)) continue; // 空洞，直接跃过这 1GB！

        if (pdpte & PAGE_PS) {
            // 🎯 遭遇 1GB 巨页，直接秒杀！
            pdptt[idx] = 0;
            asm_invlpg(addr);
            continue;
        }

        memblock_unmap_pde_range(&pdptt[idx], addr, next);

    } while (idx++, addr = next, addr != end);

    // 🧹 级联释放：当前 PDPT 表是否全空？
    if (asm_forward_find_qword(pdptt, 512, 0) == 0) {
        *pml4e = 0;
        memblock_free(va_to_pa(pdptt), PAGE_4K_SIZE);
        return 1;
    }
    return 0;
}

// ===================================================================
// 【主入口】 批量释放引擎 (无状态化解耦版)
// ===================================================================
INIT_TEXT int32 memblock_unvmmap_range(uint64 *pml4t, uint64 start_va, uint64 size) {
    if (size == 0) return 0;

    // 确保传入的 pml4t 是虚拟地址，以防传错物理地址引发缺页异常
    pml4t = (uint64 *)pa_to_va((uint64)pml4t);

    uint64 addr = start_va;
    uint64 end = addr + size;
    uint32 idx = get_table_idx(addr, PML4E_SHIFT);
    uint64 next;

    do {
        next = get_addr_end(addr, end, PML4E_SHIFT);
        uint64 pml4e = pml4t[idx];

        if (!(pml4e & PAGE_P)) continue;

        memblock_unmap_pdpte_range(&pml4t[idx], addr, next);

        // 注意：PML4 表是顶层根表。哪怕 512 项全部清空了，我们也不在此处 free(pml4t)。
        // 根表的生命周期由进程销毁逻辑单独控制。

    } while (idx++, addr = next, addr != end);

    return 0;
}

//=================================================================================================================
