#include "vmm.h"

// UEFI 运行时服务专属虚拟地址空间 (2GB 预留)
#define UEFI_RTS_VA_START   0xFFFFFFFF00000000ULL
#define UEFI_RTS_VA_END     0xFFFFFFFFA0000000ULL

/* ========================================================================== */
/*                 静态内核代码与模块区 (必须固定在顶部 2GB)                  */
/* ========================================================================== */
// -----------------------------------------------------------------------------
// 【架构师警告】：无论 4 级还是 5 级页表，内核代码段绝对不能挪动！
// 操作系统编译时通常采用 gcc -mcmodel=kernel，编译器会强制假定内核代码、
// 全局变量全部分布在虚拟地址空间最高的 2GB 内，以便使用极速的 32 位相对寻址。
// -----------------------------------------------------------------------------

// 动态内核模块 (KO) 加载空间 (1.5 GB，紧贴最高 2GB 往下排布)
#define MODULES_VA_START  0xFFFFFFFFA0000000ULL
#define MODULES_VA_END    0xFFFFFFFFFFFFFFFFULL

// 内核主代码 (Text) 与数据区起始虚拟地址 (512 MB，紧贴在模块空间下方)
#define KERNEL_VA_START   0xFFFFFFFF80000000ULL
#define KERNEL_VA_END     0xFFFFFFFFA0000000ULL

/**
 * @brief 初始化全局虚拟内存布局
 * @note  根据 CPU 是否开启 5 级分页 (LA57)，动态划分高半核地址空间。
 *        各个核心区域之间强制插入 Guard Hole (警戒空洞)，彻底阻断跨区越界访问。
 */
void vm_layout_init(void) {
    if (tmp_paging_level == 5) {
        // 【5 级页表模式 - LA57】(理论上限 128 PB，单位: PB)
        vm_layout.direct_map_start = 0xFF00000000000000ULL;
        vm_layout.direct_map_end   = vm_layout.direct_map_start + 0x0080000000000000ULL; // 占用 32 PB

        // 【安全隔离】：留出 1 PB 的 Guard Hole！越过直接映射区后不可立即接盘。
        vm_layout.vmalloc_start    = vm_layout.direct_map_end + 0x0004000000000000ULL;
        vm_layout.vmalloc_end      = vm_layout.vmalloc_start + 0x0040000000000000ULL;    // vmalloc 占用 16 PB

        vm_layout.page_map_start   = vm_layout.vmalloc_end + 0x0004000000000000ULL;      // Guard Hole: 1 PB
        vm_layout.page_map_end     = vm_layout.page_map_start + 0x0004000000000000ULL;   // vmemmap 占用 1 PB

        vm_layout.io_map_start     = vm_layout.page_map_end + 0x0004000000000000ULL;     // Guard Hole: 1 PB
        vm_layout.io_map_end       = vm_layout.io_map_start + 0x0004000000000000ULL;     // MMIO 占用 1 PB
    } else {
        // 【4 级页表模式 - LA48】(理论上限 128 TB，单位: TB)
        vm_layout.direct_map_start = 0xFFFF800000000000ULL;
        vm_layout.direct_map_end   = vm_layout.direct_map_start + 0x0000400000000000ULL; // 占用 64 TB

        // 【安全隔离】：留出 1 TB 的 Guard Hole！
        vm_layout.vmalloc_start    = vm_layout.direct_map_end + 0x0000010000000000ULL;
        vm_layout.vmalloc_end      = vm_layout.vmalloc_start + 0x0000200000000000ULL;    // vmalloc 占用 32 TB

        vm_layout.page_map_start   = vm_layout.vmalloc_end + 0x0000010000000000ULL;      // Guard Hole: 1 TB
        vm_layout.page_map_end     = vm_layout.page_map_start + 0x0000010000000000ULL;   // vmemmap 占用 1 TB

        vm_layout.io_map_start     = vm_layout.page_map_end + 0x0000010000000000ULL;     // Guard Hole: 1 TB
        vm_layout.io_map_end       = vm_layout.io_map_start + 0x0000020000000000ULL;     // MMIO 占用 2 TB
    }

    vm_layout.efi_rts_start = UEFI_RTS_VA_START;
    vm_layout.efi_rts_end = UEFI_RTS_VA_END;
    vm_layout.kernel_start = KERNEL_VA_START;
    vm_layout.kernel_end = KERNEL_VA_END;
    vm_layout.module_start = MODULES_VA_START;
    vm_layout.module_end = MODULES_VA_END;

    PR_INFO("Direct Map Start Addr:%#lx  End Addr:%#lx \n",vm_layout.direct_map_start,vm_layout.direct_map_end);
    PR_INFO("Vmalloc Start Addr:%#lx  End Addr:%#lx \n",vm_layout.vmalloc_start,vm_layout.vmalloc_end);
    PR_INFO("Page Map Start Addr:%#lx  End Addr:%#lx \n",vm_layout.page_map_start,vm_layout.page_map_end);
    PR_INFO("IO Map Start Addr:%#lx  End Addr:%#lx \n",vm_layout.io_map_start,vm_layout.io_map_end);
    PR_INFO("UEFI RTS Start Addr:%#lx  End Addr:%#lx \n",vm_layout.efi_rts_start,vm_layout.efi_rts_end);
    PR_INFO("Kernel Start Addr:%#lx  End Addr:%#lx \n",vm_layout.kernel_start,vm_layout.kernel_end);
    PR_INFO("Modules Start Addr:%#lx  End Addr:%#lx \n",vm_layout.module_start,vm_layout.module_end);
}