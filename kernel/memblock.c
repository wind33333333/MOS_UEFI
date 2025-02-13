#include "memblock.h"
#include "uefi.h"
#include "vmm.h"

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
    if (size != 0) {
        for (UINT32 i = 0; i < memblock.memory.count; i++) {
            UINT64 align_base = align_up(memblock.memory.region[i].base, align);
            UINT64 align_size = align_base - memblock.memory.region[i].base + size;
            if (align_size > memblock.memory.region[i].size) continue;
            if (align_base == memblock.memory.region[i].base && align_size == memblock.memory.region[i].size) {
                //如果对齐地址和长度都相等则直接从中取出内存块，向前移动数组和数组数量减一
                for (UINT32 j = i; j < memblock.memory.count; j++) {
                    memblock.memory.region[j] = memblock.memory.region[j + 1];
                }
                memblock.memory.count--;
            } else if (align_base == memblock.memory.region[i].base) {
                //如果对齐后地址相等且长度小于这则修正当前的块起始地址和长度
                memblock.memory.region[i].base += align_size;
                memblock.memory.region[i].size -= align_size;
            } else if (align_base != memblock.memory.region[i].base) {
                //如果对齐地址不相等但是长度小于先拆分块再分配，向后移动数组和数组加一
                for (UINT32 j = memblock.memory.count; j > i; j--) {
                    memblock.memory.region[j] = memblock.memory.region[j - 1];
                }
                memblock.memory.region[i + 1].base += align_size;
                memblock.memory.region[i + 1].size -= align_size;
                memblock.memory.region[i].size = align_base - memblock.memory.region[i].base;
                memblock.memory.count++;
            }
            return (void *) align_base;
        }
    }
    return NULL;
}

//物理内存映射虚拟内存,如果虚拟地址已被占用则从后面的虚拟内存中找一块可用空间挂载物理内存，并返回更新后的虚拟地址。
INIT_TEXT void *memblock_vmmap(UINT64 phy_addr, void *virt_addr, UINT64 page_count, UINT64 attr) {
    while (TRUE) {
        UINT64 *pte_vaddr = vaddr_to_pte_vaddr(virt_addr);
        UINT64 *pde_vaddr = vaddr_to_pde_vaddr(virt_addr);
        UINT64 *pdpte_vaddr = vaddr_to_pdpte_vaddr(virt_addr);
        UINT64 *pml4e_vaddr = vaddr_to_pml4e_vaddr(virt_addr);
        UINT64 count;

        //pml4e为空则挂载物理页
        count = calculate_pml4e_count(virt_addr, page_count);
        for (UINT64 i = 0; i < count; i++) {
            if (pml4e_vaddr[i] == 0) {
                pml4e_vaddr[i] = (UINT64) memblock_alloc(PAGE_4K_SIZE,PAGE_4K_SIZE) | (
                                     attr & (PAGE_US | PAGE_P | PAGE_RW) | PAGE_RW);
                //pml4e属性设置为可读可写，其余位保持默认。
                mem_set((void *) ((UINT64) pdpte_vaddr & PAGE_4K_MASK) + (i << PAGE_4K_SHIFT), 0x0, PAGE_4K_SIZE);
            }
        }

        //PDPE为空则挂载物理页
        count = calculate_pdpte_count(virt_addr, page_count);
        for (UINT64 i = 0; i < count; i++) {
            if (pdpte_vaddr[i] == 0) {
                pdpte_vaddr[i] = (UINT64) memblock_alloc(PAGE_4K_SIZE,PAGE_4K_SIZE) | (
                                     attr & (PAGE_US | PAGE_P | PAGE_RW) | PAGE_RW);
                //pdpte属性设置为可读可写，其余位保持默认。
                mem_set((void *) ((UINT64) pde_vaddr & PAGE_4K_MASK) + (i << PAGE_4K_SHIFT), 0x0, PAGE_4K_SIZE);
            }
        }

        //PDE为空则挂载物理页
        count = calculate_pde_count(virt_addr, page_count);
        for (UINT64 i = 0; i < count; i++) {
            if (pde_vaddr[i] == 0) {
                pde_vaddr[i] = (UINT64) memblock_alloc(PAGE_4K_SIZE,PAGE_4K_SIZE) | (
                                   attr & (PAGE_US | PAGE_P | PAGE_RW) | PAGE_RW); //pde属性设置为可读可写，其余位保持默认。
                mem_set((void *) ((UINT64) pte_vaddr & PAGE_4K_MASK) + (i << PAGE_4K_SHIFT), 0x0, PAGE_4K_SIZE);
            }
        }

        //如果虚拟地址被占用，则从后面找一块可用的虚拟地址挂载，并返回更新后的虚拟地址。
        count = reverse_find_qword(pte_vaddr, page_count, 0);
        if (count == 0) {
            //PTE挂载物理页，刷新TLB
            for (UINT64 i = 0; i < page_count; i++) {
                pte_vaddr[i] = phy_addr + (i << PAGE_4K_SHIFT) | attr;
                invlpg((void *) ((UINT64) virt_addr & PAGE_4K_MASK) + (i << PAGE_4K_SHIFT));
            }
            return virt_addr;
        } else {
            virt_addr = (void *) ((UINT64) virt_addr + (count << PAGE_4K_SHIFT));
        }
    }
}

//物理内存映射虚拟内存,如果虚拟地址已被占用则从后面的虚拟内存中找一块可用空间挂载物理内存，并返回更新后的虚拟地址。
INIT_TEXT void memblock_vmmap1(UINT64 phy_addr, void *virt_addr, UINT64 *pml4t, UINT64 length, UINT64 attr) {
    UINT64 *pdptt, *pdt, *ptt;
    UINT64 count = length+(PAGE_4K_SIZE-1) >> PAGE_4K_SHIFT; //把长度向上对齐4K
    UINT32 index;
    while (count > 0) {
        index = get_pml4e_index(virt_addr);
        if (pml4t[index] == 0) {
            pml4t[index] = (UINT64) memblock_alloc(PAGE_4K_SIZE,PAGE_4K_SIZE) | (
                                     attr & (PAGE_US | PAGE_P | PAGE_RW) | PAGE_RW);
        }

        pdptt = PA_TO_VA(pml4t[index]&0x7FFFFFFFF000);
        index = get_pdpte_index(virt_addr);
        if (pdptt[index] == 0) {
            pdptt[index] = (UINT64) memblock_alloc(PAGE_4K_SIZE,PAGE_4K_SIZE) | (
                                     attr & (PAGE_US | PAGE_P | PAGE_RW) | PAGE_RW);
        }

        pdt = PA_TO_VA(pdptt[index]&0x7FFFFFFFF000);
        index = get_pde_index(virt_addr);
        if (pdt[index] == 0) {
            pdt[index] = (UINT64) memblock_alloc(PAGE_4K_SIZE,PAGE_4K_SIZE) | (
                                 attr & (PAGE_US | PAGE_P | PAGE_RW) | PAGE_RW);
        }

        ptt = PA_TO_VA(pdt[index]&0x7FFFFFFFF000);
        index = get_pte_index(virt_addr);
        if (ptt[index] == 0) {
            ptt[index] = phy_addr | attr;
        }

        phy_addr += PAGE_4K_SIZE;
        virt_addr += PAGE_4K_SIZE;
        count--;
    }
}
