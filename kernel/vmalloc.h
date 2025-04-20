#ifndef VMALLOC_H
#define VMALLOC_H
#include "moslib.h"
#include "rbtree.h"

//vmlloc分配空间0xFFFFC80000000000-0xFFFFE80000000000 32TB
#define VMALLOC_START 0xFFFFC80000000000UL
#define VMALLOC_END   0xFFFFE80000000000UL

typedef struct {
    UINT64           va_start;  // 虚拟地址起始
    UINT64           va_end;    // 虚拟地址结束（va_start + size）
    rb_node_t        rb_node;   // 红黑树节点，按地址排序
    list_head_t      list;      // 链表节点，连接所有 vmap_area
    union {
        UINT64 subtree_max_size; //子树最大size
    };
}vmap_area_t;

//初始化vmalloc
void init_vmalloc(void);

// 分配内存
void *vmalloc(UINT64 size);

// 释放内存
void vfree(const void *addr);

//获取节点subtree_max_size
static inline UINT64 get_subtree_max_size(rb_node_t *node) {
    if (!node)return 0;
    // 通过 rb_entry 获取 vmap_area，返回其 subtree_max_size
    return (CONTAINER_OF(node,vmap_area_t, rb_node))->subtree_max_size;
}


#endif