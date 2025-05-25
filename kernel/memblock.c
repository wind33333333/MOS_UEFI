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
INIT_TEXT UINT64 memblock_alloc(UINT64 size, UINT64 align) {
    if (!size) return 0;
    UINT64 align_base, align_size;
    UINT32 index = 0;
    while (index < memblock.memory.count) {
        align_base = align_up(memblock.memory.region[index].base, align);
        align_size = align_base - memblock.memory.region[index].base + size;
        if (align_size <= memblock.memory.region[index].size) break;
        index++;
    }
    //没有合适大小块
    if (index >= memblock.memory.count) return 0;
    //如果长度相等则刚好等于一个块
    if (size == memblock.memory.region[index].size) {
        for (UINT32 j = index; j < memblock.memory.count; j++) {
            memblock.memory.region[j] = memblock.memory.region[j + 1];
        }
        memblock.memory.count--;
        //如果对齐后地址等于起始地址则从头切
    } else if (align_base == memblock.memory.region[index].base) {
        memblock.memory.region[index].base += size;
        memblock.memory.region[index].size -= size;
        //如果对齐后地址等于结束地址则尾部切
    } else if (align_size == memblock.memory.region[index].size) {
        memblock.memory.region[index].size -= size;
        //否则中间切
    } else {
        for (UINT32 j = memblock.memory.count; j > index; j--) {
            memblock.memory.region[j] = memblock.memory.region[j - 1];
        }
        memblock.memory.region[index + 1].base += align_size;
        memblock.memory.region[index + 1].size -= align_size;
        memblock.memory.region[index].size = align_base - memblock.memory.region[index].base;
        memblock.memory.count++;
    }
    return align_base;
}

//释放物理内存
INIT_TEXT INT32 memblock_free(UINT64 ptr, UINT64 size) {
    //根据align_base找合适的插入位置
    UINT32 index = 0;
    while (index < memblock.memory.count) {
        if (ptr <= memblock.memory.region[index].base + memblock.memory.region[index].size) break;
        index++;
    }
    //释放地址在头部
    if (ptr + size == memblock.memory.region[index].base) {
        memblock.memory.region[index].base = ptr;
        memblock.memory.region[index].size += size;
        //释放的地址在尾部
    } else if (memblock.memory.region[index].base + memblock.memory.region[index].size == ptr) {
        memblock.memory.region[index].size += size;
        //合并
        if (memblock.memory.region[index].base + memblock.memory.region[index].size == memblock.memory.region[index+1].base) {
            memblock.memory.region[index].size += memblock.memory.region[index+1].size;
            for (UINT32 j = index+1; j < memblock.memory.count; j++) {
                memblock.memory.region[j] = memblock.memory.region[j + 1];
            }
            memblock.memory.count--;
        }
        //释放的地址不在块中
    } else {
        for (UINT32 j = memblock.memory.count; j > index; j--) {
            memblock.memory.region[j] = memblock.memory.region[j - 1];
        }
        memblock.memory.region[index].base = ptr;
        memblock.memory.region[index].size = size;
        memblock.memory.count++;
    }
    return 0;
}

//映射一个页表
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

//批量映射
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

//删除一个页表映射
INT32 memblock_unmmap(UINT64 *pml4t, void *va, UINT64 page_size) {
    UINT64 *pdptt, *pdt, *ptt;
    UINT32 pml4e_index, pdpte_index, pde_index, pte_index;

    pml4t = pa_to_va((UINT64) pml4t);
    pml4e_index = get_pml4e_index(va);
    if (pml4t[pml4e_index] == 0) return -1; //pml4e无效

    pdptt = pa_to_va(pml4t[pml4e_index] & PAGE_PA_MASK);
    pdpte_index = get_pdpte_index(va);
    if (pdptt[pdpte_index] == 0) return -1; //pdpte无效
    if (page_size == PAGE_1G_SIZE) {
        //如果为1G巨页，跳转到巨页释放
        pdptt[pdpte_index] = 0;
        invlpg(va);
        goto huge_page;
    }

    pdt = pa_to_va(pdptt[pdpte_index] & PAGE_PA_MASK);
    pde_index = get_pde_index(va);
    if (pdt[pde_index] == 0) return -1; //pde无效
    if (page_size == PAGE_2M_SIZE) {
        //如果等于1则表示该页为2M大页，跳转到大页释放
        pdt[pde_index] = 0;
        invlpg(va);
        goto big_page;
    }

    ptt = pa_to_va(pdt[pde_index] & PAGE_PA_MASK); //4K页
    pte_index = get_pte_index(va);
    ptt[pte_index] = 0;
    invlpg(va);


    //ptt为空则释放
    if (forward_find_qword(ptt, 512, 0) == 0) {
        memblock_free(va_to_pa(ptt),PAGE_4K_SIZE);
        pdt[pde_index] = 0;
    } else {
        return 0;
    }

    big_page:
        //pde为空则释放
        if (forward_find_qword(pdt, 512, 0) == 0) {
            memblock_free(va_to_pa(pdt),PAGE_4K_SIZE);
            pdptt[pdpte_index] = 0;
        } else {
            return 0;
        }

    huge_page:
        //pdpt为空则释放
        if (forward_find_qword(pdptt, 512, 0) == 0) {
            memblock_free(va_to_pa(pdptt),PAGE_4K_SIZE);
            pml4t[pml4e_index] = 0;
        }
    return 0;
}

//批量删除页表映射
INT32 memblock_unmmap_range(UINT64 *pml4t, void *va, UINT64 size, UINT64 page_size) {
    UINT64 page_count = size / page_size;
    while (page_count--) {
        if (memblock_unmmap(pml4t, va, page_size)) return -1;
        va += page_size;
    }
    return 0;
}
