#include "slub.h"
#include "buddy_system.h"
#include "memory.h"

//kmem_cache对象专用缓存池
UINT8 kmem_cache_name[16];
kmem_cache_t kmem_cache;
kmem_cache_node_t cache_kmem_cache_node;

//kmem_cache_node对象专用缓存池
UINT8 kmem_cache_node_name[16];
kmem_cache_t kmem_cache_node;
kmem_cache_node_t node_kmem_cache_node;

UINT8 kmalloc_name[18][16];
kmem_cache_t *kmalloc_cache[18];

//初始化slub分配器
void slub_init(void) {
    //创建kmem_cache对象缓存池
    strcpy(kmem_cache_name, "kmem_cache");
    create_cache(kmem_cache_name, &kmem_cache, sizeof(kmem_cache_t));
    add_cache_node(&kmem_cache, &cache_kmem_cache_node);

    //创建kmem_cache_node对象缓存池
    strcpy(kmem_cache_node_name, "kmem_cache_node");
    create_cache(kmem_cache_node_name, &kmem_cache_node, sizeof(kmem_cache_node_t));
    add_cache_node(&kmem_cache_node, &node_kmem_cache_node);

    //创建kmalloc缓存池
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
    for (UINT32 i=0;i<19;i++) {
        kmalloc_cache[i]=kmem_cache_create(kmalloc_name[i],object_size);
        object_size <<=1;
    }






    while (TRUE);
}

//创建kmem_cache缓存池
kmem_cache_t *kmem_cache_create(char *cache_name, UINT32 object_size) {
    kmem_cache_t *new_cache = kmem_cache_alloc(&kmem_cache);
    kmem_cache_node_t *new_cache_node = kmem_cache_alloc(&kmem_cache_node);

    create_cache(cache_name, new_cache, object_size);
    add_cache_node(new_cache, new_cache_node);
    return new_cache;
}

//销毁kmem_cache缓存池
void kmem_cache_destroy(kmem_cache_t *destroy_cache) {
    kmem_cache_node_t *next_node = (kmem_cache_node_t *) destroy_cache->slub_head.next;
    while (next_node != NULL) {
        buddy_unmap_pages(next_node->object_start_vaddr);
        kmem_cache_free(&kmem_cache_node, next_node);
        next_node = (kmem_cache_node_t *) next_node->slub_node.next;
    }
    kmem_cache_free(&kmem_cache, destroy_cache);
}

//从kmem_cache缓存池分配对象
void *kmem_cache_alloc(kmem_cache_t *cache) {
    //如果kmem_cache_node专用空闲对象只剩下1个则先进行slub扩容
    if (kmem_cache_node.total_free == 1) add_cache_node(&kmem_cache_node, alloc_cache_object(&kmem_cache_node));

    //如果当前cache的总空闲对象只剩下一个则先进行slub扩容
    if (cache->total_free == 0) add_cache_node(cache, alloc_cache_object(&kmem_cache_node));

    //返回缓存池对象
    return alloc_cache_object(cache);
}

//释放对象到kmem_cache缓存池
void kmem_cache_free(kmem_cache_t *cache, void *object) {
    //释放object
    free_cache_object(cache, object);

    //遍历当前cache_node如果空闲对象大于一个slub对象数量则释放空闲node
    kmem_cache_node_t *next_node = (kmem_cache_node_t *) cache->slub_head.next;
    while (next_node != NULL) {
        if (cache->total_free <= cache->object_per_slub) break;
        if (next_node->using_count == 0) {
            buddy_unmap_pages(next_node->object_start_vaddr);
            list_del((list_head_t *) next_node);
            free_cache_object(&kmem_cache_node, next_node);
            cache->total_free -= cache->object_per_slub;
            cache->slub_count--;
        }
        next_node = (kmem_cache_node_t *) next_node->slub_node.next;
    }
}

//从cache摘取一个对象
void *alloc_cache_object(kmem_cache_t *cache) {
    kmem_cache_node_t *next_node = (kmem_cache_node_t *) cache->slub_head.next;
    UINT64 *object = NULL;
    while (next_node != NULL) {
        if (next_node->free_list != NULL) {
            object = next_node->free_list;
            next_node->free_list = (void *) *object;
            next_node->free_count--;
            next_node->using_count++;
            cache->total_free--;
            cache->total_using++;
            return object;
        }
        next_node = (kmem_cache_node_t *) next_node->slub_node.next;
    }
    return NULL;
}

//释放一个对象到cache
void free_cache_object(kmem_cache_t *cache, void *object) {
    kmem_cache_node_t *next_node = (kmem_cache_node_t *) cache->slub_head.next;
    void *align_addr = (void *) ((UINT64) object & (PAGE_4K_MASK << cache->order_per_slub));

    while (next_node != NULL) {
        if (align_addr == next_node->object_start_vaddr) {
            *(UINT64 *) object = (UINT64) next_node->free_list;
            next_node->free_list = object;
            next_node->free_count++;
            next_node->using_count--;
            cache->total_free++;
            cache->total_using--;
            return;
        }
        next_node = (kmem_cache_node_t *) next_node->slub_node.next;
    }
}

//新建一个cache
void create_cache(char *cache_name, kmem_cache_t *new_cache, UINT32 object_size) {
    new_cache->name = cache_name;
    new_cache->object_size = object_size_align(object_size);
    new_cache->order_per_slub = object_size_order(new_cache->object_size);
    new_cache->object_per_slub = (PAGE_4K_SIZE << new_cache->order_per_slub) / new_cache->object_size;
    new_cache->slub_count = 0;
    new_cache->total_using = 0;
    new_cache->total_free = (PAGE_4K_SIZE << new_cache->order_per_slub) / new_cache->object_size;
    new_cache->slub_head.prev = NULL;
    new_cache->slub_head.next = NULL;
}

//cache中添加一个cache_node
void add_cache_node(kmem_cache_t *cache, kmem_cache_node_t *new_cache_node) {
    new_cache_node->slub_node.prev = NULL;
    new_cache_node->slub_node.next = NULL;
    new_cache_node->using_count = 0;
    new_cache_node->free_count = cache->object_per_slub;
    new_cache_node->free_list = buddy_map_pages(buddy_alloc_pages(cache->order_per_slub),
                                                (void *) memory_management.kernel_end_address,PAGE_ROOT_RW);
    new_cache_node->object_start_vaddr = new_cache_node->free_list;
    free_list_init(new_cache_node->free_list, cache->object_size, cache->object_per_slub - 1);
    list_add_forward(&cache->slub_head, &new_cache_node->slub_node);
    cache->slub_count++;
    cache->total_free = cache->object_per_slub;
}

//通用内存分配器
void *kmaollc(UINT64 size) {
    UINT32 index=0;
    size=object_size_align(size) >> 4;
    while (size >= 1) {
        index++;
        size >>=1;
    }
    return kmem_cache_alloc(kmalloc_cache[index]);
}

//通用内存释放器
void kfree(void *virtual_address) {
    align_addr= virtual_address;
}
