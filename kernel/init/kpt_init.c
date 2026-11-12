#include "kpt_init.h"
#include "../include/vmm.h"
#include "memblock_init.h"
#include "pmm.h"
#include "printk.h"
#include "slub.h"
#include "tlb.h"
#include "video_init.h"

extern vm_space_t kernel_space;

// =========================================================================
// 核心映射器：流式执行，智能挂载 1GB / 2MB / 4KB 页
// =========================================================================
static void __map_large_pages(uint64 va_start, uint64 pa_start, uint64 size) {
    uint64 va_curr = va_start;
    uint64 pa_curr = pa_start;
    uint64 va_end  = va_start + size;

    while (va_curr < va_end) {
        uint64 remaining = va_end - va_curr;
        uint64 step_size;
        uint64 map_flags = PAGE_KERNEL_DATA_RW;

        // 1GB 巨页对齐检测
        if ((va_curr & PAGE_1G_OFFSET_MASK) == 0 &&
            (pa_curr & PAGE_1G_OFFSET_MASK) == 0 && remaining >= PAGE_1G_SIZE) {
            step_size = PAGE_1G_SIZE;
            map_flags |= SW_FLAG_MAX_1G;
        }
        // 2MB 大页对齐检测
        else if ((va_curr & PAGE_2M_OFFSET_MASK) == 0 &&
                 (pa_curr & PAGE_2M_OFFSET_MASK) == 0 && remaining >= PAGE_2M_SIZE) {
            step_size = PAGE_2M_SIZE;
            map_flags |= SW_FLAG_MAX_2M;
        }
        // 4KB 兜底映射
        else {
            step_size = PAGE_4K_SIZE;
        }

        // 呼叫底层 VMM 铺砖
        vm_map_range(&kernel_space, va_curr, pa_curr, step_size, map_flags);

        va_curr += step_size;
        pa_curr += step_size;
    }
}

// =========================================================================
// 阶段一：建立高半区直接映射区 (Direct Map)
// =========================================================================
static void direct_map_init(void) {
    // 🌟 降维打击：直接遍历 phys_map 全景视图。
    // 在 memblock_init 中， Loader 造成的碎片已经被自动缝合成完美的大块，TLB 火力全开！
    // 且 NVS 和 Runtime 已被提前剔除，物理级免疫越权访问。
    for (uint32 i = 0; i < memblock.phys_map.count; i++) {
        uint64 p_start = memblock.phys_map.region[i].base;
        uint64 size    = memblock.phys_map.region[i].size;
        __map_large_pages((uint64)pa_to_va(p_start), p_start, size);
    }
}

// =========================================================================
// 阶段二：建立 struct page 映射区 (Vmemmap) - 🌟 NUMA 感知
// =========================================================================
static void mount_vmemmap_region(uint64 p_start, uint64 size, uint8 target_nid) {
    uint64 p_end = p_start + size;
    uint64 va_start = (uint64)pa_to_page(p_start);
    uint64 va_end   = (uint64)pa_to_page(p_end);

    // Vmemmap 容量庞大，向上向下严格按 2MB 边界对齐映射
    va_start = PAGE_2M_ALIGN_DOWN(va_start);
    va_end   = PAGE_2M_ALIGN(va_end);

    uint64 va_curr = va_start;
    while (va_curr < va_end) {
        // 防止两个相邻物理块映射同一片 2MB 虚拟重叠区
        if (!vm_is_mapped(&kernel_space, va_curr)) {
            // 🌟 核心：NUMA 亲和力！强制在目标 Node 节点上分配属于它的 struct page 内存。
            uint64 vmemmap_pa = memblock_alloc_node(PAGE_2M_SIZE, PAGE_2M_SIZE, target_nid);
            if (!vmemmap_pa) {
                PR_ERROR("OOM allocating vmemmap for Node %d!\n", target_nid);
                while(1) asm_hlt();
            }
            // 映射 2MB 的页表砖块
            vm_map_range(&kernel_space, va_curr, vmemmap_pa, PAGE_2M_SIZE, PAGE_KERNEL_DATA_RW | SW_FLAG_MAX_2M);
        }
        va_curr += PAGE_2M_SIZE;
    }
}

static void page_map_init(void) {
    // 🌟 全景映射：遍历 phys_map，为所有合法 RAM (含 ACPI Reclaim) 提供 page 元数据基础
    for (uint32 i = 0; i < memblock.phys_map.count; i++) {
        mount_vmemmap_region(memblock.phys_map.region[i].base,
                             memblock.phys_map.region[i].size,
                             memblock.phys_map.region[i].nid);
    }
}

// =========================================================================
// 总装：页表初始化入口
// =========================================================================
void kpage_table_init(void) {
    kernel_space.cr3_root = memblock_alloc_node(PAGE_4K_SIZE, PAGE_4K_SIZE, NUMA_NO_NODE);
    kernel_space.paging_level = tmp_paging_level;

    tmp_video_mem_map(); // 临时显存映射

    direct_map_init();   // 映射所有有效 RAM 到高半区 (使用 1GB/2MB 大页)
    page_map_init();     // 映射 struct page 全局数组 (绑定 NUMA 本地内存)

    // ---------------------------------------------------------------------
    // 映射内核 ELF 映像区 (精准控制 W^X 权限)
    // ---------------------------------------------------------------------
    uint64 init_text_va = (uint64)_start_init_text;
    uint64 init_text_pa = init_text_va - vm_layout.kernel_start;
    uint64 init_text_sz = (uint64)_end_init_text - init_text_va;
    vm_map_range(&kernel_space, init_text_va, init_text_pa, init_text_sz, PAGE_KERNEL_CODE); // RX

    uint64 init_rodata_va = (uint64)_start_init_rodata;
    uint64 init_rodata_pa = init_rodata_va - vm_layout.kernel_start;
    uint64 init_rodata_sz = (uint64)_end_init_rodata - init_rodata_va;
    vm_map_range(&kernel_space, init_rodata_va, init_rodata_pa, init_rodata_sz, PAGE_KERNEL_DATA_RO); // R

    uint64 init_data_va = (uint64)_start_init_data;
    uint64 init_data_pa = init_data_va - vm_layout.kernel_start;
    uint64 init_data_sz = (uint64)_end_init_bss - init_data_va;
    vm_map_range(&kernel_space, init_data_va, init_data_pa, init_data_sz, PAGE_KERNEL_DATA_RW); // RW

    uint64 text_va = (uint64)_start_text;
    uint64 text_pa = text_va - vm_layout.kernel_start;
    uint64 text_sz = (uint64)_end_text - text_va;
    vm_map_range(&kernel_space, text_va, text_pa, text_sz, PAGE_KERNEL_CODE); // RX

    uint64 rodata_va = (uint64)_start_rodata;
    uint64 rodata_pa = rodata_va - vm_layout.kernel_start;
    uint64 rodata_sz = (uint64)_end_rodata - rodata_va;
    vm_map_range(&kernel_space, rodata_va, rodata_pa, rodata_sz, PAGE_KERNEL_DATA_RO); // R

    uint64 data_va = (uint64)_start_data;
    uint64 data_pa = data_va - vm_layout.kernel_start;
    uint64 data_sz = (uint64)_end_bss - data_va;
    vm_map_range(&kernel_space, data_va, data_pa, data_sz, PAGE_KERNEL_DATA_RW); // RW

    // 载入新世界！
    asm_set_cr3(kernel_space.cr3_root);
    tlb_flush_all();

    PR_OK("Kernel page table switch successful, CR3:%lx \n", kernel_space.cr3_root);
}