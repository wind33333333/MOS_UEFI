#include "uefi.h"
#include "memblock.h"
#include "printk.h"
#include "vmalloc.h"

extern vm_space_t kernel_space;

void efi_runtime_service_map(void) {

    // 将起点转换为单字节指针，以便进行精准的字节级跨度跳跃
    uint8 *desc_ptr = (uint8 *)efi_runtime_memmap.mem_map;

    for (uint32 i = 0; i < efi_runtime_memmap.count; i++) {
        // 将当前字节指针强转为结构体指针，以便操作具体字段
        EFI_MEMORY_DESCRIPTOR *desc = (EFI_MEMORY_DESCRIPTOR *)desc_ptr;

        uint64 efi_pa = desc->PhysicalStart;
        uint64 efi_size = desc->NumberOfPages << PAGE_4K_SHIFT;
        uint64 efi_va = (uint64)module_remap(efi_pa, efi_size);

        // 赋值给当前的描述符
        desc->VirtualStart = efi_va;

        if (desc->Type == EFI_RUNTIME_SERVICES_CODE) {
            set_memory_rwx(efi_va, efi_size); // 代码段
        }

        // 【核心修复】：手动加上主板固件给的真实跨度，而不是靠 C 语言的数组隐式推导！
        desc_ptr += boot_info->mem_descriptor_size;
    }

    boot_info->gRTS->SetVirtualAddressMap(efi_runtime_memmap.count * boot_info->mem_descriptor_size,
                                          boot_info->mem_descriptor_size, boot_info->mem_descriptor_version,
                                          (void*)efi_runtime_memmap.mem_map);

    //初始化后尝试获取时间信息并打印检测是否映射成功
    EFI_TIME efi_time;
    boot_info->gRTS->GetTime(&efi_time,NULL);
    color_printk(GREEN,BLACK,"UEFI Run Time Service Get Time:%d-%d-%d %d:%d:%d\n",efi_time.Year,efi_time.Month,efi_time.Day,efi_time.Hour,efi_time.Minute,efi_time.Second);
}
