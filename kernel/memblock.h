#pragma once

#include "moslib.h"
#include "uefi.h"
#include "vmm.h"

#define MAX_MEMBLOCK 128

typedef struct memblock_region_t {
    uint64 base;      /* 该区域的起始物理地址 */
    uint64 size;      /* 该区域的大小 */
}memblock_region_t;

typedef struct memblock_type_t {
    memblock_region_t region[MAX_MEMBLOCK];
    uint32 count;
}memblock_type_t;

typedef struct memblock_t {
    memblock_type_t memory;         /* 可用内存区域 */
    memblock_type_t reserved;       /* 保留内存区域 */
}memblock_t;

typedef struct {
    EFI_MEMORY_DESCRIPTOR mem_map[10];
    uint32 count;
}efi_runtime_memmap_t;

extern memblock_t memblock;
extern memblock_type_t phy_vmemmap;
extern efi_runtime_memmap_t efi_runtime_memmap;


void memblock_add(memblock_type_t *memblock_type, uint64 base, uint64 size);
int32 memblock_unmmap(uint64 *pml4t, void *va, uint64 page_size);
int32 memblock_unmmap_range(uint64 *pml4t, void *va, uint64 size, uint64 page_size);
int32 memblock_mmap(uint64 *pml4t, uint64 pa, void *va, uint64 attr,uint64 page_size);
int32 memblock_mmap_range(uint64 *pml4t, uint64 pa, void *va,uint64 size, uint64 attr,uint64 page_size);
void init_memblock(void);
uint64 memblock_alloc(uint64 size, uint64 align);
int32 memblock_free(uint64 ptr, uint64 size);
