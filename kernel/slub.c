#include "slub.h"
#include "memory.h"

//kmem_cache对象专用缓存池
kmem_cache_t cache_kmem_cache;
kmem_cache_node_t cache_kmem_cache_node;

//kmem_cache_node对象专用缓存池
kmem_cache_t node_kmem_cache;
kmem_cache_node_t node_kmem_cache_node;

//初始化slub分配器
void slub_init(void){
    //创建kmem_cache对象缓存池
    //cache_kmem_cache.name[32]="kmem_cache";
    cache_kmem_cache.partial = &cache_kmem_cache_node;
    cache_kmem_cache.size = sizeof(kmem_cache_t);
    cache_kmem_cache.total_free = PAGE_4K_SIZE/sizeof(kmem_cache_t);
    cache_kmem_cache.total_using = 0;

    cache_kmem_cache_node.free_list = buddy_map_pages(buddy_alloc_pages(0),(void*)memory_management.kernel_end_address,PAGE_ROOT_RW);
    cache_kmem_cache_node.free_count = PAGE_4K_SIZE/sizeof(kmem_cache_t);
    cache_kmem_cache_node.using_count = 0;
    cache_kmem_cache_node.partial.prev = (list_head_t*)&cache_kmem_cache.partial;
    cache_kmem_cache_node.partial.next = NULL;

    UINT64 *current = cache_kmem_cache_node.free_list;  // 获取空闲链表头
    for (UINT32 i = 0; i < cache_kmem_cache_node.free_count-1; i++) {
        UINT64 *next = (UINT64 *)((UINT8 *)current + cache_kmem_cache.size);  // 计算下一个对象地址
        *current = (UINT64)next;
        current = next;           // 更新 current 指针
    }
    *current = NULL;  // 最后一个对象的 next 设置为 NULL

    //创建kmem_cache_node对象缓存池
    //node_kmem_cache.name[32]="kmem_cache_node";
    node_kmem_cache.partial = &node_kmem_cache_node;
    node_kmem_cache.size = sizeof(kmem_cache_node_t);
    node_kmem_cache.total_free = PAGE_4K_SIZE/sizeof(kmem_cache_node_t);
    node_kmem_cache.total_using = 0;

    node_kmem_cache_node.free_list = buddy_map_pages(buddy_alloc_pages(0),(void*)memory_management.kernel_end_address,PAGE_ROOT_RW);
    node_kmem_cache_node.free_count = PAGE_4K_SIZE/sizeof(kmem_cache_node_t);
    node_kmem_cache_node.using_count = 0;
    node_kmem_cache_node.partial.prev = (list_head_t*)&node_kmem_cache.partial;
    node_kmem_cache_node.partial.next = NULL;

    current = node_kmem_cache_node.free_list;  // 获取空闲链表头
    for (UINT32 i = 0; i < cache_kmem_cache_node.free_count-1; i++) {
        UINT64 *next = (UINT64 *)((UINT8 *)current + node_kmem_cache.size);  // 计算下一个对象地址
        *current = (UINT64)next;
        current = next;           // 更新 current 指针
    }
    *current = NULL;  // 最后一个对象的 next 设置为 NULL

}

//创建kmem_cache缓存池
kmem_cache_t* kmem_cache_create(char *name,UINT64 size) {

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