#ifndef __MEMORY_H__
#define __MEMORY_H__
#include "moslib.h"
#include "linkage.h"

//页级别
#define PML4E_LEVEL  4
#define PDPTE_LEVEL  3
#define PDE_LEVEL    2
#define PTE_LEVEL    1

#define PAGE_PA_MASK    0x7FFFFFFFF000UL

#define PAGE_4K_SHIFT    12
#define PAGE_4K_SIZE    (1UL << PAGE_4K_SHIFT)
#define PAGE_4K_MASK    (~(PAGE_4K_SIZE - 1))
#define PAGE_4K_ALIGN(ADDR)    (((UINT64)(ADDR) + PAGE_4K_SIZE - 1) & PAGE_4K_MASK)

#define PAGE_2M_SHIFT    21
#define PAGE_2M_SIZE    (1UL << PAGE_2M_SHIFT)
#define PAGE_2M_MASK    (~(PAGE_2M_SIZE - 1))
#define PAGE_2M_ALIGN(ADDR)     (((UINT64)(ADDR) + PAGE_2M_SIZE - 1) & PAGE_2M_MASK)

#define PAGE_1G_SHIFT    30
#define PAGE_1G_SIZE    (1UL << PAGE_1G_SHIFT)
#define PAGE_1G_MASK    (~(PAGE_1G_SIZE - 1))
#define PAGE_1G_ALIGN(ADDR)     (((UINT64)(ADDR) + PAGE_1G_SIZE - 1) & PAGE_1G_MASK)

//虚拟地址转物理地址
static inline UINT64 va_to_pa(void *va) {
    return (UINT64)va & ~DIRECT_MAP_OFFSET;
}

//物理地址转虚拟地址
static inline void *pa_to_va(UINT64 pa) {
    return (void *)(pa | DIRECT_MAP_OFFSET);
}

typedef struct{
    UINT64 address;
    UINT64 length;
    UINT32 type;
}mem_map_t;

typedef struct {
    mem_map_t mem_map[20];
    UINT32 mem_map_count;
    UINT64 avl_mem_size;

    UINT64 *bitmap;
    UINT64 bitmap_size;
    UINT64 bitmap_length;

    UINT64 total_pages;
    UINT64 used_pages;
    UINT64 avl_pages;

    UINT64 kernel_start_address;
    UINT64 kernel_end_address;

    UINT8 lock;
} global_memory_descriptor_t;

extern global_memory_descriptor_t memory_management;

#define PAGE_NX     1UL<<63
#define PAGE_G      1UL<<8
#define PAGE_PS     1UL<<7      //pdpte第7位ps位置位表示1G巨页 pde第7位ps位置位2M大页
#define PAGE_PAT    1UL<<7      //pte第7位
#define PAGE_D      1UL<<6
#define PAGE_A      1UL<<5
#define PAGE_PCD    1UL<<4
#define PAGE_PWT    1UL<<3
#define PAGE_US     1UL<<2
#define PAGE_RW     1UL<<1
#define PAGE_P      1UL<<0

#define PAGE_WB     0                         //回写普通内存
#define PAGE_UC     PAGE_PWT                  //不可缓存，要求严格顺序的内存映射 I/O
#define PAGE_WC     PAGE_PCD                  //写合并，聚合写入操作，优化写入性能 适合视频内存等批量写入场景
#define PAGE_WT     (PAGE_PWT|PAGE_PCD)       //写通
#define PAGE_WP     PAGE_PAT                  //读操作先访问缓存，写操作扩散到所有处理器
#define PAGE_UC_    (PAGE_PWT|PAGE_PAT)       //部分不可缓存

#define PAGE_ROOT_RWX_4K    (PAGE_G | PAGE_RW |PAGE_P | PAGE_WB)             //可读-可写-可执行
#define PAGE_ROOT_RX_4K     (PAGE_G | PAGE_P | PAGE_WB)                      //可读-可执行
#define PAGE_ROOT_R_4K      (PAGE_NX | PAGE_G | PAGE_P | PAGE_WB)            //只读
#define PAGE_ROOT_RW_4K     (PAGE_NX | PAGE_G | PAGE_RW |PAGE_P | PAGE_WB)   //可读-可写-普通内存
#define PAGE_ROOT_RW_WC_4K  (PAGE_NX | PAGE_G | PAGE_RW |PAGE_P | PAGE_WC)   //可读-可写-显卡内存
#define PAGE_ROOT_RW_UC_4K  (PAGE_NX | PAGE_G | PAGE_RW |PAGE_P | PAGE_UC)   //可读-可写-IO映射内存

#define PAGE_ROOT_RWX_2M1G    (PAGE_G | PAGE_RW |PAGE_P | PAGE_WB | PAGE_PS)             //可读-可写-可执行
#define PAGE_ROOT_RX_2M1G     (PAGE_G | PAGE_P | PAGE_WB | PAGE_PS)                      //可读-可执行
#define PAGE_ROOT_R_2M1G      (PAGE_NX | PAGE_G | PAGE_P | PAGE_WB | PAGE_PS)            //只读
#define PAGE_ROOT_RW_2M1G     (PAGE_NX | PAGE_G | PAGE_RW |PAGE_P | PAGE_WB | PAGE_PS)   //可读-可写-普通内存
#define PAGE_ROOT_RW_WC_2M1G  (PAGE_NX | PAGE_G | PAGE_RW |PAGE_P | PAGE_WC | PAGE_PS)   //可读-可写-显卡内存
#define PAGE_ROOT_RW_UC_2M1G  (PAGE_NX | PAGE_G | PAGE_RW |PAGE_P | PAGE_UC | PAGE_PS)   //可读-可写-IO映射内存

#define PAGE_USER_R      (PAGE_NX | PAGE_US | PAGE_P | PAGE_WB)              //只读
#define PAGE_USER_RW     (PAGE_NX | PAGE_US | PAGE_RW | PAGE_P | PAGE_WB)    //可读可写
#define PAGE_USER_RX     (PAGE_US | PAGE_P | PAGE_WB)                        //可读可执行
#define PAGE_USER_RWX    (PAGE_US | PAGE_RW | PAGE_P | PAGE_WB)              //可读可写可执行

/////////////////////////////////////////////////////////////////////
//虚拟地址转换pte虚拟地址
static inline void *vaddr_to_pte_vaddr(void *va){
    return (void*)(~(~(UINT64)va<<16>>28)<<3);
}

//虚拟地址转换pde虚拟地址
static inline void *vaddr_to_pde_vaddr(void *va){
    return (void*)(~(~(UINT64)va<<16>>37)<<3);
}

//虚拟地址转换pdpte虚拟地址
static inline void *vaddr_to_pdpte_vaddr(void *va){
    return (void*)(~(~(UINT64)va<<16>>46)<<3);
}

//虚拟地址转换pml4e虚拟地址
static inline void *vaddr_to_pml4e_vaddr(void *va){
    return (void*)(~(~(UINT64)va<<16>>55)<<3);
}

//虚拟地址和page数量计算pde数量
static inline UINT64 calculate_pde_count(void *va, UINT64 page_count) {
    return (page_count + (((UINT64)va >> 12) & 0x1FF) + 0x1FF) >> 9;
}

//虚拟地址和page数量计算pdpte数量
static inline UINT64 calculate_pdpte_count(void *va, UINT64 page_count) {
    return (page_count + (((UINT64)va >> 12) & 0x3FFFF) + 0x3FFFF) >> 18;
}

//虚拟地址和page数量计算pml4e数量
static inline UINT64 calculate_pml4e_count(void *va, UINT64 page_count) {
    return (page_count + (((UINT64)va >> 12) & 0x7FFFFFF) + 0x7FFFFFF) >> 27;
}

//虚拟地址查找物理页
static inline UINT64 find_pages(void *va){
    UINT64 *pte_addr = vaddr_to_pte_vaddr(va);
    return  *pte_addr;
}

//虚拟地址修改物理页
static inline void revise_pages(void *va,UINT64 value){
    UINT64 *pte_addr= vaddr_to_pte_vaddr(va);
    *pte_addr=value;
}

////////////////////////////////////////////////////////////
#define PML4E_SHIFT 39  // PML4E 索引的位移量
#define PDPTE_SHIFT 30  // PDPTE 索引的位移量
#define PDE_SHIFT 21    // PDE 索引的位移量
#define PTE_SHIFT 12    // PTE 索引的位移量

// 计算 PML4E 索引
static inline UINT32 get_pml4e_index(void *va)
{
    return ((UINT64)va >> PML4E_SHIFT) & 0x1FF;
}

// 计算 PDPTE 索引
static inline UINT32 get_pdpte_index(void *va)
{
    return ((UINT64)va >> PDPTE_SHIFT) & 0x1FF;
}

// 计算 PDE 索引
static inline UINT32 get_pde_index(void *va)
{
    return ((UINT64)va >> PDE_SHIFT) & 0x1FF;
}

// 计算 PTE 索引
static inline UINT32 get_pte_index(void *va)
{
    return ((UINT64)va >> PTE_SHIFT) & 0x1FF;
}

UINT64 bitmap_alloc_pages(UINT64 page_count);
void bitmap_free_pages(UINT64 pa, UINT64 page_count);
void *bitmap_map_pages(UINT64 pa, void *va, UINT64 page_count, UINT64 attr);
void bitmap_unmap_pages(void *va, UINT64 page_count);

INT32 vmmap(UINT64 *pml4t, UINT64 pa, void *va, UINT64 attr,UINT64 page_size);
INT32 vmunmap(UINT64 *pml4t, void *va,UINT64 page_size);
INT32 vmmap_range(UINT64 *pml4t, UINT64 pa, void *va, UINT64 length, UINT64 attr,UINT64 page_size);
INT32 vmunmap_range(UINT64 *pml4t, void *va, UINT64 length, UINT64 page_size);
UINT64 get_page_table_entry(UINT64 *pml4t,void *va,UINT32 page_level);


#endif
