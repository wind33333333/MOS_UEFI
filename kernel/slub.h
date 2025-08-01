#pragma once

#include "moslib.h"

#define MAX_OBJECT_SIZE (1*1024*1024)
#define KMALLOC_CACHE_SIZE 18

typedef struct kmem_cache_t {
    char* name;                   // 缓存池名称
    UINT32 object_size;           // 对象大小
    UINT32 order_per_slub;        // 每个slub页数量
    UINT32 object_per_slub;       // 每个slub对象数量
    UINT32 slub_count;            // 当前cache slub总数量
    UINT64 total_using;           // 当前cache已使用对象数量
    UINT64 total_free;            // 当前cache空闲对象数量
    list_head_t slub_head;        // slub链表头
}kmem_cache_t;


void init_slub(void);
kmem_cache_t* kmem_cache_create(char *cache_name,UINT32 object_size);
INT32 kmem_cache_destroy(kmem_cache_t *kmem_cache);
void *kmem_cache_alloc(kmem_cache_t *cache);
INT32 kmem_cache_free(kmem_cache_t *cache, void *object);
void *kmalloc(UINT64 size);
void *kzalloc(UINT64 size);
INT32 kfree(void *va);

