#include "../include/memblock.h"
#include "../include/vmm.h"
#include "../include/printk.h"

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
    uint32 count = boot_info->mem_map_size / boot_info->mem_descriptor_size;
    for (uint32 i = 0; i < count; i++) {
        EFI_MEMORY_DESCRIPTOR *mem_des = &boot_info->mem_map[i];
        uint64 pa_start = mem_des->PhysicalStart;
        uint64 free_pa_start = pa_start;
        uint64 size = mem_des->NumberOfPages << PAGE_4K_SHIFT;
        if (mem_des->NumberOfPages == 0) continue;
        uint32 type = mem_des->Type;
        if (type == EFI_LOADER_DATA ||
            type == EFI_LOADER_CODE ||
            type == EFI_BOOT_SERVICES_CODE ||
            type == EFI_BOOT_SERVICES_DATA ||
            type == EFI_CONVENTIONAL_MEMORY) {
            phy_mem_size += size;
            // 🛡️ 架构级防御：1MB 以下的物理内存强行划入 used (保留区)
            if (pa_start < MEM_1MB) {
                // 如果跨越了 1MB 边界，需要截断处理 (高级处理手法)
                if (pa_start + size > MEM_1MB) {
                    uint64 reserved_size = MEM_1MB - pa_start;
                    size = size - reserved_size;
                    free_pa_start = MEM_1MB;
                }
            }
            // 1MB 以上的安全区域，放心喂给空闲池
            memblock_add(&memblock.free, free_pa_start, size);
            memblock_add(&page_mem_map, pa_start, size);
            memblock_add(&direct_mem_map, pa_start, size);
        } else if (type == EFI_ACPI_RECLAIM_MEMORY) {
            phy_mem_size += size;
            memblock_add(&direct_mem_map, pa_start, size);
        } else if (type == EFI_RUNTIME_SERVICES_DATA || type == EFI_RUNTIME_SERVICES_CODE) {
            efi_runtime_memmap.mem_map[efi_runtime_memmap.count] = *mem_des;
            efi_runtime_memmap.count++;
        }
    }


    color_printk(GREEN, BLACK, "Total Physics Memory:%dMB\n", phy_mem_size / 1024 / 1024);
}


//线性分配物理内存
INIT_TEXT uint64 memblock_alloc(uint64 size, uint64 align) {
    if (!size) return 0;
    uint64 align_base, align_size;
    uint32 index = 0;
    while (index < memblock.free.count) {
        align_base = align_up(memblock.free.region[index].start_pa, align);
        align_size = align_base - memblock.free.region[index].start_pa + size;
        if (align_size <= memblock.free.region[index].size) break;
        index++;
    }
    //没有合适大小块
    if (index >= memblock.free.count) return 0;
    //如果长度相等则刚好等于一个块
    if (size == memblock.free.region[index].size) {
        for (uint32 j = index; j < memblock.free.count; j++) {
            memblock.free.region[j] = memblock.free.region[j + 1];
        }
        memblock.free.count--;
        //如果对齐后地址等于起始地址则从头切
    } else if (align_base == memblock.free.region[index].start_pa) {
        memblock.free.region[index].start_pa += size;
        memblock.free.region[index].size -= size;
        //如果对齐后地址等于结束地址则尾部切
    } else if (align_size == memblock.free.region[index].size) {
        memblock.free.region[index].size -= size;
        //否则中间切
    } else {
        for (uint32 j = memblock.free.count; j > index; j--) {
            memblock.free.region[j] = memblock.free.region[j - 1];
        }
        memblock.free.region[index + 1].start_pa += align_size;
        memblock.free.region[index + 1].size -= align_size;
        memblock.free.region[index].size = align_base - memblock.free.region[index].start_pa;
        memblock.free.count++;
    }
    return align_base;
}

//释放物理内存
INIT_TEXT int32 memblock_free(uint64 ptr, uint64 size) {
    //根据align_base找合适的插入位置
    uint32 index = 0;
    while (index < memblock.free.count) {
        if (ptr <= memblock.free.region[index].start_pa + memblock.free.region[index].size) break;
        index++;
    }
    //释放地址在头部
    if (ptr + size == memblock.free.region[index].start_pa) {
        memblock.free.region[index].start_pa = ptr;
        memblock.free.region[index].size += size;
        //释放的地址在尾部
    } else if (memblock.free.region[index].start_pa + memblock.free.region[index].size == ptr) {
        memblock.free.region[index].size += size;
        //合并
        if (memblock.free.region[index].start_pa + memblock.free.region[index].size == memblock.free.region[index + 1]
            .start_pa) {
            memblock.free.region[index].size += memblock.free.region[index + 1].size;
            for (uint32 j = index + 1; j < memblock.free.count; j++) {
                memblock.free.region[j] = memblock.free.region[j + 1];
            }
            memblock.free.count--;
        }
        //释放的地址不在块中
    } else {
        for (uint32 j = memblock.free.count; j > index; j--) {
            memblock.free.region[j] = memblock.free.region[j - 1];
        }
        memblock.free.region[index].start_pa = ptr;
        memblock.free.region[index].size = size;
        memblock.free.count++;
    }
    return 0;
}

//映射一个页表
INIT_TEXT int32 memblock_mmap(uint64 *pml4t, uint64 pa, void *va, uint64 attr, uint64 page_size) {
    uint64 *pdptt, *pdt, *ptt;
    uint32 index;
    pml4t = pa_to_va(pml4t);

    index = get_pml4e_index(va);
    if (pml4t[index] == 0) {
        pml4t[index] = (uint64) memblock_alloc(PAGE_4K_SIZE,PAGE_4K_SIZE) | (
                           attr & (PAGE_US | PAGE_P | PAGE_RW) | PAGE_RW);
        asm_mem_set(pa_to_va(pml4t[index] & 0x7FFFFFFFF000), 0,PAGE_4K_SIZE);
    }

    pdptt = pa_to_va(pml4t[index] & 0x7FFFFFFFF000);
    index = get_pdpte_index(va);
    if (page_size == PAGE_1G_SIZE) {
        //1G页
        if (pdptt[index] == 0) {
            pdptt[index] = pa | attr;
            asm_invlpg(va);
            return 0; //1G页映射成功
        }
        return -1; //已被占用
    }

    if (pdptt[index] == 0) {
        pdptt[index] = (uint64) memblock_alloc(PAGE_4K_SIZE,PAGE_4K_SIZE) | (
                           attr & (PAGE_US | PAGE_P | PAGE_RW) | PAGE_RW);
        asm_mem_set(pa_to_va(pdptt[index] & 0x7FFFFFFFF000), 0,PAGE_4K_SIZE);
    }

    pdt = pa_to_va(pdptt[index] & 0x7FFFFFFFF000);
    index = get_pde_index(va);
    if (page_size == PAGE_2M_SIZE) {
        //2M页
        if (pdt[index] == 0) {
            pdt[index] = pa | attr;
            asm_invlpg(va);
            return 0; //2M页映射成功
        }
        return -1; //以占用
    }

    if (pdt[index] == 0) {
        pdt[index] = (uint64) memblock_alloc(PAGE_4K_SIZE,PAGE_4K_SIZE) | (
                         attr & (PAGE_US | PAGE_P | PAGE_RW) | PAGE_RW);
        asm_mem_set(pa_to_va(pdt[index] & 0x7FFFFFFFF000), 0,PAGE_4K_SIZE);
    }

    ptt = pa_to_va(pdt[index] & 0x7FFFFFFFF000);
    index = get_pte_index(va);
    if (ptt[index] == 0) {
        ptt[index] = pa | attr;
        asm_invlpg(va);
        return 0; //4K页映射成功
    }
    return -1; //失败
}

//批量映射
INIT_TEXT int32 memblock_mmap_range(uint64 *pml4t, uint64 pa, void *va, uint64 size, uint64 attr,
                                    uint64 page_size) {
    uint64 page_count = size / page_size;
    while (page_count--) {
        if (memblock_mmap(pml4t, pa, va, attr, page_size)) return -1;
        pa += page_size;
        va += page_size;
    }
    return 0;
}

//删除一个页表映射
INIT_TEXT int32 memblock_unmmap(uint64 *pml4t, void *va, uint64 page_size) {
    uint64 *pdptt, *pdt, *ptt;
    uint32 pml4e_index, pdpte_index, pde_index, pte_index;

    pml4t = pa_to_va((uint64) pml4t);
    pml4e_index = get_pml4e_index(va);
    if (pml4t[pml4e_index] == 0) return -1; //pml4e无效

    pdptt = pa_to_va(pml4t[pml4e_index] & PAGE_PA_MASK);
    pdpte_index = get_pdpte_index(va);
    if (pdptt[pdpte_index] == 0) return -1; //pdpte无效
    if (page_size == PAGE_1G_SIZE) {
        //如果为1G巨页，跳转到巨页释放
        pdptt[pdpte_index] = 0;
        asm_invlpg(va);
        goto huge_page;
    }

    pdt = pa_to_va(pdptt[pdpte_index] & PAGE_PA_MASK);
    pde_index = get_pde_index(va);
    if (pdt[pde_index] == 0) return -1; //pde无效
    if (page_size == PAGE_2M_SIZE) {
        //如果等于1则表示该页为2M大页，跳转到大页释放
        pdt[pde_index] = 0;
        asm_invlpg(va);
        goto big_page;
    }

    ptt = pa_to_va(pdt[pde_index] & PAGE_PA_MASK); //4K页
    pte_index = get_pte_index(va);
    ptt[pte_index] = 0;
    asm_invlpg(va);


    //ptt为空则释放
    if (asm_forward_find_qword(ptt, 512, 0) == 0) {
        memblock_free(va_to_pa(ptt),PAGE_4K_SIZE);
        pdt[pde_index] = 0;
    } else {
        return 0;
    }

big_page:
    //pde为空则释放
    if (asm_forward_find_qword(pdt, 512, 0) == 0) {
        memblock_free(va_to_pa(pdt),PAGE_4K_SIZE);
        pdptt[pdpte_index] = 0;
    } else {
        return 0;
    }

huge_page:
    //pdpt为空则释放
    if (asm_forward_find_qword(pdptt, 512, 0) == 0) {
        memblock_free(va_to_pa(pdptt),PAGE_4K_SIZE);
        pml4t[pml4e_index] = 0;
    }
    return 0;
}

//批量删除页表映射
INIT_TEXT int32 memblock_unmmap_range(uint64 *pml4t, void *va, uint64 size, uint64 page_size) {
    uint64 page_count = size / page_size;
    while (page_count--) {
        if (memblock_unmmap(pml4t, va, page_size)) return -1;
        va += page_size;
    }
    return 0;
}
