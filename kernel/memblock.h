#ifndef __MEMBLOCK_H__
#define __MEMBLOCK_H__
#include "moslib.h"

#define MAX_MEMBLOCK 128

typedef struct memblock_region_t {
    UINT64 phys_addr;  /* 该区域的起始物理地址 */
    UINT64 size;       /* 该区域的大小 */
}memblock_region_t;

typedef struct memblock_type_t {
    memblock_region_t region[MAX_MEMBLOCK];
    UINT32 count;
}memblock_type;

typedef struct memblock_t {
    memblock_type memory;         /* 可用内存区域 */
    memblock_type reserved;       /* 保留内存区域 */
}memblock_t;



#endif