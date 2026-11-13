#include "kpt_init.h"
#include "../include/vmm.h"
#include "memblock_init.h"
#include "pmm.h"
#include "printk.h"
#include "slub.h"
#include "../mm/tlb.h"
#include "video_init.h"

extern vm_space_t kernel_space;


// =========================================================================
// 阶段一：建立高半区直接映射区 (Direct Map)
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
// 阶段二：建立 struct page 映射区 (Vmemmap) - 🌟 智能三档变速 (1G / 2M / 4K)
// =========================================================================
static inline void mount_vmemmap_region(uint64 p_start, uint64 size, uint8 target_nid) {
    uint64 p_end = p_start + size;
    uint64 va_start = (uint64)pa_to_page(p_start);
    uint64 va_end   = (uint64)pa_to_page(p_end);

    // 保持 4KB 边界对齐，确保 0 碎片浪费
    va_start = PAGE_4K_ALIGN_DOWN(va_start);
    va_end   = PAGE_4K_ALIGN(va_end);

    uint64 va_curr = va_start;
    while (va_curr < va_end) {
        uint64 remaining = va_end - va_curr;
        uint64 step_size;
        uint64 map_flags = PAGE_KERNEL_DATA_RW;

        // 🌟 1档嗅探：针对 TB 级服务器，尝试 1GB 巨页映射
        // 条件：虚拟地址 1GB 对齐，且剩余元数据需求 >= 1GB (对应 64GB 的连续物理内存)
        if ((va_curr & PAGE_1G_OFFSET_MASK) == 0 && remaining >= PAGE_1G_SIZE) {

            // 尝试申请 1GB 的连续物理页来存放 struct page
            uint64 vmemmap_pa = memblock_alloc_node(PAGE_1G_SIZE, PAGE_1G_SIZE, target_nid);

            // 只有成功申请到 1GB 物理连续空间，才使用 1GB 映射档位
            if (vmemmap_pa) {
                step_size = PAGE_1G_SIZE;
                map_flags |= SW_FLAG_MAX_1G;
                goto do_map;
            }
            // 如果申请失败 (早期物理内存不够连续)，降级尝试 2MB 档位
        }

        // 🌟 2档嗅探：尝试 2MB 大页映射
        // 条件：虚拟地址 2MB 对齐，且剩余元数据需求 >= 2MB (对应 128MB 的连续物理内存)
        if ((va_curr & PAGE_2M_OFFSET_MASK) == 0 && remaining >= PAGE_2M_SIZE) {
            uint64 vmemmap_pa = memblock_alloc_node(PAGE_2M_SIZE, PAGE_2M_SIZE, target_nid);
            if (vmemmap_pa) {
                step_size = PAGE_2M_SIZE;
                map_flags |= SW_FLAG_MAX_2M;
                goto do_map;
            }
        }

        // 🌟 3档兜底：4KB 碎片映射 (如前 4GB 的零散内存)
        step_size = PAGE_4K_SIZE;
        uint64 vmemmap_pa = memblock_alloc_node(PAGE_4K_SIZE, PAGE_4K_SIZE, target_nid);
        if (!vmemmap_pa) {
            PR_ERROR("OOM allocating vmemmap (4KB)! Node %d\n", target_nid);
            while(1) asm_hlt();
        }

do_map:
        // 清洗元数据内存
        asm_mem_set(pa_to_va(vmemmap_pa), 0, step_size);

        // 精准映射
        vm_map_range(&kernel_space, va_curr, vmemmap_pa, step_size, map_flags);

        va_curr += step_size;
    }
}

static inline void vmemmap_init(void) {
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
    vmemmap_init();     // 映射 struct page 全局数组 (绑定 NUMA 本地内存)

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