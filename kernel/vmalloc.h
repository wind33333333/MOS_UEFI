#ifndef VMALLOC_H
#define VMALLOC_H
#include "moslib.h"
#include "rbtree.h"

//vmlloc分配空间0xFFFFC80000000000-0xFFFFE80000000000 32TB
#define VMALLOC_START_ADDR 0xFFFFC80000000000UL
#define VMALLOC_END_ADDR   0xFFFFE80000000000UL

typedef struct {
    UINT64           va_start;  // 虚拟地址起始
    UINT64           va_end;    // 虚拟地址结束（va_start + size）
    rb_node_t        rb_node;   // 红黑树节点，按地址排序
    // list_head         list;      // 链表节点，连接所有 vmap_area
}vmap_area_t;

#endif