#include "uefi.h"
#include "vmm.h"
#include "kpage_table.h"
#include "linkage.h"
#include "vmalloc.h"

INIT_DATA efi_runtime_mem_t efi_runtime_mem;

void init_efi_runtime_service(void) {
    UINT64 efi_max_length = efi_runtime_mem.mem_map[efi_runtime_mem.conut-1].PhysicalStart + (efi_runtime_mem.mem_map[efi_runtime_mem.conut-1].NumberOfPages << PAGE_4K_SHIFT);
    efi_max_length = PAGE_1G_ALIGN(efi_max_length);
    mmap_range(kpml4t_ptr,0,(void*)0,efi_max_length,PAGE_ROOT_RWX_2M1G,PAGE_1G_SIZE);

    for (UINT32 i = 0; i < efi_runtime_mem.conut; i++) {
        if (efi_runtime_mem.mem_map[i].Type == EFI_RUNTIME_SERVICES_DATA) {
            efi_runtime_mem.mem_map[i].VirtualStart = pa_to_va(efi_runtime_mem.mem_map[i].PhysicalStart);
        }else {
            efi_runtime_mem.mem_map[i].VirtualStart = (UINT64)iomap(efi_runtime_mem.mem_map[i].PhysicalStart,efi_runtime_mem.mem_map[i].NumberOfPages << PAGE_4K_SHIFT,PAGE_ROOT_RWX_4K);
        }
    }

    boot_info->gRTS->SetVirtualAddressMap(efi_runtime_mem.conut * boot_info->mem_descriptor_size,boot_info->mem_descriptor_size,boot_info->mem_descriptor_version,efi_runtime_mem.mem_map);

    unmmap_range(kpml4t_ptr,(void*)0,efi_max_length,PAGE_1G_SIZE);
}