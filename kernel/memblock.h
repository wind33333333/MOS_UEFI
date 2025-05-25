#ifndef __MEMBLOCK_H__
#define __MEMBLOCK_H__
#include "moslib.h"
#include "vmm.h"

#define MAX_MEMBLOCK 128

#define PAGE_ROOT_RWX_4K_INIT       PAGE_ROOT_RWX_4K | 1<<9     //可读-可写-可执行
#define PAGE_ROOT_RX_4K_INIT        PAGE_ROOT_RX_4K | 1<<9      //可读-可执行
#define PAGE_ROOT_R_4K_INIT         PAGE_ROOT_R_4K | 1<<9       //只读
#define PAGE_ROOT_RW_4K_INIT        PAGE_ROOT_RW_4K | 1<<9      //可读-可写-普通内存
#define PAGE_ROOT_RW_WC_4K_INIT     PAGE_ROOT_RW_WC_4K | 1<<9   //可读-可写-显卡内存
#define PAGE_ROOT_RW_UC_4K_INIT     PAGE_ROOT_RW_UC_4K | 1<<9   //可读-可写-IO映射内存

#define PAGE_ROOT_RWX_2M1G_INIT     PAGE_ROOT_RWX_2M1G | 1<<9  //可读-可写-可执行
#define PAGE_ROOT_RX_2M1G_INIT      PAGE_ROOT_RX_2M1G | 1<<9   //可读-可执行
#define PAGE_ROOT_RX_2M1G_INIT      PAGE_ROOT_R_2M1G | 1<<9    //只读
#define PAGE_ROOT_RWX_2M1G_INIT     PAGE_ROOT_RW_2M1G | 1<<9   //可读-可写-普通内存
#define PAGE_ROOT_RW_WC_2M1G_INIT   PAGE_ROOT_RW_2M1G | 1<<9   //可读-可写-显卡内存
#define PAGE_ROOT_RW_UC_2M1G_INIT   PAGE_ROOT_RW_2M1G | 1<<9   //可读-可写-IO映射内存

typedef enum {
    kernel = 0,
    init = 1
}memblock_attr_e;

typedef struct memblock_region_t {
    UINT64 base;      /* 该区域的起始物理地址 */
    UINT64 size;      /* 该区域的大小 */
}memblock_region_t;

typedef struct memblock_type_t {
    memblock_region_t region[MAX_MEMBLOCK];
    UINT32 count;
}memblock_type_t;

typedef struct memblock_t {
    memblock_type_t memory;         /* 可用内存区域 */
    memblock_type_t reserved;       /* 保留内存区域 */
}memblock_t;

extern memblock_t memblock;

void memblock_add(memblock_type_t *memblock_type, UINT64 base, UINT64 size);
INT32 memblock_mmap(UINT64 *pml4t, UINT64 pa, void *va, UINT64 attr,UINT64 page_size);
INT32 memblock_mmap_range(UINT64 *pml4t, UINT64 pa, void *va,UINT64 length, UINT64 attr,UINT64 page_size);
void init_memblock(void);
void *memblock_alloc(UINT64 size, UINT64 align, memblock_attr_e attr);


#endif