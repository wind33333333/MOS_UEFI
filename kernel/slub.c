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
    add_cache(name,&cache_kmem_cache,sizeof(kmem_cache_t));
    add_cache_node(&cache_kmem_cache,&cache_kmem_cache_node);

    //创建kmem_cache_node对象缓存池
    char name1[]={"kmem_cache_node"};
    memcpy(name1,kmem_cache_node_name,sizeof(name1));
    add_cache(name1,&node_kmem_cache,sizeof(kmem_cache_node_t));
    add_cache_node(&node_kmem_cache,&node_kmem_cache_node);

    char name2[]={"kmalloc_1024"};
    kmem_cache_t *kmalloc_1024 = kmem_cache_create(name2,1023);
    //node_kmem_cache.total_free=1;
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
kmem_cache_t* kmem_cache_create(char *name,UINT32 object_size) {
    kmem_cache_t *new_cache = kmem_cache_alloc(&cache_kmem_cache);
    kmem_cache_node_t *new_cache_node = kmem_cache_alloc(&node_kmem_cache);

    add_cache(name,new_cache,object_size_align(object_size));
    add_cache_node(new_cache,new_cache_node);
    return new_cache;
}

//销毁kmem_cache缓存池
void kmem_cache_destroy(kmem_cache_t *destroy_cache) {
    kmem_cache_node_t *next_node = (kmem_cache_node_t*)destroy_cache->slub_head.next;
    for (UINT32 i = 0; i < destroy_cache->slub_count; i++) {
        UINT64* node_vaddr = vaddr_to_pte_vaddr(next_node->object_start_vaddr);
        UINT64 node_paddr = *node_vaddr & 0x7FFFFFFFFFFFF000UL;
        page_t* page = phyaddr_to_page(node_paddr);
        buddy_free_pages(page);
        kmem_cache_free(&node_kmem_cache,next_node);
        next_node = (kmem_cache_node_t*)next_node->slub_node.next;
    }
    kmem_cache_free(&cache_kmem_cache,destroy_cache);
}

//从kmem_cache缓存池分配对象
void* kmem_cache_alloc(kmem_cache_t *cache) {
    //如果kmem_cache专用空闲对象只剩下1个则先进行slub扩容
    if (cache_kmem_cache.total_free == 1) add_cache_node(&cache_kmem_cache,get_cache_node_object(&cache_kmem_cache));

    //如果kmem_cache_node专用空闲对象只剩下1个则先进行slub扩容
    if (node_kmem_cache.total_free == 1) add_cache_node(&node_kmem_cache,get_cache_node_object(&node_kmem_cache));

    //如果当前cache的总空闲对象只剩下一个则先进行slub扩容
    if(cache->total_free == 0) add_cache_node(cache,get_cache_node_object(&node_kmem_cache));

    //返回缓存池对象
    return get_cache_node_object(cache);

}

//释放对象到kmem_cache缓存池
void kmem_cache_free(kmem_cache_t *cache, void *object) {
    kmem_cache_node_t* cache_node =(kmem_cache_node_t*)cache->slub_head.next;
    void* align_addr = (void*)((UINT64)object & (PAGE_4K_MASK << cache->order_per_slub));

    for (UINT32 i = 0; i < cache->slub_count; i++) {
        if (align_addr != cache_node->object_start_vaddr) {
            cache_node = (kmem_cache_node_t*)cache_node->slub_node.next;
            continue;
        }
        cache_node->free_count++;
        cache_node->using_count--;
        cache->total_free++;
        cache->total_using--;
        *(UINT64*)object = cache_node->free_list;
        cache_node->free_list = object;

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

void* get_cache_node_object(kmem_cache_t* cache) {
    kmem_cache_node_t* cache_node = (kmem_cache_node_t*)cache->slub_head.next;
    UINT64* object = NULL;
    for (UINT32 i = 0; i < cache->slub_count; i++) {
        if (cache_node->free_list != NULL) {
            cache_node->free_count--;
            cache_node->using_count++;
            cache->total_free--;
            cache->total_using++;
            object = cache_node->free_list;
            cache_node->free_list = (void*)*object;
            break;
        }
        cache_node = (kmem_cache_node_t*)cache_node->slub_node.next;
    }
    return object;
}

void add_cache(char* name,kmem_cache_t* new_cache, UINT32 object_size) {
    new_cache->name = name;
    new_cache->object_size = object_size_align(object_size);
    new_cache->order_per_slub = object_size_order(new_cache->object_size);
    new_cache->object_per_slub = (PAGE_4K_SIZE<<new_cache->order_per_slub)/new_cache->object_size;
    new_cache->slub_count = 0;
    new_cache->total_using = 0;
    new_cache->total_free = (PAGE_4K_SIZE<<new_cache->order_per_slub)/new_cache->object_size;
    new_cache->slub_head.prev = NULL;
    new_cache->slub_head.next = NULL;
}

void add_cache_node(kmem_cache_t* cache,kmem_cache_node_t* new_cache_node) {
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

//通用内存分配器
void *kmaollc(UINT64 size) {
    return 0;
}

//通用内存释放器
int kfree(void *virtual_address) {
    return 0;
}