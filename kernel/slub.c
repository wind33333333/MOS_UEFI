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
void slub_init(void) {
    //创建kmem_cache对象缓存池
    char name[] = {"kmem_cache"};
    memcpy(name, kmem_cache_name, sizeof(name));
    create_cache(name, &cache_kmem_cache, sizeof(kmem_cache_t));
    add_cache_node(&cache_kmem_cache, &cache_kmem_cache_node);

    //创建kmem_cache_node对象缓存池
    char name1[] = {"kmem_cache_node"};
    memcpy(name1, kmem_cache_node_name, sizeof(name1));
    create_cache(name1, &node_kmem_cache, sizeof(kmem_cache_node_t));
    add_cache_node(&node_kmem_cache, &node_kmem_cache_node);

    ///////////////////////////////////////////////////////

    char name01[] = {"1"};
    char name02[] = {"2"};
    char name03[] = {"3"};
    char name04[] = {"4"};
    char name05[] = {"5"};
    char name06[] = {"6"};
    char name07[] = {"7"};
    char name08[] = {"8"};
    char name09[] = {"9"};
    char name10[] = {"10"};

    char *ptr01 = kmem_cache_create(name01, 1024);
    char *ptr02 = kmem_cache_create(name02, 1024);
    char *ptr03 = kmem_cache_create(name03, 1024);
    char *ptr04 = kmem_cache_create(name04, 1024);
    char *ptr05 = kmem_cache_create(name05, 1024);
    char *ptr06 = kmem_cache_create(name06, 1024);
    char *ptr07 = kmem_cache_create(name07, 1024);
    char *ptr08 = kmem_cache_create(name08, 1024);
    char *ptr09 = kmem_cache_create(name09, 1024);
    char *ptr10 = kmem_cache_create(name10, 1024);

    kmem_cache_destroy(ptr10);
    kmem_cache_destroy(ptr09);
    kmem_cache_destroy(ptr08);
    kmem_cache_destroy(ptr07);
    kmem_cache_destroy(ptr06);
    kmem_cache_destroy(ptr05);
    kmem_cache_destroy(ptr04);
    kmem_cache_destroy(ptr03);
    kmem_cache_destroy(ptr02);
    kmem_cache_destroy(ptr01);


    char name100[] = {"kmalloc_1024"};
    kmem_cache_t *kmalloc_1024 = kmem_cache_create(name100, 1023);
    UINT64 *ptr1, ptr2, ptr3, ptr4, ptr5, ptr6, ptr7, ptr8;
    ptr1 = kmem_cache_alloc(kmalloc_1024);
    ptr2 = kmem_cache_alloc(kmalloc_1024);
    ptr3 = kmem_cache_alloc(kmalloc_1024);
    ptr4 = kmem_cache_alloc(kmalloc_1024);
    ptr5 = kmem_cache_alloc(kmalloc_1024);
    ptr6 = kmem_cache_alloc(kmalloc_1024);
    ptr7 = kmem_cache_alloc(kmalloc_1024);
    ptr8 = kmem_cache_alloc(kmalloc_1024);
    kmem_cache_free(kmalloc_1024, ptr1);
    kmem_cache_free(kmalloc_1024, ptr2);
    kmem_cache_free(kmalloc_1024, ptr3);
    kmem_cache_free(kmalloc_1024, ptr4);
    kmem_cache_free(kmalloc_1024, ptr5);
    kmem_cache_free(kmalloc_1024, ptr6);
    kmem_cache_free(kmalloc_1024, ptr7);
    kmem_cache_free(kmalloc_1024, ptr8);
    kmem_cache_destroy(kmalloc_1024);
}

//创建kmem_cache缓存池
kmem_cache_t *kmem_cache_create(char *name, UINT32 object_size) {
    kmem_cache_t *new_cache = kmem_cache_alloc(&cache_kmem_cache);
    kmem_cache_node_t *new_cache_node = kmem_cache_alloc(&node_kmem_cache);

    create_cache(name, new_cache, object_size);
    add_cache_node(new_cache, new_cache_node);
    return new_cache;
}

//销毁kmem_cache缓存池
void kmem_cache_destroy(kmem_cache_t *destroy_cache) {
    kmem_cache_node_t *next_node = (kmem_cache_node_t *) destroy_cache->slub_head.next;
    for (UINT32 i = 0; i < destroy_cache->slub_count; i++) {
        kmem_cache_free(&node_kmem_cache, next_node);
        next_node = (kmem_cache_node_t *) next_node->slub_node.next;
    }
    kmem_cache_free(&cache_kmem_cache, destroy_cache);
}

//从kmem_cache缓存池分配对象
void *kmem_cache_alloc(kmem_cache_t *cache) {
    //如果kmem_cache_node专用空闲对象只剩下1个则先进行slub扩容
    if (node_kmem_cache.total_free == 1) add_cache_node(&node_kmem_cache, alloc_cache_object(&node_kmem_cache));

    //如果当前cache的总空闲对象只剩下一个则先进行slub扩容
    if (cache->total_free == 0) add_cache_node(cache, alloc_cache_object(&node_kmem_cache));

    //返回缓存池对象
    return alloc_cache_object(cache);
}

//释放对象到kmem_cache缓存池
void kmem_cache_free(kmem_cache_t *cache, void *object) {
    //释放object
    free_cache_object(cache, object);

    //遍历当前cache如果空闲对象大于一个slub对象数量则释放空闲node
    if (cache->total_free > cache->object_per_slub) {
        kmem_cache_node_t *next_node = (kmem_cache_node_t *) cache->slub_head.next;
        for (UINT32 i = 0; i < cache->slub_count; i++) {
            if (next_node->using_count == 0) {
                del_cache_node(cache, next_node);
                break;
            }
            next_node = (kmem_cache_node_t *) next_node->slub_node.next;
        }
    }
}

//从cache摘取一个对象
void *alloc_cache_object(kmem_cache_t *cache) {
    kmem_cache_node_t *cache_node = (kmem_cache_node_t *) cache->slub_head.next;
    UINT64 *object = NULL;
    for (UINT32 i = 0; i < cache->slub_count; i++) {
        if (cache_node->free_list != NULL) {
            object = cache_node->free_list;
            cache_node->free_list = (void *) *object;
            cache_node->free_count--;
            cache_node->using_count++;
            cache->total_free--;
            cache->total_using++;
            break;
        }
        cache_node = (kmem_cache_node_t *) cache_node->slub_node.next;
    }
    return object;
}

//释放一个对象到cache
void free_cache_object(kmem_cache_t *cache, void *object) {
    kmem_cache_node_t *cache_node = (kmem_cache_node_t *) cache->slub_head.next;
    void *align_addr = (void *) ((UINT64) object & (PAGE_4K_MASK << cache->order_per_slub));

    for (UINT32 i = 0; i < cache->slub_count; i++) {
        if (align_addr == cache_node->object_start_vaddr) {
            *(UINT64 *) object = (UINT64) cache_node->free_list;
            cache_node->free_list = object;
            cache_node->free_count++;
            cache_node->using_count--;
            cache->total_free++;
            cache->total_using--;
            break;
        }
        cache_node = (kmem_cache_node_t *) cache_node->slub_node.next;
    }
}

//新建一个cache
void create_cache(char *name, kmem_cache_t *new_cache, UINT32 object_size) {
    new_cache->name = name;
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

//从cache中删除一个node
void del_cache_node(kmem_cache_t *cache, kmem_cache_node_t *cache_node) {
    UINT64 *slub_vaddr = vaddr_to_pte_vaddr(cache_node->object_start_vaddr);
    UINT64 slub_paddr = *slub_vaddr & 0x7FFFFFFFFFFFF000UL;
    page_t *page = phyaddr_to_page(slub_paddr);
    buddy_unmap_pages(cache_node->object_start_vaddr);
    buddy_free_pages(page);
    list_del((list_head_t *) cache_node);
    free_cache_object(&node_kmem_cache, cache_node);
    cache->total_free -= cache->object_per_slub;
    cache->slub_count--;
}

//通用内存分配器
void *kmaollc(UINT64 size) {
    return 0;
}

//通用内存释放器
int kfree(void *virtual_address) {
    return 0;
}
