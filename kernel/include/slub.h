#pragma once
#include "moslib.h"
#include "vmm.h"
#include "list.h"
#include "../x64/numa_node.h"

// -----------------------------------------------------------------------------
// SLUB 分配器配置参数
// -----------------------------------------------------------------------------
#define MAX_OBJECT_SIZE     (1 * 1024 * 1024) ///< 支持的最大分配对象为 1MB
#define KMALLOC_CACHE_SIZE  18                ///< kmalloc 预置的常规缓存池阶数 (8B ~ 1MB)

// =========================================================================
// SLUB 的核心层级结构
// =========================================================================

// 1. 本地 CPU 无锁缓存池 (高频热路径，绝对不能加锁)
typedef struct {
    void   **freelist; // 指向当前活动 SLUB 页内的第一个空闲对象
    page_t *page;      // 当前正在使用的这个活动 SLUB 物理大页
} kmem_cache_cpu_t;

// 2. 节点级共享备用池 (需要加自旋锁)
typedef struct {
    list_head_t partial_list; // 那些还有空闲对象，但目前没被任何 CPU 独占的 SLUB 页
    uint32      nr_partial;   // 备用链表上的 SLUB 页数量
    // spinlock_t list_lock;  // 留给将来 SMP 锁机制实现
} kmem_cache_node_t;

// 3. 全局缓存池描述符
typedef struct kmem_cache_t {
    char   *name;
    uint32 object_size;
    uint32 order_per_slub;
    uint32 object_per_slub;

    // 🌟 每 CPU 私有无锁阵地（由 alloc_percpu 提供支持）
    kmem_cache_cpu_t *cpu_slab;

    // 🌟 NUMA 节点级共享后备阵地
    kmem_cache_node_t node[CONFIG_NUMA_NODES];

    // 统计信息
    uint32 slub_count;
    uint32 total_using;
    uint32 total_free;
} kmem_cache_t;

// -----------------------------------------------------------------------------
// 全局 API 声明
// -----------------------------------------------------------------------------

kmem_cache_t* kmem_cache_create(char *cache_name, uint32 object_size);
int32 kmem_cache_destroy(kmem_cache_t *kmem_cache);

void* kmem_cache_alloc(kmem_cache_t *cache);
int32 kmem_cache_free(kmem_cache_t *cache, void *object);

void* kmalloc(uint64 size);
int32 kfree(void *va);


static inline void *kzalloc(uint64 size) {
    if (size == 0) return NULL;
    void *ptr = kmalloc(size);
    if (ptr) asm_mem_set(ptr, 0, size);
    return ptr;
}

/**
 * @brief 分配用于 DMA 的内存 (严格 64 字节 CacheLine 对齐并清零)
 */
static inline void* kzalloc_dma(uint64 size) {
    return kzalloc(align_up(size, 64));
}

// 极速物理/虚拟地址转换引擎 (⚠️ 仅适用于 Direct Map 线性直接映射区域)
static inline uint64 va_to_pa(void *va) { return (uint64)va & ~vm_layout.direct_map_start; }
static inline void *pa_to_va(uint64 pa) { return (void *)(pa + vm_layout.direct_map_start); }