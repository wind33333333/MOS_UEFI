#pragma once

#include "moslib.h"
#include "../init/linkage.h"


//直接映射区起始地址 覆盖64TB物理内存地址空间
#define DIRECT_MAP_VA_START 0xFFFF800000000000UL
#define DIRECT_MAP_VA_END   0xFFFF8FFFFFFFFFFFUL

//60TB vmalloc映射区
#define VMALLOC_VA_START 0xFFFFC00000000000UL
#define VMALLOC_VA_END   0xFFFFFBFFFFFFFFFFUL

//page稀疏映射区,每个page记录一个4K物理页状态， 0x10000000000 / 40 * 4096 = 64TB ,覆盖64TB物理内存
#define PAGE_MAP_VA_START 0xFFFFFC0000000000UL
#define PAGE_MAP_VA_END   0xFFFFFCFFFFFFFFFFUL

//3070GB IO/UEFI/ACPI/APIC等映射区
#define IO_MAP_VA_START 0xFFFFFD0000000000UL
#define IO_MAP_VA_END   0xFFFFFFFF7FFFFFFFUL

//内核起始虚拟地址
#define KERNEL_VA_START 0xFFFFFFFF80000000UL
#define KERNEL_VA_END   0xFFFFFFFF8FFFFFFFUL

//动态模块空间1536MB
#define MODULES_VA_START 0xFFFFFFFFA0000000UL
#define MODULES_VA_END   0xFFFFFFFFFFFFFFFFUL


//页表项物理地址掩码
#define PAGE_PA_MASK    0x7FFFFFFFF000UL

//4K页表
#define PAGE_4K_SHIFT    12
#define PAGE_4K_SIZE    (1UL << PAGE_4K_SHIFT)
#define PAGE_4K_MASK    (~(PAGE_4K_SIZE - 1))
#define PAGE_4K_ALIGN(ADDR)    (((uint64)(ADDR) + PAGE_4K_SIZE - 1) & PAGE_4K_MASK)

//2M页表
#define PAGE_2M_SHIFT    21
#define PAGE_2M_SIZE    (1UL << PAGE_2M_SHIFT)
#define PAGE_2M_MASK    (~(PAGE_2M_SIZE - 1))
#define PAGE_2M_ALIGN(ADDR)     (((uint64)(ADDR) + PAGE_2M_SIZE - 1) & PAGE_2M_MASK)

//1G页表
#define PAGE_1G_SHIFT    30
#define PAGE_1G_SIZE    (1UL << PAGE_1G_SHIFT)
#define PAGE_1G_MASK    (~(PAGE_1G_SIZE - 1))
#define PAGE_1G_ALIGN(ADDR)     (((uint64)(ADDR) + PAGE_1G_SIZE - 1) & PAGE_1G_MASK)

//页属性
#define PAGE_NX     (1UL<<63)     // No-Execute (不可执行防漏洞，现代安全基石)
#define PAGE_G      (1UL<<8)      // Global (全局页，切换 CR3 时不刷新该 TLB)
#define PAGE_PS     (1UL<<7)      // Page Size (用于 PDE/PDPTE，1代表启用 2MB/1GB 巨页)
#define PAGE_PAT    (1UL<<7)      // Page Attribute Table (PAT 位，改变缓存规则)
#define PAGE_HUGE_PAT (1UL<<12)   // 2M 1G大页的PAT位移动到了12位
#define PAGE_D      (1UL<<6)      // Dirty (脏位，CPU写数据时自动置1)
#define PAGE_A      (1UL<<5)      // Accessed (访问位，CPU硬件自动置1)
#define PAGE_PCD    (1UL<<4)      // Page-Level Cache Disable (配合 PAT 使用)
#define PAGE_PWT    (1UL<<3)      // Page-Level Write-Through (配合 PAT 使用)
#define PAGE_US     (1UL<<2)      // User/Supervisor (用户态/内核态，1为允许Ring3访问)
#define PAGE_RW     (1UL<<1)      // Read/Write (读写权限，0为只读)
#define PAGE_P      (1UL<<0)      // Present (存在位)

// 基础缓存模式组合 
#define CACHE_WB    0                           // 回写 (普通内存)
#define CACHE_WC    (PAGE_PAT | PAGE_PCD)       // 写合并 (WC)
#define CACHE_WUC   (PAGE_PCD)                  // 弱不可缓存 (UC-)
#define CACHE_UC    (PAGE_PCD | PAGE_PWT)       // 强不可缓存 (Strong UC)

// --- 内核态 (Ring 0) 常用属性 ---
#define PAGE_KERNEL_RW      (PAGE_G | PAGE_NX | PAGE_RW | PAGE_P | CACHE_WB) // 1. 普通内核数据/堆栈
#define PAGE_KERNEL_RX      (PAGE_G | PAGE_RW | PAGE_P | CACHE_WB)           // 内核代码段 (无NX)
#define PAGE_KERNEL_RO      (PAGE_G | PAGE_NX | PAGE_P | CACHE_WB)           // 内核只读数据
#define PAGE_KERNEL_WUC     (PAGE_G | PAGE_NX | PAGE_RW | PAGE_P | CACHE_WUC) // 2. 普通外设 IO (顺从 MTRR，安全兜底)
#define PAGE_KERNEL_WC      (PAGE_G | PAGE_NX | PAGE_RW | PAGE_P | CACHE_WC) // 4. 高吞吐 IO (强行聚合并发)
#define PAGE_KERNEL_UC      (PAGE_G | PAGE_NX | PAGE_RW | PAGE_P | CACHE_UC) // 3. 暴力外设 IO (无视 MTRR，六亲不认的强制直达！)

// --- 用户态 (Ring 3) 常用属性 ---
#define PAGE_USER_RW          (PAGE_NX | PAGE_US | PAGE_RW | PAGE_P | CACHE_WB)    // 用户堆栈/数据
#define PAGE_USER_RX        (PAGE_US | PAGE_P | CACHE_WB)                        // 用户代码段
#define PAGE_USER_RO        (PAGE_NX | PAGE_US | PAGE_P | CACHE_WB)              // 用户只读数据

#define PML4E_SHIFT 39  // PML4E 索引的位移量
#define PDPTE_SHIFT 30  // PDPTE 索引的位移量
#define PDE_SHIFT 21    // PDE 索引的位移量
#define PTE_SHIFT 12    // PTE 索引的位移量

// 缺页异常硬件错误码解析宏 (来源于 CPU 压入栈的 Error Code)
#define PF_ERR_PRESENT  (1 << 0)  // 0=页不存在，1=页存在但权限冲突(如写只读页)
#define PF_ERR_WRITE    (1 << 1)  // 0=因读触发，1=因写触发
#define PF_ERR_USER     (1 << 2)  // 0=内核态触发，1=用户态进程触发
#define PF_ERR_INSTR    (1 << 4)  // 1=因取指令触发 (如触发了 NX 位保护)


// 对齐函数，确保 addr 按 align 对齐（align 为 2 的幂）
static inline uint64 align_up(uint64 value, uint64 align) {
    return value + (align - 1) & -align;
}

static inline uint64 align_down(uint64 value, uint64 align) {
    return value & -align;
}

//虚拟地址转物理地址
static inline uint64 va_to_pa(void *va) {
    return (uint64)va & ~DIRECT_MAP_VA_START;
}

//物理地址转虚拟地址
static inline void *pa_to_va(uint64 pa) {
    return (void *)(pa | DIRECT_MAP_VA_START);
}

// 提取出一个内联函数，专门解决 PAT 碰撞陷阱
static inline uint64 adjust_huge_page_attr(uint64 attr) {
    uint32 pat = (attr & PAGE_PAT)<<5;
    uint64 huge_attr = attr | pat | PAGE_PS;        // 设置到巨页合法的 PAT 位置,强制打上 PAGE_PS (Bit 7)
    return huge_attr;
}

// 核心边界切割器：计算在当前层级的块大小内，本次遍历最多能走到哪个虚拟地址。
// shift 参数代表对应层级的位移量：PT(12), PD(21), PDPT(30), PML4(39)
static inline uint64 get_addr_end(uint64 addr, uint64 end, uint8 shift) {
    // 算出当前块的下一个天然物理边界 (例如 2MB 对齐边界)
    uint64 boundary = (addr + (1ULL << shift)) & ~((1ULL << shift) - 1);

    // 防溢出回绕设计：如果边界还没超过总终点，就走到边界；如果超过了，就走到终点。
    return (boundary - 1 < end - 1) ? boundary : end;
}

// 计算页表索引索引
static inline uint32 get_table_idx(uint64 va,uint8 shift){
    return (va >> shift) & 0x1FF;
}

int32 vmmap(uint64 *pml4t, uint64 va,uint64 pa,  uint64 attr, uint64 page_size);
int32 unvmmap(uint64 *pml4t, uint64 va);
int32 vmmap_range(uint64 *pml4t, uint64 start_va,uint64 start_pa, uint64 size, uint64 attr);
int32 unvmmap_range(uint64 *pml4t, uint64 start_va, uint64 size);
uint64 vmm_get_pmm(uint64 *pml4t, void *va);

