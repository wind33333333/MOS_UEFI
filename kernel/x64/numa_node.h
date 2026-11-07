#pragma once
#include "cpu.h"
#include "moslib.h"

typedef struct {
    uint64 base;
    uint64 size;
    uint8 node_id;
    boolean is_hotpluggable; // 是否支持热插拔
} numa_mem_region_t;

// 🌟 领土备忘录：保存 ACPI SRAT 定义的绝对物理边界
#define CONFIG_NUMA_NODES 64
#define CONFIG_NUMA_MEM_REGIONS 256 // 覆盖多节点及复杂的交错内存拓扑
typedef struct {
    // 热路径数组：记录 逻辑CPU ID -> 逻辑NUMA Node ID 的映射
    // 初始化为 0xFF 表示该 CPU 尚未关联或不存在
    uint8 cpu_lid_to_numa_map[CONFIG_NR_CPUS];

    // 核心映射表：物理 Domain 数组。数组的下标就是分配的【逻辑 Node ID】
    uint32 phys_domain_table[CONFIG_NUMA_NODES];
    uint8 numa_nodes_count; // 系统实际存在的 NUMA 节点总数

    // 热路径数组：紧凑的二维矩阵，记录 逻辑Node 到 逻辑Node 的访问距离
    uint8 numa_distance[CONFIG_NUMA_NODES][CONFIG_NUMA_NODES];

    //NUMA内存区域信息
    numa_mem_region_t numa_mem_region[CONFIG_NUMA_MEM_REGIONS];
    uint32 numa_mem_count;
}numa_info_t;

extern numa_info_t numa_info;