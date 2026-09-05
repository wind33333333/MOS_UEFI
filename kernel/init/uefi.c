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
    //用一个局部变量暂存 gRTS 的新虚拟地址，先不要覆盖原变量
    uint64 new_grts_va = 0;

    for (uint32 i = 0; i < efi_runtime_memmap.count; i++) {
        EFI_MEMORY_DESCRIPTOR *desc = (EFI_MEMORY_DESCRIPTOR *)desc_ptr;

        efi_rts_start_va += efi_size;
        desc->VirtualStart = efi_rts_start_va;

        uint64 efi_pa = desc->PhysicalStart;
        efi_size = desc->NumberOfPages << PAGE_4K_SHIFT;

        uint64 flags;
        // 完整的 if-else 链，保证 flags 绝对不会是随机野值！
        if (desc->Type == EFI_RUNTIME_SERVICES_CODE) {
            flags = PAGE_KERNEL_CODE;
        } else if (desc->Type == EFI_MEMORY_MAPPED_IO || desc->Type == EFI_MEMORY_MAPPED_IO_PORT_SPACE) {
            flags = PAGE_KERNEL_MMIO_WUC;
        } else {
            // 兜底分支：DATA 以及所有其他带有 Runtime 属性的奇葩内存类型，统统当数据段映射
            flags = PAGE_KERNEL_DATA_RW;

            // 嗅探 gRTS 表的新地址
            if (grts_pa >= efi_pa && grts_pa < efi_pa + efi_size) {
                // 【修复3-2】：纯整形计算，避免 void* 指针运算
                new_grts_va = efi_rts_start_va + (grts_pa - efi_pa);
            }
        }

        vm_map_range(&kernel_space, efi_rts_start_va, efi_pa, efi_size, flags);

        // 手动加上主板固件给的真实跨度
        desc_ptr += boot_info->mem_descriptor_size;
    }

    // 启用临时虚拟地址 0 映射 (你的神来之笔！)
    uint64 *root_table = pa_to_va(kernel_space.cr3_root);
    root_table[0] = (kernel_space.paging_level == 5) ? tmp_pml5t[0] : tmp_pml4t[0];

    // 此时调用 SVAM，依然使用的是物理层面的 gRTS 指针（因为 boot_info->gRTS 还没被我们改掉）
    boot_info->gRTS->SetVirtualAddressMap(efi_runtime_memmap.count * boot_info->mem_descriptor_size,
                                          boot_info->mem_descriptor_size, boot_info->mem_descriptor_version,
                                          (void*)efi_runtime_memmap.mem_map);

    // 关闭临时映射
    root_table[0] = 0;

    // 直接重新加载一遍 CR3 寄存器即可清空全局 TLB
    asm_set_cr3(kernel_space.cr3_root);

    // 【修复3-3】：SVAM 安全执行完毕后，操作系统正式将指针切换为高位虚拟地址！
    if (new_grts_va != 0) {
        boot_info->gRTS = (EFI_RUNTIME_SERVICES *)new_grts_va;
    } else {
        color_printk(RED, BLACK, "PANIC: gRTS table lost!\n");
        while(1);
    }

    // 初始化后尝试获取时间信息并打印检测是否映射成功
    EFI_TIME efi_time;
    boot_info->gRTS->GetTime(&efi_time, NULL);
    color_printk(GREEN, BLACK, "UEFI Run Time Service Get Time: %d-%d-%d %d:%d:%d\n",
                 efi_time.Year, efi_time.Month, efi_time.Day,
                 efi_time.Hour, efi_time.Minute, efi_time.Second);
}
