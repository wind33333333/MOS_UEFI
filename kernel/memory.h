#ifndef __MEMORY_H__
#define __MEMORY_H__
#include "moslib.h"

void init_memory(void);

UINT64 alloc_pages(UINT64 page_number);

void free_pages(UINT64 pages_addr, UINT64 page_number);

void map_pages(UINT64 phy_addr, UINT64 vir_addr, UINT64 page_number, UINT64 attr);

void unmap_pages(UINT64 vir_addr, UINT64 page_number);

extern UINT64 *pml4t_vbase;  //pml4t虚拟地址基址
extern UINT64 *pdptt_vbase;  //pdptt虚拟地址基址
extern UINT64 *pdt_vbase;    //pdt虚拟地址基址
extern UINT64 *ptt_vbase;    //ptt虚拟地址基址

extern UINT64 kernel_stack_top;
extern CHAR8 _start_text[];
extern CHAR8 _start_init_text[];

#define PAGE_OFFSET    ((UINT64)0xffff800000000000)
#define PAGE_4K_SHIFT    12
#define PAGE_4K_SIZE    (1UL << PAGE_4K_SHIFT)
#define PAGE_4K_MASK    (~(PAGE_4K_SIZE - 1))
#define PAGE_4K_ALIGN(ADDR)    (((UINT64)(ADDR) + PAGE_4K_SIZE - 1) & PAGE_4K_MASK)

#define HADDR_TO_LADDR(ADDR)    ((UINT64)(ADDR) & (~PAGE_OFFSET))
#define LADDR_TO_HADDR(ADDR)    ((UINT64)((UINT64)(ADDR) | PAGE_OFFSET))

typedef struct{
    UINT64 address;
    UINT64 length;
    UINT32 type;
}mem_map_t;

typedef struct {
    mem_map_t mem_map[20];
    UINT32 mem_map_number;
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
#define PAGE_ROOT_R      (PAGE_NX | PAGE_G | PAGE_P | PAGE_WB)            //可读
#define PAGE_ROOT_RW     (PAGE_NX | PAGE_G | PAGE_RW |PAGE_P | PAGE_WB)   //可读-可写-普通内存
#define PAGE_ROOT_RW_WC  (PAGE_NX | PAGE_G | PAGE_RW |PAGE_P | PAGE_WC)   //可读-可写-显卡内存
#define PAGE_ROOT_RW_UC  (PAGE_NX | PAGE_G | PAGE_RW |PAGE_P | PAGE_UC)   //可读-可写-IO映射内存

#define PAGE_USER_R      (PAGE_NX | PAGE_US | PAGE_P | PAGE_WB)              //只读
#define PAGE_USER_RW     (PAGE_NX | PAGE_US | PAGE_RW | PAGE_P | PAGE_WB)    //可读可写
#define PAGE_USER_RX     (PAGE_US | PAGE_P | PAGE_WB)                        //可读可执行
#define PAGE_USER_RWX    (PAGE_US | PAGE_RW | PAGE_P | PAGE_WB)              //可读可写可执行

#endif
