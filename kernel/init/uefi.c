#include "uefi.h"
#include "memblock.h"
#include "printk.h"
#include "vmalloc.h"

extern vm_space_t kernel_space;

void efi_runtime_service_map(void) {

    // 将起点转换为单字节指针，以便进行精准的字节级跨度跳跃
    uint8 *desc_ptr = (uint8 *)efi_runtime_memmap.mem_map;
    uint64 efi_rts_start_va = UEFI_RTS_VA_START;
    uint64 efi_size = 0;

    uint64 grts_pa = (uint64)boot_info->gRTS;

    for (uint32 i = 0; i < efi_runtime_memmap.count; i++) {
        // 将当前字节指针强转为结构体指针，以便操作具体字段
        EFI_MEMORY_DESCRIPTOR *desc = (EFI_MEMORY_DESCRIPTOR *)desc_ptr;

        efi_rts_start_va += efi_size;
        desc->VirtualStart = efi_rts_start_va;

        uint64 efi_pa = desc->PhysicalStart;
        efi_size = desc->NumberOfPages << PAGE_4K_SHIFT;

        uint64 flags;
        if (desc->Type == EFI_RUNTIME_SERVICES_DATA ) {
            flags = PAGE_KERNEL_DATA_RW ;

            if (grts_pa >= efi_pa && grts_pa < efi_pa + efi_size ) {
                boot_info->gRTS = (void*)efi_rts_start_va + (grts_pa-efi_pa);
            }

        }else if (desc->Type == EFI_RUNTIME_SERVICES_CODE) {
            flags = PAGE_KERNEL_CODE;
        }else if (desc->Type == EFI_MEMORY_MAPPED_IO || desc->Type == EFI_MEMORY_MAPPED_IO_PORT_SPACE) {
            flags = PAGE_KERNEL_MMIO_WUC;
        }
        vm_map_range(&kernel_space,efi_rts_start_va,efi_pa,efi_size,flags);


        // 【核心修复】：手动加上主板固件给的真实跨度，而不是靠 C 语言的数组隐式推导！
        desc_ptr += boot_info->mem_descriptor_size;
    }

    //启用临时虚拟地址0映射
    uint64 *root_table = pa_to_va(kernel_space.cr3_root);
    root_table[0] = (kernel_space.paging_level == 5) ? tmp_pml5t[0] : tmp_pml4t[0];

    boot_info->gRTS->SetVirtualAddressMap(efi_runtime_memmap.count * boot_info->mem_descriptor_size,
                                          boot_info->mem_descriptor_size, boot_info->mem_descriptor_version,
                                          (void*)efi_runtime_memmap.mem_map);
    //关闭临时映射
    root_table[0] = 0;

    //初始化后尝试获取时间信息并打印检测是否映射成功
    EFI_TIME efi_time;
    boot_info->gRTS->GetTime(&efi_time,NULL);
    color_printk(GREEN,BLACK,"UEFI Run Time Service Get Time:%d-%d-%d %d:%d:%d\n",efi_time.Year,efi_time.Month,efi_time.Day,efi_time.Hour,efi_time.Minute,efi_time.Second);
}
