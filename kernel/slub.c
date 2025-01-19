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
    cache_kmem_cache.slub_head.prev = NULL;
    cache_kmem_cache.slub_head.next = NULL;

    cache_kmem_cache_node.slub_node.prev = NULL;
    cache_kmem_cache_node.slub_node.next = NULL;
    cache_kmem_cache_node.using_count = 0;
    cache_kmem_cache_node.free_count = cache_kmem_cache.total_free;
    cache_kmem_cache_node.free_list = buddy_map_pages(buddy_alloc_pages(cache_kmem_cache.order_per_slub),(void*)memory_management.kernel_end_address,PAGE_ROOT_RW);
    cache_kmem_cache_node.object_start_vaddr = cache_kmem_cache_node.free_list;
    free_list_init(cache_kmem_cache_node.free_list,cache_kmem_cache.object_size,cache_kmem_cache.object_per_slub-1);
    list_add_forward(&cache_kmem_cache.slub_head,&cache_kmem_cache_node.slub_node);

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
    node_kmem_cache.slub_head.prev = NULL;
    node_kmem_cache.slub_head.prev = NULL;

    node_kmem_cache_node.slub_node.prev = NULL;
    node_kmem_cache_node.slub_node.next = NULL;
    node_kmem_cache_node.using_count = 0;
    node_kmem_cache_node.free_count = node_kmem_cache.total_free;
    node_kmem_cache_node.free_list = buddy_map_pages(buddy_alloc_pages(node_kmem_cache.order_per_slub),(void*)memory_management.kernel_end_address,PAGE_ROOT_RW);
    node_kmem_cache_node.object_start_vaddr = node_kmem_cache_node.free_list;
    free_list_init(node_kmem_cache_node.free_list,node_kmem_cache.object_size,node_kmem_cache.object_per_slub-1);
    list_add_forward(&node_kmem_cache.slub_head,&node_kmem_cache_node.slub_node);

    char name2[]={"kmalloc_1024"};
    kmem_cache_t *kmalloc_1024 = kmem_cache_create(name2,1023);
    UINT64 *ptr = kmem_cache_alloc(kmalloc_1024);
    ptr = kmem_cache_alloc(kmalloc_1024);
    ptr = kmem_cache_alloc(kmalloc_1024);
    ptr = kmem_cache_alloc(kmalloc_1024);
    ptr = kmem_cache_alloc(kmalloc_1024);
    kmem_cache_destroy(kmalloc_1024);

    *ptr=0x12346;
    kmem_cache_free(kmalloc_1024,ptr);
    kmem_cache_destroy(kmalloc_1024);


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
    kmem_cache_node_t *cerate_node = kmem_cache_alloc(&node_kmem_cache);

    cerate_cache->name=name;
    cerate_cache->object_size = object_size_align(object_size);
    cerate_cache->order_per_slub = object_size_order(cerate_cache->object_size);
    cerate_cache->object_per_slub = (PAGE_4K_SIZE << cerate_cache->order_per_slub)/cerate_cache->object_size;
    cerate_cache->slub_count = 1;
    cerate_cache->total_using = 0;
    cerate_cache->total_free = cerate_cache->object_per_slub;
    cerate_cache->slub_head.prev = NULL;
    cerate_cache->slub_head.next = NULL;

    cerate_node->slub_node.prev = NULL;
    cerate_node->slub_node.next = NULL;
    cerate_node->using_count = 0;
    cerate_node->free_count = cerate_cache->total_free;
    cerate_node->free_list = buddy_map_pages(buddy_alloc_pages(cerate_cache->order_per_slub),(void*)memory_management.kernel_end_address,PAGE_ROOT_RW);
    cerate_node->object_start_vaddr =  cerate_node->free_list;
    free_list_init(cerate_node->free_list,cerate_cache->object_size,cerate_cache->object_per_slub-1);
    list_add_forward((list_head_t*)&cerate_cache->slub_head,(list_head_t*)&cerate_node->slub_node);
    return cerate_cache;
}

//销毁kmem_cache缓存池
void kmem_cache_destroy(kmem_cache_t *destroy_cache) {
    for (UINT32 i = 0; i < destroy_cache->slub_count; i++) {
        kmem_cache_node_t *destroy_node = (kmem_cache_node_t*)destroy_cache->slub_head.next;
        UINT64* vir_addr = vaddr_to_pte_vaddr(destroy_node->object_start_vaddr);
        UINT64 phy_addr = *vir_addr & 0x7FFFFFFFFFFFF000UL;
        page_t* page = phyaddr_to_page(phy_addr);
        buddy_free_pages(page);
        kmem_cache_free(&node_kmem_cache,destroy_cache->slub_head.next);
        list_del(destroy_cache->slub_head.next);
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
        kmem_cache_node_t* new_cache_node = kmem_cache_alloc(&node_kmem_cache);
        new_cache_node->slub_node.prev = NULL;
        new_cache_node->slub_node.next = NULL;
        new_cache_node->using_count = 0;
        new_cache_node->free_count = cache->object_per_slub;
        new_cache_node->free_list = buddy_map_pages(buddy_alloc_pages(cache->order_per_slub),(void*)memory_management.kernel_end_address,PAGE_ROOT_RW);
        new_cache_node->object_start_vaddr =  new_cache_node->free_list;
        free_list_init(new_cache_node->free_list,cache->object_size,cache->object_per_slub-1);
        list_add_forward(&cache->slub_head,&new_cache_node->slub_node);
        cache->slub_count++;
        cache->total_free = cache->object_per_slub;
    }

    kmem_cache_node_t* cache_node =cache->slub_head.next;
    for (UINT32 i = 0; i < cache->slub_count; i++) {
        if (cache_node->free_list == NULL) {
            cache_node = (kmem_cache_node_t*)cache_node->slub_node.next;
            continue;
        }
        cache_node->free_count--;
        cache_node->using_count++;
        cache->total_free--;
        cache->total_using++;
        UINT64* ptr = cache_node->free_list;
        cache_node->free_list = *ptr;
        return ptr;
    }
    return NULL;
}

//释放对象到kmem_cache缓存池
void kmem_cache_free(kmem_cache_t *cache, void *ptr) {
    kmem_cache_node_t* cache_node =cache->slub_head.next;
    void* align_addr = (UINT64)ptr & (PAGE_4K_MASK << cache->order_per_slub);

    for (UINT32 i = 0; i < cache->slub_count; i++) {
        if (align_addr != cache_node->object_start_vaddr) {
            cache_node = cache_node->slub_node.next;
            continue;
        }
        cache_node->free_count++;
        cache_node->using_count--;
        cache->total_free++;
        cache->total_using--;
        *(UINT64*)ptr = cache_node->free_list;
        cache_node->free_list = ptr;

        //如果当前slub所有对象已经释放，且cache总空闲对象大于slub对象则释放当前的slub到伙伴系统
        if (cache_node->using_count == 0 && cache->total_free > cache->object_per_slub) {
            UINT64* vir_addr = vaddr_to_pte_vaddr(cache_node->object_start_vaddr);
            UINT64 phy_addr = *vir_addr & 0x7FFFFFFFFFFFF000UL;
            page_t* page = phyaddr_to_page(phy_addr);
            buddy_free_pages(page);
            kmem_cache_free(&node_kmem_cache,cache_node);
            list_del(&cache_node->slub_node);
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