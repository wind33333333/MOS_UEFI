#pragma once

#include "moslib.h"
#include "uefi.h"
#include "vmm.h"

#define MAX_MEMBLOCK 128

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

typedef struct {
    EFI_MEMORY_DESCRIPTOR mem_map[10];
    UINT32 count;
}efi_runtime_memmap_t;

extern memblock_t memblock;
extern memblock_type_t phy_vmemmap;
extern efi_runtime_memmap_t efi_runtime_memmap;


void memblock_add(memblock_type_t *memblock_type, UINT64 base, UINT64 size);
INT32 memblock_unmmap(UINT64 *pml4t, void *va, UINT64 page_size);
INT32 memblock_unmmap_range(UINT64 *pml4t, void *va, UINT64 size, UINT64 page_size);
INT32 memblock_mmap(UINT64 *pml4t, UINT64 pa, void *va, UINT64 attr,UINT64 page_size);
INT32 memblock_mmap_range(UINT64 *pml4t, UINT64 pa, void *va,UINT64 size, UINT64 attr,UINT64 page_size);
void init_memblock(void);
UINT64 memblock_alloc(UINT64 size, UINT64 align);
INT32 memblock_free(UINT64 ptr, UINT64 size);
