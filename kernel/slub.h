#ifndef SLUB_H
#define SLUB_H
#include "moslib.h"

typedef struct kmem_cache_node {
    list_head_t partial;       // 部分使用 slub 的链表
    UINT64 using_count;        // 当前slab节点已用对象数量
    UINT64 free_count;         // 当前slab节点空闲对象数量
    void *free_list;           // 下一个空闲对象指针
}kmem_cache_node_t;

typedef struct kmem_cache {
    char* name;                   // 缓存池名称
    UINT32 object_size;           // 对象大小
    UINT32 order_per_slub;        // 每个slub页数量
    UINT32 object_per_slub;       // 每个slub对象数量
    UINT64 total_using;           // 总已使用对象数量
    UINT64 total_free;            // 总空闲对象数量
    kmem_cache_node_t *partial;   // 部分使用 slub 的链表头
}kmem_cache_t;

//把对象真是size对齐到2^n字节，提高内存访问性能和每页刚好整数
static inline UINT32 object_size_align(UINT32 objcet_size) {
    if (objcet_size <= 8) return 8; // 最小对齐值
    --objcet_size;
    objcet_size |= objcet_size >> 1;
    objcet_size |= objcet_size >> 2;
    objcet_size |= objcet_size >> 4;
    objcet_size |= objcet_size >> 8;
    objcet_size |= objcet_size >> 16;
    return ++objcet_size;
}

//1K以内的分配1一个4K页，1K以上乘4。
static inline UINT32 object_size_order(UINT32 objcet_size) {
    if (objcet_size <= 1024) return 0;
    return objcet_size >>= 11;
}

void slub_init(void);
kmem_cache_t* kmem_cache_create(char *name,UINT64 object_size);
void kmem_cache_destroy(kmem_cache_t *kmem_cache);
void* kmem_cache_alloc(kmem_cache_t *kmem_cache);
void kmem_cache_free(kmem_cache_t *kmem_cache, void *ptr);

#endif