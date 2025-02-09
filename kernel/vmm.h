#ifndef __MEMORY_H__
#define __MEMORY_H__
#include "moslib.h"
#include "linkage.h"

#define PAGE_4K_SHIFT    12
#define PAGE_4K_SIZE    (1UL << PAGE_4K_SHIFT)
#define PAGE_4K_MASK    (~(PAGE_4K_SIZE - 1))
#define PAGE_4K_ALIGN(ADDR)    (((UINT64)(ADDR) + PAGE_4K_SIZE - 1) & PAGE_4K_MASK)

#define HADDR_TO_LADDR(ADDR)    ((UINT64)(ADDR) & (~HIGH_BASE))
#define LADDR_TO_HADDR(ADDR)    ((UINT64)((UINT64)(ADDR) | HIGH_BASE))

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
#define PAGE_PAT    1UL<<7
#define PAGE_D      1UL<<6
#define PAGE_A      1UL<<5
#define PAGE_PCD    1UL<<4
#define PAGE_PWT    1UL<<3
#define PAGE_US     1UL<<2
#define PAGE_RW     1UL<<1
#define PAGE_P      1UL<<0

#define PAGE_WB     0                               //回写
#define PAGE_WT     (PAGE_PWT)                      //写通
#define PAGE_UC_    (PAGE_PCD)                      //部分不可缓存
#define PAGE_UC     (PAGE_PCD | PAGE_PWT)           //不可缓存，要求严格顺序的内存映射 I/O
#define PAGE_WC     (PAGE_PAT)                      //写合并，聚合写入操作，优化写入性能 适合视频内存等批量写入场景
#define PAGE_WP     (PAGE_PAT|PAGE_PWT)             //读操作先访问缓存，写操作扩散到所有处理器

#define PAGE_ROOT_RWX    (PAGE_G | PAGE_RW |PAGE_P | PAGE_WB)             //可读-可写-可执行
#define PAGE_ROOT_RX     (PAGE_G | PAGE_P | PAGE_WB)                      //可读-可执行
#define PAGE_ROOT_R      (PAGE_NX | PAGE_G | PAGE_P | PAGE_WB)            //只读
#define PAGE_ROOT_RW     (PAGE_NX | PAGE_G | PAGE_RW |PAGE_P | PAGE_WB)   //可读-可写-普通内存
#define PAGE_ROOT_RW_WC  (PAGE_NX | PAGE_G | PAGE_RW |PAGE_P | PAGE_WC)   //可读-可写-显卡内存
#define PAGE_ROOT_RW_UC  (PAGE_NX | PAGE_G | PAGE_RW |PAGE_P | PAGE_UC)   //可读-可写-IO映射内存

#define PAGE_USER_R      (PAGE_NX | PAGE_US | PAGE_P | PAGE_WB)              //只读
#define PAGE_USER_RW     (PAGE_NX | PAGE_US | PAGE_RW | PAGE_P | PAGE_WB)    //可读可写
#define PAGE_USER_RX     (PAGE_US | PAGE_P | PAGE_WB)                        //可读可执行
#define PAGE_USER_RWX    (PAGE_US | PAGE_RW | PAGE_P | PAGE_WB)              //可读可写可执行

//虚拟地址转换pte虚拟地址
static inline void *vaddr_to_pte_vaddr(void *virt_addr){
    return (void*)(~(~(UINT64)virt_addr<<16>>28)<<3);
}

//虚拟地址转换pde虚拟地址
static inline void *vaddr_to_pde_vaddr(void *virt_addr){
    return (void*)(~(~(UINT64)virt_addr<<16>>37)<<3);
}

//虚拟地址转换pdpte虚拟地址
static inline void *vaddr_to_pdpte_vaddr(void *virt_addr){
    return (void*)(~(~(UINT64)virt_addr<<16>>46)<<3);
}

//虚拟地址转换pml4e虚拟地址
static inline void *vaddr_to_pml4e_vaddr(void *virt_addr){
    return (void*)(~(~(UINT64)virt_addr<<16>>55)<<3);
}

//虚拟地址和page数量计算pde数量
static inline UINT64 calculate_pde_count(void *virt_addr, UINT64 page_count) {
    return (page_count + (((UINT64)virt_addr >> 12) & 0x1FF) + 0x1FF) >> 9;
}

//虚拟地址和page数量计算pdpte数量
static inline UINT64 calculate_pdpte_count(void *virt_addr, UINT64 page_count) {
    return (page_count + (((UINT64)virt_addr >> 12) & 0x3FFFF) + 0x3FFFF) >> 18;
}

//虚拟地址和page数量计算pml4e数量
static inline UINT64 calculate_pml4e_count(void *virt_addr, UINT64 page_count) {
    return (page_count + (((UINT64)virt_addr >> 12) & 0x7FFFFFF) + 0x7FFFFFF) >> 27;
}

//虚拟地址查找物理页
static inline UINT64 find_pages(void *virt_addr){
    UINT64 *pte_addr = vaddr_to_pte_vaddr(virt_addr);
    return  *pte_addr;
}

//虚拟地址修改物理页
static inline void revise_pages(void *virt_addr,UINT64 value){
    UINT64 *pte_addr= vaddr_to_pte_vaddr(virt_addr);
    *pte_addr=value;
}

UINT64 bitmap_alloc_pages(UINT64 page_count);
void bitmap_free_pages(UINT64 phy_addr, UINT64 page_count);
void *bitmap_map_pages(UINT64 phy_addr, void *virt_addr, UINT64 page_count, UINT64 attr);
void bitmap_unmap_pages(void *virt_addr, UINT64 page_count);


#endif
