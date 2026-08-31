#include "uefi.h"
#include "memblock.h"
#include "printk.h"
#include "vmalloc.h"

void efi_runtime_service_map(void) {
    for (uint32 i = 0; i < efi_runtime_memmap.count; i++) {
        uint64 size = efi_runtime_memmap.mem_map[i].NumberOfPages << PAGE_4K_SHIFT;
        uint64 efi_va =(uint64) module_remap(efi_runtime_memmap.mem_map[i].PhysicalStart,size);
        efi_runtime_memmap.mem_map[i].VirtualStart = efi_va;
        if (efi_runtime_memmap.mem_map[i].Type == EFI_RUNTIME_SERVICES_DATA) {
            set_memory_rw(efi_va,size); //数据段
        }else {
            set_memory_rx(efi_va,size); //代码段
        }
    }

    boot_info->gRTS->SetVirtualAddressMap(efi_runtime_memmap.count * boot_info->mem_descriptor_size,
                                          boot_info->mem_descriptor_size, boot_info->mem_descriptor_version,
                                          (void*)&efi_runtime_memmap.mem_map);

    //初始化后尝试获取时间信息并打印检测是否映射成功
    EFI_TIME efi_time;
    boot_info->gRTS->GetTime(&efi_time,NULL);
    color_printk(GREEN,BLACK,"UEFI Run Time Service Get Time:%d-%d-%d %d:%d:%d\n",efi_time.Year,efi_time.Month,efi_time.Day,efi_time.Hour,efi_time.Minute,efi_time.Second);
}
