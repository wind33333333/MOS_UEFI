#pragma once

#include "moslib.h"
#include "vmm.h"

// -----------------------------------------------------------------------------
// SLUB 分配器配置参数
// -----------------------------------------------------------------------------
#define MAX_OBJECT_SIZE     (1 * 1024 * 1024) ///< 支持的最大分配对象为 1MB
#define KMALLOC_CACHE_SIZE  18                ///< kmalloc 预置的常规缓存池阶数 (8B ~ 1MB)

/**
 * @brief kmem_cache 缓存池管理描述符
 * @note  负责管理一类固定大小对象的分配与回收。每个缓存池下挂载多个 slub (物理页框集合)。
 */
typedef struct kmem_cache_t {
    char*       name;             ///< 缓存池名称 (用于调试与统计)
    uint32      object_size;      ///< 经过严格对齐后的单个对象字节大小
    uint32      order_per_slub;   ///< 每个 slub 块占据的伙伴系统阶数 (0阶=4K, 1阶=8K...)
    uint32      object_per_slub;  ///< 每个 slub 块最多能容纳的对象总数
    uint32      slub_count;       ///< 当前缓存池名下挂载的 slub 块总数
    uint64      total_using;      ///< 全局统计：当前正在被使用的对象总数
    uint64      total_free;       ///< 全局统计：当前可供分配的空闲对象总数
    list_head_t slub_head;        ///< 双向链表头：挂载所有属于该池的 slub (page_t)
} kmem_cache_t;

// -----------------------------------------------------------------------------
// 全局 API 声明
// -----------------------------------------------------------------------------

void slub_init(void);

kmem_cache_t* kmem_cache_create(char *cache_name, uint32 object_size);
int32 kmem_cache_destroy(kmem_cache_t *kmem_cache);

void* kmem_cache_alloc(kmem_cache_t *cache);
int32 kmem_cache_free(kmem_cache_t *cache, void *object);

void* kmalloc(uint64 size);
void* kzalloc(uint64 size);
int32 kfree(void *va);

/**
 * @brief 分配用于 DMA 的内存 (严格 64 字节 CacheLine 对齐并清零)
 */
static inline void* kzalloc_dma(uint64 size) {
    return kzalloc(align_up(size, 64));
}

// 极速物理/虚拟地址转换引擎 (⚠️ 仅适用于 Direct Map 线性直接映射区域)
static inline uint64 va_to_pa(void *va) { return (uint64)va & ~vm_layout.direct_map_start; }
static inline void *pa_to_va(uint64 pa) { return (void *)(pa + vm_layout.direct_map_start); }