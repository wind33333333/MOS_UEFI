#include "memblock_init.h"
#include "../x64/cpu.h"
#include "../include/vmm.h"
#include "../include/printk.h"
#include "../include/errno.h"
#include "slub.h"

#define MAX(a, b) ((a) > (b) ? (a) : (b))
#define MIN(a, b) ((a) < (b) ? (a) : (b))
#define MEM_1MB (0x100000ULL)

memblock_t memblock = {0}; // 临时内存器内存地图，后面buddy system需要用到空闲地图。
memblock_type_t page_mem_map = {0}; // page页映射区内存地图
efi_runtime_memmap_t efi_runtime_memmap = {0}; // uefi运行时的数据和代码地图
memblock_type_t direct_mem_map = {0}; // 直接映射区内存地图

extern vm_space_t kernel_space;

// =========================================================================
// 提前声明合并函数，以供切割和分配逻辑调用，随时清理碎片
// =========================================================================
/**
 * @brief 清理算法：扫描指定的 Memblock 账本，合并物理地址接壤且 Node ID 相同的连续区间
 */
static void memblock_merge_regions(memblock_type_t *type) {
    if (type->count < 2) return;

    for (uint32 i = 0; i < type->count - 1; i++) {
        // 🌟 严格检查：物理地址接壤，且 逻辑 Node ID 完全相同
        if (type->region[i].base + type->region[i].size == type->region[i + 1].base &&
            type->region[i].nid == type->region[i + 1].nid) {

            // 将下一块吞并
            type->region[i].size += type->region[i + 1].size;

            // 把数组整体向左平移，覆盖掉被吞并的下一块
            for (uint32 j = i + 1; j < type->count - 1; j++) {
                type->region[j] = type->region[j + 1];
            }

            // 🌟 终极修复：彻底清零数组尾部残留的脏数据（Dirty Tail）
            type->region[type->count - 1].base = 0;
            type->region[type->count - 1].size = 0;
            type->region[type->count - 1].nid  = 0;

            type->count--;

            // 退一步重试：因为吞并后的新区块可能还能跟后面的继续接壤！
            i--;
        }
    }
}

// =========================================================================
// 核心辅助函数：从指定的 memblock 账本中精确移除一段区域 (用于剔除 reserved)
// =========================================================================
static void memblock_remove_region(memblock_type_t *type, uint64 base, uint64 size) {
    for (uint32 i = 0; i < type->count; i++) {
        uint64 r_base = type->region[i].base;
        uint64 r_size = type->region[i].size;

        // 查找包含要删除部分的区域 (假设释放的内存一定完全落在某个 reserved 区间内)
        if (base >= r_base && base + size <= r_base + r_size) {
            if (base == r_base && size == r_size) {
                // 1. 完全重合，整块抹除。后面的元素向前平移
                for (uint32 j = i; j < type->count - 1; j++) {
                    type->region[j] = type->region[j + 1];
                }

                // 🌟 清零尾部脏数据
                type->region[type->count - 1].base = 0;
                type->region[type->count - 1].size = 0;
                type->region[type->count - 1].nid  = 0;

                type->count--;
            } else if (base == r_base) {
                // 2. 切头：移除头部，剩下的往后缩
                type->region[i].base += size;
                type->region[i].size -= size;
            } else if (base + size == r_base + r_size) {
                // 3. 切尾：只需缩小体积
                type->region[i].size -= size;
            } else {
                // 4. 中间掏空 (一分为二)
                if (type->count >= MAX_MEMBLOCK_REGIONS) {
                    color_printk(RED, BLACK, "PANIC: memblock reserved array overflow!\n");
                    while(1) asm_hlt();
                }
                // 后面的元素整体向右挪一格，腾出 i+1 的位置
                for (uint32 j = type->count; j > i + 1; j--) {
                    type->region[j] = type->region[j - 1];
                }
                // 设置右半部分 (新分裂出的块)
                type->region[i + 1].base = base + size;
                type->region[i + 1].size = r_base + r_size - (base + size);
                type->region[i + 1].nid = type->region[i].nid; // 继承 Node ID

                // 修正左半部分 (原来的块)
                type->region[i].size = base - r_base;
                type->count++;
            }
            break;
        }
    }
    // 🌟 斩杀碎片：切除操作可能引发新的断层，立即执行合并以维持数组极简
    memblock_merge_regions(type);
}

// =========================================================================
// 物理内存区域添加到 memblock 的列表中 (自动合并接壤区域)
// =========================================================================
void memblock_add(memblock_type_t *memblock_type, uint64 pa_start, uint64 size) {
    if (memblock_type->count == 0) {
        memblock_type->region[0].base = pa_start;
        memblock_type->region[0].size = size;
        memblock_type->region[0].nid = NUMA_NO_NODE; // 默认未知标签
        memblock_type->count++;
    } else {
        uint32 last_idx = memblock_type->count - 1;
        // 只有当物理地址接壤，且属于同一个 NUMA 节点时，才允许合并！
        if (memblock_type->region[last_idx].base + memblock_type->region[last_idx].size == pa_start &&
            memblock_type->region[last_idx].nid == NUMA_NO_NODE) {
            memblock_type->region[last_idx].size += size;
        } else {
            // 不接壤或跨 Node，作为独立区间追加
            memblock_type->region[memblock_type->count].base = pa_start;
            memblock_type->region[memblock_type->count].size = size;
            memblock_type->region[memblock_type->count].nid = NUMA_NO_NODE; // 默认未知标签
            memblock_type->count++;
        }
    }
}

// =========================================================================
// 底层核心接口：按 NUMA 节点要求分配内存
// =========================================================================
uint64 memblock_alloc_node(uint64 size, uint64 align, uint8 target_nid) {
    if (!size) return EINVAL;
    uint64 align_base, align_size;
    uint32 index = 0;
    boolean found = FALSE;

    // 1. 第一次尝试：严格在目标 NUMA 节点中寻找空闲块
    for (index = 0; index < memblock.memory.count; index++) {
        if (target_nid != NUMA_NO_NODE && memblock.memory.region[index].nid != target_nid) {
            continue;
        }

        uint64 base = memblock.memory.region[index].base;
        align_base = align_up(base, align);
        align_size = align_base - base + size;

        if (align_size <= memblock.memory.region[index].size) {
            found = TRUE;
            break;
        }
    }

    // 2. 第二次尝试 (Fallback机制)：降级为从任意节点分配
    if (!found && target_nid != NUMA_NO_NODE) {
        PR_WARN("NUMA Node %d memory exhausted! Falling back to any node.\n", target_nid);
        return memblock_alloc_node(size, align, NUMA_NO_NODE);
    }

    if (!found) return ENOMEM; // 彻底 OOM

    // 🌟 保存被切割区域原本的真实 Node ID，极其重要！
    uint8 original_nid = memblock.memory.region[index].nid;

    // 3. 🛡️ 架构级补全：将分配出去的内存记录到 reserved 池中，保持物理地址递增以便合并
    uint32 insert_idx = 0;
    while (insert_idx < memblock.reserved.count && memblock.reserved.region[insert_idx].base < align_base) {
        insert_idx++;
    }

    if (memblock.reserved.count >= MAX_MEMBLOCK_REGIONS) {
        color_printk(RED, BLACK, "PANIC: memblock reserved array overflow!\n");
        while (1) asm_hlt();
    }

    // 向右平移腾出坑位，保证 reserved 数组地址严格递增
    for (uint32 j = memblock.reserved.count; j > insert_idx; j--) {
        memblock.reserved.region[j] = memblock.reserved.region[j - 1];
    }
    memblock.reserved.region[insert_idx].base = align_base;
    memblock.reserved.region[insert_idx].size = size;
    memblock.reserved.region[insert_idx].nid  = original_nid; // 带着纯正的血统进入债务簿
    memblock.reserved.count++;

    // 🌟 斩杀碎片：插入 reserved 后，立刻扫描合并！消除细碎的 4KB 分配隐患
    memblock_merge_regions(&memblock.reserved);

    // 4. 开始执行切割逻辑 (从 memory 空闲池中剔除该区域)
    if (align_base == memblock.memory.region[index].base && size == memblock.memory.region[index].size) {
        // 全等：完美切除，整体挪动覆盖
        for (uint32 j = index; j < memblock.memory.count - 1; j++) {
            memblock.memory.region[j] = memblock.memory.region[j + 1];
        }

        // 🌟 清零尾部脏数据
        memblock.memory.region[memblock.memory.count - 1].base = 0;
        memblock.memory.region[memblock.memory.count - 1].size = 0;
        memblock.memory.region[memblock.memory.count - 1].nid  = 0;

        memblock.memory.count--;
    } else if (align_base == memblock.memory.region[index].base) {
        // 切头：剩下的区间往后缩
        memblock.memory.region[index].base += size;
        memblock.memory.region[index].size -= size;
    } else if (align_base + size == memblock.memory.region[index].base + memblock.memory.region[index].size) {
        // 切尾逻辑修正。剩下的是前面的部分，长度为对齐点与原基址的差值。
        memblock.memory.region[index].size = align_base - memblock.memory.region[index].base;
    } else {
        // 中间切 (一分为二，挖掉中间)
        if (memblock.memory.count >= MAX_MEMBLOCK_REGIONS) {
            color_printk(RED, BLACK, "PANIC: memblock free array overflow in alloc!\n");
            while (1) asm_hlt();
        }

        // 把后面的数据全部向右挪一位，腾出空间
        for (uint32 j = memblock.memory.count; j > index + 1; j--) {
            memblock.memory.region[j] = memblock.memory.region[j - 1];
        }

        // 设置右侧的新分裂碎块
        memblock.memory.region[index + 1].base = align_base + size;
        memblock.memory.region[index + 1].size = memblock.memory.region[index].size - align_size;
        memblock.memory.region[index + 1].nid = original_nid; // 坚决继承原有的 Node ID

        // 修正左侧的旧碎块
        memblock.memory.region[index].size = align_base - memblock.memory.region[index].base;

        memblock.memory.count++;
    }

    asm_mem_set(pa_to_va(align_base), 0, size); // 必须清零分配出的内存！
    return align_base;
}


// =========================================================================
// 释放内存：归还至 memory 空闲池，并从 reserved 中精准切除销账
// =========================================================================
int32 memblock_free(uint64 ptr, uint64 size) {
    if (!size) return EINVAL;

    // 🌟 自动寻祖：从 reserved 债务簿中反查这块内存的原本 NUMA 归属！
    uint8 free_nid = NUMA_NO_NODE;
    for (uint32 i = 0; i < memblock.reserved.count; i++) {
        uint64 r_base = memblock.reserved.region[i].base;
        uint64 r_size = memblock.reserved.region[i].size;

        // 必须完全包含在某一条 reserved 记录内才能安全继承
        if (ptr >= r_base && ptr + size <= r_base + r_size) {
            free_nid = memblock.reserved.region[i].nid; // 找到了原生标签！
            break;
        }
    }

    uint32 insert_idx = 0;
    // 1. 寻找插入点：保证整个数组物理地址严格递增
    while (insert_idx < memblock.memory.count && memblock.memory.region[insert_idx].base < ptr) {
        insert_idx++;
    }

    boolean merged_left = FALSE;

    // 2. 尝试向左合并
    if (insert_idx > 0) {
        uint32 left = insert_idx - 1;
        // 不仅要物理地址接壤，Node ID 也必须完全一致才能合并！
        if (memblock.memory.region[left].base + memblock.memory.region[left].size == ptr &&
            memblock.memory.region[left].nid == free_nid) {
            memblock.memory.region[left].size += size;
            merged_left = TRUE;
            // 此时，ptr 实际上已经成了 left 区间的延伸尾巴
        }
    }

    // 3. 尝试向右合并
    if (insert_idx < memblock.memory.count) {
        // 同理，不仅接壤，Node ID 也必须一致！
        if (ptr + size == memblock.memory.region[insert_idx].base &&
            memblock.memory.region[insert_idx].nid == free_nid) {

            if (merged_left) {
                // 🌟 双向合并：左侧不仅吃掉了释放的块，还顺带碰到了右侧的头，把右侧也吃了
                uint32 left = insert_idx - 1;
                memblock.memory.region[left].size += memblock.memory.region[insert_idx].size;

                // 因为右侧被吃了，把后面的数据整体向左平移，覆盖掉原来的右侧
                for (uint32 j = insert_idx; j < memblock.memory.count - 1; j++) {
                    memblock.memory.region[j] = memblock.memory.region[j + 1];
                }

                // 🌟 清零尾部脏数据
                memblock.memory.region[memblock.memory.count - 1].base = 0;
                memblock.memory.region[memblock.memory.count - 1].size = 0;
                memblock.memory.region[memblock.memory.count - 1].nid  = 0;

                memblock.memory.count--;
            } else {
                // 仅向右合并：把右侧的基址往前拉
                memblock.memory.region[insert_idx].base = ptr;
                memblock.memory.region[insert_idx].size += size;
            }
            goto cleanup_reserved; // 合并成功，直接去销账
        }
    }

    // 4. 如果两边都不接壤或跨了 NUMA 节点（孤岛），老老实实插入新块
    if (!merged_left) {
        if (memblock.memory.count >= MAX_MEMBLOCK_REGIONS) {
            color_printk(RED, BLACK, "PANIC: memblock free array overflow in free!\n");
            while (1) asm_hlt();
        }
        // 数组向右平移，腾出坑位
        for (uint32 j = memblock.memory.count; j > insert_idx; j--) {
            memblock.memory.region[j] = memblock.memory.region[j - 1];
        }
        memblock.memory.region[insert_idx].base = ptr;
        memblock.memory.region[insert_idx].size = size;
        memblock.memory.region[insert_idx].nid = free_nid; // 打上纯正标签
        memblock.memory.count++;
    }

cleanup_reserved:
    // 5. 🛡️ 架构级补全：内存既然回到了空闲池，就必须从 reserved 债务簿中切除销账！
    memblock_remove_region(&memblock.reserved, ptr, size);

    return 0;
}

/**
 * @brief 核心算法：用 SRAT 的物理边界去切割并标记指定的 Memblock 账本
 */
void memblock_set_node_by_intersection(memblock_type_t *type, uint64 srat_base, uint64 srat_size, uint8 nid) {
    uint64 srat_end = srat_base + srat_size;

    // 遍历指定的资产簿或债务簿
    for (uint32 i = 0; i < type->count; i++) {
        uint64 reg_base = type->region[i].base;
        uint64 reg_size = type->region[i].size;
        uint64 reg_end = reg_base + reg_size;

        if (reg_base < srat_end && reg_end > srat_base) {
            uint64 inter_start = MAX(reg_base, srat_base);
            uint64 inter_end = MIN(reg_end, srat_end);

            // 左侧突出处理 (Left Overhang Split)
            if (inter_start > reg_base) {
                if (type->count >= MAX_MEMBLOCK_REGIONS) {
                    color_printk(RED, BLACK, "PANIC: memblock array overflow during NUMA left split!\n");
                    while (1) asm_hlt();
                }

                for (uint32 j = type->count; j > i + 1; j--) {
                    type->region[j] = type->region[j - 1];
                }

                type->region[i].size = inter_start - reg_base;
                type->region[i + 1].base = inter_start;
                type->region[i + 1].size = reg_end - inter_start;
                type->region[i + 1].nid = type->region[i].nid;

                type->count++;
                continue;
            }

            // 右侧突出处理 (Right Overhang Split)
            if (inter_end < reg_end) {
                if (type->count >= MAX_MEMBLOCK_REGIONS) {
                    color_printk(RED, BLACK, "PANIC: memblock array overflow during NUMA right split!\n");
                    while (1) asm_hlt();
                }

                for (uint32 j = type->count; j > i + 1; j--) {
                    type->region[j] = type->region[j - 1];
                }

                type->region[i].size = inter_end - reg_base;
                type->region[i + 1].base = inter_end;
                type->region[i + 1].size = reg_end - inter_end;
                type->region[i + 1].nid = type->region[i].nid;

                type->count++;
            }

            // 精准盖章 (Stamping)
            type->region[i].nid = nid;
        }
    }
}

/* ========================================================================== */
/*                      Memblock 适配器核心实现                               */
/* ========================================================================== */

static page_t *memblock_alloc_pages(uint32 order) {
    uint64 size = PAGE_4K_SIZE << order;
    uint64 paddr = memblock_alloc(size, size);
    if (!paddr) return NULL;
    return (page_t *) paddr;
}

static void memblock_free_pages(page_t *page) {
    if (!page) return;
    uint64 paddr = (uint64) page;
    memblock_free(paddr, PAGE_4K_SIZE);
}

static uint64 memblock_page_to_pa(page_t *page) {
    return (uint64) page;
}

static page_t *memblock_pa_to_page(uint64 paddr) {
    return (page_t *) paddr;
}

void memory_init(void) {
    uint64 phy_mem_size = 0;
    uint64 kernel_pa_start = (uint64) _start - vm_layout.kernel_start;
    uint64 kernel_pa_end = (uint64) _end - vm_layout.kernel_start;

    uint32 count = tmp_boot_info->mem_map_size / tmp_boot_info->mem_descriptor_size;
    uint8 *src_ptr = (uint8 *) tmp_boot_info->mem_map;

    for (uint32 i = 0; i < count; i++) {
        EFI_MEMORY_DESCRIPTOR *mem_des = (EFI_MEMORY_DESCRIPTOR *) src_ptr;

        if (mem_des->NumberOfPages == 0) {
            src_ptr += tmp_boot_info->mem_descriptor_size;
            continue;
        }

        uint64 pa_start = mem_des->PhysicalStart;
        uint64 size = mem_des->NumberOfPages << 12;
        uint64 pa_end = pa_start + size;
        EFI_MEMORY_TYPE type = mem_des->Type;
        uint64 attr = mem_des->Attribute;

        // 1. 独立提取 UEFI Runtime 内存
        if (attr & EFI_MEMORY_RUNTIME) {
            uint8 *dst_ptr = (uint8 *) efi_runtime_memmap.mem_map +
                             (efi_runtime_memmap.count * tmp_boot_info->mem_descriptor_size);
            asm_mem_cpy(dst_ptr, src_ptr, tmp_boot_info->mem_descriptor_size);
            efi_runtime_memmap.count++;
        }

        // 2. 处理普通可用内存
        if (type == EFI_LOADER_DATA ||
            type == EFI_LOADER_CODE ||
            type == EFI_BOOT_SERVICES_CODE ||
            type == EFI_BOOT_SERVICES_DATA ||
            type == EFI_CONVENTIONAL_MEMORY) {
            phy_mem_size += size;

            memblock_add(&direct_mem_map, pa_start, size);
            memblock_add(&page_mem_map, pa_start, size);

            // 1MB 截断处理逻辑
            if (pa_start < MEM_1MB) {
                if (pa_end > MEM_1MB) {
                    size -= MEM_1MB - pa_start;
                    pa_start = MEM_1MB;
                } else {
                    src_ptr += tmp_boot_info->mem_descriptor_size;
                    continue;
                }
            }

            // 把内核切出来并丢入 reserved，剩下可用区域交入 memory 资产池
            if (pa_start < kernel_pa_end && pa_end > kernel_pa_start) {
                if (pa_start < kernel_pa_start) {
                    memblock_add(&memblock.memory, pa_start, kernel_pa_start - pa_start);
                }
                if (pa_end > kernel_pa_end) {
                    memblock_add(&memblock.memory, kernel_pa_end, pa_end - kernel_pa_end);
                }
                // 内核所在区域本身放入 reserved
                memblock_add(&memblock.reserved, pa_start > kernel_pa_start ? pa_start : kernel_pa_start,
                             pa_end < kernel_pa_end ? pa_end - pa_start : kernel_pa_end - pa_start);
            } else {
                memblock_add(&memblock.memory, pa_start, size);
            }

        // 3. 处理 ACPI 内存 (仅映射，不作为可用空闲页)
        } else if (type == EFI_ACPI_RECLAIM_MEMORY || type == EFI_ACPI_MEMORY_NVS) {
            phy_mem_size += size;
            memblock_add(&direct_mem_map, pa_start, size);
            // 同样放入 reserved，防止被误分
            memblock_add(&memblock.reserved, pa_start, size);
        }

        src_ptr += tmp_boot_info->mem_descriptor_size;
    }

    // 绑定 VMM 回调接口
    kernel_space.ops.alloc_pages = memblock_alloc_pages;
    kernel_space.ops.free_pages = memblock_free_pages;
    kernel_space.ops.page_to_phys = memblock_page_to_pa;
    kernel_space.ops.phys_to_page = memblock_pa_to_page;
    kernel_space.ops.phys_to_virt = pa_to_va;
    kernel_space.ops.virt_to_phys = va_to_pa;

    PR_INFO("Total Physics Memory:%dMB\n", phy_mem_size / 1024 / 1024);
}