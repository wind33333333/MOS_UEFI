#include "memblock.h"
#include "uefi.h"
#include "vmm.h"

INIT_DATA memblock_t memblock;

INIT_TEXT void init_memblock(void) {
    boot_info = pa_to_va(boot_info);
    boot_info->mem_map = pa_to_va(boot_info->mem_map);
    for (UINT32 i = 0; i < (boot_info->mem_map_size / boot_info->mem_descriptor_size); i++) {
        //如果内存类型是1M内或是lode_data或是acpi则先放入保留区
        if ((boot_info->mem_map[i].PhysicalStart < 0x100000 && boot_info->mem_map[i].NumberOfPages != 0) || boot_info->
            mem_map[i].Type == EFI_LOADER_DATA || boot_info->mem_map[i].Type == EFI_ACPI_RECLAIM_MEMORY) {
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
    if (!size) return NULL;
    UINT64 align_base, align_size;
    UINT32 index = 0;
    while (index <= memblock.memory.count) {
        align_base = align_up(memblock.memory.region[index].base, align);
        align_size = align_base - memblock.memory.region[index].base + size;
        if (align_size <= memblock.memory.region[index].size) break;
        index++;
    }
    //没有合适大小块
    if (index >= memblock.memory.count) return NULL;
    //如果对齐地址和长度都相等则直接从中取出内存块，向前移动数组和数组数量减一
    if (size == memblock.memory.region[index].size) {
        for (UINT32 j = index; j < memblock.memory.count; j++) {
            memblock.memory.region[j] = memblock.memory.region[j + 1];
        }
        memblock.memory.count--;
        //如果对齐后地址相等且长度小于这则修正当前的块起始地址和长度
    }else if (align_base == memblock.memory.region[index].base) {
        memblock.memory.region[index].base += size;
        memblock.memory.region[index].size -= size;
        //如果对齐地址不相等但是长度小于先拆分块再分配，向后移动数组和数组加一
    }else if (align_size == memblock.memory.region[index].size) {
        memblock.memory.region[index].size -= size;
    }else{
        for (UINT32 j = memblock.memory.count; j > index; j--) {
            memblock.memory.region[j] = memblock.memory.region[j - 1];
        }
        memblock.memory.region[index + 1].base += align_size;
        memblock.memory.region[index + 1].size -= align_size;
        memblock.memory.region[index].size = align_base - memblock.memory.region[index].base;
        memblock.memory.count++;
    }
    return (void *) align_base;
}

INIT_TEXT INT32 memblock_mmap(UINT64 *pml4t, UINT64 pa, void *va, UINT64 attr, UINT64 page_size) {
    UINT64 *pdptt, *pdt, *ptt;
    UINT32 index;
    pml4t = pa_to_va(pml4t);

    index = get_pml4e_index(va);
    if (pml4t[index] == 0) {
        pml4t[index] = (UINT64) memblock_alloc(PAGE_4K_SIZE,PAGE_4K_SIZE) | (
                           attr & (PAGE_US | PAGE_P | PAGE_RW) | PAGE_RW);
        mem_set(pa_to_va(pml4t[index] & 0x7FFFFFFFF000), 0,PAGE_4K_SIZE);
    }

    pdptt = pa_to_va(pml4t[index] & 0x7FFFFFFFF000);
    index = get_pdpte_index(va);
    if (page_size == PAGE_1G_SIZE) {
        //1G页
        if (pdptt[index] == 0) {
            pdptt[index] = pa | attr;
            invlpg(va);
            return 0; //1G页映射成功
        }
        return -1; //已被占用
    }

    if (pdptt[index] == 0) {
        pdptt[index] = (UINT64) memblock_alloc(PAGE_4K_SIZE,PAGE_4K_SIZE) | (
                           attr & (PAGE_US | PAGE_P | PAGE_RW) | PAGE_RW);
        mem_set(pa_to_va(pdptt[index] & 0x7FFFFFFFF000), 0,PAGE_4K_SIZE);
    }

    pdt = pa_to_va(pdptt[index] & 0x7FFFFFFFF000);
    index = get_pde_index(va);
    if (page_size == PAGE_2M_SIZE) {
        //2M页
        if (pdt[index] == 0) {
            pdt[index] = pa | attr;
            invlpg(va);
            return 0; //2M页映射成功
        }
        return -1; //以占用
    }

    if (pdt[index] == 0) {
        pdt[index] = (UINT64) memblock_alloc(PAGE_4K_SIZE,PAGE_4K_SIZE) | (
                         attr & (PAGE_US | PAGE_P | PAGE_RW) | PAGE_RW);
        mem_set(pa_to_va(pdt[index] & 0x7FFFFFFFF000), 0,PAGE_4K_SIZE);
    }

    ptt = pa_to_va(pdt[index] & 0x7FFFFFFFF000);
    index = get_pte_index(va);
    if (ptt[index] == 0) {
        ptt[index] = pa | attr;
        invlpg(va);
        return 0; //4K页映射成功
    }
    return -1; //失败
}


INIT_TEXT INT32 memblock_mmap_range(UINT64 *pml4t, UINT64 pa, void *va, UINT64 size, UINT64 attr,
                                    UINT64 page_size) {
    UINT64 page_count = size / page_size;
    while (page_count--) {
        if (memblock_mmap(pml4t, pa, va, attr, page_size)) return -1;
        pa += page_size;
        va += page_size;
    }
    return 0;
}
