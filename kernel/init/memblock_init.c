#include "memblock_init.h"
#include "../x64/cpu.h"
#include "../include/vmm.h"
#include "../include/printk.h"
#include "../include/errno.h"
#include "slub.h"

#define MAX(a, b) ((a) > (b) ? (a) : (b))
#define MIN(a, b) ((a) < (b) ? (a) : (b))


// =========================================================================
// 全局账本定义
// =========================================================================
memblock_t memblock = {0};

extern vm_space_t kernel_space;

extern efi_runtime_memmap_t efi_runtime_memmap;

// =========================================================================
// 内部算法：清理、合并与切割
// =========================================================================

/**
 * @brief 清理算法：合并物理地址接壤、Node ID 相同 且 Type 相同的连续区间
 */
static void memblock_merge_regions(memblock_type_t *mem_type) {
    if (mem_type->count < 2) return;

    for (uint32 i = 0; i < mem_type->count - 1; i++) {
        // 🌟 严格合并法则：三者必须同时满足 (接壤、同 Node、同类型)
        if (mem_type->region[i].base + mem_type->region[i].size == mem_type->region[i + 1].base &&
            mem_type->region[i].nid == mem_type->region[i + 1].nid &&
            mem_type->region[i].type == mem_type->region[i + 1].type) {

            // 吞并下一块
            mem_type->region[i].size += mem_type->region[i + 1].size;

            // 数组整体左移覆盖
            for (uint32 j = i + 1; j < mem_type->count - 1; j++) {
                mem_type->region[j] = mem_type->region[j + 1];
            }

            // 🌟 斩断尾巴：清零脏数据，防止游离野指针
            mem_type->region[mem_type->count - 1].base = 0;
            mem_type->region[mem_type->count - 1].size = 0;
            mem_type->region[mem_type->count - 1].nid = 0;
            mem_type->region[mem_type->count - 1].type = 0;

            mem_type->count--;
            i--; // 退一步重试：因为新吞并的块可能还能跟后面的接壤
        }
    }
}

/**
 * @brief 核心辅助：从指定的 memblock 账本中精确移除一段区域 (会引发区间分裂)
 */
static void memblock_remove_region(memblock_type_t *mem_type, uint64 base, uint64 size) {
    for (uint32 i = 0; i < mem_type->count; i++) {
        uint64 r_base = mem_type->region[i].base;
        uint64 r_size = mem_type->region[i].size;

        if (base >= r_base && base + size <= r_base + r_size) {
            if (base == r_base && size == r_size) {
                // 完全重合，整块抹除，左移覆盖
                for (uint32 j = i; j < mem_type->count - 1; j++) {
                    mem_type->region[j] = mem_type->region[j + 1];
                }
                mem_type->region[mem_type->count - 1].base = 0;
                mem_type->region[mem_type->count - 1].size = 0;
                mem_type->region[mem_type->count - 1].nid = 0;
                mem_type->region[mem_type->count - 1].type = 0;
                mem_type->count--;
            } else if (base == r_base) {
                // 切头
                mem_type->region[i].base += size;
                mem_type->region[i].size -= size;
            } else if (base + size == r_base + r_size) {
                // 切尾
                mem_type->region[i].size -= size;
            } else {
                // 掏空中间 (分裂为两块)
                if (mem_type->count >= MAX_MEMBLOCK_REGIONS) {
                    color_printk(RED, BLACK, "PANIC: memblock reserved array overflow!\n");
                    while (1) asm_hlt();
                }
                for (uint32 j = mem_type->count; j > i + 1; j--) {
                    mem_type->region[j] = mem_type->region[j - 1];
                }
                // 设置右半部 (必须继承左侧的 Node ID 和 Type)
                mem_type->region[i + 1].base = base + size;
                mem_type->region[i + 1].size = r_base + r_size - (base + size);
                mem_type->region[i + 1].nid = mem_type->region[i].nid;
                mem_type->region[i + 1].type = mem_type->region[i].type;

                // 修正左半部
                mem_type->region[i].size = base - r_base;
                mem_type->count++;
            }
            break;
        }
    }
    memblock_merge_regions(mem_type);
}

// =========================================================================
// 核心对外接口
// =========================================================================

/**
 * @brief 向 memblock 添加内存区域，保持自动合并
 */
void memblock_add(memblock_type_t *memblock_type, uint64 pa_start, uint64 size, uint32 type) {
    if (memblock_type->count == 0) {
        memblock_type->region[0].base = pa_start;
        memblock_type->region[0].size = size;
        memblock_type->region[0].nid = NUMA_NO_NODE;
        memblock_type->region[0].type = type;
        memblock_type->count++;
    } else {
        uint32 last_idx = memblock_type->count - 1;
        // 追加合并判定：除了接壤，还要求 nid 和 type 完全一致
        if (memblock_type->region[last_idx].base + memblock_type->region[last_idx].size == pa_start &&
            memblock_type->region[last_idx].nid == NUMA_NO_NODE &&
            memblock_type->region[last_idx].type == type) {
            memblock_type->region[last_idx].size += size;
        } else {
            memblock_type->region[memblock_type->count].base = pa_start;
            memblock_type->region[memblock_type->count].size = size;
            memblock_type->region[memblock_type->count].nid = NUMA_NO_NODE;
            memblock_type->region[memblock_type->count].type = type;
            memblock_type->count++;
        }
    }
}

/**
 * @brief 按 NUMA 节点要求分配内存 (从 memory 切割，并带血统记录到 reserved)
 */
uint64 memblock_alloc_node(uint64 size, uint64 align, uint8 target_nid) {
    if (!size) return EINVAL;
    uint64 align_base, align_size;
    uint32 index = 0;
    boolean found = FALSE;

    for (index = 0; index < memblock.memory.count; index++) {
        if (target_nid != NUMA_NO_NODE && memblock.memory.region[index].nid != target_nid) continue;
        uint64 base = memblock.memory.region[index].base;
        align_base = align_up(base, align);
        align_size = align_base - base + size;

        if (align_size <= memblock.memory.region[index].size) {
            found = TRUE;
            break;
        }
    }

    // 降级尝试
    if (!found && target_nid != NUMA_NO_NODE) {
        PR_WARN("NUMA Node %d memory exhausted! Falling back to any node.\n", target_nid);
        return memblock_alloc_node(size, align, NUMA_NO_NODE);
    }
    if (!found) return ENOMEM;

    // 🌟 锁定源血统：记录这块内存原本属于哪个节点、什么类型
    uint8 original_nid = memblock.memory.region[index].nid;
    uint32 original_type = memblock.memory.region[index].type;

    // ---- 记入债务簿 (reserved) ----
    uint32 insert_idx = 0;
    while (insert_idx < memblock.reserved.count && memblock.reserved.region[insert_idx].base < align_base) {
        insert_idx++;
    }

    if (memblock.reserved.count >= MAX_MEMBLOCK_REGIONS) {
        color_printk(RED, BLACK, "PANIC: memblock reserved array overflow!\n");
        while (1) asm_hlt();
    }

    for (uint32 j = memblock.reserved.count; j > insert_idx; j--) {
        memblock.reserved.region[j] = memblock.reserved.region[j - 1];
    }
    memblock.reserved.region[insert_idx].base = align_base;
    memblock.reserved.region[insert_idx].size = size;
    memblock.reserved.region[insert_idx].nid = original_nid;
    memblock.reserved.region[insert_idx].type = original_type; // 血统继承
    memblock.reserved.count++;
    memblock_merge_regions(&memblock.reserved);

    // ---- 从空闲池 (memory) 中剔除 ----
    if (align_base == memblock.memory.region[index].base && size == memblock.memory.region[index].size) {
        for (uint32 j = index; j < memblock.memory.count - 1; j++) {
            memblock.memory.region[j] = memblock.memory.region[j + 1];
        }
        memblock.memory.region[memblock.memory.count - 1].base = 0;
        memblock.memory.region[memblock.memory.count - 1].size = 0;
        memblock.memory.region[memblock.memory.count - 1].nid = 0;
        memblock.memory.region[memblock.memory.count - 1].type = 0;
        memblock.memory.count--;
    } else if (align_base == memblock.memory.region[index].base) {
        memblock.memory.region[index].base += size;
        memblock.memory.region[index].size -= size;
    } else if (align_base + size == memblock.memory.region[index].base + memblock.memory.region[index].size) {
        memblock.memory.region[index].size = align_base - memblock.memory.region[index].base;
    } else {
        if (memblock.memory.count >= MAX_MEMBLOCK_REGIONS) {
            color_printk(RED, BLACK, "PANIC: memblock free array overflow in alloc!\n");
            while (1) asm_hlt();
        }
        for (uint32 j = memblock.memory.count; j > index + 1; j--) {
            memblock.memory.region[j] = memblock.memory.region[j - 1];
        }
        memblock.memory.region[index + 1].base = align_base + size;
        memblock.memory.region[index + 1].size = memblock.memory.region[index].size - align_size;
        memblock.memory.region[index + 1].nid = original_nid;
        memblock.memory.region[index + 1].type = original_type; // 继承属性

        memblock.memory.region[index].size = align_base - memblock.memory.region[index].base;
        memblock.memory.count++;
    }

    asm_mem_set(pa_to_va(align_base), 0, size);
    return align_base;
}

/**
 * @brief 释放内存：归还至 memory 空闲池，并从 reserved 中精准切除
 */
int32 memblock_free(uint64 ptr, uint64 size) {
    if (!size) return EINVAL;

    // 🌟 认祖归宗：去 reserved 账本里查清楚，这块内存当初被分配时是啥类型的？
    uint8 free_nid = NUMA_NO_NODE;
    uint32 free_type = 0;
    for (uint32 i = 0; i < memblock.reserved.count; i++) {
        uint64 r_base = memblock.reserved.region[i].base;
        uint64 r_size = memblock.reserved.region[i].size;

        if (ptr >= r_base && ptr + size <= r_base + r_size) {
            free_nid = memblock.reserved.region[i].nid;
            free_type = memblock.reserved.region[i].type;
            break;
        }
    }

    uint32 insert_idx = 0;
    while (insert_idx < memblock.memory.count && memblock.memory.region[insert_idx].base < ptr) {
        insert_idx++;
    }

    boolean merged_left = FALSE;

    if (insert_idx > 0) {
        uint32 left = insert_idx - 1;
        // 🌟 只有物理接壤且 type、nid 全部吻合才能还进左边的池子
        if (memblock.memory.region[left].base + memblock.memory.region[left].size == ptr &&
            memblock.memory.region[left].nid == free_nid &&
            memblock.memory.region[left].type == free_type) {
            memblock.memory.region[left].size += size;
            merged_left = TRUE;
        }
    }

    if (insert_idx < memblock.memory.count) {
        if (ptr + size == memblock.memory.region[insert_idx].base &&
            memblock.memory.region[insert_idx].nid == free_nid &&
            memblock.memory.region[insert_idx].type == free_type) {

            if (merged_left) {
                // 左右大贯通合并
                uint32 left = insert_idx - 1;
                memblock.memory.region[left].size += memblock.memory.region[insert_idx].size;
                for (uint32 j = insert_idx; j < memblock.memory.count - 1; j++) {
                    memblock.memory.region[j] = memblock.memory.region[j + 1];
                }
                memblock.memory.region[memblock.memory.count - 1].base = 0;
                memblock.memory.region[memblock.memory.count - 1].size = 0;
                memblock.memory.region[memblock.memory.count - 1].nid = 0;
                memblock.memory.region[memblock.memory.count - 1].type = 0;
                memblock.memory.count--;
            } else {
                memblock.memory.region[insert_idx].base = ptr;
                memblock.memory.region[insert_idx].size += size;
            }
            goto cleanup_reserved;
        }
    }

    if (!merged_left) {
        if (memblock.memory.count >= MAX_MEMBLOCK_REGIONS) {
            color_printk(RED, BLACK, "PANIC: memblock free array overflow in free!\n");
            while (1) asm_hlt();
        }
        for (uint32 j = memblock.memory.count; j > insert_idx; j--) {
            memblock.memory.region[j] = memblock.memory.region[j - 1];
        }
        memblock.memory.region[insert_idx].base = ptr;
        memblock.memory.region[insert_idx].size = size;
        memblock.memory.region[insert_idx].nid = free_nid;
        memblock.memory.region[insert_idx].type = free_type;
        memblock.memory.count++;
    }

cleanup_reserved:
    memblock_remove_region(&memblock.reserved, ptr, size);
    return 0;
}

/**
 * @brief NUMA 节点盖章机制 (SRAT 切割)
 */
void memblock_set_node_by_intersection(memblock_type_t *mem_type, uint64 srat_base, uint64 srat_size, uint8 nid) {
    uint64 srat_end = srat_base + srat_size;

    for (uint32 i = 0; i < mem_type->count; i++) {
        uint64 reg_base = mem_type->region[i].base;
        uint64 reg_size = mem_type->region[i].size;
        uint64 reg_end = reg_base + reg_size;

        if (reg_base < srat_end && reg_end > srat_base) {
            uint64 inter_start = MAX(reg_base, srat_base);
            uint64 inter_end = MIN(reg_end, srat_end);

            // 左侧溢出切割
            if (inter_start > reg_base) {
                if (mem_type->count >= MAX_MEMBLOCK_REGIONS) {
                    color_printk(RED, BLACK, "PANIC: memblock overflow during NUMA left split!\n");
                    while (1) asm_hlt();
                }
                for (uint32 j = mem_type->count; j > i + 1; j--) {
                    mem_type->region[j] = mem_type->region[j - 1];
                }
                mem_type->region[i].size = inter_start - reg_base;
                mem_type->region[i + 1].base = inter_start;
                mem_type->region[i + 1].size = reg_end - inter_start;
                mem_type->region[i + 1].nid = mem_type->region[i].nid;
                mem_type->region[i + 1].type = mem_type->region[i].type; // 继承类型

                mem_type->count++;
                continue; // 切割后，当前块不再满足相交条件，进入下一次循环处理切割出的右块
            }

            // 右侧溢出切割
            if (inter_end < reg_end) {
                if (mem_type->count >= MAX_MEMBLOCK_REGIONS) {
                    color_printk(RED, BLACK, "PANIC: memblock overflow during NUMA right split!\n");
                    while (1) asm_hlt();
                }
                for (uint32 j = mem_type->count; j > i + 1; j--) {
                    mem_type->region[j] = mem_type->region[j - 1];
                }
                mem_type->region[i].size = inter_end - reg_base;
                mem_type->region[i + 1].base = inter_end;
                mem_type->region[i + 1].size = reg_end - inter_end;
                mem_type->region[i + 1].nid = mem_type->region[i].nid;
                mem_type->region[i + 1].type = mem_type->region[i].type; // 继承类型

                mem_type->count++;
            }

            // 完美包围，精准盖章
            mem_type->region[i].nid = nid;
        }
    }
}

// =========================================================================
// Memblock 高阶封装，对接系统层
// =========================================================================

static page_t *memblock_alloc_pages(uint32 order) {
    uint64 size = PAGE_4K_SIZE << order;
    uint64 paddr = memblock_alloc_node(size, size, NUMA_NO_NODE); // 默认全局分配
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

// =========================================================================
// 全局入口：极其清爽的启动扫描
// =========================================================================
/**
 * @brief 物理内存的“造物主”阶段初始化
 * 扫描 UEFI 传给内核的物理内存描述符，并将其清洗归类到全局 Memblock 账本。
 */
void memblock_init(void) {
    uint64 phy_mem_size = 0;

    uint32 count = tmp_boot_info->mem_map_size / tmp_boot_info->mem_descriptor_size;
    uint8 *src_ptr = (uint8 *) tmp_boot_info->mem_map;

    for (uint32 i = 0; i < count; i++) {
        EFI_MEMORY_DESCRIPTOR *mem_des = (EFI_MEMORY_DESCRIPTOR *) src_ptr;

        if (mem_des->NumberOfPages == 0) {
            src_ptr += tmp_boot_info->mem_descriptor_size;
            continue;
        }

        uint64 pa_start = mem_des->PhysicalStart;
        uint64 size = mem_des->NumberOfPages << 12; // 算出字节大小
        EFI_MEMORY_TYPE type = mem_des->Type;
        uint64 attr = mem_des->Attribute;

        // 🌟 只要不是真正的硬件坏块和 IO 保留，全部纳入系统的全局“物理内存”统计容量
        if (type != EFI_RESERVED_MEMORY_TYPE && type != EFI_MEMORY_MAPPED_IO && type != EFI_UNUSABLE_MEMORY) {
            phy_mem_size += size;
        }

        // ---------------------------------------------------------------------
        // 分支 1：UEFI 运行时固件 (将来需映射 RX/RW 权限供系统调用)
        // ---------------------------------------------------------------------
        if (attr & EFI_MEMORY_RUNTIME) {
            // 直接放入债务簿。它不会被映射进普通直接映射区，也不会拥有 struct page。
            memblock_add(&memblock.reserved, pa_start, size, type);

            // 备份原数据给后期的 SetVirtualAddressMap 使用
            uint8 *dst_ptr = (uint8 *) efi_runtime_memmap.mem_map +
                             (efi_runtime_memmap.count * tmp_boot_info->mem_descriptor_size);
            asm_mem_cpy(dst_ptr, src_ptr, tmp_boot_info->mem_descriptor_size);
            efi_runtime_memmap.count++;
        }

        // ---------------------------------------------------------------------
        // 分支 2：内核与引导期不可动摇的核心资产
        // ---------------------------------------------------------------------
        else if (type == EFI_LOADER_DATA ||
                 type == EFI_LOADER_CODE ||
                 type == EFI_ACPI_RECLAIM_MEMORY ||
                 type == EFI_ACPI_MEMORY_NVS) {
            // 统统打上它们原来的 type 印章，关进 reserved 保护起来
            memblock_add(&memblock.reserved, pa_start, size, type);
        }

        // ---------------------------------------------------------------------
        // 分支 3：毫无挂碍的纯净可用内存
        // ---------------------------------------------------------------------
        else if (type == EFI_BOOT_SERVICES_CODE ||
                 type == EFI_BOOT_SERVICES_DATA ||
                 type == EFI_CONVENTIONAL_MEMORY) {
            // 统一“洗白”成常规可用 RAM
            memblock_add(&memblock.memory, pa_start, size, EFI_CONVENTIONAL_MEMORY);
        }

        src_ptr += tmp_boot_info->mem_descriptor_size;
    }

    // 绑定内核全局 ops
    kernel_space.ops.alloc_pages = memblock_alloc_pages;
    kernel_space.ops.free_pages = memblock_free_pages;
    kernel_space.ops.page_to_phys = memblock_page_to_pa;
    kernel_space.ops.phys_to_page = memblock_pa_to_page;
    kernel_space.ops.phys_to_virt = pa_to_va; // 假定你已实现虚拟物理地址转换
    kernel_space.ops.virt_to_phys = va_to_pa;

    PR_INFO("Total Physics Memory: %d MB\n", phy_mem_size / 1024 / 1024);
}