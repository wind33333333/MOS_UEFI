#pragma once

#include "moslib.h"
#include "rbtree.h"
#include "../include/vmm.h"

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

//底层虚拟地址分配映射
void *__ioremap(uint64 start_pa, uint64 size, uint64 attr);

/*
 * 设备虚拟地址分配和映射 (Uncacheable, 最常用)
 */
void *ioremap(uint64 start_pa, uint64 size) {
    return __ioremap(start_pa, size, PAGE_KERNEL_WUC);
}

/*
 * 设备虚拟地址分配和映射 (Write-Combining, 常用于显卡 FrameBuffer)
 */
void *ioremap_wc(uint64 start_pa, uint64 size) {
    return __ioremap(start_pa, size, PAGE_KERNEL_WC);
}

//卸载映射归还虚拟内存
int32 ioreunmap(void *ptr);

void *memremap(uint64 start_pa,uint64 size);
int32 unmemremap(void *ptr);

