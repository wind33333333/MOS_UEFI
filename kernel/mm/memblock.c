#include "../include/memblock.h"
#include "../include/vmm_page.h"
#include "vmalloc.h"
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
        uint64 size = mem_des->NumberOfPages << 12;
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


//分配一个4K页
uint64 memblock_alloc_4k(void) {
    uint64 pa = memblock_alloc(4096,4096);
    asm_mem_set(pa_to_va(pa),0,4096);
    return pa;
}

//释放一个4K页
void memblock_free_4k(uint64 ptr) {
    memblock_free(ptr,4096);
}