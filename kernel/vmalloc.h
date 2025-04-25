#ifndef VMALLOC_H
#define VMALLOC_H
#include "moslib.h"
#include "rbtree.h"

//vmlloc分配空间0xFFFFC80000000000-0xFFFFE80000000000 32TB
#define VMALLOC_START 0xFFFFC80000000000UL
#define VMALLOC_END   0xFFFFE80000000000UL

/* bits in flags of vmalloc's vm_struct below */
#define VM_IOREMAP		        0x00000001	/* ioremap() and friends */
#define VM_ALLOC		        0x00000002	/* vmalloc() */
#define VM_MAP			        0x00000004	/* vmap()ed pages */
#define VM_USERMAP		        0x00000008	/* suitable for remap_vmalloc_range */
#define VM_DMA_COHERENT		    0x00000010	/* dma_alloc_coherent */
#define VM_UNINITIALIZED	    0x00000020	/* vm_struct is not fully initialized */
#define VM_NO_GUARD		        0x00000040      /* ***DANGEROUS*** don't add guard page */
#define VM_KASAN		        0x00000080      /* has allocated kasan shadow memory */
#define VM_FLUSH_RESET_PERMS	0x00000100	/* reset direct map and flush TLB on unmap, can't be freed in atomic context */
#define VM_MAP_PUT_PAGES	    0x00000200	/* put pages and free array in vfree */
#define VM_ALLOW_HUGE_VMAP	    0x00000400      /* Allow for huge pages on archs with HAVE_ARCH_HUGE_VMALLOC */


typedef struct {
    UINT64           va_start;  // 虚拟地址起始
    UINT64           va_end;    // 虚拟地址结束（va_start + size）
    rb_node_t        rb_node;   // 红黑树节点，按地址排序
    list_head_t      list;      // 链表节点，连接所有 vmap_area
    union {
        UINT64 subtree_max_size; //子树最大size
    };
    UINT64           flags;               //状态
}vmap_area_t;

//初始化vmalloc
void init_vmalloc(void);

// 分配内存
void *vmalloc(UINT64 size);

// 释放内存
void vfree(void *ptr);


#endif