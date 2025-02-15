#ifndef __MEMBLOCK_H__
#define __MEMBLOCK_H__
#include "moslib.h"

#define MAX_MEMBLOCK 128

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
void memblock_vmmap(UINT64 *pml4t, UINT64 phy_addr, void *virt_addr, UINT64 attr);
void memblock_vmmap_range(UINT64 *pml4t, UINT64 phy_addr, void *virt_addr,UINT64 length, UINT64 attr);
void init_memblock(void);


#endif