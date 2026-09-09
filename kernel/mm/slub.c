#include "../include/slub.h"
#include "../include/buddy_system.h"

// =========================================================================
// 全局缓存池定义
// =========================================================================

// kmem_cache 描述符自身的超级缓存池 (用于打破先有鸡还是先有蛋的自举问题)
char kmem_cache_name[] = "kmem_cache";
kmem_cache_t kmem_cache;

// kmalloc 通用动态内存缓存池阵列 (从 8 Byte 到 1 MB，共 18 档)
char *kmalloc_name[KMALLOC_CACHE_SIZE] = {
    "kmalloc-8",    "kmalloc-16",   "kmalloc-32",   "kmalloc-64",
    "kmalloc-128",  "kmalloc-256",  "kmalloc-512",  "kmalloc-1k",
    "kmalloc-2k",   "kmalloc-4k",   "kmalloc-8k",   "kmalloc-16k",
    "kmalloc-32k",  "kmalloc-64k",  "kmalloc-128k", "kmalloc-256k",
    "kmalloc-512k", "kmalloc-1m"
};
kmem_cache_t *kmalloc_cache[KMALLOC_CACHE_SIZE];

// =========================================================================
// 内部核心辅助算法
// =========================================================================

/**
 * @brief 强悍的 2 的幂次方向上对齐算法 (Round Up to Power of 2)
 * @note  例如传入 43 字节，将通过位运算抹平右侧 0，最终返回 64。
 *        保证对象大小必定是 2^N，极大地提升了 CPU L1 Cache 命中率并避免跨页碎片。
 */
static inline uint32 object_size_align(uint32 object_size) {
    if (object_size <= 8) return 8; // 最低粒度保底

    --object_size;
    object_size |= object_size >> 1;
    object_size |= object_size >> 2;
    object_size |= object_size >> 4;
    object_size |= object_size >> 8;
    object_size |= object_size >> 16;
    return ++object_size;
}

/**
 * @brief 计算适配对象大小的最优物理页阶数 (Order)
 * @note  小于 1KB 的对象一律只申请 4KB(Order 0) 页；
 *        大于 1KB 的，保证一个 SLUB 页内至少能容纳 4 个该对象，抵消页表碎片消耗。
 */
static inline uint32 object_size_order(uint32 object_size) {
    if (object_size <= 1024) return 0;

    // 要求容量至少能装下 4 个，等价于总大小 = obj_size * 4 (即左移 2 位)
    // 转换为页数计算：总大小 / 4096，等价于右移 12 位。
    // 综合起来：object_size >> 10 (即 12 - 2)
    object_size >>= 10;

    uint32 order = 0;
    while (object_size >= 1) {
        order++;
        object_size >>= 1;
    }
    return order;
}

/**
 * @brief 隐式空闲链表穿针引线 (Freelist Threading)
 * @note  在刚刚分配出的大块连续内存中，利用每个空闲对象的前 8 个字节存储下一个对象的物理/虚拟指针。
 *        【修复 Bug】：严格采用 (char*) 步进运算，彻底杜绝 64 位平台下的高位指针截断与隐式转换灾难。
 */
static inline void free_list_init(void *start_ptr, uint32 obj_size, uint32 count) {
    char *cur = (char *)start_ptr;
    for (uint32 i = 0; i < count; i++) {
        char *next_obj = cur + obj_size;
        // 将 next 指针强转为 uint64 类型存储在当前对象的头部 8 字节中
        *(uint64 *)cur = (uint64)next_obj;
        cur = next_obj;
    }
    // 最后一个节点打上封口标记 NULL
    *(uint64 *)cur = 0;
}

// =========================================================================
// 缓存池基础生命周期管理
// =========================================================================

/**
 * @brief 格式化并挂载一个新的缓存池控制块
 */
static void create_cache(char *cache_name, kmem_cache_t *cache, uint32 object_size) {
    cache->name            = cache_name;
    cache->object_size     = object_size_align(object_size);
    cache->order_per_slub  = object_size_order(cache->object_size);
    // 计算单个 SLUB 物理块中能精准切出的对象个数
    cache->object_per_slub = (PAGE_4K_SIZE << cache->order_per_slub) / cache->object_size;

    cache->slub_count      = 0;
    cache->total_using     = 0;
    cache->total_free      = 0;
    list_head_init(&cache->slub_head);
}

/**
 * @brief 缓存池扩容：向伙伴系统申请一组新的物理连续页框，并切分为 SLUB 块
 */
static inline void new_slub(kmem_cache_t *cache) {
    page_t *slub = alloc_pages(cache->order_per_slub);
    if (!slub) return; // OOM 保护

    // 🌟 修复 Bug：严格使用位操作打上 SLUB 烙印，绝不污染复合大页的其他标志！
    slub->flags |= (1ULL << PG_SLUB);

    slub->using_count = 0;
    slub->free_count  = cache->object_per_slub;
    slub->free_list   = page_to_va(slub);

    // 建立 page_t 到归属缓存池的逆向溯源通道，实现 O(1) 的 kfree
    slub->slub_cache  = cache;

    // 执行内存切片与隐式空闲链表穿针引线
    free_list_init(slub->free_list, cache->object_size, cache->object_per_slub - 1);

    // 优先挂在头部，利用 L1 Cache 局部性原理
    list_add_head(&cache->slub_head, &slub->list);

    cache->slub_count++;
    cache->total_free += cache->object_per_slub;
}

/**
 * @brief 缓存池缩容：回收利用率为零的 SLUB 块，将物理内存还给伙伴系统
 */
static inline void recycle_slub(kmem_cache_t *cache) {
    // 🌟 修复 Use-After-Free 致命 Bug：遍历链表并伴随删除节点操作，必须使用 Safe 模式保存 next 副本！
    list_head_t *pos = cache->slub_head.next;
    list_head_t *n;

    while (pos != &cache->slub_head) {
        n = pos->next; // 提前保存下一节点，因为当前 pos 随时可能被摧毁归还给系统

        page_t *slub = CONTAINER_OF(pos, page_t, list);

        // 阈值限制：避免缓存震荡 (Thrashing)。只在总空闲量极为宽裕时才执行物理回收
        if (cache->total_free <= cache->object_per_slub) break;

        // 当该 SLUB 块中所有对象均被归还 (100% 空闲) 时，执行全本拆除
        if (slub->using_count == 0) {
            list_del(&slub->list);

            // 撕掉 SLUB 专属标签，准备回归底层大自然
            slub->flags &= ~(1ULL << PG_SLUB);
            slub->slub_cache = NULL;

            free_pages(slub);

            cache->total_free -= cache->object_per_slub;
            cache->slub_count--;
        }
        pos = n; // 安全步进
    }
}

// =========================================================================
// 核心分配与回收操作
// =========================================================================

/**
 * @brief 从指定的缓存池中摘取一个对象
 */
void *kmem_cache_alloc(kmem_cache_t *cache) {
    if (cache == NULL) return NULL;

    // 饥饿检测：若整个缓存池被抽干，触发底层的按需扩容
    if (cache->total_free == 0) {
        new_slub(cache);
    }

    list_head_t *pos = cache->slub_head.next;
    while (pos != &cache->slub_head) {
        page_t *slub = CONTAINER_OF(pos, page_t, list);

        // 发现猎物：当前 SLUB 块尚有盈余
        if (slub->free_list) {
            void *object = slub->free_list;

            // 剥离指针：顺藤摸瓜取出下一个空闲节点的指针覆盖在 free_list 上
            slub->free_list = (void *)(*(uint64 *)object);

            slub->free_count--;
            slub->using_count++;
            cache->total_free--;
            cache->total_using++;

            // 🌟 MRU 优化：由于该 slub 发生了活跃读写，其内容极大概率存在于 CPU Cache 中。
            // 把它移到链表最头部，下一次分配将直接 O(1) 命中！
            if (pos != cache->slub_head.next) {
                list_del(&slub->list);
                list_add_head(&cache->slub_head, &slub->list);
            }

            return object;
        }
        pos = pos->next;
    }
    return NULL;
}

/**
 * @brief 将使用完毕的对象归还给对应的缓存池
 */
int32 kmem_cache_free(kmem_cache_t *cache, void *object) {
    if (cache == NULL || object == NULL) return -1;

    // 🌟 史诗级 O(N) 到 O(1) 性能跨越优化！
    // 绝对摒弃遍历。直接利用 HHDM 机制通过虚拟地址瞬间推算出该对象所在的 page_t！
    page_t *object_slub = compound_head(va_to_page(object));

    // 防御性拦截：校验归属权，防止释放跨界的野指针或误释放普通底层页
    if (object_slub->slub_cache != cache || !(object_slub->flags & (1ULL << PG_SLUB))) {
        return -1;
    }

    // 将归还的节点头 8 字节接入隐式链表，并更新当前 SLUB 的 free_list 指针指向它 (头插法)
    *(uint64 *)object = (uint64)object_slub->free_list;
    object_slub->free_list = object;

    object_slub->free_count++;
    object_slub->using_count--;

    cache->total_free++;
    cache->total_using--;

    // 触发被动的垃圾回收检查，若缓存池空闲泛滥则归还物理资源
    recycle_slub(cache);

    return 0;
}

// =========================================================================
// 统一分配器接口层 (kmalloc/kfree)
// =========================================================================

void *kmalloc(uint64 size) {
    if (size == 0 || size > MAX_OBJECT_SIZE) return NULL;

    uint32 index = 0;
    // 使用极速位移定位法计算落入哪个 kmalloc_cache 档位
    // 例如：size=10 -> object_size_align 会补齐到 16 -> 右移 4 为 1 -> index=1 (即 kmalloc-16)
    size = object_size_align(size) >> 4;
    while (size >= 1) {
        index++;
        size >>= 1;
    }
    return kmem_cache_alloc(kmalloc_cache[index]);
}

void *kzalloc(uint64 size) {
    if (size == 0) return NULL;
    void *ptr = kmalloc(size);
    if (ptr) asm_mem_set(ptr, 0, size);
    return ptr;
}

int32 kfree(void *va) {
    if (va == NULL) return -1;

    // 🌟 神级溯源：无论什么对象，丢进 kfree，瞬间提取所属 page_t，再倒推找出所属 cache
    page_t *slub = compound_head(va_to_page(va));

    // 拦截防护：绝不允许释放非 SLUB 系统产出的普通内存页
    if (!(slub->flags & (1ULL << PG_SLUB)) || slub->slub_cache == NULL) {
        return -1;
    }

    return kmem_cache_free(slub->slub_cache, va);
}

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
}

// =========================================================================
// 缓存池高阶管理接口 (销毁/新建)
// =========================================================================

kmem_cache_t *kmem_cache_create(char *cache_name, uint32 object_size) {
    if (object_size > MAX_OBJECT_SIZE) return NULL;

    kmem_cache_t *cache = kmem_cache_alloc(&kmem_cache);
    if (!cache) return NULL;

    create_cache(cache_name, cache, object_size);
    return cache;
}

int32 kmem_cache_destroy(kmem_cache_t *cache) {
    if (cache == NULL) return -1;

    list_head_t *pos = cache->slub_head.next;
    list_head_t *n;

    // 🌟 修复：安全遍历释放所有残留物理块
    while (pos != &cache->slub_head) {
        n = pos->next;
        page_t *slub = CONTAINER_OF(pos, page_t, list);

        slub->flags &= ~(1ULL << PG_SLUB);
        slub->slub_cache = NULL;
        free_pages(slub);

        pos = n;
    }

    // 销毁缓存块控制体本身
    kmem_cache_free(&kmem_cache, cache);
    return 0;
}