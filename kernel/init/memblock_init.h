#pragma once

#include "../include/moslib.h"
#include "uefi_init.h"

// Memblock 在伙伴系统建立前使用，必须依赖静态数组打破自举死锁
#define MAX_MEMBLOCK_REGIONS 128
#define NUMA_NO_NODE 0xFF

// =========================================================================
// 1. 最微观的物理内存区间 (Region) - 🌟 重构：引入 type 标签
// =========================================================================
typedef struct {
    uint64 base;  // 物理基址
    uint64 size;  // 区间大小
    uint8  nid;   // 逻辑 NUMA Node ID (默认初始化为 NUMA_NO_NODE)
    EFI_MEMORY_TYPE type;  // 🌟 物理内存的类型标签 (映射为 EFI_MEMORY_TYPE)
} memblock_region_t;

typedef struct {
    memblock_region_t region[MAX_MEMBLOCK_REGIONS]; // 静态分配的物理区间数组
    uint32 count;                                   // 当前已记录的区间数量
} memblock_type_t;

typedef struct {
    memblock_type_t memory;         // 【资产簿】系统所有物理存在的可用 RAM
    memblock_type_t reserved;       // 【债务簿】已经被使用或保留的内存
    memblock_type_t phys_map;       // 🌟【全景基石】仅用于建立页表，记录物理绝对连续性
} memblock_t;


extern memblock_t memblock;



void numa_node_init(void);
void memblock_init(void);
uint64 memblock_alloc_node(uint64 size, uint64 align, uint8 target_nid);
int32 memblock_free(uint64 ptr, uint64 size);
// 通用兼容接口：谁不关心位置，就传 NUMA_NO_NODE
uint64 memblock_alloc(uint64 size, uint64 align);
void memblock_set_node_by_intersection(memblock_type_t *type, uint64 srat_base, uint64 srat_size, uint8 nid);

