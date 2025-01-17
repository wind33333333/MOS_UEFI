#ifndef SLUB_H
#define SLUB_H
#include "moslib.h"

typedef struct kmem_cache_node {
    list_head_t partial;       // 部分使用 slab 的链表头
    UINT64 using_count;        // 当前slab节点已用对象数量
    UINT64 free_count;         // 当前slab节点空闲对象数量
    void *free_list;           // 下一个空闲对象指针
}kmem_cache_node_t;

typedef struct kmem_cache {
    char name[32];                // 缓存池名称32字节
    UINT64 total_using;           // 总已使用对象数量
    UINT64 total_free;            // 总空闲对象数量
    UINT64 size;                  // 对象大小
    kmem_cache_node_t *partial;     // 部分使用 slab 的链表
}kmem_cache_t;

void slub_init(void);

#endif