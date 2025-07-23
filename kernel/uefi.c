#include "uefi.h"
#include "vmm.h"
#include "kernel_page_table.h"
#include "linkage.h"
#include "memblock.h"
#include "printk.h"
#include "slub.h"
#include "vmalloc.h"

void efi_runtime_service_map(void) {
    for (UINT32 i = 0; i < efi_runtime_memmap.count; i++) {
        if (efi_runtime_memmap.mem_map[i].Type == EFI_RUNTIME_SERVICES_DATA) {
            efi_runtime_memmap.mem_map[i].VirtualStart = (UINT64) pa_to_va(efi_runtime_memmap.mem_map[i].PhysicalStart);
        } else {
            efi_runtime_memmap.mem_map[i].VirtualStart = (UINT64) iomap(efi_runtime_memmap.mem_map[i].PhysicalStart,
                                                                efi_runtime_memmap.mem_map[i].NumberOfPages << PAGE_4K_SHIFT,
                                                                PAGE_4K_SIZE,PAGE_ROOT_RWX_4K);
        }
    }
    boot_info->gRTS->SetVirtualAddressMap(efi_runtime_memmap.count * boot_info->mem_descriptor_size,
                                          boot_info->mem_descriptor_size, boot_info->mem_descriptor_version,
                                          &efi_runtime_memmap.mem_map);

    //初始化后尝试获取时间信息并打印检测是否映射成功
    EFI_TIME efi_time;
    boot_info->gRTS->GetTime(&efi_time,NULL);
    color_printk(GREEN,BLACK,"UEFI Run Time Service Get Time:%d-%d-%d %d:%d:%d\n",efi_time.Year,efi_time.Month,efi_time.Day,efi_time.Hour,efi_time.Minute,efi_time.Second);
}
