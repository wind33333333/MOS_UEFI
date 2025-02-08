#ifndef __MEMBLOCK_H__
#define __MEMBLOCK_H__
#include "moslib.h"

#define MAX_MEMBLOCK 128

#define PAGE_4K_SHIFT    12
#define PAGE_4K_SIZE    (1UL << PAGE_4K_SHIFT)
#define PAGE_4K_MASK    (~(PAGE_4K_SIZE - 1))
#define PAGE_4K_ALIGN(ADDR)    (((UINT64)(ADDR) + PAGE_4K_SIZE - 1) & PAGE_4K_MASK)

#define H_BASE_ADDR             ((UINT64)0xffff800000000000UL)
#define HADDR_TO_LADDR(ADDR)    ((UINT64)(ADDR) & (~H_BASE_ADDR))
#define LADDR_TO_HADDR(ADDR)    ((UINT64)((UINT64)(ADDR) | H_BASE_ADDR))

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

/*
typedef struct{
    list_head_t block;
    UINT32 falgs;
    UINT32 order;
    UINT32 refcount;
}page_t;
*/

typedef struct memblock_region_t {
    UINT64 base;  /* 该区域的起始物理地址 */
    UINT64 size;       /* 该区域的大小 */
}memblock_region_t;

typedef struct memblock_type_t {
    memblock_region_t region[MAX_MEMBLOCK];
    UINT32 count;
}memblock_type_t;

typedef struct memblock_t {
    memblock_type_t memory;         /* 可用内存区域 */
    memblock_type_t reserved;       /* 保留内存区域 */
}memblock_t;

// 对齐函数，确保 addr 按 align 对齐（align 为 2 的幂）
static inline UINT64 align_up(UINT64 addr, UINT64 align) {
    return (addr + align - 1) & -align;
}

extern memblock_t memblock;

void memblock_add(memblock_type_t *memblock_type, UINT64 base, UINT64 size);
void *memblock_alloc(UINT64 size, UINT64 align);
void *memblock_mmap(UINT64 phy_addr, void *virt_addr, UINT64 page_count, UINT64 attr);
void init_memblock(void);

#endif