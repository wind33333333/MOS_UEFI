#include "slub.h"
#include "buddy_system.h"
#include "memory.h"

//kmem_cache对象专用缓存池
char kmem_cache_name[16];
kmem_cache_t cache_kmem_cache;
kmem_cache_node_t cache_kmem_cache_node;

//kmem_cache_node对象专用缓存池
char kmem_cache_node_name[16];
kmem_cache_t node_kmem_cache;
kmem_cache_node_t node_kmem_cache_node;

//初始化slub分配器
void slub_init(void){
    //创建kmem_cache对象缓存池
    char name[]={"kmem_cache"};
    memcpy(name,kmem_cache_name,sizeof(name));
    cache_kmem_cache.name=kmem_cache_name;
    cache_kmem_cache.object_size = object_size_align(sizeof(kmem_cache_t));
    cache_kmem_cache.order_per_slub = object_size_order(cache_kmem_cache.object_size);
    cache_kmem_cache.object_per_slub = (PAGE_4K_SIZE<<cache_kmem_cache.order_per_slub)/cache_kmem_cache.object_size;
    cache_kmem_cache.total_using = 0;
    cache_kmem_cache.total_free = (PAGE_4K_SIZE<<cache_kmem_cache.order_per_slub)/cache_kmem_cache.object_size;
    cache_kmem_cache.partial = &cache_kmem_cache_node;

    cache_kmem_cache_node.partial.prev = (list_head_t*)&cache_kmem_cache.partial;
    cache_kmem_cache_node.partial.next = NULL;
    cache_kmem_cache_node.using_count = 0;
    cache_kmem_cache_node.free_count = cache_kmem_cache.total_free;
    cache_kmem_cache_node.free_list = buddy_map_pages(buddy_alloc_pages(cache_kmem_cache.order_per_slub),(void*)memory_management.kernel_end_address,PAGE_ROOT_RW);

    UINT64 *next = cache_kmem_cache_node.free_list;  // 获取空闲链表头
    for (UINT32 i = 0; i < cache_kmem_cache.object_per_slub-1; i++) {
        *next = (UINT64)next + cache_kmem_cache.object_size; // 计算下一个对象地址
        next = (UINT64*)*next;           // 更新 current 指针
    }
    *next = NULL;  // 最后一个对象的 next 设置为 NULL

    //创建kmem_cache_node对象缓存池
    char name1[]={"kmem_cache_node"};
    memcpy(name1,kmem_cache_node_name,sizeof(name1));
    node_kmem_cache.name=kmem_cache_node_name;
    node_kmem_cache.object_size = object_size_align(sizeof(kmem_cache_node_t));
    node_kmem_cache.order_per_slub = object_size_order(node_kmem_cache.object_size);
    node_kmem_cache.object_per_slub = (PAGE_4K_SIZE<<node_kmem_cache.order_per_slub)/node_kmem_cache.object_size;
    node_kmem_cache.total_using = 0;
    node_kmem_cache.total_free = (PAGE_4K_SIZE<<node_kmem_cache.order_per_slub)/node_kmem_cache.object_size;
    node_kmem_cache.partial = &node_kmem_cache_node;

    node_kmem_cache_node.partial.prev = (list_head_t*)&node_kmem_cache.partial;
    node_kmem_cache_node.partial.next = NULL;
    node_kmem_cache_node.using_count = 0;
    node_kmem_cache_node.free_count = node_kmem_cache.total_free;
    node_kmem_cache_node.free_list = buddy_map_pages(buddy_alloc_pages(node_kmem_cache.order_per_slub),(void*)memory_management.kernel_end_address,PAGE_ROOT_RW);

    next = node_kmem_cache_node.free_list;  // 获取空闲链表头
    for (UINT32 i = 0; i < node_kmem_cache.object_per_slub-1; i++) {
        *next = (UINT64)next + node_kmem_cache.object_size; // 计算下一个对象地址
        next = (UINT64*)*next;           // 更新 current 指针
    }
    *next = NULL;  // 最后一个对象的 next 设置为 NULL

}

//创建kmem_cache缓存池
kmem_cache_t* kmem_cache_create(char *name,UINT64 object_size) {
    kmem_cache_t *kmem_cache = kmem_cache_alloc(&cache_kmem_cache);
    kmem_cache->name=name;
    kmem_cache->object_size = object_size_align(object_size);
    kmem_cache->order_per_slub = object_size_order(kmem_cache->object_size);
    kmem_cache->object_per_slub = (PAGE_4K_SIZE << kmem_cache->order_per_slub)/kmem_cache->object_size;
    kmem_cache->total_using = 0;
    kmem_cache->total_free = kmem_cache->object_per_slub;
    kmem_cache->partial = kmem_cache_alloc(&node_kmem_cache);

    kmem_cache->partial->partial.prev = (list_head_t*)&kmem_cache->partial;
    kmem_cache->partial->partial.next = NULL;
    kmem_cache->partial->using_count = 0;
    kmem_cache->partial->free_count = kmem_cache->total_free;
    kmem_cache->partial->free_list = buddy_map_pages(buddy_alloc_pages(kmem_cache->order_per_slub),(void*)memory_management.kernel_end_address,PAGE_ROOT_RW);

    UINT64 *next = kmem_cache->partial->free_list;  // 获取空闲链表头
    for (UINT32 i = 0; i < kmem_cache->object_per_slub-1; i++) {
        *next = (UINT64)next + kmem_cache->object_size; // 计算下一个对象地址
        next = (UINT64*)*next;           // 更新 current 指针
    }
    *next = NULL;  // 最后一个对象的 next 设置为 NULL
    return kmem_cache;
}

//销毁kmem_cache缓存池
void kmem_cache_destroy(kmem_cache_t *kmem_cache) {

}

//从kmem_cache缓存池分配对象
void* kmem_cache_alloc(kmem_cache_t *kmem_cache) {

}

//释放对象到kmem_cache缓存池
void kmem_cache_free(kmem_cache_t *kmem_cache, void *ptr) {

}

//通用内存分配器
void *kmaollc(UINT64 size) {
    return 0;
}

//通用内存释放器
int kfree(void *virtual_address) {
    return 0;
}