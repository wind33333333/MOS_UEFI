#include "uefi.h"
#include "vmm.h"
#include "kpage_table.h"
#include "linkage.h"
#include "slub.h"
#include "vmalloc.h"

void init_efi_runtime_service(void) {
    UINT32 efi_runtime_count = 0;
    UINT32 efi_runtime_index = 0;
    UINT32 mem_map_index = 0;
    UINT32 mem_map_count = boot_info->mem_map_size / boot_info->mem_descriptor_size;
    while (mem_map_index < mem_map_count) {
        if (boot_info->mem_map[mem_map_index].Type == EFI_RUNTIME_SERVICES_CODE || boot_info->mem_map[mem_map_index].Type == EFI_RUNTIME_SERVICES_DATA) {
            efi_runtime_count++;
        }
        mem_map_index++;
    }

    EFI_MEMORY_DESCRIPTOR *efi_runtime_mem = kmalloc(efi_runtime_count * boot_info->mem_descriptor_size);
    mem_map_index = 0;
    while (mem_map_index < mem_map_count && efi_runtime_index < efi_runtime_count ) {
        if (boot_info->mem_map[mem_map_index].Type == EFI_RUNTIME_SERVICES_DATA) {
            efi_runtime_mem[efi_runtime_index] = boot_info->mem_map[mem_map_index];
            efi_runtime_mem[efi_runtime_index].VirtualStart = (UINT64)pa_to_va(efi_runtime_mem[efi_runtime_index].PhysicalStart);
            efi_runtime_index++;
        } else if (boot_info->mem_map[mem_map_index].Type == EFI_RUNTIME_SERVICES_CODE) {
            efi_runtime_mem[efi_runtime_index] = boot_info->mem_map[mem_map_index];
            efi_runtime_mem[efi_runtime_index].VirtualStart = (UINT64)iomap(efi_runtime_mem[efi_runtime_index].PhysicalStart,efi_runtime_mem[efi_runtime_index].NumberOfPages << PAGE_4K_SHIFT,PAGE_ROOT_RWX_4K);
            efi_runtime_index++;
        }
        mem_map_index++;
    }

    UINT64 efi_max_length = efi_runtime_mem[efi_runtime_count-1].PhysicalStart + (efi_runtime_mem[efi_runtime_count-1].NumberOfPages << PAGE_4K_SHIFT);
    efi_max_length = PAGE_1G_ALIGN(efi_max_length);
    mmap_range(kpml4t_ptr,0,(void*)0,efi_max_length,PAGE_ROOT_RWX_2M1G,PAGE_1G_SIZE);

    boot_info->gRTS->SetVirtualAddressMap(efi_runtime_count * boot_info->mem_descriptor_size,boot_info->mem_descriptor_size,boot_info->mem_descriptor_version,efi_runtime_mem);

    unmmap_range(kpml4t_ptr,(void*)0,efi_max_length,PAGE_1G_SIZE);
    kfree(efi_runtime_mem);
}