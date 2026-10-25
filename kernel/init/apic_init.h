#pragma once
#include "moslib.h"

// 定义一个极其轻量级的硬件拓扑描述符
typedef struct {
    uint32 apic_id;
    uint32 acpi_proc_id;
    uint32 logical_id;
    uint32 numa_node;     // 后续通过解析 SRAT 表填入
    uint8  lint_nmi;
} cpu_hw_t;

typedef struct {
    cpu_hw_t *cpu_hw;
    uint32 total_cpu_count;
}cpu_apic_info_t;


void apic_table_init(void);


