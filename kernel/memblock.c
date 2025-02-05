#include "memblock.h"
#include "uefi.h"
#include "printk.h"

INIT_DATA memblock_t memblock;

INIT_TEXT void init_memblock(void) {
    for (UINT32 i = 0; i < (boot_info->mem_map_size / boot_info->mem_descriptor_size); i++) {
        //如果内存类型是1M内或是lode_data或是acpi则先放入保留区
        if ((boot_info->mem_map[i].PhysicalStart < 0x100000 && (
                 boot_info->mem_map[i].Type == EFI_LOADER_CODE || boot_info->mem_map[i].Type == EFI_BOOT_SERVICES_CODE
                 || boot_info->mem_map[i].Type == EFI_BOOT_SERVICES_DATA || boot_info->mem_map[i].Type ==
                 EFI_CONVENTIONAL_MEMORY)) || boot_info->mem_map[i].Type == EFI_LOADER_DATA || boot_info->mem_map[i].
            Type ==
            EFI_ACPI_RECLAIM_MEMORY) {
            memblock_add(&memblock.reserved, boot_info->mem_map[i].PhysicalStart,
                         boot_info->mem_map[i].NumberOfPages << PAGE_4K_SHIFT);
            //其他可用类型合并放入可用类型保存
        } else if (boot_info->mem_map[i].Type == EFI_LOADER_CODE || boot_info->mem_map[i].Type == EFI_BOOT_SERVICES_CODE
                   || boot_info->mem_map[i].Type == EFI_BOOT_SERVICES_DATA || boot_info->mem_map[i].Type ==
                   EFI_CONVENTIONAL_MEMORY) {
            memblock_add(&memblock.memory, boot_info->mem_map[i].PhysicalStart,
                         boot_info->mem_map[i].NumberOfPages << PAGE_4K_SHIFT);
        }
    }
}

//物理内存区域添加到 memblock 的列表中
INIT_TEXT void memblock_add(memblock_type_t *memblock_type, UINT64 base, UINT64 size) {
    if (memblock_type->count == 0) {
        memblock_type->region[0].base = base;
        memblock_type->region[0].size = size;
        memblock_type->count++;
    } else if (memblock_type->region[memblock_type->count - 1].base + memblock_type->region[memblock_type->count - 1].
               size == base) {
        memblock_type->region[memblock_type->count - 1].size += size;
    } else {
        memblock_type->region[memblock_type->count].base = base;
        memblock_type->region[memblock_type->count].size = size;
        memblock_type->count++;
    }
}

//线性分配物理内存
INIT_TEXT void *memblock_alloc(UINT64 size, UINT64 align) {
    for (UINT32 i = 0; i < memblock.memory.count; i++) {
        UINT64 align_base = align_up(memblock.memory.region[i].base, align);
        UINT64 align_size = align_base - memblock.memory.region[i].base + size;
        if (align_base == memblock.memory.region[i].base && align_size < memblock.memory.region[i].size) {
            //如果对齐后地址相等且长度小于这则修正当前的块起始地址和长度
            memblock.memory.region[i].base += align_size;
            memblock.memory.region[i].size -= align_size;
            return (void *) align_base;
        }else if (align_base != memblock.memory.region[i].base && align_size < memblock.memory.region[i].size) {
            //如果对齐地址不相等但是长度小于先拆分块再分配，向后移动数组和数组加一
            for (UINT32 j = memblock.memory.count; j > i; j--) {
                memblock.memory.region[j].base = memblock.memory.region[j - 1].base;
                memblock.memory.region[j].size = memblock.memory.region[j - 1].size;
            }
            memblock.memory.region[i + 1].base += align_size;
            memblock.memory.region[i + 1].size -= align_size;
            memblock.memory.region[i].size = align_base - memblock.memory.region[i].base;
            memblock.memory.count++;
            return (void *) align_base;
        } else if (align_base == memblock.memory.region[i].base && align_size == memblock.memory.region[i].size) {
            //如果对齐地址和长度都相等则直接从中取出内存块，向前移动数组和数组数量减一
            for (UINT32 j = i; j < memblock.memory.count; j++) {
                memblock.memory.region[j].base = memblock.memory.region[j + 1].base;
                memblock.memory.region[j].size = memblock.memory.region[j + 1].size;
            }
            memblock.memory.count--;
            return (void*)align_base;
        }
    }
    return NULL;
}