#pragma once

#include "moslib.h"

/**
 * @file vmm_page.h
 * @brief MOS_UEFI 统一虚拟内存页操作子系统头文件
 *
 * 核心架构特性：
 * 1. 泛化级数支持：原生兼容 x86_64 标准 4 级分页 (PML4) 与 5 级分页 (LA57 / PML5)。
 * 2. 多粒度大页：支持 4 KiB 标准页、2 MiB 大页及 1 GiB 巨页映射与动态就地拆分。
 * 3. 对象级内存管理：与底层 Buddy System 完全基于 page_t 对象交互，实现 O(1) 极限回收。
 * 4. 事务性与回滚：范围映射支持故障现场全自动原子回滚，彻底杜绝页表残留与物理页泄漏。
 * 5. 树自修剪回收：解映射时后序遍历自底向上回收全空物理页表，杜绝页表空悬。
 */

/* ========================================================================== */
/*                           内存页基础尺寸与对齐宏                           */
/* ========================================================================== */

// -----------------------------------------------------------------------------
// 4K 页表 (Level 1 - PT)
// -----------------------------------------------------------------------------
#define PAGE_4K_SHIFT            12
#define PAGE_4K_SIZE             (1ULL << PAGE_4K_SHIFT)               // 0x1000
#define PAGE_4K_OFFSET_MASK      (PAGE_4K_SIZE - 1)                    // 0xFFF
#define PAGE_4K_MASK             (~PAGE_4K_OFFSET_MASK)                // 0xFFFFFFFFFFFFF000
#define PAGE_4K_ALIGN(addr)      (((uint64)(addr) + PAGE_4K_OFFSET_MASK) & PAGE_4K_MASK)
#define PAGE_4K_ALIGN_DOWN(addr) ((uint64)(addr) & PAGE_4K_MASK)

// -----------------------------------------------------------------------------
// 2M 大页 (Level 2 - PD)
// -----------------------------------------------------------------------------
#define PAGE_2M_SHIFT            21
#define PAGE_2M_SIZE             (1ULL << PAGE_2M_SHIFT)               // 0x200000
#define PAGE_2M_OFFSET_MASK      (PAGE_2M_SIZE - 1)                    // 0x1FFFFF
#define PAGE_2M_MASK             (~PAGE_2M_OFFSET_MASK)                // 0xFFFFFFFFFFE00000
#define PAGE_2M_ALIGN(addr)      (((uint64)(addr) + PAGE_2M_OFFSET_MASK) & PAGE_2M_MASK)
#define PAGE_2M_ALIGN_DOWN(addr) ((uint64)(addr) & PAGE_2M_MASK)

// -----------------------------------------------------------------------------
// 1G 巨页 (Level 3 - PDPT)
// -----------------------------------------------------------------------------
#define PAGE_1G_SHIFT            30
#define PAGE_1G_SIZE             (1ULL << PAGE_1G_SHIFT)               // 0x40000000
#define PAGE_1G_OFFSET_MASK      (PAGE_1G_SIZE - 1)                    // 0x3FFFFFFF
#define PAGE_1G_MASK             (~PAGE_1G_OFFSET_MASK)                // 0xFFFFFFFFC0000000
#define PAGE_1G_ALIGN(addr)      (((uint64)(addr) + PAGE_1G_OFFSET_MASK) & PAGE_1G_MASK)
#define PAGE_1G_ALIGN_DOWN(addr) ((uint64)(addr) & PAGE_1G_MASK)


/* ========================================================================== */
/*                           硬件 PTE 属性标志位                              */
/* ========================================================================== */

// --- 基础控制与权限 ---
#define HW_PAGE_P           (1ULL << 0)   ///< 存在位 (Present): 1 表示页有效，0 触发 #PF
#define HW_PAGE_RW          (1ULL << 1)   ///< 读写权限 (R/W): 0 = 只读，1 = 可读写
#define HW_PAGE_US          (1ULL << 2)   ///< 访问级别 (U/S): 0 = 仅内核态，1 = 允许用户态 (Ring 3)
#define HW_PAGE_NX          (1ULL << 63)  ///< 不可执行 (NX/XD): 1 表示禁止指令提取 (需开启 EFER.NXE)

// --- 缓存控制策略 ---
#define HW_PAGE_PWT         (1ULL << 3)   ///< 缓存策略: Page-level Write-Through
#define HW_PAGE_PCD         (1ULL << 4)   ///< 缓存策略: Page-level Cache Disable
#define HW_PAGE_4K_PAT      (1ULL << 7)   ///< 缓存策略: 4KB 小页的 PAT 标志位
#define HW_PAGE_HUGE_PAT    (1ULL << 12)  ///< 缓存策略: 2MB/1GB 大页的 PAT 标志位

// --- 硬件状态与特殊模式 ---
#define HW_PAGE_A           (1ULL << 5)   ///< 访问标志 (Accessed): 硬件自动置 1
#define HW_PAGE_D           (1ULL << 6)   ///< 脏页标志 (Dirty): 硬件在写入后自动置 1
#define HW_PAGE_PS          (1ULL << 7)   ///< 大页标志 (Page Size): 在 Level 2/3 表示大页/巨页
#define HW_PAGE_G           (1ULL << 8)   ///< 全局页 (Global): 切换 CR3 时不刷新此页的 TLB (需 CR4.PGE)

#define PTE_ADDR_MASK       0x000FFFFFFFFFF000ULL  ///< 物理地址提取掩码 (Bits 12..51)


/* ========================================================================== */
/*                      VMM 软件控制宏 (Software Flags)                       */
/* ========================================================================== */

// -----------------------------------------------------------------------------
// VMM 内部控制位 (利用 x86_64 硬件忽略的保留位：Bits 9..11 与 高位 52..62)
// -----------------------------------------------------------------------------
#define SW_FLAG_COW             (1ULL << 9)   ///< 软件扩展: 写时复制 (Copy-On-Write)
#define SW_FLAG_GUARD           (1ULL << 10)  ///< 软件扩展: 警戒页 (Guard Page，栈溢出检测)

// --- 映射行为控制 (Map Engine) ---
#define SW_FLAG_OVERWRITE       (1ULL << 52)  ///< 允许强行覆写已存在的映射 (否则报 VM_ERR_ALREADY_MAPPED)
#define SW_FLAG_FREE_OLD_PHYS   (1ULL << 59)  ///< 安全覆写: 覆盖原映射时，自动回收原属物理页 (防内存泄漏)
#define SW_FLAG_STRICT_HUGE     (1ULL << 55)  ///< 严苛大页: 强迫使用最大可能的页层级，拒绝退化为 4K 小页

// --- 最大页层级约束 ---
#define SW_FLAG_MAX_LEVEL_MASK  (3ULL << 53)  ///< 用于提取最大允许层级的掩码 (占用 Bits 53..54)
#define SW_FLAG_MAX_4K          (1ULL << 53)  ///< 限制上限: 仅允许使用 Level 1 (4KB)
#define SW_FLAG_MAX_2M          (2ULL << 53)  ///< 限制上限: 最高允许 Level 2 (2MB)
#define SW_FLAG_MAX_1G          (3ULL << 53)  ///< 限制上限: 最高允许 Level 3 (1GB)

/** @brief 提取用户允许的最大页层级。若未配置则安全退化至 Level 1 (4KB) */
#define GET_MAX_LEVEL(flags)    (((flags) & SW_FLAG_MAX_LEVEL_MASK) ? (((flags) & SW_FLAG_MAX_LEVEL_MASK) >> 53) : 1)

// --- 解映射行为控制 (Unmap Engine) ---
#define UNMAP_FLAG_NONE         0x00          ///< 仅拆除虚拟索引，不触碰底层物理页 (适用于 MMIO/共享内存)
#define UNMAP_FLAG_FREE_PHYS    0x01          ///< 彻底摧毁：拆除索引并把真金白银的物理内存退还给分配器


/* ========================================================================== */
/*                      缓存策略预设 (PAT Profile Presets)                    */
/* ========================================================================== */

// 预设绑定系统早期的 IA32_PAT_MSR 配置: [Index3=UC] [Index2=UC-] [Index1=WC] [Index0=WB]
#define CACHE_WB    (0)                               ///< Write-Back (常规内存默认)
#define CACHE_WC    (HW_PAGE_PWT)                     ///< Write-Combining (显卡 Framebuffer 狂暴引擎)
#define CACHE_WUC   (HW_PAGE_PCD)                     ///< Uncacheable Minus (常规 MMIO 寄存器)
#define CACHE_UC    (HW_PAGE_PCD | HW_PAGE_PWT)       ///< Strong Uncacheable (强一致性要求硬件，如 APIC)

// --- 内核态 (Ring 0) 常用属性组合 ---
#define PAGE_KERNEL_CODE         (HW_PAGE_P | HW_PAGE_G | CACHE_WB)
#define PAGE_KERNEL_CODE_RW      (HW_PAGE_P | HW_PAGE_G | HW_PAGE_RW | CACHE_WB)
#define PAGE_KERNEL_DATA_RO      (HW_PAGE_P | HW_PAGE_G | HW_PAGE_NX | CACHE_WB)
#define PAGE_KERNEL_DATA_RW      (HW_PAGE_P | HW_PAGE_G | HW_PAGE_NX | HW_PAGE_RW | CACHE_WB)
#define PAGE_KERNEL_MMIO_WUC     (HW_PAGE_P | HW_PAGE_G | HW_PAGE_NX | HW_PAGE_RW | CACHE_WUC)
#define PAGE_KERNEL_MMIO_WC      (HW_PAGE_P | HW_PAGE_G | HW_PAGE_NX | HW_PAGE_RW | CACHE_WC)
#define PAGE_KERNEL_MMIO_UC      (HW_PAGE_P | HW_PAGE_G | HW_PAGE_NX | HW_PAGE_RW | CACHE_UC)

// --- 用户态 (Ring 3) 常用属性组合 ---
#define PAGE_USER_CODE           (HW_PAGE_P | HW_PAGE_US | CACHE_WB)
#define PAGE_USER_DATA_RO        (HW_PAGE_P | HW_PAGE_US | CACHE_WB | HW_PAGE_NX)
#define PAGE_USER_DATA_RW        (HW_PAGE_P | HW_PAGE_US | CACHE_WB | HW_PAGE_NX | HW_PAGE_RW)

// -----------------------------------------------------------------------------
// 硬件写入掩码 (终极安全防火墙)：过滤注入硬件的 PTE 属性，拦截非法的 SW_FLAG
// -----------------------------------------------------------------------------
#define PTE_WRITE_MASK  (HW_PAGE_P | HW_PAGE_RW | HW_PAGE_US | HW_PAGE_PWT | \
                         HW_PAGE_PCD | HW_PAGE_A | HW_PAGE_D | HW_PAGE_PS | \
                         HW_PAGE_G | HW_PAGE_4K_PAT | HW_PAGE_HUGE_PAT | HW_PAGE_NX | \
                         0x0000000000000E00ULL) // 0xE00 包含 Bit 9~11 (OS AVL)，予以放行


/* ========================================================================== */
/*                         类型定义与状态枚举                                 */
/* ========================================================================== */

/**
 * @brief 虚拟内存操作状态与错误码
 */
typedef enum {
    VM_SUCCESS            =  0,  ///< 操作成功
    VM_ERR_NOMEM          = -1,  ///< 内存耗尽：无法为中间目录申请物理帧
    VM_ERR_ALREADY_MAPPED = -2,  ///< 冲突：目标地址已被占用 (且未指定 OVERWRITE)
    VM_ERR_NOT_MAPPED     = -3,  ///< 访问异常：目标地址不存在有效映射
    VM_ERR_INVALID_ARGS   = -4,  ///< 非法参数：地址未对齐、长度异常或级数非法
    VM_ERR_CANONICAL      = -5,  ///< 越界：虚拟地址违反 x86_64 符号位扩展规则
    VM_ERR_SPLIT_FAILED   = -6,  ///< 大页强拆失败
    VM_ERR_UNSUPPORTED    = -7   ///< 硬件拒载：CPU 不支持请求的特性 (如 1G 巨页)
} vm_status_e;

/**
 * @brief 硬件支持的有效叶子页规格
 */
typedef enum {
    PAGE_LVL_4K = 1,   ///< Level 1 (PT)   : 4 KiB 标准页
    PAGE_LVL_2M = 2,   ///< Level 2 (PD)   : 2 MiB 大页
    PAGE_LVL_1G = 3    ///< Level 3 (PDPT) : 1 GiB 巨页
} vm_page_lvl_e;

// 硬件引发的缺页异常 (#PF) 错误码掩码解析
#define PF_ERR_PRESENT  (1 << 0)  ///< 0=缺页引发，1=权限冲突(如写只读页)引发
#define PF_ERR_WRITE    (1 << 1)  ///< 0=读操作引发，1=写操作引发
#define PF_ERR_USER     (1 << 2)  ///< 0=内核 Ring 0 触发，1=用户 Ring 3 触发
#define PF_ERR_INSTR    (1 << 4)  ///< 1=因取指令触发 (执行了 NX 拦截区)


// =========================================================================
// 动态虚拟内存布局边界控制块
// =========================================================================
typedef struct {
    uint64 direct_map_start;
    uint64 direct_map_end;

    uint64 vmalloc_start;
    uint64 vmalloc_end;

    uint64 page_map_start;
    uint64 page_map_end;

    uint64 io_map_start;
    uint64 io_map_end;

    uint64 efi_rts_start;
    uint64 efi_rts_end;

    uint64 module_start;
    uint64 module_end;

    uint64 kernel_start;
    uint64 kernel_end;
} vm_layout_t;

extern vm_layout_t vm_layout;

/* ========================================================================== */
/*                         底层依赖注入与上下文结构体                         */
/* ========================================================================== */

// 物理页帧描述符前置声明 (由底层 Buddy System 定义)
typedef struct page_t page_t;

/**
 * @brief 虚拟内存管理器与底层 PMM 对接的物理操作集 (Buddy System 接口)
 * @note  全面基于 page_t 对象设计，实现极速 O(1) 转换与释放。
 */
typedef struct {
    page_t* (*alloc_pages)(uint32 order);          ///< 分配连续物理页对象 (order: 0=4K, 9=2M, 18=1G)
    void    (*free_pages)(page_t *page);          ///< 释放物理页对象 (底层从 page 中自动读取 order)

    uint64  (*page_to_phys)(page_t *page);        ///< 将页描述符降维为物理裸地址 (注入 PTE 时用)
    page_t* (*phys_to_page)(uint64 paddr);        ///< 将旧 PTE 物理地址逆向升维为对象 (解映射回收时用)

    void*   (*phys_to_virt)(uint64 paddr);        ///< 物理转虚拟 (依赖高半核 HHDM 线性映射区)
    uint64  (*virt_to_phys)(void* vaddr);         ///< 虚拟转物理 (依赖高半核 HHDM 线性映射区)
} vm_allocator_ops_t;

/**
 * @brief 虚拟地址空间实例上下文
 * @note  对应一个进程 (Process) 或独立的内核内存空间。
 */
typedef struct vm_space {
    uint64             cr3_root;     ///< 顶层根页表 (PML4 / PML5) 的物理地址
    uint8              paging_level; ///< 分页模式规格: 4 (48位) 或 5 (57位)
    vm_allocator_ops_t ops;          ///< 当前空间绑定的底层物理操作回调集
    void*              lock;         ///< 并发保护锁 (预留 SMP 扩展)
} vm_space_t;


/* ========================================================================== */
/*                         核心公共 API 接口声明                              */
/* ========================================================================== */

/**
 * @brief 创建并初始化独立的虚拟地址空间
 * @param space        目标地址空间对象指针
 * @param level        分页级数 (4 或 5)
 * @param ops          底层操作回调集 (采用 const 指针传参，避开 32 字节结构体溢出 ABI 寄存器限制)
 * @param clone_kernel 是否共享克隆高半核的内核空间 (映射 PML4/5 顶层 256..511 项)
 * @return vm_status_e 执行状态
 */
vm_status_e vm_space_init(vm_space_t *space, uint8 level, const vm_allocator_ops_t *ops, boolean clone_kernel);

/**
 * @brief 销毁虚拟地址空间
 * @note  将递归销毁低半区的所有用户级页表并释放底层物理数据帧，根页表本身也被摧毁。
 * @param space 待销毁的地址空间指针
 * @return vm_status_e 执行状态
 */
vm_status_e vm_space_destroy(vm_space_t *space);

/**
 * @brief 将指定地址空间切换为当前活跃空间 (载入硬件 MMU)
 * @param space 目标地址空间指针
 */
void vm_space_switch(const vm_space_t *space);

/**
 * @brief 核心事务引擎：范围虚拟内存映射
 * @details 自动进行 1G/2M/4K 弹性大页升阶。自带原子回滚，遇险即撤，零泄漏。
 * @param space 目标地址空间指针
 * @param vaddr 起始虚拟地址 (须 4KB 对齐)
 * @param paddr 目标起始物理地址 (须 4KB 对齐)
 * @param size  映射总字节长度 (须 4KB 对齐)
 * @param flags 属性组合 (HW_PAGE_* 与 SW_FLAG_*)
 * @return vm_status_e 执行状态
 */
vm_status_e vm_map_range(vm_space_t *space, uint64 vaddr, uint64 paddr, uint64 size, uint64 flags);

/**
 * @brief 核心裁剪引擎：范围解除映射
 * @details 自动执行局部大页强拆破壁，解映射后自动自底向上执行无用页表树枝修剪。
 * @param space 目标地址空间指针
 * @param vaddr 起始虚拟地址 (须 4KB 对齐)
 * @param size  解映射总字节长度 (须 4KB 对齐)
 * @param flags 行为控制 (如 UNMAP_FLAG_FREE_PHYS，决定是否真的回收物理数据内存)
 * @return vm_status_e 执行状态
 */
vm_status_e vm_unmap_range(vm_space_t *space, uint64 vaddr, uint64 size, uint32 flags);

/**
 * @brief 核心属性引擎：范围权限修饰
 * @details 动态调整大批量页面的存取属性 (如保护只读，启用不可执行)。遇到局部大页会自动拆分。
 * @param space     目标地址空间指针
 * @param vaddr     起始虚拟地址 (须 4KB 对齐)
 * @param size      修改总字节长度 (须 4KB 对齐)
 * @param new_flags 待应用的新属性标志组合
 * @return vm_status_e 执行状态
 */
vm_status_e vm_protect_range(vm_space_t *space, uint64 vaddr, uint64 size, uint64 new_flags);

/**
 * @brief 动态查询虚拟地址的映射详情
 * @param space     目标地址空间指针
 * @param vaddr     待查询虚拟地址
 * @param out_paddr 输出对应的完整物理地址 (连带页内 offset)
 * @param out_flags 输出该地址承载的 PTE 属性
 * @param out_size  输出该地址是由多大规格的页承载的 (4K/2M/1G)
 * @return vm_status_e 若该处尚未铺设页表则返回 VM_ERR_NOT_MAPPED
 */
vm_status_e vm_query(const vm_space_t *space, uint64 vaddr, uint64 *out_paddr, uint64 *out_flags, vm_page_lvl_e *out_size);

/**
 * @brief 高级手动控制：大页就地破壁拆分
 * @details 强行将 2MB 拆分为 512 个 4KB，或 1GB 拆分为 512 个 2MB。
 * @param space    目标地址空间指针
 * @param vaddr    位于目标大页体内的任意虚拟地址
 * @param from_lvl 要拆分的页级别 (PAGE_LVL_2M 或 PAGE_LVL_1G)
 * @return vm_status_e 执行状态
 */
vm_status_e vm_split_huge_page(vm_space_t *space, uint64 vaddr, vm_page_lvl_e from_lvl);


// 初始化系统内存布局
void vm_layout_init(void);