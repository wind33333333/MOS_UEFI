#include "kpt_init.h"
#include "slub.h"
#include "memblock_init.h"
#include "printk.h"
#include "../include/vmm.h"
#include "pmm.h"
#include "../mm/tlb.h"
#include "video_init.h"
#include "../x64/msr.h"

vm_space_t kernel_space;

// =========================================================================
// 核心映射器：支持巨页 / 大页自动对齐与回落映射
// =========================================================================
static void __map_large_pages(uint64 va_start, uint64 pa_start, uint64 size) {
    uint64 va_curr = va_start;
    uint64 pa_curr = pa_start;
    uint64 va_end  = va_start + size;

    while (va_curr < va_end) {
        uint64 remaining = va_end - va_curr;
        uint64 step_size;
        uint64 map_flags = PAGE_KERNEL_DATA_RW;

        // 智能嗅探：VA 和 PA 必须同时按 1GB 对齐，且剩余空间足够
        if ((va_curr & PAGE_1G_OFFSET_MASK) == 0 &&
            (pa_curr & PAGE_1G_OFFSET_MASK) == 0 &&
            remaining >= PAGE_1G_SIZE) {

            step_size = PAGE_1G_SIZE;
            map_flags |= SW_FLAG_MAX_1G;
        }
        // 退而求其次：2MB 大页对齐
        else if ((va_curr & PAGE_2M_OFFSET_MASK) == 0 &&
                 (pa_curr & PAGE_2M_OFFSET_MASK) == 0 &&
                 remaining >= PAGE_2M_SIZE) {

            step_size = PAGE_2M_SIZE;
            map_flags |= SW_FLAG_MAX_2M;
        }
        // 碎片页：4KB 映射
        else {
            step_size = PAGE_4K_SIZE;
        }

        vm_map_range(&kernel_space, va_curr, pa_curr, step_size, map_flags);

        va_curr += step_size;
        pa_curr += step_size;
    }
}

// =========================================================================
// 阶段一：建立高半区直接映射区 (Direct Map)
// =========================================================================
static void direct_map_init(void) {
    // 1. 映射所有纯净空闲 RAM
    for (uint32 i = 0; i < memblock.memory.count; i++) {
        uint64 p_start = memblock.memory.region[i].base;
        uint64 size    = memblock.memory.region[i].size;
        __map_large_pages((uint64)pa_to_va(p_start), p_start, size);
    }

    // 2. 映射受保护的保留区 (剔除绝地隔离的 NVS)
    for (uint32 i = 0; i < memblock.reserved.count; i++) {
        uint32 type = memblock.reserved.region[i].type;
        uint64 p_start = memblock.reserved.region[i].base;
        uint64 size    = memblock.reserved.region[i].size;

        // 🚨 剔除：NVS (防踩踏) 和 UEFI Runtime (后期独立映射 RX/RW)
        if (type != EFI_ACPI_MEMORY_NVS &&
            type != EFI_RUNTIME_SERVICES_CODE &&
            type != EFI_RUNTIME_SERVICES_DATA) {
            __map_large_pages((uint64)pa_to_va(p_start), p_start, size);
        }
    }
}

// =========================================================================
// 阶段二：建立 struct page 映射区 (Vmemmap) - 🌟 NUMA 感知
// =========================================================================
static void page_map_init(void) {
    // 提取通用挂载逻辑，支持 NUMA 亲和性分配
    auto __mount_vmemmap = [](uint64 p_start, uint64 size, uint8 target_nid) {
        uint64 p_end = p_start + size;
        uint64 va_start = (uint64)pa_to_page(p_start);
        uint64 va_end   = (uint64)pa_to_page(p_end);

        // 向上、向下按 2MB 边界对齐 (因为 struct page 数组体积庞大，强制用 2M 大页最合理)
        va_start = PAGE_2M_ALIGN_DOWN(va_start);
        va_end   = PAGE_2M_ALIGN(va_end);

        uint64 va_curr = va_start;
        while (va_curr < va_end) {
            // 检查该 2MB 虚拟块是否已经被映射过 (防止两个紧邻的物理块对应的 struct page 落在同一个 2M 大页里被重复分配)
            if (!vm_is_mapped(&kernel_space, va_curr)) {
                // 🌟 NUMA 亲和力分配：在物理页所在的 Node 上，分配它的 struct page！
                uint64 vmemmap_pa = memblock_alloc_node(PAGE_2M_SIZE, PAGE_2M_SIZE, target_nid);
                if (!vmemmap_pa) {
                    PR_ERROR("OOM allocating vmemmap for Node %d!\n", target_nid);
                    while(1);
                }

                // 映射这个 2MB 的大砖块
                vm_map_range(&kernel_space, va_curr, vmemmap_pa, PAGE_2M_SIZE, PAGE_KERNEL_DATA_RW | SW_FLAG_MAX_2M);
            }
            va_curr += PAGE_2M_SIZE;
        }
    };

    // 1. 给纯净 RAM 挂载 struct page
    for (uint32 i = 0; i < memblock.memory.count; i++) {
        __mount_vmemmap(memblock.memory.region[i].base,
                        memblock.memory.region[i].size,
                        memblock.memory.region[i].nid);
    }

    // 2. 给特定的 reserved 资产挂载 (只有未来会被释放的 Loader 资产才需要)
    for (uint32 i = 0; i < memblock.reserved.count; i++) {
        uint32 type = memblock.reserved.region[i].type;
        // 只有 Loader 资产会在后期归还伙伴系统
        if (type == EFI_LOADER_CODE || type == EFI_LOADER_DATA ||
            type == EFI_BOOT_SERVICES_CODE || type == EFI_BOOT_SERVICES_DATA) {

            __mount_vmemmap(memblock.reserved.region[i].base,
                            memblock.reserved.region[i].size,
                            memblock.reserved.region[i].nid);
        }
    }
}

// =========================================================================
// 总装：页表初始化入口
// =========================================================================
void kpage_table_init(void) {
    // 强制全局分配根页表，对齐 4KB
    kernel_space.cr3_root = memblock_alloc_node(PAGE_4K_SIZE, PAGE_4K_SIZE, NUMA_NO_NODE);
    kernel_space.paging_level = tmp_paging_level;

    // 1. 临时显存对等映射 (通常是 0xB8000 或者是 UEFI Framebuffer)
    tmp_video_mem_map();

    // 2. 直接映射区 (Direct Map)
    direct_map_init();

    // 3. struct page 映射区 (Vmemmap)
    page_map_init();

    // 4. 映射内核映像 (Kernel Image - 严格权限控制)
    // .init_text (用完即抛)
    uint64 init_text_va = (uint64)_start_init_text;
    uint64 init_text_pa = init_text_va - vm_layout.kernel_start;
    uint64 init_text_sz = (uint64)_end_init_text - init_text_va;
    vm_map_range(&kernel_space, init_text_va, init_text_pa, init_text_sz, PAGE_KERNEL_CODE);

    // .init_rodata
    uint64 init_rodata_va = (uint64)_start_init_rodata;
    uint64 init_rodata_pa = init_rodata_va - vm_layout.kernel_start;
    uint64 init_rodata_sz = (uint64)_end_init_rodata - init_rodata_va;
    vm_map_range(&kernel_space, init_rodata_va, init_rodata_pa, init_rodata_sz, PAGE_KERNEL_DATA_RO);

    // .init_data
    uint64 init_data_va = (uint64)_start_init_data;
    uint64 init_data_pa = init_data_va - vm_layout.kernel_start;
    uint64 init_data_sz = (uint64)_end_init_bss - init_data_va;
    vm_map_range(&kernel_space, init_data_va, init_data_pa, init_data_sz, PAGE_KERNEL_DATA_RW);

    // .text (正式代码，RX)
    uint64 text_va = (uint64)_start_text;
    uint64 text_pa = text_va - vm_layout.kernel_start;
    uint64 text_sz = (uint64)_end_text - text_va;
    vm_map_range(&kernel_space, text_va, text_pa, text_sz, PAGE_KERNEL_CODE);

    // .rodata (只读数据)
    uint64 rodata_va = (uint64)_start_rodata;
    uint64 rodata_pa = rodata_va - vm_layout.kernel_start;
    uint64 rodata_sz = (uint64)_end_rodata - rodata_va;
    vm_map_range(&kernel_space, rodata_va, rodata_pa, rodata_sz, PAGE_KERNEL_DATA_RO);

    // .data .bss (可读写)
    uint64 data_va = (uint64)_start_data;
    uint64 data_pa = data_va - vm_layout.kernel_start;
    uint64 data_sz = (uint64)_end_bss - data_va;
    vm_map_range(&kernel_space, data_va, data_pa, data_sz, PAGE_KERNEL_DATA_RW);

    // 5. 切换 CR3 并刷新 TLB
    asm_set_cr3(kernel_space.cr3_root);
    tlb_flush_all();

    PR_OK("Kernel page table switch successful, %d level paging enabled, CR3:%lx \n", kernel_space.paging_level, kernel_space.cr3_root);
}