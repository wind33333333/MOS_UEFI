#ifndef SLUB_H
#define SLUB_H
#include "moslib.h"

typedef struct kmem_cache_node {
    list_head_t slub_node;     // slub链表
    UINT64 using_count;        // 当前slab节点已用对象数量
    UINT64 free_count;         // 当前slab节点空闲对象数量
    void *object_start_vaddr;  // 对象起始虚拟地址
    void *free_list;           // 下一个空闲对象指针
}kmem_cache_node_t;

typedef struct kmem_cache {
    char* name;                   // 缓存池名称
    UINT32 object_size;           // 对象大小
    UINT32 order_per_slub;        // 每个slub页数量
    UINT32 object_per_slub;       // 每个slub对象数量
    UINT32 slub_count;            // 缓存池slub数量
    UINT64 total_using;           // 总已使用对象数量
    UINT64 total_free;            // 总空闲对象数量
    list_head_t slub_head;        // slub链表头
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

//空闲链表初始化
static inline void free_list_init(UINT64* next,UINT32 size,UINT32 count) {
    while (count--) {
        *next = (UINT64)next + size;
        next = *next;
    }
    *next = 0;
}

void slub_init(void);
kmem_cache_t* kmem_cache_create(char *name,UINT32 object_size);
void kmem_cache_destroy(kmem_cache_t *kmem_cache);
void* kmem_cache_alloc(kmem_cache_t *kmem_cache);
void kmem_cache_free(kmem_cache_t *cache, void *object)
void kmem_cache_free1(kmem_cache_t *cache, void *object);
void* alloc_cache_object(kmem_cache_t* cache);
void free_cache_object(kmem_cache_t* cache, void* object);
void add_cache_node(kmem_cache_t* cache,kmem_cache_node_t* new_cache_node);
void add_cache(char* name,kmem_cache_t* new_cache, UINT32 object_size);

#endif