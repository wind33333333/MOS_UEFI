#include "apic_init.h"
#include "../x64/cpu.h"
#include "memblock_init.h"
#include "acpi_init.h"
#include "slub.h"
#include "../include/printk.h"
#include "ioapic.h"
#include "../x64/msr.h"

extern cpu_core_t *cpu_cores;
extern uint32 active_cpu_count;

extern ioapic_devive_t *ioapic_dev;
extern uint32 ioapic_count;


typedef struct {
    int8  bus_source;                    // 总线源（通常为 0，表示 ISA 总线）
    uint8 irq_source;                    // ISA IRQ 号（0-15）
    uint16 flags;                        // 中断标志（边沿/电平触发，高/低电平等）
    uint32 global_system_interrupt;      // 重定向后的全局系统中断号
}irq_override_t;

irq_override_t *irq_override = NULL;
uint32 irq_override_count = 0;

// 1. 全局 LAPIC 基地址
uint64 global_lapic_base = 0;
// 2. 现代多核邮箱唤醒地址
uint64 multiprocessor_wakeup_mailbox = 0;


void apic_init(void) {
    madt_t *madt = acpi_get_table(ACPI_SIG_APIC, 0);
    if (!madt) {
        PR_ERROR("No MADT!\n");
        while (1);
    }

    madt_header_t *madt_start = (madt_header_t *)&madt->entry;
    uint64 madt_end = (uint64) madt + madt->acpi_header.length;

    // =========================================================
    // 第一遍扫描：纯计数 (Count) - 【全面统计 3 大巨头】
    // =========================================================
    uint32 count_xapic = 0, count_x2apic = 0;
    uint32 ioapic_count = 0, irq_override_count = 0;

    while ((uint64) madt_start < madt_end) {
        switch (madt_start->type) {
            case 0: if (((apic_entry_t *)madt_start)->flags & 1) count_xapic++; break;
            case 9: if (((x2apic_entry_t *)madt_start)->flags & 1) count_x2apic++; break;
            case 1: ioapic_count++; break; // 💡 统计 IO APIC 个数
            case 2: irq_override_count++; break;    // 💡 统计 中断重定向 个数
        }
        madt_start = (madt_header_t *)((uint64) madt_start + madt_start->length);
    }
    // 🌟 核心决策：如果存在 x2APIC，就用 x2APIC 的数量，否则回退到传统 xAPIC
    active_cpu_count = (count_x2apic > 0) ? count_x2apic : count_xapic;
    if (active_cpu_count == 0) {
        PR_ERROR("No active CPU found!\n");
        while (1);
    }

    uint64 cpu_core_size = active_cpu_count * sizeof(cpu_core_t);
    uint64 ioapic_dev_size = ioapic_count * sizeof(ioapic_devive_t);
    uint64 irq_override_size = irq_override_count * sizeof(irq_override_t);
    uint64 total_size = PAGE_4K_ALIGN(cpu_core_size+ioapic_dev_size +irq_override_size);
    cpu_cores = pa_to_va(memblock_alloc(total_size,PAGE_4K_SIZE));
    asm_mem_set(cpu_cores,0,total_size);

    if (ioapic_count > 0) {
        ioapic_dev = (ioapic_devive_t*)((uint64)cpu_cores+cpu_core_size);
    }

    if (irq_override_count > 0) {
        irq_override = (irq_override_t*)((uint64)cpu_cores+cpu_core_size+ioapic_dev_size);
    }

    // 💡 必须让 BSP 永远霸占 logical_id = 0 的宝座！
    uint32 ap_idx = 1,ioapic_idx=0,irq_idx=0;
    uint32 bsp_apic_id = asm_rdmsr(APIC_ID_MSR);

    // ✅ 修复 3：设置 LAPIC 的默认基地址 (如果后面有 case 5 会将其覆盖)
    global_lapic_base = madt->local_apic_address;

    // =========================================================
    // 第二遍扫描：提取数据并填充 (Populate)
    // =========================================================
    madt_start = (madt_header_t *)&madt->entry;
    while ((uint64) madt_start < madt_end) {
        switch (madt_start->type) {
            case 0: // 🌟 补全：传统的 Local APIC (Type 0)
                if (count_x2apic == 0) { // 只有在系统没有 x2APIC 时才使用 Type 0
                    apic_entry_t *lapic = (apic_entry_t *) madt_start;
                    if (lapic->flags & 1) {
                        uint32 logical_id;
                        if (lapic->apic_id == bsp_apic_id) {
                            logical_id = 0;
                        }else {
                            logical_id = ap_idx++;
                        }
                        cpu_cores[logical_id].apic_id = lapic->apic_id;
                        cpu_cores[logical_id].acpi_proc_id = lapic->processor_id;
                        cpu_cores[logical_id].logical_id = logical_id;
                    }
                }
                break;
            case 1: //ioapic
                ioapic_entry_t *ioapic_entry = (ioapic_entry_t *) madt_start;
                ioapic_dev[ioapic_idx].phys_addr = ioapic_entry->ioapic_address;
                ioapic_dev[ioapic_idx].gsi_base = ioapic_entry->global_system_interrupt_base;
                ioapic_dev[ioapic_idx].id = ioapic_entry->ioapic_id;
                ioapic_idx++;
                break;
            case 2: //中断重定向
                interrupt_source_override_entry_t *iso_entry = (interrupt_source_override_entry_t *) madt_start;
                irq_override[irq_idx].flags = iso_entry->flags;
                irq_override[irq_idx].bus_source = iso_entry->bus_source;
                irq_override[irq_idx].irq_source = iso_entry->irq_source;
                irq_override[irq_idx].global_system_interrupt = iso_entry->global_system_interrupt;
                irq_idx++;
                break;
            case 3: //不可屏蔽中断
                nmi_source_entry_t *nmi_source_entry = (nmi_source_entry_t *) madt_start;
                break;
            case 4: //apic nmi引脚
                apic_nmi_entry_t *apic_nmi_entry = (apic_nmi_entry_t *) madt_start;
                // 如果 ID 是 0xFF，说明这是“全服广播”
                if (apic_nmi_entry->apic_id == 0xFF) {
                    for (int i = 0; i < active_cpu_count; i++) {
                        cpu_cores[i].lint_nmi = apic_nmi_entry->lint;
                    }
                } else {
                    // 针对特定 CPU，去数组里找它
                    for (int i = 0; i < active_cpu_count; i++) {
                        if (cpu_cores[i].apic_id == apic_nmi_entry->apic_id) {
                            cpu_cores[i].lint_nmi = apic_nmi_entry->lint;
                            break;
                        }
                    }
                }
                break;
            case 5: //64位local apic地址
                apic_address_override_entry_t *apic_addr_override_entry = (apic_address_override_entry_t *)
                        madt_start;
                global_lapic_base = apic_addr_override_entry->apic_address;
                break;
            case 9: //X2APIC ID
                if (count_x2apic > 0) { // 如果系统支持 x2APIC，独占解析权！
                    x2apic_entry_t *x2apic_entry = (x2apic_entry_t *) madt_start;
                    if (x2apic_entry->flags & 1) {
                        uint32 logical_id;
                        if (x2apic_entry->x2apic_id == bsp_apic_id) {
                            logical_id = 0;
                        }else {
                            logical_id = ap_idx++;
                        }
                        cpu_cores[logical_id].apic_id = x2apic_entry->x2apic_id;
                        cpu_cores[logical_id].acpi_proc_id = x2apic_entry->processor_id;
                        cpu_cores[logical_id].logical_id = logical_id;
                    }
                }
                break;
            case 10: //X2APIC不可屏蔽中断
                x2apic_nmi_entry_t *x2apic_nmi_entry = (x2apic_nmi_entry_t *) madt_start;
                // X2APIC 的广播 ID 是 0xFFFFFFFF
                if (x2apic_nmi_entry->x2apic_id == 0xFFFFFFFF) {
                    for (int i = 0; i < active_cpu_count; i++) {
                        cpu_cores[i].lint_nmi = x2apic_nmi_entry->lint;
                    }
                } else {
                    for (int i = 0; i < active_cpu_count; i++) {
                        if (cpu_cores[i].apic_id == x2apic_nmi_entry->x2apic_id) {
                            cpu_cores[i].lint_nmi = x2apic_nmi_entry->lint;
                            break;
                        }
                    }
                }
                color_printk(RED,BLACK, "X2APIC NMI X2ApicID:%#lX LINT:%d\n", x2apic_nmi_entry->x2apic_id,
                             x2apic_nmi_entry->lint);
                break;
            case 16: //多处理器唤醒
                multiprocessor_wakeup_entry_t *mult_proc_wakeup_entry = (multiprocessor_wakeup_entry_t *)
                        madt_start;
                multiprocessor_wakeup_mailbox = mult_proc_wakeup_entry->mailbox_address;
                break;
            default:
                // 建议保留一行调试日志，方便以后排查奇葩主板
                PR_WARN("Ignored unknown MADT Type: %d, length: %d\n", madt_start->type, madt_start->length);
                break;
        }
        madt_start = (madt_header_t *) ((uint64) madt_start + madt_start->length);
    }

    PR_OK("APIC Init done! Dynamically allocated CPU structures.\n");
}

