#include "slub.h"
#include "printk.h"
#include "cpu.h" // 引入 asm_hlt 等宏

// 外部依赖声明
extern void create_cache(char *cache_name, kmem_cache_t *cache, uint32 object_size);
extern char kmem_cache_name[];
extern kmem_cache_t kmem_cache;
extern kmem_cache_t *kmalloc_cache[];
extern char *kmalloc_name[];

// =========================================================================
// SLUB 系统总控初始化 (NUMA & Per-CPU 适配版)
// =========================================================================

void slub_init(void) {
    PR_INFO("[SLUB] Bootstrapping SLUB allocator (Per-CPU & NUMA aware)...\n");

    // 1. 突破自举限制 (Bootstrapping)：
    // 此时 alloc_percpu 依靠第一块静态预留区 (First Chunk) 提供无锁底座支持，
    // 底层的伙伴系统 (Buddy System) 已全面就绪，提供按 NUMA 节点划分的物理大页。
    // 我们手工格式化根级池 kmem_cache，它将作为“母体”孵化后续所有的 kmem_cache_t 控制块。
    create_cache(kmem_cache_name, &kmem_cache, sizeof(kmem_cache_t));

    // 2. 梯队展开：创建从 8 Bytes 到 1 MB 的通用动态内存申请池
    // 共计 18 个档次 (KMALLOC_CACHE_SIZE)
    uint32 object_size = 8;
    for (uint32 i = 0; i < KMALLOC_CACHE_SIZE; i++) {

        // 从刚刚自举成功的根级池母体中，动态“下蛋”切出一个新的控制块
        kmalloc_cache[i] = kmem_cache_alloc(&kmem_cache);

        // 🛡️ 架构级防御：核心内存池如果分配失败，内核绝对无法存活，必须立即 Panic
        if (!kmalloc_cache[i]) {
            PR_ERROR("PANIC: Failed to allocate kmem_cache_t for %s\n", kmalloc_name[i]);
            while (1) asm_hlt();
        }

        // 格式化并激活这个特定规格的 SLUB 池
        // (内部会自动调用 alloc_percpu 建立它的快路径，并在节点层面建立备用链表)
        create_cache(kmalloc_name[i], kmalloc_cache[i], object_size);

        // 规格翻倍 (8 -> 16 -> 32 ... 512K -> 1M)
        object_size <<= 1;
    }

    // 3. 架构宣发：
    // 运行到这里，kmalloc/kfree 体系正式全面接管内核动态内存！
    // 无论是驱动层、VFS、还是网络栈，都可以享受极致 O(1) 无锁且贴合 NUMA 拓扑的分配。
    PR_OK("SLUB Memory System initialized. Array size: %d, Max Object: 1MB.\n", KMALLOC_CACHE_SIZE);
}