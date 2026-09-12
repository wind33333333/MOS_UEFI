#pragma once

#include "moslib.h"
#include "rbtree.h"
#include "../include/vmm.h"

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


// 初始化系统内存布局
void vm_layout_init(void);

// 极速物理/虚拟地址转换引擎 (⚠️ 仅适用于 Direct Map 线性直接映射区域)
static inline uint64 va_to_pa(void *va) { return (uint64)va & ~g_direct_map_start; }
static inline void *pa_to_va(uint64 pa) { return (void *)(pa + g_direct_map_start); }

/* ========================================================================== */
/*                         VMA 虚拟内存区域属性标志位                         */
/* ========================================================================== */
#define VM_ALLOC            0x00000002 ///< 由 vmalloc() 分配的常规虚拟内存
#define VM_MODULES          0x00000004 ///< 由 module_remap() 分配的内核模块执行区
#define VM_IOREMAP          0x00000006 ///< 由 ioremap() 映射的外设寄存器区

/**
 * @brief 离散虚拟内存管理核心描述符 (Virtual Memory Area)
 * @note  用于描述一段连续的虚拟地址空间。通过红黑树进行管理。
 */
typedef struct {
    uint64           va_start;         // 虚拟地址起点
    uint64           va_end;           // 虚拟地址终点（不包含，即 [va_start, va_end)）
    rb_node_t        rb_node;          // 挂载到忙碌/空闲红黑树的节点
    list_head_t      list;             // 按照虚拟地址从低到高严格排序的双向链表

    union {
        // 🌟 增强红黑树 (Augmented RB-Tree) 的核心字段：
        // 记录以当前节点为根的子树中，最大的空闲块容量。
        // 分配时，通过判断子树的最大容量，可以 O(\log N) 极速剪枝，跳过空间不足的分支。
        uint64 subtree_max_size;
    };

    uint64           flags;            // 描述符属性 (如 VM_ALLOC, VM_IOREMAP)
} vmap_area_t;

// =========================================================================
// 核心 API 声明
// =========================================================================

void vmalloc_init(void);
void *vmalloc(uint64 size);
void vfree(void *ptr);

void *_ioremap(uint64 start_pa, uint64 size, uint64 flags);
int32 ioreunmap(void *ptr);

void *ioremap(uint64 start_pa, uint64 size);
void *ioremap_wc(uint64 start_pa, uint64 size);

void *memremap(uint64 start_pa, uint64 size);
int32 unmemremap(void *ptr);

void *module_remap(uint64 start_pa, uint64 size);
int32 unmodule_remap(void *ptr);

// 内存权限动态修饰接口
int32 _set_memory_flags(uint64 vaddr, uint64 size, uint64 flags);
static inline int32 set_memory_ro(uint64 vaddr, uint64 size) { return _set_memory_flags(vaddr, size, HW_PAGE_NX); }
static inline int32 set_memory_rw(uint64 vaddr, uint64 size) { return _set_memory_flags(vaddr, size, HW_PAGE_NX | HW_PAGE_RW); }
static inline int32 set_memory_rx(uint64 vaddr, uint64 size) { return _set_memory_flags(vaddr, size, 0); }
static inline int32 set_memory_rwx(uint64 vaddr, uint64 size){ return _set_memory_flags(vaddr, size, HW_PAGE_RW); }