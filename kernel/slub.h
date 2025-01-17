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
    char* name;                   // 缓存池名称
    UINT64 total_using;           // 总已使用对象数量
    UINT64 total_free;            // 总空闲对象数量
    UINT32 size;                  // 对象大小
    kmem_cache_node_t *partial;   // 部分使用 slab 的链表
}kmem_cache_t;

static inline UINT32 object_size_align(UINT32 size) {
    if (size <= 8) return 8; // 最小对齐值
    size--;
    size |= size >> 1;
    size |= size >> 2;
    size |= size >> 4;
    size |= size >> 8;
    size |= size >> 16;
    return size + 1;
}

void slub_init(void);

#endif