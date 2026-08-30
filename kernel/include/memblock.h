#pragma once

#include "moslib.h"
#include "../init/uefi.h"

#define MAX_MEMBLOCK 128

typedef struct mem_region_t {
    uint64 start_pa;      /* 该区域的起始物理地址 */
    uint64 size;          /* 该区域的大小 */
}mem_region_t;

typedef struct mem_arr_t {
    mem_region_t region[MAX_MEMBLOCK];
    uint32 count;
}mem_arr_t;

typedef struct memblock_alloc_t {
    mem_arr_t free;         /* 空闲内存区域 */
    mem_arr_t used;         /* 已用内存区域 */
}memblock_alloc_t;

typedef struct {
    EFI_MEMORY_DESCRIPTOR mem_map[10];
    uint32 count;
}efi_runtime_memmap_t;

extern memblock_alloc_t memblock;
extern mem_arr_t page_mem_map;
extern efi_runtime_memmap_t efi_runtime_memmap;
extern mem_arr_t direct_mem_map;;


void init_memblock(void);
uint64 memblock_alloc(uint64 size, uint64 align);
int32 memblock_free(uint64 ptr, uint64 size);
uint64 memblock_alloc_4k(void);
void memblock_free_4k(uint64 ptr);
