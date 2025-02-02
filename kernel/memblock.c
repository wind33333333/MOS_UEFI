#include "memblock.h"
#include "uefi.h"
#include "printk.h"


INIT_DATA memblock_t memblock;

INIT_TEXT void init_memblock(void) {
    for (UINT32 i = 0; i < (boot_info->mem_map_size / boot_info->mem_descriptor_size); i++) {
        if (boot_info->mem_map[i].Type == EFI_LOADER_CODE || boot_info->mem_map[i].Type == EFI_LOADER_DATA || boot_info
            ->mem_map[i].Type == EFI_BOOT_SERVICES_CODE || boot_info->mem_map[i].Type == EFI_BOOT_SERVICES_DATA ||
            boot_info->mem_map[i].Type == EFI_CONVENTIONAL_MEMORY || boot_info->mem_map[i].Type ==
            EFI_ACPI_RECLAIM_MEMORY) {

        }
    }

}

//物理内存区域添加到 memblock 的“可用内存”列表中
INIT_TEXT void memblock_add(memblock_type_t *memblock_type, UINT64 base, UINT64 size) {
    if (memblock_type->count == 0) {
        memblock_type->region[0].base = base;
        memblock_type->region[0].size = size;
        memblock_type->count++;
    } else if (memblock_type->region[memblock_type->count - 1].base+memblock_type->region[memblock_type->count - 1].size == base) {
        memblock_type->region[memblock_type->count - 1].size += size;
    } else {
        memblock_type->region[memblock_type->count].base = base;
        memblock_type->region[memblock_type->count].size = size;
        memblock_type->count++;
    }
}

//将指定区间标记为保留，
INIT_TEXT void memblock_reserve(UINT64 base, UINT64 size) {
}

//线性分配内存
INIT_TEXT void *memblock_alloc(UINT64 size, UINT64 align) {
}
