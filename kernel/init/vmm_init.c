#include "vmm_init.h"
#include "printk.h"
#include "slub.h"
#include "uefi_init.h"

// UEFI 运行时服务专属虚拟地址空间 (2GB 预留)
#define UEFI_RTS_VA_START   0xFFFFFFFF00000000ULL
#define UEFI_RTS_VA_END     0xFFFFFFFF80000000ULL

/* ========================================================================== */
/*                 静态内核代码与模块区 (必须固定在顶部 2GB)                  */
/* ========================================================================== */
// -----------------------------------------------------------------------------
// 【架构师警告】：无论 4 级还是 5 级页表，内核代码段绝对不能挪动！
// 操作系统编译时通常采用 gcc -mcmodel=kernel，编译器会强制假定内核代码、
// 全局变量全部分布在虚拟地址空间最高的 2GB 内，以便使用极速的 32 位相对寻址。
// -----------------------------------------------------------------------------

// 内核主代码 (Text) 与数据区起始虚拟地址 (512 MB，紧贴在模块空间下方)
#define KERNEL_VA_START   0xFFFFFFFF80000000ULL
#define KERNEL_VA_END     0xFFFFFFFFA0000000ULL

// 动态内核模块 (KO) 加载空间 (1.5 GB，紧贴最高 2GB 往下排布)
#define MODULES_VA_START  0xFFFFFFFFA0000000ULL
#define MODULES_VA_END    0xFFFFFFFFFFFFFFFFULL

/**
 * @brief 初始化全局虚拟内存布局
 * @note  根据 CPU 是否开启 5 级分页 (LA57)，动态划分高半核地址空间。
 *        各个核心区域之间强制插入 Guard Hole (警戒空洞)，彻底阻断跨区越界访问。
 */
void vm_layout_init(void) {
    if (tmp_paging_level == 5) {
        //5级分页用户地址空间
        vm_layout.user_map_start = 0x400000;
        vm_layout.user_map_end   = 0xFFFFFFFFFFFFFF;

        // 【5 级页表模式 - LA57】(理论上限 128 PB，单位: PB)
        vm_layout.direct_map_start = 0xFF00000000000000ULL;
        vm_layout.direct_map_end   = vm_layout.direct_map_start + 0x0080000000000000ULL; // 占用 32 PB

        vm_layout.page_map_start   = vm_layout.direct_map_end + 0x0004000000000000ULL;      // Guard Hole: 1 PB
        vm_layout.page_map_end     = vm_layout.page_map_start + 0x0004000000000000ULL;   // vmemmap 占用 1 PB

        vm_layout.percpu_vaddr_start = vm_layout.page_map_end+ 0x0004000000000000ULL;      // Guard Hole: 1 PB
        vm_layout.percpu_vaddr_end   = vm_layout.percpu_vaddr_start + 0x0004000000000000ULL; //percpu占用1PB

        // 【安全隔离】：留出 1 PB 的 Guard Hole！越过直接映射区后不可立即接盘。
        vm_layout.vmalloc_start    = vm_layout.percpu_vaddr_end + 0x0004000000000000ULL;
        vm_layout.vmalloc_end      = vm_layout.vmalloc_start + 0x0040000000000000ULL;    // vmalloc 占用 16 PB

        vm_layout.io_map_start     = vm_layout.vmalloc_end + 0x0004000000000000ULL;     // Guard Hole: 1 PB
        vm_layout.io_map_end       = vm_layout.io_map_start + 0x0004000000000000ULL;     // MMIO 占用 1 PB

    } else {
        //4级分页用户地址空间
        vm_layout.user_map_start = 0x400000;
        vm_layout.user_map_end   = 0x7FFFFFFFFFFF;

        // 【4 级页表模式 - LA48】(理论上限 128 TB，单位: TB)
        vm_layout.direct_map_start = 0xFFFF800000000000ULL;
        vm_layout.direct_map_end   = vm_layout.direct_map_start + 0x0000400000000000ULL; // 占用 64 TB

        vm_layout.page_map_start   = vm_layout.direct_map_end + 0x0000010000000000ULL;      // Guard Hole: 1 TB
        vm_layout.page_map_end     = vm_layout.page_map_start + 0x0000010000000000ULL;   // vmemmap 占用 1 TB

        vm_layout.percpu_vaddr_start = vm_layout.page_map_end + 0x0000010000000000ULL;      // Guard Hole: 1 TB
        vm_layout.percpu_vaddr_end   = vm_layout.percpu_vaddr_start + 0x0000010000000000ULL; //percpu占用1TB

        // 【安全隔离】：留出 1 TB 的 Guard Hole！
        vm_layout.vmalloc_start    = vm_layout.percpu_vaddr_end  + 0x0000010000000000ULL;
        vm_layout.vmalloc_end      = vm_layout.vmalloc_start + 0x0000200000000000ULL;    // vmalloc 占用 32 TB

        vm_layout.io_map_start     = vm_layout.vmalloc_end + 0x0000010000000000ULL;     // Guard Hole: 1 TB
        vm_layout.io_map_end       = vm_layout.io_map_start + 0x0000020000000000ULL;     // MMIO 占用 2 TB

    }

    vm_layout.efi_rts_start = UEFI_RTS_VA_START;
    vm_layout.efi_rts_end = UEFI_RTS_VA_END;
    vm_layout.kernel_start = KERNEL_VA_START;
    vm_layout.kernel_end = KERNEL_VA_END;
    vm_layout.module_start = MODULES_VA_START;
    vm_layout.module_end = MODULES_VA_END;

    PR_INFO("Direct Map Start Addr:%#lx  End Addr:%#lx \n",vm_layout.direct_map_start,vm_layout.direct_map_end);
    PR_INFO("Page Map Start Addr:%#lx  End Addr:%#lx \n",vm_layout.page_map_start,vm_layout.page_map_end);
    PR_INFO("PerCpu Map Start Addr:%#lx  End Addr:%#lx \n",vm_layout.percpu_vaddr_start,vm_layout.percpu_vaddr_end);
    PR_INFO("Vmalloc Start Addr:%#lx  End Addr:%#lx \n",vm_layout.vmalloc_start,vm_layout.vmalloc_end);
    PR_INFO("IO Map Start Addr:%#lx  End Addr:%#lx \n",vm_layout.io_map_start,vm_layout.io_map_end);
    PR_INFO("UEFI RTS Start Addr:%#lx  End Addr:%#lx \n",vm_layout.efi_rts_start,vm_layout.efi_rts_end);
    PR_INFO("Kernel Start Addr:%#lx  End Addr:%#lx \n",vm_layout.kernel_start,vm_layout.kernel_end);
    PR_INFO("Modules Start Addr:%#lx  End Addr:%#lx \n",vm_layout.module_start,vm_layout.module_end);

}


// 将高半区页表指针转换为纯物理地址的宏
#define EARLY_VA_TO_PA(va) ((uint64)(va) - vm_layout.kernel_start + tmp_boot_info->KernelBase)
/*
 * 内核启动初期临时页表映射 (状态驱动版)
 * pa: 物理地址
 * size: 映射长度
 * page_table: 调用方提供的一块预分配内存，用于按需充当新的 PDPT/PML4 (大小由层级深度决定)
 */
void* tmp_directly_map(uint64 pa, uint64 size, uint64* page_table_buffer) {
    // 1GB 边界对齐：向下对齐起点，向上对齐终点
    uint64 start_pa = PAGE_1G_ALIGN_DOWN(pa);
    uint64 aligned_end = PAGE_1G_ALIGN(pa + size);
    
    // 页表分配游标
    uint64* alloc_ptr = page_table_buffer;

    // 遍历所需覆盖的每一个 1GB 物理内存块
    for (uint64 curr_pa = start_pa; curr_pa < aligned_end; curr_pa += (1ULL << 30)) {
        
        uint64 va = (uint64)pa_to_va(curr_pa);
        uint64* current_pml4 = tmp_pml4t; 
        
        // -----------------------------------------------------------
        // 阶段 1：5级分页专属路由 (按需创建 PML4 表)
        // -----------------------------------------------------------
        if (tmp_paging_level == 5) {
            uint64 pml5_idx = (va >> 48) & 0x1FF;
            
            // 状态检测：PML5 槽位是否为空？
            if (!(tmp_pml5t[pml5_idx] & 1)) { 
                for (int i = 0; i < 512; i++) alloc_ptr[i] = 0; // 清零新页表
                tmp_pml5t[pml5_idx] = EARLY_VA_TO_PA(alloc_ptr) | 0x3; // 挂载 PML4 (PRESENT | RW)
                alloc_ptr += 512;
            }
            
            // 解析出下级 PML4 的虚拟地址 (必须走直接映射区访问)
            uint64 pml4_pa = tmp_pml5t[pml5_idx] & ~0xFFFULL;
            current_pml4 = pa_to_va(pml4_pa);
        }
        
        // -----------------------------------------------------------
        // 阶段 2：4级/5级通用路由 (按需创建 PDPT 表)
        // -----------------------------------------------------------
        uint64 pml4_idx = (va >> 39) & 0x1FF;
        
        // 状态检测：PML4 槽位是否为空？
        if (!(current_pml4[pml4_idx] & 1)) {
            for (int i = 0; i < 512; i++) alloc_ptr[i] = 0; // 清零新页表
            current_pml4[pml4_idx] = EARLY_VA_TO_PA(alloc_ptr) | 0x3; // 挂载 PDPT (PRESENT | RW)
            alloc_ptr += 512;
        }
        
        // 解析出下级 PDPT 的虚拟地址
        uint64 pdpt_pa = current_pml4[pml4_idx] & ~0xFFFULL;
        uint64* current_pdpt = pa_to_va(pdpt_pa);
        
        // -----------------------------------------------------------
        // 阶段 3：终极状态检测与 1GB 大页映射
        // -----------------------------------------------------------
        uint64 pdpt_idx = (va >> 30) & 0x1FF;
        
        // 🌟 核心精简逻辑：检测 PDPT 槽位的 PRESENT 位！
        // 如果它为 1，说明 head.S 的直接映射区或其他逻辑已经映射过这 1GB 空间，直接跳过！
        if (current_pdpt[pdpt_idx] & 1) {
            continue; 
        }
        
        // 执行 1GB 物理页映射 (PRESENT=1 | RW=2 | HUGE=0x80) -> 0x83
        current_pdpt[pdpt_idx] = curr_pa | 0x83; 
    }
    
    // 强刷 TLB，确保新挂载的高位映射立刻生效
    asm volatile("mov %%cr3, %%rax; mov %%rax, %%cr3" ::: "rax", "memory");
    
    // 返回计算好的精确虚拟地址（原物理地址直接加上基址）
    return pa_to_va(pa);
}