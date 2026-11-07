#include "../include/slub.h"
#include "alloc_percpu.h"
#include "../include/pmm.h"
#include "printk.h"


char kmem_cache_name[] = "kmem_cache";
kmem_cache_t kmem_cache;
char *kmalloc_name[KMALLOC_CACHE_SIZE] = {
    "kmalloc-8", "kmalloc-16", "kmalloc-32", "kmalloc-64", "kmalloc-128",
    "kmalloc-256", "kmalloc-512", "kmalloc-1k", "kmalloc-2k", "kmalloc-4k",
    "kmalloc-8k", "kmalloc-16k", "kmalloc-32k", "kmalloc-64k", "kmalloc-128k",
    "kmalloc-256k", "kmalloc-512k", "kmalloc-1m"
};
kmem_cache_t *kmalloc_cache[KMALLOC_CACHE_SIZE];

static inline void *page_to_va(page_t *page) { return pa_to_va(page_to_pa(page)); }
static inline page_t *va_to_page(void *va) { return pa_to_page(va_to_pa(va)); }

static inline uint32 object_size_align(uint32 object_size) {
    if (object_size <= 8) return 8;
    --object_size;
    object_size |= object_size >> 1;
    object_size |= object_size >> 2;
    object_size |= object_size >> 4;
    object_size |= object_size >> 8;
    object_size |= object_size >> 16;
    return ++object_size;
}

static inline uint32 object_size_order(uint32 object_size) {
    if (object_size <= 1024) return 0;
    object_size >>= 10;
    uint32 order = 0;
    while (object_size >= 1) {
        order++;
        object_size >>= 1;
    }
    return order;
}

// 🌟 修复 Bug 1：强制使用 uint64，彻底切断指针强转截断的隐患
static inline void free_list_init(void *start_ptr, uint32 obj_size, uint32 count) {
    char *cur = (char *)start_ptr;
    for (uint32 i = 0; i < count; i++) {
        char *next_obj = cur + obj_size;
        *(uint64 *)cur = (uint64)next_obj;
        cur = next_obj;
    }
    *(uint64 *)cur = 0;
}

void create_cache(char *cache_name, kmem_cache_t *cache, uint32 object_size) {
    cache->name            = cache_name;
    cache->object_size     = object_size_align(object_size);
    cache->order_per_slub  = object_size_order(cache->object_size);
    cache->object_per_slub = (PAGE_4K_SIZE << cache->order_per_slub) / cache->object_size;
    cache->slub_count      = 0;
    cache->total_using     = 0;
    cache->total_free      = 0;

    for (int i = 0; i < CONFIG_NUMA_NODES; i++) {
        list_head_init(&cache->node[i].partial_list);
        cache->node[i].nr_partial = 0;
        // spinlock_init(&cache->node[i].list_lock); // 🌟 架构预留：未来在此处初始化自旋锁
    }

    cache->cpu_slab = (kmem_cache_cpu_t *)alloc_percpu(sizeof(kmem_cache_cpu_t), 8);
    for (int i = 0; i < CONFIG_NR_CPUS; i++) {
        kmem_cache_cpu_t *c = per_cpu_ptr(cache->cpu_slab, i);
        c->freelist = NULL;
        c->page = NULL;
    }
}

static inline page_t *new_slub(kmem_cache_t *cache, uint8 nid) {
    page_t *slub = alloc_pages_node(cache->order_per_slub, nid);
    if (!slub) return NULL;

    slub->flags |= (1ULL << PG_SLUB);
    slub->using_count = 0;
    slub->free_count  = cache->object_per_slub;
    slub->free_list   = page_to_va(slub);
    slub->slub_cache  = cache;

    free_list_init(slub->free_list, cache->object_size, cache->object_per_slub - 1);

    // 我们不再在 new_slub 时增加全局统计，改在最终挂载给 CPU 时统计，保证一致性
    return slub;
}

void *kmem_cache_alloc(kmem_cache_t *cache) {
    if (cache == NULL) return NULL;

    // preempt_disable();

    kmem_cache_cpu_t *c = this_cpu_ptr(cache->cpu_slab);
    void *object = c->freelist;

    if (object) {
        c->freelist = (void *)(*(uint64 *)object);
        c->page->free_count--;
        c->page->using_count++;
        cache->total_free--;
        cache->total_using++;
        // preempt_enable();
        return object;
    }

    uint8 current_nid = THIS_CCB->numa_node;
    kmem_cache_node_t *n = &cache->node[current_nid];
    page_t *new_page = NULL;

    // 🌟 修复 Bug 3：跨核摘取 Node 共享池时，必须加锁防撕裂！
    // spin_lock(&n->list_lock);
    if (!list_empty(&n->partial_list)) {
        new_page = CONTAINER_OF(n->partial_list.next, page_t, list);
        list_del(&new_page->list);
        n->nr_partial--;
    }
    // spin_unlock(&n->list_lock);

    if (!new_page) {
        new_page = new_slub(cache, current_nid);
        if (!new_page) {
            // preempt_enable();
            return NULL;
        }
        cache->slub_count++;
        cache->total_free += cache->object_per_slub;
    }

    c->page = new_page;
    c->freelist = new_page->free_list;

    object = c->freelist;
    c->freelist = (void *)(*(uint64 *)object);

    c->page->free_count--;
    c->page->using_count++;
    cache->total_free--;
    cache->total_using++;

    // preempt_enable();
    return object;
}

int32 kmem_cache_free(kmem_cache_t *cache, void *object) {
    if (cache == NULL || object == NULL) return -1;

    // preempt_disable();

    page_t *object_slub = compound_head(va_to_page(object));
    if (object_slub->slub_cache != cache || !(object_slub->flags & (1ULL << PG_SLUB))) {
        // preempt_enable();
        return -1;
    }

    kmem_cache_cpu_t *c = this_cpu_ptr(cache->cpu_slab);

    if (c->page == object_slub) {
        // 🌟 修复 Bug 2：强制使用 uint64 转换，并同步维护 page_t 级别的 free_list 一致性
        *(uint64 *)object = (uint64)c->freelist;
        c->freelist = object;
        // 同步大页元数据，防止 CPU 被切走导致状态分离
        object_slub->free_list = object;

        object_slub->free_count++;
        object_slub->using_count--;
        cache->total_free++;
        cache->total_using--;
        // preempt_enable();
        return 0;
    }

    // 慢路径回收：归还非当前 CPU 独占的大页
    *(uint64 *)object = (uint64)object_slub->free_list;
    object_slub->free_list = object;
    object_slub->free_count++;
    object_slub->using_count--;
    cache->total_free++;
    cache->total_using--;

    if (object_slub->using_count == cache->object_per_slub - 1) {
        uint8 nid = page_to_nid(object_slub);
        kmem_cache_node_t *n = &cache->node[nid];

        // 🌟 锁防线：向 Node 共享链表追加数据，必须防撕裂
        // spin_lock(&n->list_lock);
        list_add(&object_slub->list, &n->partial_list);
        n->nr_partial++;
        // spin_unlock(&n->list_lock);
    }
    else if (object_slub->using_count == 0) {
        uint8 nid = page_to_nid(object_slub);
        kmem_cache_node_t *n = &cache->node[nid];

        // 当共享池比较充足时，释放整个物理页
        if (n->nr_partial >= 2) {
            // 🌟 锁防线：将大页从共享链表彻底摘除
            // spin_lock(&n->list_lock);
            list_del(&object_slub->list);
            n->nr_partial--;
            // spin_unlock(&n->list_lock);

            object_slub->flags &= ~(1ULL << PG_SLUB);
            object_slub->slub_cache = NULL;
            free_pages(object_slub);

            cache->total_free -= cache->object_per_slub;
            cache->slub_count--;
        }
    }

    // preempt_enable();
    return 0;
}

void *kmalloc(uint64 size) {
    if (size == 0 || size > MAX_OBJECT_SIZE) return NULL;
    uint32 index = 0;
    size = object_size_align(size) >> 4;
    while (size >= 1) { index++; size >>= 1; }
    return kmem_cache_alloc(kmalloc_cache[index]);
}

int32 kfree(void *va) {
    if (va == NULL) return -1;
    page_t *slub = compound_head(va_to_page(va));
    if (!(slub->flags & (1ULL << PG_SLUB)) || slub->slub_cache == NULL) return -1;
    return kmem_cache_free(slub->slub_cache, va);
}

kmem_cache_t *kmem_cache_create(char *cache_name, uint32 object_size) {
    if (object_size > MAX_OBJECT_SIZE) return NULL;
    kmem_cache_t *cache = kmem_cache_alloc(&kmem_cache);
    if (!cache) return NULL;
    create_cache(cache_name, cache, object_size);
    return cache;
}