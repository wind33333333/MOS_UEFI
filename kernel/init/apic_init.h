#pragma once
#include "../x64/irq_dispatcher.h"
#include "moslib.h"
#include "ioapic.h"



irq_override_info_t early_irq_override_info;
ioapic_info_t early_ioapic_info;

// 1. 全局 LAPIC 基地址
uint64 global_lapic_base;
// 2. 现代多核邮箱唤醒地址
uint64 multiprocessor_wakeup_mailbox;



void apic_table_init(void);


