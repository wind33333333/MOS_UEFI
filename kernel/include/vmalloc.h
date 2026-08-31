#pragma once

#include "moslib.h"
#include "rbtree.h"
#include "../include/vmm_page.h"

/* ========================================================================== */
/*                   动态内核虚拟内存空间边界 (在启动时计算)                  */
/* ========================================================================== */
extern uint64 g_direct_map_start;
extern uint64 g_direct_map_end;
extern uint64 g_vmalloc_start;
extern uint64 g_vmalloc_end;
extern uint64 g_page_map_start;
extern uint64 g_page_map_end;
extern uint64 g_io_map_start;
extern uint64 g_io_map_end;

/* ========================================================================== */
/*                 静态内核代码与模块区 (雷打不动，必须在最顶端)              */
/* ========================================================================== */
// -----------------------------------------------------------------------------
// 【架构师死穴警告】：无论 4 级还是 5 级页表，代码段绝对不能挪动！
// 现代 64 位 OS 使用 gcc -mcmodel=kernel 编译，编译器强制假定内核代码、
// 全局变量全部分布在地址空间最高的 2GB 内，以便使用极速的 32 位相对寻址。
// -----------------------------------------------------------------------------

// 动态模块空间 (1.5 GB，最高位往下挤一点)
#define MODULES_VA_START  0xFFFFFFFFA0000000ULL
#define MODULES_VA_END    0xFFFFFFFFFFFFFFFFULL

// 内核主代码与数据起始虚拟地址 (512 MB，紧贴在模块空间下方)
#define KERNEL_VA_START   0xFFFFFFFF80000000ULL
#define KERNEL_VA_END     0xFFFFFFFF9FFFFFFFULL

// 初始化函数声明
void vm_layout_init(void);

//虚拟地址转物理地址
static inline uint64 va_to_pa(void *va) {
    return (uint64)va & ~g_direct_map_start;
}

//物理地址转虚拟地址
static inline void *pa_to_va(uint64 pa) {
    return (void *)(pa + g_direct_map_start);
}

/* bits in flags of vmalloc's vm_struct below */
#define VM_ALLOC		        0x00000002	/* vmalloc() */
#define VM_MODULES	            0x00000004	/* vmap()ed pages */
#define VM_IOREMAP		        0x00000006	/* ioremap() and friends */
#define VM_USERMAP		        0x00000008	/* suitable for remap_vmalloc_range */
#define VM_DMA_COHERENT		    0x00000010	/* dma_alloc_coherent */
#define VM_UNINITIALIZED	    0x00000020	/* vm_struct is not fully initialized */
#define VM_NO_GUARD		        0x00000040      /* ***DANGEROUS*** don't add guard page */
#define VM_KASAN		        0x00000080      /* has allocated kasan shadow memory */
#define VM_FLUSH_RESET_PERMS	0x00000100	/* reset direct map and flush TLB on unmap, can't be freed in atomic context */
#define VM_MAP_PUT_PAGES	    0x00000200	/* put pages and free array in vfree */
#define VM_ALLOW_HUGE_VMAP	    0x00000400      /* Allow for huge pages on archs with HAVE_ARCH_HUGE_VMALLOC */


typedef struct {
    uint64           va_start;  // 虚拟地址起始
    uint64           va_end;    // 虚拟地址结束（va_start + size）
    rb_node_t        rb_node;   // 红黑树节点，按地址排序
    list_head_t      list;      // 链表节点，连接所有 vmap_area
    union {
        uint64 subtree_max_size; //子树最大size
    };
    uint64           flags;               //状态bit0 0为空闲，1为忙碌
}vmap_area_t;

//初始化vmalloc
void init_vmalloc(void);
void *vmalloc(uint64 size);
void vfree(void *ptr);


void *_ioremap(uint64 start_pa, uint64 size, uint64 flags);//底层虚拟地址分配映射
int32 ioreunmap(void *ptr);//卸载映射归还虚拟内存

/*
 * 设备虚拟地址分配和映射 (Uncacheable, 最常用)
 */
void *ioremap(uint64 start_pa, uint64 size) {
    return _ioremap(start_pa, size, PAGE_KERNEL_MMIO_WUC);
}

/*
 * 设备虚拟地址分配和映射 (Write-Combining, 常用于显卡 FrameBuffer)
 */
void *ioremap_wc(uint64 start_pa, uint64 size) {
    return _ioremap(start_pa, size, PAGE_KERNEL_MMIO_WC);
}

void *memremap(uint64 start_pa,uint64 size); //映射普通内存
int32 unmemremap(void *ptr); //卸载普通内存映射

void *module_remap(uint64 start_pa, uint64 size);
int32 unmodule_remap(void *ptr);

int32 _set_memory_flags(uint64 vaddr,uint64 size,uint64 flags);
/**
 * 假设你的基础权限宏定义如下：
 * PAGE_KERNEL_RO      : HW_PAGE_P | HW_PAGE_NX                  (只读，不可执行)
 * PAGE_KERNEL_DATA_RW : HW_PAGE_P | HW_PAGE_RW | HW_PAGE_NX     (读写，不可执行)
 * PAGE_KERNEL_CODE    : HW_PAGE_P                               (只读，可执行)
 */

/**
 * @brief 将内核内存区间设置为只读 (Read-Only + No-Execute)
 * @note 常用于保护 .rodata 常量段，或在只读数据加载完毕后锁定权限
 */
static inline int32 set_memory_ro(uint64 vaddr, uint64 size) {
    return _set_memory_flags(vaddr,size, PAGE_KERNEL_DATA_RO);
}

/**
 * @brief 将内核内存区间设置为可读写 (Read-Write + No-Execute)
 * @note 常用于 .data / .bss 段，或在模块准备卸载前恢复权限以便系统安全回收
 */
static inline int32 set_memory_rw(uint64 vaddr, uint64 size) {
    return _set_memory_flags(vaddr,size, PAGE_KERNEL_DATA_RW);
}

/**
 * @brief 将内核内存区间设置为只读可执行 (Read-Only + Execute)
 * @note 常用于锁定 .text 代码段，严格落实 W^X (写与执行互斥) 安全原则
 */
static inline int32 set_memory_rx(uint64 vaddr, uint64 size) {
    return _set_memory_flags(vaddr,size, PAGE_KERNEL_CODE);
}

