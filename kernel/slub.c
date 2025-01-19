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
    cache_kmem_cache.slub_count = 1;
    cache_kmem_cache.total_using = 0;
    cache_kmem_cache.total_free = (PAGE_4K_SIZE<<cache_kmem_cache.order_per_slub)/cache_kmem_cache.object_size;
    cache_kmem_cache.partial = &cache_kmem_cache_node;

    cache_kmem_cache_node.partial.prev = (list_head_t*)&cache_kmem_cache.partial;
    cache_kmem_cache_node.partial.next = NULL;
    cache_kmem_cache_node.using_count = 0;
    cache_kmem_cache_node.free_count = cache_kmem_cache.total_free;
    cache_kmem_cache_node.free_list = buddy_map_pages(buddy_alloc_pages(cache_kmem_cache.order_per_slub),(void*)memory_management.kernel_end_address,PAGE_ROOT_RW);
    cache_kmem_cache_node.object_start_vaddr = cache_kmem_cache_node.free_list;

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
    node_kmem_cache.slub_count = 1;
    node_kmem_cache.total_using = 0;
    node_kmem_cache.total_free = (PAGE_4K_SIZE<<node_kmem_cache.order_per_slub)/node_kmem_cache.object_size;
    node_kmem_cache.partial = &node_kmem_cache_node;

    node_kmem_cache_node.partial.prev = (list_head_t*)&node_kmem_cache.partial;
    node_kmem_cache_node.partial.next = NULL;
    node_kmem_cache_node.using_count = 0;
    node_kmem_cache_node.free_count = node_kmem_cache.total_free;
    node_kmem_cache_node.free_list = buddy_map_pages(buddy_alloc_pages(node_kmem_cache.order_per_slub),(void*)memory_management.kernel_end_address,PAGE_ROOT_RW);
    node_kmem_cache_node.object_start_vaddr = node_kmem_cache_node.free_list;

    next = node_kmem_cache_node.free_list;  // 获取空闲链表头
    for (UINT32 i = 0; i < node_kmem_cache.object_per_slub-1; i++) {
        *next = (UINT64)next + node_kmem_cache.object_size; // 计算下一个对象地址
        next = (UINT64*)*next;           // 更新 current 指针
    }
    *next = NULL;  // 最后一个对象的 next 设置为 NULL

    char name2[]={"kmalloc_16"};
    kmem_cache_t *kmalloc_16 = kmem_cache_create(name2,15);
    UINT16 *ptr = kmem_cache_alloc(kmalloc_16);
    *ptr=0x12346;


}

//创建kmem_cache缓存池
kmem_cache_t* kmem_cache_create(char *name,UINT64 object_size) {
    if (cache_kmem_cache.total_free == 1) {
        //如果cache_kmem_cache只剩下最后一个对象了就扩容cache_kmem_cache的slub
    }
    if (node_kmem_cache.total_free == 1) {
        //如果node_kmem_cache只剩下最后一个对象了就扩容node_kmem_cache的slub
    }
    kmem_cache_t *cerate_cache = kmem_cache_alloc(&cache_kmem_cache);
    cerate_cache->name=name;
    cerate_cache->object_size = object_size_align(object_size);
    cerate_cache->order_per_slub = object_size_order(cerate_cache->object_size);
    cerate_cache->object_per_slub = (PAGE_4K_SIZE << cerate_cache->order_per_slub)/cerate_cache->object_size;
    cerate_cache->slub_count = 1;
    cerate_cache->total_using = 0;
    cerate_cache->total_free = cerate_cache->object_per_slub;
    cerate_cache->partial = kmem_cache_alloc(&node_kmem_cache);

    cerate_cache->partial->partial.prev = (list_head_t*)&cerate_cache->partial;
    cerate_cache->partial->partial.next = NULL;
    cerate_cache->partial->using_count = 0;
    cerate_cache->partial->free_count = cerate_cache->total_free;
    cerate_cache->partial->free_list = buddy_map_pages(buddy_alloc_pages(cerate_cache->order_per_slub),(void*)memory_management.kernel_end_address,PAGE_ROOT_RW);
    cerate_cache->partial->object_start_vaddr =  cerate_cache->partial->free_list;

    UINT64 *next = cerate_cache->partial->free_list;  // 获取空闲链表头
    for (UINT32 i = 0; i < cerate_cache->object_per_slub-1; i++) {
        *next = (UINT64)next + cerate_cache->object_size; // 计算下一个对象地址
        next = (UINT64*)*next;           // 更新 current 指针
    }
    *next = NULL;  // 最后一个对象的 next 设置为 NULL
    return cerate_cache;
}

//销毁kmem_cache缓存池
void kmem_cache_destroy(kmem_cache_t *destroy_cache) {
    for (UINT32 i = 0; i < destroy_cache->slub_count; i++) {
        UINT64* vir_addr = vaddr_to_pte_vaddr(destroy_cache->partial);
        UINT64 phy_addr = *vir_addr & PAGE_4K_MASK;
        page_t* page = phyaddr_to_page(phy_addr);
        buddy_free_pages(page);
        list_del(destroy_cache->partial);
        kmem_cache_free(&node_kmem_cache,destroy_cache->partial);
    }
    kmem_cache_free(&cache_kmem_cache,destroy_cache);
}

//从kmem_cache缓存池分配对象
void* kmem_cache_alloc(kmem_cache_t *cache) {
    //如果当前cache的总空闲对象为空着先进行slub扩容
    if(cache->total_free == 0) {
        //如果node_kmem_cache只剩下最后一个对象了就扩容node_kmem_cache的slub
        if (node_kmem_cache.total_free == 1) {
        }
    }

    kmem_cache_node_t* cache_node =cache->partial;
    for (UINT32 i = 0; i < cache->slub_count; i++) {
        if (cache_node->free_list == NULL) {
            cache_node = (kmem_cache_node_t*)cache_node->partial.next;
            continue;
        }
        cache_node->free_count--;
        cache_node->using_count++;
        UINT64* ptr = cache_node->free_list;
        cache_node->free_list = *ptr;
        return ptr;
    }
    return NULL;
}

//释放对象到kmem_cache缓存池
void kmem_cache_free(kmem_cache_t *cache, void *ptr) {
    kmem_cache_node_t* cache_node =cache->partial;
    void* align_addr = (UINT64)ptr & (PAGE_4K_MASK << cache->order_per_slub);
    for (UINT32 i = 0; i < cache->slub_count; i++) {
        if (align_addr != cache_node->object_start_vaddr) {
            cache_node = cache_node->partial.next;
            continue;
        }
        cache_node->free_count++;
        cache_node->using_count--;
        *(UINT64*)ptr = cache_node->free_list;
        cache_node->free_list = ptr;

        //如果当前slub所有对象已经释放，且cache总空闲对象大于slub对象则释放当前的slub到伙伴系统
        if (cache_node->using_count == 0 && cache->total_free > cache->object_per_slub) {
            UINT64* vir_addr = vaddr_to_pte_vaddr(cache->partial->object_start_vaddr);
            UINT64 phy_addr = *vir_addr & PAGE_4K_MASK;
            page_t* page = phyaddr_to_page(phy_addr);
            buddy_free_pages(page);
            list_del(cache->partial);
            kmem_cache_free(&node_kmem_cache,cache->partial);
        }
        return;
    }
}

//通用内存分配器
void *kmaollc(UINT64 size) {
    return 0;
}

//通用内存释放器
int kfree(void *virtual_address) {
    return 0;
}