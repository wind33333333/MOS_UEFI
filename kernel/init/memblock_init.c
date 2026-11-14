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
 * @brief 合并物理地址接壤、Node ID 相同 且 Type 相同的连续区间
 */
static void memblock_merge_regions(memblock_type_t *mem_type) {
    if (mem_type->count < 2) return;

    for (uint32 i = 0; i < mem_type->count - 1; i++) {
        // 三者必须同时满足：接壤、同 Node、同类型
        if (mem_type->region[i].base + mem_type->region[i].size == mem_type->region[i + 1].base &&
            mem_type->region[i].nid == mem_type->region[i + 1].nid &&
            mem_type->region[i].type == mem_type->region[i + 1].type) {

            mem_type->region[i].size += mem_type->region[i + 1].size;

            for (uint32 j = i + 1; j < mem_type->count - 1; j++) {
                mem_type->region[j] = mem_type->region[j + 1];
            }

            mem_type->region[mem_type->count - 1].base = 0;
            mem_type->region[mem_type->count - 1].size = 0;
            mem_type->region[mem_type->count - 1].nid = 0;
            mem_type->region[mem_type->count - 1].type = 0;
            mem_type->count--;
            i--;
        }
    }
}

/**
 * @brief 从指定的账本中精确移除区域 (用于后期释放内存时从 reserved 销账)
 */
static void memblock_remove_region(memblock_type_t *mem_type, uint64 base, uint64 size) {
    for (uint32 i = 0; i < mem_type->count; i++) {
        uint64 r_base = mem_type->region[i].base;
        uint64 r_size = mem_type->region[i].size;

        if (base >= r_base && base + size <= r_base + r_size) {
            if (base == r_base && size == r_size) { // 全等
                for (uint32 j = i; j < mem_type->count - 1; j++) {
                    mem_type->region[j] = mem_type->region[j + 1];
                }
                mem_type->region[mem_type->count - 1].base = 0;
                mem_type->region[mem_type->count - 1].size = 0;
                mem_type->region[mem_type->count - 1].nid = 0;
                mem_type->region[mem_type->count - 1].type = 0;
                mem_type->count--;
            } else if (base == r_base) { // 切头
                mem_type->region[i].base += size;
                mem_type->region[i].size -= size;
            } else if (base + size == r_base + r_size) { // 切尾
                mem_type->region[i].size -= size;
            } else { // 掏空中部
                if (mem_type->count >= MAX_MEMBLOCK_REGIONS) {
                    PR_ERROR("PANIC: memblock reserved array overflow!\n");
                    while (1) asm_hlt();
                }
                for (uint32 j = mem_type->count; j > i + 1; j--) {
                    mem_type->region[j] = mem_type->region[j - 1];
                }
                mem_type->region[i + 1].base = base + size;
                mem_type->region[i + 1].size = r_base + r_size - (base + size);
                mem_type->region[i + 1].nid = mem_type->region[i].nid;
                mem_type->region[i + 1].type = mem_type->region[i].type;

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
void memblock_add(memblock_type_t *mem_type, uint64 pa_start, uint64 size, uint32 type) {
    if (mem_type->count == 0) {
        mem_type->region[0].base = pa_start;
        mem_type->region[0].size = size;
        mem_type->region[0].nid = NUMA_NO_NODE;
        mem_type->region[0].type = type;
        mem_type->count++;
    } else {
        uint32 last_idx = mem_type->count - 1;
        // 追加合并判定
        if (mem_type->region[last_idx].base + mem_type->region[last_idx].size == pa_start &&
            mem_type->region[last_idx].nid == NUMA_NO_NODE &&
            mem_type->region[last_idx].type == type) {
            mem_type->region[last_idx].size += size;
        } else {
            mem_type->region[mem_type->count].base = pa_start;
            mem_type->region[mem_type->count].size = size;
            mem_type->region[mem_type->count].nid = NUMA_NO_NODE;
            mem_type->region[mem_type->count].type = type;
            mem_type->count++;
        }
    }
}

/**
 * @brief 按 NUMA 节点要求分配内存 (从 memory 切割，带血统记录到 reserved)
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

    if (!found && target_nid != NUMA_NO_NODE) {
        PR_WARN("NUMA Node %d exhausted! Fallback to any node.\n", target_nid);
        return memblock_alloc_node(size, align, NUMA_NO_NODE);
    }
    if (!found) return ENOMEM;

    // 锁定源血统
    uint8 original_nid = memblock.memory.region[index].nid;
    uint32 original_type = memblock.memory.region[index].type;

    // ---- 记入债务簿 (reserved) ----
    uint32 insert_idx = 0;
    while (insert_idx < memblock.reserved.count && memblock.reserved.region[insert_idx].base < align_base) {
        insert_idx++;
    }

    if (memblock.reserved.count >= MAX_MEMBLOCK_REGIONS) {
        PR_ERROR("PANIC: memblock reserved array overflow!\n");
        while (1) asm_hlt();
    }

    for (uint32 j = memblock.reserved.count; j > insert_idx; j--) {
        memblock.reserved.region[j] = memblock.reserved.region[j - 1];
    }
    memblock.reserved.region[insert_idx].base = align_base;
    memblock.reserved.region[insert_idx].size = size;
    memblock.reserved.region[insert_idx].nid = original_nid;
    memblock.reserved.region[insert_idx].type = original_type;
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
            PR_ERROR("PANIC: memblock free array overflow!\n");
            while (1) asm_hlt();
        }
        for (uint32 j = memblock.memory.count; j > index + 1; j--) {
            memblock.memory.region[j] = memblock.memory.region[j - 1];
        }
        memblock.memory.region[index + 1].base = align_base + size;
        memblock.memory.region[index + 1].size = memblock.memory.region[index].size - align_size;
        memblock.memory.region[index + 1].nid = original_nid;
        memblock.memory.region[index + 1].type = original_type;

        memblock.memory.region[index].size = align_base - memblock.memory.region[index].base;
        memblock.memory.count++;
    }

    asm_mem_set(pa_to_va(align_base), 0, size);
    return align_base;
}

// 通用兼容接口：谁不关心位置，就传 NUMA_NO_NODE
uint64 memblock_alloc(uint64 size, uint64 align) {
    return memblock_alloc_node(size, align, NUMA_NO_NODE);
}

/**
 * @brief 后期释放函数，从 reserved 销账并回填 memory
 */
int32 memblock_free(uint64 ptr, uint64 size) {
    if (!size) return EINVAL;

    // 认祖归宗
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

            if (merged_left) { // 大贯通
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
            PR_ERROR("PANIC: memblock free array overflow!\n");
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
 * @brief NUMA SRAT 节点盖章 (切割时自然继承 type)
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

            if (inter_start > reg_base) { // 左侧溢出切割
                if (mem_type->count >= MAX_MEMBLOCK_REGIONS) {
                    PR_ERROR("PANIC: memblock overflow during NUMA left split!\n");
                    while (1) asm_hlt();
                }
                for (uint32 j = mem_type->count; j > i + 1; j--) {
                    mem_type->region[j] = mem_type->region[j - 1];
                }
                mem_type->region[i].size = inter_start - reg_base;
                mem_type->region[i + 1].base = inter_start;
                mem_type->region[i + 1].size = reg_end - inter_start;
                mem_type->region[i + 1].nid = mem_type->region[i].nid;
                mem_type->region[i + 1].type = mem_type->region[i].type;
                mem_type->count++;
                continue;
            }

            if (inter_end < reg_end) { // 右侧溢出切割
                if (mem_type->count >= MAX_MEMBLOCK_REGIONS) {
                    PR_ERROR("PANIC: memblock overflow during NUMA right split!\n");
                    while (1) asm_hlt();
                }
                for (uint32 j = mem_type->count; j > i + 1; j--) {
                    mem_type->region[j] = mem_type->region[j - 1];
                }
                mem_type->region[i].size = inter_end - reg_base;
                mem_type->region[i + 1].base = inter_end;
                mem_type->region[i + 1].size = reg_end - inter_end;
                mem_type->region[i + 1].nid = mem_type->region[i].nid;
                mem_type->region[i + 1].type = mem_type->region[i].type;
                mem_type->count++;
            }

            mem_type->region[i].nid = nid; // 精准盖章
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
        uint64 size = mem_des->NumberOfPages << 12;
        EFI_MEMORY_TYPE type = mem_des->Type;
        uint64 attr = mem_des->Attribute;

        // 🌟 统计全系统物理 RAM
        if (type != EFI_RESERVED_MEMORY_TYPE && type != EFI_MEMORY_MAPPED_IO && type != EFI_UNUSABLE_MEMORY) {
            phy_mem_size += size;
        }

        // ====================================================================
        // 第一层：全景物理视图 (phys_map) - 专供页表初始化使用
        // ====================================================================
        // 无视原本 type（填 0），纯粹靠 base 缝合因为 Loader 而碎裂的物理内存！
        // 但严格排除 NVS（防踩）和 Runtime（权限特殊），它们对全局直接映射隐身。
        if (type == EFI_LOADER_DATA || type == EFI_LOADER_CODE ||
            type == EFI_BOOT_SERVICES_CODE || type == EFI_BOOT_SERVICES_DATA ||
            type == EFI_CONVENTIONAL_MEMORY || type == EFI_ACPI_RECLAIM_MEMORY) {

            memblock_add(&memblock.phys_map, pa_start, size, 0);
        }

        // ====================================================================
        // 第二层：特权隔离与运行时备份
        // ====================================================================
        if (attr & EFI_MEMORY_RUNTIME) {
            memblock_add(&memblock.reserved, pa_start, size, type);
            uint8 *dst_ptr = (uint8 *) efi_runtime_memmap.mem_map +
                             (efi_runtime_memmap.count * tmp_boot_info->mem_descriptor_size);
            asm_mem_cpy(dst_ptr, src_ptr, tmp_boot_info->mem_descriptor_size);
            efi_runtime_memmap.count++;
            src_ptr += tmp_boot_info->mem_descriptor_size;
            continue; // Runtime 直接短路，不进入常规筛选
        }

        if (type == EFI_ACPI_MEMORY_NVS) {
            memblock_add(&memblock.reserved, pa_start, size, type); // 关入死牢保护
            src_ptr += tmp_boot_info->mem_descriptor_size;
            continue; // NVS 直接短路
        }

        // ====================================================================
        // 第三层：纯净的状态自描述分类
        // ====================================================================
        if (type == EFI_LOADER_DATA || type == EFI_LOADER_CODE || type == EFI_ACPI_RECLAIM_MEMORY) {
            // 资产一律封存，保留其原有的 type 作为身份标识
            memblock_add(&memblock.reserved, pa_start, size, type);
        } else if (type == EFI_BOOT_SERVICES_CODE || type == EFI_BOOT_SERVICES_DATA || type == EFI_CONVENTIONAL_MEMORY) {
            // 空闲内存一律洗白充公，统一打上 EFI_CONVENTIONAL_MEMORY 标记
            memblock_add(&memblock.memory, pa_start, size, EFI_CONVENTIONAL_MEMORY);
        }

        src_ptr += tmp_boot_info->mem_descriptor_size;
    }

    // 绑定内核全局 ops...
    kernel_space.ops.alloc_pages = memblock_alloc_pages;
    kernel_space.ops.free_pages  = memblock_free_pages;
    kernel_space.ops.page_to_phys = memblock_page_to_pa;
    kernel_space.ops.phys_to_page = memblock_pa_to_page;
    kernel_space.ops.phys_to_virt = pa_to_va;
    kernel_space.ops.virt_to_phys = va_to_pa;
    PR_INFO("Total Physics Memory: %d MB\n", phy_mem_size / 1024 / 1024);
}
