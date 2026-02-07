#include "slub.h"
#include "buddy_system.h"
#include "vmm.h"

//kmem_cache专用缓存池
char kmem_cache_name[] = {"kmem_cache"};
kmem_cache_t kmem_cache;

//kmalloc专用缓存池
char *kmalloc_name[KMALLOC_CACHE_SIZE] = {
    "kmalloc-8",
    "kmalloc-16",
    "kmalloc-32",
    "kmalloc-64",
    "kmalloc-128",
    "kmalloc-256",
    "kmalloc-512",
    "kmalloc-1k",
    "kmalloc-2k",
    "kmalloc-4k",
    "kmalloc-8k",
    "kmalloc-16k",
    "kmalloc-32k",
    "kmalloc-64k",
    "kmalloc-128k",
    "kmalloc-256k",
    "kmalloc-512k",
    "kmalloc-1m"
};
kmem_cache_t *kmalloc_cache[KMALLOC_CACHE_SIZE];

//把对象size对齐到2^n字节，提高内存访问性能和每页刚好整数
static inline uint32 object_size_align(uint32 objcet_size) {
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
static inline uint32 object_size_order(uint32 objcet_size) {
    if (objcet_size <= 1024) return 0;
    objcet_size >>= 11;
    uint32 order = 0;
    while (objcet_size >= 1) {
        order++;
        objcet_size >>= 1;
    };
    return order;
}

//空闲链表初始化
static inline void free_list_init(uint64* next,uint32 size,uint32 count) {
    while (count--) {
        *next = (uint64)next + size;
        next = (uint64*)*next;
    }
    *next = 0;
}

//新建一个cache
void create_cache(char *cache_name, kmem_cache_t *cache, uint32 object_size) {
    cache->name = cache_name;
    cache->object_size = object_size_align(object_size);
    cache->order_per_slub = object_size_order(cache->object_size);
    cache->object_per_slub = (PAGE_4K_SIZE << cache->order_per_slub) / cache->object_size;
    cache->slub_count = 0;
    cache->total_using = 0;
    cache->total_free = 0;
    list_head_init(&cache->slub_head);
}

//cache中添加一个slub
static inline void new_slub(kmem_cache_t *cache) {
    page_t *slub = alloc_pages(cache->order_per_slub);
    asm_bts(&slub->flags,PG_SLUB);
    slub->list.prev = NULL;
    slub->list.next = NULL;
    slub->using_count = 0;
    slub->free_count = cache->object_per_slub;
    slub->free_list = page_to_va(slub);
    slub->slub_cache = cache;
    free_list_init(slub->free_list, cache->object_size, cache->object_per_slub - 1);
    list_add_head(&cache->slub_head, &slub->list);
    cache->slub_count++;
    cache->total_free += cache->object_per_slub;
}

//cache中回收空闲slub
static inline void recycle_slub(kmem_cache_t *cache) {
    //遍历当前cache如果空闲对象大于一个slub对象数量则释放空闲node
    list_head_t *pos = cache->slub_head.next;
    while (pos != &cache->slub_head) {
        page_t *slub = CONTAINER_OF(pos,page_t,list);
        if (cache->total_free <= cache->object_per_slub) break;
        if (slub->using_count == 0) {
            list_del(&slub->list);
            free_pages(slub);
            cache->total_free -= cache->object_per_slub;
            cache->slub_count--;
        }
        pos = pos->next;
    }
}

//从cache摘取一个对象
static inline void *alloc_cache_object(kmem_cache_t *cache) {
    list_head_t *pos = cache->slub_head.next;
    while (pos != &cache->slub_head) {
        page_t *slub = CONTAINER_OF(pos,page_t,list);
        if (slub->free_list) {
            uint64 *object = slub->free_list;
            slub->free_list = (void *)*object;
            slub->free_count--;
            slub->using_count++;
            cache->total_free--;
            cache->total_using++;
            return object;
        }
        pos = pos->next;
    }
    return NULL;
}

//释放一个对象到cache
static inline int32 free_cache_object(kmem_cache_t *cache, void *object) {
    list_head_t *pos = cache->slub_head.next;
    while (pos != &cache->slub_head) {
        page_t *slub = CONTAINER_OF(pos,page_t,list);
        page_t *object_slub = compound_head(va_to_page(object));
        if (object_slub == slub){
            *(uint64 *)object = (uint64) slub->free_list;
            slub->free_list = object;
            slub->free_count++;
            slub->using_count--;
            cache->total_free++;
            cache->total_using--;
            return 0;
        }
        pos = pos->next;
    }
    return -1;
}

//从kmem_cache缓存池分配对象
void *kmem_cache_alloc(kmem_cache_t *cache) {
    if (cache == NULL) return NULL;

    //如果当前cache的总空闲对象为空则先进行slub扩容
    if (cache->total_free == 0) new_slub(cache);

    //返回缓存池对象
    return alloc_cache_object(cache);
}

//释放对象到kmem_cache缓存池
int32 kmem_cache_free(kmem_cache_t *cache, void *object) {
    if (cache == NULL || object == NULL) return -1;

    //释放object
    if (free_cache_object(cache, object)) return -1;

    //检查cache是否如果空闲slub大于1个则回收部分空闲slub
    recycle_slub(cache);

    return 0;
}

//创建kmem_cache缓存池
kmem_cache_t *kmem_cache_create(char *cache_name, uint32 object_size) {
    if (object_size > MAX_OBJECT_SIZE) return NULL;

    kmem_cache_t *cache = kmem_cache_alloc(&kmem_cache);
    create_cache(cache_name, cache, object_size);
    return cache;
}

//销毁kmem_cache缓存池
int32 kmem_cache_destroy(kmem_cache_t *cache) {
    if (cache == NULL) return -1;

    list_head_t *pos = cache->slub_head.next;
    while (pos != &cache->slub_head) {
        page_t *slub = CONTAINER_OF(pos,page_t,list);
        free_pages(slub);
        pos = pos->next;
    }
    kmem_cache_free(&kmem_cache,cache);
    return 0;
}


//通用内存分配器
void *kmalloc(uint64 size) {
    if (size == 0 || size > MAX_OBJECT_SIZE) return NULL;

    uint32 index = 0;
    size = object_size_align(size) >> 4;
    while (size >= 1) {
        index++;
        size >>= 1;
    }
    return kmem_cache_alloc(kmalloc_cache[index]);
}

//通用内存分配器(清零)
inline void *kzalloc(uint64 size) {
    if (size  == 0) return NULL;
    void *ptr = kmalloc(size);
    asm_mem_set(ptr,0,size);
    return ptr;
}

//通用内存释放器
int32 kfree(void *va) {
    if (va == NULL) return -1;

    page_t *slub = compound_head(va_to_page(va));
    kmem_cache_free(slub->slub_cache,va);

    return 0;
}

//初始化slub分配器
INIT_TEXT void init_slub(void) {
    //创建kmem_cache对象缓存池
    create_cache(kmem_cache_name, &kmem_cache, sizeof(kmem_cache_t));

    //创建kmalloc缓存池 8字节到1M
    uint32 object_size = 8;
    for (uint32 i = 0; i < KMALLOC_CACHE_SIZE; i++) {
        kmalloc_cache[i] = kmem_cache_create(kmalloc_name[i], object_size);
        object_size <<= 1;
    }

}