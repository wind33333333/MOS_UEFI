#include "memblock.h"
#include "vmm.h"
#include "printk.h"

INIT_DATA memblock_t memblock;
INIT_DATA memblock_type_t phy_vmemmap;
INIT_DATA efi_runtime_memmap_t efi_runtime_memmap;

INIT_TEXT void init_memblock(void) {
    UINT64 phy_mem_size = 0;
    UINT64 kernel_end = _end_stack - KERNEL_OFFSET;
    UINT64 kernel_size = _end_stack - _start_text;
    UINT32 count = boot_info->mem_map_size / boot_info->mem_descriptor_size;
    for (UINT32 i = 0; i < count; i++) {
        EFI_MEMORY_DESCRIPTOR *mem_des = &boot_info->mem_map[i];
        if (mem_des->NumberOfPages == 0) continue;
        UINT32 type = mem_des->Type;
        if (type == EFI_LOADER_DATA || type == EFI_LOADER_CODE || type == EFI_BOOT_SERVICES_CODE ||\
            type == EFI_BOOT_SERVICES_DATA || type == EFI_CONVENTIONAL_MEMORY || type == EFI_ACPI_RECLAIM_MEMORY) {
            UINT64 mem_des_pstart = mem_des->PhysicalStart;
            UINT64 mem_des_size = mem_des->NumberOfPages << PAGE_4K_SHIFT;
            //如果内存类型是1M内或是lode_data或是acpi则先放入保留区
            if (mem_des_pstart < 0x100000 || type == EFI_LOADER_DATA || type == EFI_ACPI_RECLAIM_MEMORY) {
                //在memblock.reserved中找出内核段并剔除，防止后期错误释放
                UINT64 memblock_pend = mem_des_pstart + mem_des_size;
                if (kernel_end == memblock_pend) {
                    mem_des_size -= kernel_size;
                    mem_des->NumberOfPages -= (kernel_size >> PAGE_4K_SHIFT);
                }
                memblock_add(&memblock.reserved, mem_des_pstart, mem_des_size);
                //其他可用类型合并放入可用类型保存
            } else {
                memblock_add(&memblock.memory, mem_des_pstart, mem_des_size);
            }

            //统计系统总物理内存容量
            phy_mem_size += mem_des_size;

            //把所可用物理内存放入phy_mem_map，后续vmemmap区初始化需要使用
            memblock_region_t *phy_vmemmap_block = &phy_vmemmap.region[phy_vmemmap.count];
            UINT64 memblock_gap = mem_des_pstart - (phy_vmemmap_block->base + phy_vmemmap_block->size);
            if (memblock_gap < 0x8000000) {
                phy_vmemmap_block->size = mem_des_pstart + mem_des_size - phy_vmemmap_block->base;
            } else {
                phy_vmemmap.count++;
                phy_vmemmap_block = &phy_vmemmap.region[phy_vmemmap.count];
                phy_vmemmap_block->base = mem_des_pstart;
                phy_vmemmap_block->size = mem_des_size;
            }
        }else if (type==EFI_RUNTIME_SERVICES_DATA || type==EFI_RUNTIME_SERVICES_CODE) {
            efi_runtime_memmap.mem_map[efi_runtime_memmap.count] = *mem_des;
            efi_runtime_memmap.count++;
        }
    }
    color_printk(GREEN, BLACK, "Total Physics Memory:%dMB\n",phy_mem_size/1024/1024);
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
        if (memblock.memory.region[index].base + memblock.memory.region[index].size == memblock.memory.region[index + 1]
            .base) {
            memblock.memory.region[index].size += memblock.memory.region[index + 1].size;
            for (UINT32 j = index + 1; j < memblock.memory.count; j++) {
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
INIT_TEXT INT32 memblock_unmmap(UINT64 *pml4t, void *va, UINT64 page_size) {
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
INIT_TEXT INT32 memblock_unmmap_range(UINT64 *pml4t, void *va, UINT64 size, UINT64 page_size) {
    UINT64 page_count = size / page_size;
    while (page_count--) {
        if (memblock_unmmap(pml4t, va, page_size)) return -1;
        va += page_size;
    }
    return 0;
}
