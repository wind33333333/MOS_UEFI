#include "slub.h"
#include "buddy_system.h"
#include "vmm.h"

//kmem_cache_node对象专用缓存池
UINT8 kmem_cache_node_name[16];
kmem_cache_t kmem_cache_node;
kmem_cache_node_t node_kmem_cache_node;

//kmem_cache对象专用缓存池
UINT8 kmem_cache_name[16];
kmem_cache_t kmem_cache;

UINT8 kmalloc_name[18][16];
kmem_cache_t *kmalloc_cache[18];

//新建一个cache
void create_cache(char *cache_name, kmem_cache_t *new_cache, UINT32 object_size) {
    new_cache->name = cache_name;
    new_cache->object_size = object_size_align(object_size);
    new_cache->order_per_slub = object_size_order(new_cache->object_size);
    new_cache->object_per_slub = (PAGE_4K_SIZE << new_cache->order_per_slub) / new_cache->object_size;
    new_cache->slub_count = 0;
    new_cache->total_using = 0;
    new_cache->total_free = 0;
    list_head_init(&new_cache->slub_head);
}

//从cache摘取一个对象
void *alloc_cache_object(kmem_cache_t *cache) {
    kmem_cache_node_t *next_node = CONTAINER_OF(cache->slub_head.next,kmem_cache_node_t,slub_node);
    UINT64 *object = NULL;
    while (next_node != NULL) {
        if (next_node->free_list != NULL) {
            object = next_node->free_list;
            next_node->free_list = (void *)*object;
            next_node->free_count--;
            next_node->using_count++;
            cache->total_free--;
            cache->total_using++;
            return object;
        }
        next_node = CONTAINER_OF(next_node->slub_node.next,kmem_cache_node_t,slub_node);
    }
    return NULL;
}

//cache中添加一个cache_node
void add_cache_node(kmem_cache_t *cache, kmem_cache_node_t *new_cache_node) {
    new_cache_node->slub_node.prev = NULL;
    new_cache_node->slub_node.next = NULL;
    new_cache_node->using_count = 0;
    new_cache_node->free_count = cache->object_per_slub;
    new_cache_node->free_list = page_to_va(alloc_pages(cache->order_per_slub));
    new_cache_node->page_va = new_cache_node->free_list;
    free_list_init(new_cache_node->free_list, cache->object_size, cache->object_per_slub - 1);
    list_add_head(&cache->slub_head, &new_cache_node->slub_node);
    cache->slub_count++;
    cache->total_free += cache->object_per_slub;
}

//从kmem_cache缓存池分配对象
void *kmem_cache_alloc(kmem_cache_t *cache) {
    if (cache == NULL) return NULL;

    //如果kmem_cache_node专用空闲对象只剩下1个则先进行slub扩容
    if (kmem_cache_node.total_free == 1) add_cache_node(&kmem_cache_node, alloc_cache_object(&kmem_cache_node));

    //如果当前cache的总空闲对象只剩下一个则先进行slub扩容
    if (cache->total_free == 0) add_cache_node(cache, alloc_cache_object(&kmem_cache_node));

    //返回缓存池对象
    return alloc_cache_object(cache);
}

//释放一个对象到cache
INT32 free_cache_object(kmem_cache_t *cache, void *object) {
    kmem_cache_node_t *next_node = CONTAINER_OF(cache->slub_head.next,kmem_cache_node_t,slub_node);

    while (next_node != NULL) {
        void *page_va_end = next_node->page_va + (PAGE_4K_SIZE << cache->order_per_slub);
        if (object >= next_node->page_va && object < page_va_end) {
            *(UINT64 *) object = (UINT64) next_node->free_list;
            next_node->free_list = object;
            next_node->free_count++;
            next_node->using_count--;
            cache->total_free++;
            cache->total_using--;
            return 0;
        }
        next_node = CONTAINER_OF(next_node->slub_node.next,kmem_cache_node_t,slub_node);
    }
    return -1;
}

//释放对象到kmem_cache缓存池
INT32 kmem_cache_free(kmem_cache_t *cache, void *object) {
    if (cache == NULL || object == NULL) return -1;

    //释放object
    if (free_cache_object(cache, object) != 0) return -1;

    //遍历当前cache_node如果空闲对象大于一个slub对象数量则释放空闲node
    kmem_cache_node_t *next_node = CONTAINER_OF(cache->slub_head.next,kmem_cache_node_t,slub_node);
    while (next_node != NULL) {
        if (cache->total_free <= cache->object_per_slub) break;
        if (next_node->using_count == 0) {
            free_pages(va_to_page(next_node->page_va));
            list_del(&next_node->slub_node);
            free_cache_object(&kmem_cache_node, next_node);
            cache->total_free -= cache->object_per_slub;
            cache->slub_count--;
        }
        next_node = CONTAINER_OF(next_node->slub_node.next,kmem_cache_node_t,slub_node);
    }
    return 0;
}

//创建kmem_cache缓存池
kmem_cache_t *kmem_cache_create(char *cache_name, UINT32 object_size) {
    if (object_size > MAX_OBJECT_SIZE) return NULL;

    kmem_cache_t *new_cache = kmem_cache_alloc(&kmem_cache);
    create_cache(cache_name, new_cache, object_size);
    return new_cache;
}

//销毁kmem_cache缓存池
INT32 kmem_cache_destroy(kmem_cache_t *destroy_cache) {
    if (destroy_cache == NULL) return -1;

    kmem_cache_node_t *next_node = CONTAINER_OF(destroy_cache->slub_head.next,kmem_cache_node_t,slub_node);
    while (next_node != NULL) {
        free_pages(va_to_page(next_node->page_va));
        kmem_cache_free(&kmem_cache_node, next_node);
        next_node = CONTAINER_OF(next_node->slub_node.next,kmem_cache_node_t,slub_node);
    }
    kmem_cache_free(&kmem_cache, destroy_cache);
    return 0;
}


//通用内存分配器
void *kmalloc(UINT64 size) {
    if (size > MAX_OBJECT_SIZE) return NULL;

    UINT32 index = 0;
    size = object_size_align(size) >> 4;
    while (size >= 1) {
        index++;
        size >>= 1;
    }
    return kmem_cache_alloc(kmalloc_cache[index]);
}

//通用内存释放器
INT32 kfree(void *va) {
    if (va == NULL) return -1;

    for (UINT32 index = 0; index < KMALLOC_CACHE_SIZE; index++) {
        if (kmem_cache_free(kmalloc_cache[index], va) == 0) return 0;
    }

    return -1;
}

//初始化slub分配器
INIT_TEXT void init_slub(void) {
    //创建kmem_cache_node对象缓存池
    strcpy(kmem_cache_node_name, "kmem_cache_node");
    create_cache(kmem_cache_node_name, &kmem_cache_node, sizeof(kmem_cache_node_t));
    add_cache_node(&kmem_cache_node, &node_kmem_cache_node);

    //创建kmem_cache对象缓存池
    strcpy(kmem_cache_name, "kmem_cache");
    create_cache(kmem_cache_name, &kmem_cache, sizeof(kmem_cache_t));

    //创建kmalloc缓存池 8字节到1M
    strcpy(kmalloc_name[0], "kmalloc-8");
    strcpy(kmalloc_name[1], "kmalloc-16");
    strcpy(kmalloc_name[2], "kmalloc-32");
    strcpy(kmalloc_name[3], "kmalloc-64");
    strcpy(kmalloc_name[4], "kmalloc-128");
    strcpy(kmalloc_name[5], "kmalloc-256");
    strcpy(kmalloc_name[6], "kmalloc-512");
    strcpy(kmalloc_name[7], "kmalloc-1k");
    strcpy(kmalloc_name[8], "kmalloc-2k");
    strcpy(kmalloc_name[9], "kmalloc-4k");
    strcpy(kmalloc_name[10], "kmalloc-8k");
    strcpy(kmalloc_name[11], "kmalloc-16k");
    strcpy(kmalloc_name[12], "kmalloc-32k");
    strcpy(kmalloc_name[13], "kmalloc-64k");
    strcpy(kmalloc_name[14], "kmalloc-128k");
    strcpy(kmalloc_name[15], "kmalloc-256k");
    strcpy(kmalloc_name[16], "kmalloc-512k");
    strcpy(kmalloc_name[17], "kmalloc-1m");

    UINT32 object_size = 8;
    for (UINT32 i = 0; i < KMALLOC_CACHE_SIZE; i++) {
        kmalloc_cache[i] = kmem_cache_create(kmalloc_name[i], object_size);
        object_size <<= 1;
    }
}