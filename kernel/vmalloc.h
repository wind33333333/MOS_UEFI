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
        UINT64 subtree_max_size; //子树最大空间
    };
}vmap_area_t;

//初始化vmalloc
void vmalloc_init(void);
// 分配内存
void *vmalloc(UINT64 size);
// 释放内存
void vfree(const void *addr);
//把vmap_area插入红黑树
UINT32 insert_vmap_area(rb_root_t *root, vmap_area_t *new_data);
//新建一个vmap_area
vmap_area_t *new_vmap_area(UINT64 va_start,UINT64 va_end);

#endif