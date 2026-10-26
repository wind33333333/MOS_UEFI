#pragma once
#include "../x64/irq_dispatcher.h"
#include "moslib.h"
#include "ioapic.h"

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

cpu_apic_info_t cpu_apic_info;
irq_override_info_t early_irq_override_info;
ioapic_info_t early_ioapic_info;

// 1. 全局 LAPIC 基地址
uint64 global_lapic_base;
// 2. 现代多核邮箱唤醒地址
uint64 multiprocessor_wakeup_mailbox;



void apic_table_init(void);


