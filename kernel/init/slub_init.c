#include "slub.h"

// =========================================================================
// SLUB 系统总控初始化
// =========================================================================

INIT_TEXT void slub_init(void) {
    // 1. 突破自举限制：先手工格式化用于存放 kmem_cache_t 控制块本身的根级池
    create_cache(kmem_cache_name, &kmem_cache, sizeof(kmem_cache_t));

    // 2. 梯队展开：创建从 8 Bytes 到 1 MB 的通用动态内存申请池
    uint32 object_size = 8;
    for (uint32 i = 0; i < KMALLOC_CACHE_SIZE; i++) {
        // 利用刚刚自举成功的 kmem_cache，源源不断地生产出各个规格的控制块
        kmalloc_cache[i] = kmem_cache_alloc(&kmem_cache);
        create_cache(kmalloc_name[i], kmalloc_cache[i], object_size);
        object_size <<= 1;
    }
    PR_OK("Slub Memory System Success.\n");
}