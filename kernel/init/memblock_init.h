#pragma once

#include "../include/moslib.h"
#include "uefi_init.h"

// Memblock 在伙伴系统建立前使用，必须依赖静态数组打破自举死锁
#define MAX_MEMBLOCK_REGIONS 256
#define NUMA_NO_NODE 0xFF

// =========================================================================
// 1. 最微观的物理内存区间 (Region)
// =========================================================================
typedef struct {
    uint64 base;  // 物理基址
    uint64 size;  // 区间大小
    uint8  nid;   // 逻辑 NUMA Node ID (默认初始化为 NUMA_NO_NODE)
} memblock_region_t;

typedef struct {
    memblock_region_t region[MAX_MEMBLOCK_REGIONS]; // 静态分配的物理区间数组
    uint32 count;                                   // 当前已记录的区间数量
}memblock_type_t;

typedef struct {
    memblock_type_t memory;         // 【资产簿】系统所有物理存在的可用 RAM
    memblock_type_t reserved;       // 【债务簿】已经被系统内核镜像、UEFI、ACPI 表占用的 RAM
    boolean bottom_up;         // 分配方向策略：TRUE 为从低地址向高地址分配
    uint64 current_limit;      // 寻址上限（用于在分页完全建立前限制分配过高的物理地址）
}memblock_t;

typedef struct {
    EFI_MEMORY_DESCRIPTOR mem_map[10];
    uint32 count;
}efi_runtime_memmap_t;

extern memblock_t memblock;
extern memblock_type_t page_mem_map;
extern efi_runtime_memmap_t efi_runtime_memmap;
extern memblock_type_t direct_mem_map;;


void numa_node_init(void);
void memory_init(void);
uint64 memblock_alloc_node(uint64 size, uint64 align, uint8 target_nid);
int32 memblock_free(uint64 ptr, uint64 size);
// 通用兼容接口：谁不关心位置，就传 NUMA_NO_NODE
uint64 memblock_alloc(uint64 size, uint64 align) {
    return memblock_alloc_node(size, align, NUMA_NO_NODE);
}
void memblock_set_node_by_intersection(uint64 srat_base, uint64 srat_size, uint8 nid);
void memblock_merge_regions(memblock_type_t *type);

