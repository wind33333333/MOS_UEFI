#include "kpt_init.h"
#include "../include/vmm.h"
#include "memblock_init.h"
#include "pmm.h"
#include "printk.h"
#include "slub.h"
#include "../mm/tlb.h"
#include "video_init.h"

vm_space_t kernel_space;


// =========================================================================
// 建立高半区直接映射区 (Direct Map)
// =========================================================================
static inline void direct_map_init(void) {
    // 🌟 降维打击：直接遍历 phys_map 全景视图。
    // 在 memblock_init 中， Loader 造成的碎片已经被自动缝合成完美的大块，TLB 火力全开！
    // 且 NVS 和 Runtime 已被提前剔除，物理级免疫越权访问。
    for (uint32 i = 0; i < memblock.phys_map.count; i++) {
        uint64 pbase = memblock.phys_map.region[i].base;
        uint64 size    = memblock.phys_map.region[i].size;
        vm_map_range(&kernel_space, (uint64)pa_to_va(pbase), pbase, size, SW_FLAG_MAX_1G | PAGE_KERNEL_DATA_RW);
    }
}

// =========================================================================
// 总装：页表初始化入口
// =========================================================================
void kpage_table_init(void) {
    kernel_space.cr3_root = memblock_zalloc(PAGE_4K_SIZE, PAGE_4K_SIZE);
    kernel_space.paging_level = tmp_paging_level;

    tmp_video_mem_map(); // 临时显存映射

    direct_map_init();   // 映射所有有效 RAM 到高半区 (使用 1GB/2MB 大页)

    // ---------------------------------------------------------------------
    // 映射内核 ELF 映像区 (精准控制 W^X 权限)
    // ---------------------------------------------------------------------
    //init_text段
    uint64 init_text_pa = (uint64)tmp_boot_info->KernelBase;
    uint64 init_text_sz = (uint64)_end_init_text - (uint64)_start_init_text;
    vm_map_range(&kernel_space, (uint64)_start_init_text, init_text_pa, init_text_sz, PAGE_KERNEL_CODE); // RX

    //init_rodata段
    uint64 init_rodata_pa = (uint64)tmp_boot_info->KernelBase + ((uint64)_start_init_rodata - (uint64)_start_init_text);
    uint64 init_rodata_sz = (uint64)_end_init_rodata - (uint64)_start_init_rodata;
    vm_map_range(&kernel_space, (uint64)_start_init_rodata, init_rodata_pa, init_rodata_sz, PAGE_KERNEL_DATA_RO); // R

    //init_data 和 bss段
    uint64 init_data_pa = (uint64)tmp_boot_info->KernelBase + ((uint64)_start_init_data - (uint64)_start_init_text);
    uint64 init_data_sz = (uint64)_end_init_bss - (uint64)_start_init_data;
    vm_map_range(&kernel_space, (uint64)_start_init_data, init_data_pa, init_data_sz, PAGE_KERNEL_DATA_RW); // RW

    //正式text
    uint64 text_pa = (uint64)tmp_boot_info->KernelBase + ((uint64)_start_text - (uint64)_start_init_text);
    uint64 text_sz = (uint64)_end_text - (uint64)_start_text;
    vm_map_range(&kernel_space, (uint64)_start_text, text_pa, text_sz, PAGE_KERNEL_CODE); // RX

    //rodata
    uint64 rodata_pa = (uint64)tmp_boot_info->KernelBase + ((uint64)_start_rodata - (uint64)_start_init_text);
    uint64 rodata_sz = (uint64)_end_rodata - (uint64)_start_rodata;
    vm_map_range(&kernel_space, (uint64)_start_rodata, rodata_pa, rodata_sz, PAGE_KERNEL_DATA_RO); // R

    //data 和 bss
    uint64 data_pa = (uint64)tmp_boot_info->KernelBase + ((uint64)_start_data - (uint64)_start_init_text);
    uint64 data_sz = (uint64)_end_bss - (uint64)_start_data;
    vm_map_range(&kernel_space, (uint64)_start_data, data_pa, data_sz, PAGE_KERNEL_DATA_RW); // RW

    // 载入新世界！
    asm_set_cr3(kernel_space.cr3_root);
    tlb_flush_all();

    PR_OK("Kernel page table switch successful, CR3:%lx \n", kernel_space.cr3_root);
}