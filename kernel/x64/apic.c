#include "apic.h"
#include "cpu.h"
#include "memblock_init.h"
#include "../init/acpi_init.h"
#include "slub.h"
#include "../include/printk.h"
#include "ioapic.h"

// 定义为一个动态指针，而不是固定数组！
cpu_core_t *cpu_cores = NULL;
uint32 active_cpu_count = 0;

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
    uint32 count_ioapic = 0, count_iso = 0;

    while ((uint64) madt_start < madt_end) {
        switch (madt_start->type) {
            case 0: if (((apic_entry_t *)madt_start)->flags & 1) count_xapic++; break;
            case 9: if (((x2apic_entry_t *)madt_start)->flags & 1) count_x2apic++; break;
            case 1: count_ioapic++; break; // 💡 统计 IO APIC 个数
            case 2: count_iso++; break;    // 💡 统计 中断重定向 个数
        }
        madt_start = (madt_header_t *)((uint64) madt_start + madt_start->length);
    }
    // 🌟 核心决策：如果存在 x2APIC，就用 x2APIC 的数量，否则回退到传统 xAPIC
    uint32 count_cpu = (count_x2apic > 0) ? count_x2apic : count_xapic;
    boolean use_x2apic = (count_x2apic > 0);

    // =========================================================
    // 动态内存分配：按需切肉 (Allocate)
    // =========================================================
    if (count_cpu == 0) {
        PR_ERROR("No active CPU found!\n");
        while (1);
    }

    uint64 cpu_core_size = count_cpu * sizeof(cpu_core_t);
    uint64 ioapic_dev_size = count_ioapic * sizeof(ioapic_devive_t);
    uint64 irq_override_size = count_iso * sizeof(irq_override_t);
    uint64 total_size = PAGE_4K_ALIGN(cpu_core_size+ioapic_dev_size +irq_override_size);
    cpu_cores = pa_to_va(memblock_alloc(total_size,PAGE_4K_SIZE));
    asm_mem_set(cpu_cores,0,total_size);

    if (count_ioapic > 0) {
        ioapic_dev = (ioapic_devive_t*)((uint64)cpu_cores+cpu_core_size);
    }

    if (irq_override_count > 0) {
        irq_override = (irq_override_t*)((uint64)cpu_cores+cpu_core_size+ioapic_dev_size);
    }


    // =========================================================
    // 第二遍扫描：提取数据并填充 (Populate)
    // =========================================================
    madt_start = (madt_header_t *)&madt->entry;
    uint32 core_idx = 0;
    while ((uint64) madt_start < madt_end) {
        switch (madt_start->type) {
            case 0: // 🌟 补全：传统的 Local APIC (Type 0)
                if (!use_x2apic) { // 只有在系统没有 x2APIC 时才使用 Type 0
                    apic_entry_t *lapic = (apic_entry_t *) madt_start;
                    if (lapic->flags & 1) {
                        cpu_cores[core_idx].apic_id = lapic->apic_id;
                        cpu_cores[core_idx].acpi_proc_id = lapic->processor_id;
                        core_idx++;
                    }
                }
                break;
            case 1: //ioapic
                ioapic_entry_t *ioapic_entry = (ioapic_entry_t *) madt_start;
                ioapic_dev[ioapic_count].phys_addr = ioapic_entry->ioapic_address;
                ioapic_dev[ioapic_count].gsi_base = ioapic_entry->global_system_interrupt_base;
                ioapic_dev[ioapic_count].id = ioapic_entry->ioapic_id;
                ioapic_count++;
                break;
            case 2: //中断重定向
                interrupt_source_override_entry_t *iso_entry = (interrupt_source_override_entry_t *) madt_start;
                irq_override[irq_override_count].flags = iso_entry->flags;
                irq_override[irq_override_count].bus_source = iso_entry->bus_source;
                irq_override[irq_override_count].irq_source = iso_entry->irq_source;
                irq_override[irq_override_count].global_system_interrupt = iso_entry->global_system_interrupt;
                irq_override_count++;
                break;
            case 3: //不可屏蔽中断
                nmi_source_entry_t *nmi_source_entry = (nmi_source_entry_t *) madt_start;
                color_printk(GREEN,BLACK, "non-maskable interrupt:%d\n", nmi_source_entry->global_interrupt);
                break;
            case 4: //apic nmi引脚
                apic_nmi_entry_t *apic_nmi_entry = (apic_nmi_entry_t *) madt_start;
                // 如果 ID 是 0xFF，说明这是“全服广播”
                if (apic_nmi_entry->apic_id == 0xFF) {
                    for (int i = 0; i < core_idx; i++) {
                        cpu_cores[i].lint_nmi = apic_nmi_entry->lint;
                    }
                } else {
                    // 针对特定 CPU，去数组里找它
                    for (int i = 0; i < core_idx; i++) {
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
                color_printk(GREEN, BLACK, "LAPIC Base OVERRIDDEN to 64-bit: %#lX\n", global_lapic_base);
                break;
            case 9: //X2APIC ID
                if (use_x2apic) { // 如果系统支持 x2APIC，独占解析权！
                    x2apic_entry_t *x2apic_entry = (x2apic_entry_t *) madt_start;
                    if (x2apic_entry->flags & 1) {
                        cpu_cores[core_idx].apic_id = x2apic_entry->x2apic_id;
                        cpu_cores[core_idx].acpi_proc_id = x2apic_entry->processor_id;
                        core_idx++;
                    }
                }
                break;
            case 10: //X2APIC不可屏蔽中断
                x2apic_nmi_entry_t *x2apic_nmi_entry = (x2apic_nmi_entry_t *) madt_start;
                // X2APIC 的广播 ID 是 0xFFFFFFFF
                if (x2apic_nmi_entry->x2apic_id == 0xFFFFFFFF) {
                    for (int i = 0; i < core_idx; i++) {
                        cpu_cores[i].lint_nmi = x2apic_nmi_entry->lint;
                    }
                } else {
                    for (int i = 0; i < core_idx; i++) {
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
                color_printk(RED, BLACK, "Modern MP Wakeup Mailbox enabled: %#lX\n", multiprocessor_wakeup_mailbox);
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


void apic_init_1(void) {
    uint64 value;

    //region IA32_APIC_BASE_MSR (MSR 0x1B)
    //X2APIC（bit 10）：作用：如果该位被设置为 1，处理器启用 X2APIC 模式。
    //EN（bit 11）：作用：控制是否启用本地 APIC。设置为 1 时启用本地 APIC；设置为 0 时禁用。
    //BSP（bit 9）：作用：标记该处理器是否是系统的启动处理器（BSP）。系统启动时，BSP 是首先执行初始化代码的 CPU，其它处理器是 AP（Application Processors，应用处理器）。
    //APIC Base Address（bit 12-31）：作用：指定本地 APIC 的基地址。默认情况下，APIC 基地址为 0xFEE00000，但该值可以通过修改来改变，前提是该地址对齐到 4KB。
    //endregion
    value=asm_rdmsr(IA32_APIC_BASE_MSR);
    value |= 0xC00;
    asm_wrmsr(IA32_APIC_BASE_MSR,value);


    // ==========================================================
    // 1. 获取伪中断寄存器的当前值
    // ==========================================================
    value = asm_rdmsr(APIC_SPURIOUS_VECTOR_MSR);
    // 基础配置：开启 Local APIC (Bit 8) + 伪中断向量号设为 0xFF (Bit 0-7)
    // 此时 value 的掩码是 0x01FF
    value |= 0x01FF;

    // ==========================================================
    // 2. 动态探测是否支持“禁用 EOI 广播” (防 #GP 死机神技)
    // ==========================================================
    // 读取 APIC Version Register (在 x2APIC 下是 MSR 0x803)
    uint32 apic_version = asm_rdmsr(APIC_VERSION_MSR);

    // 检查 Bit 24 (Directed EOI Support)
    if (apic_version & (1 << 24)) {
        // 如果硬件支持，才敢把 Bit 12 置为 1！
        value |= 0x1000;
        color_printk(GREEN, BLACK, "[APIC] EOI-Broadcast Suppression ENABLED.\n");
    } else {
        color_printk(YELLOW, BLACK, "[APIC] EOI-Broadcast Suppression NOT supported (VirtualBox?), skipped.\n");
    }
    // ==========================================================
    // 3. 安全写入 SVR
    // ==========================================================
    asm_wrmsr(APIC_SPURIOUS_VECTOR_MSR, value);

    //TPR任务优先级寄存器
    asm_wrmsr(APIC_TASK_PRIORITY_MSR,0x0);

    //热传感器LVT寄存器 bit0-7中断号，bit8-10投递模式000 fixed, bit16屏蔽标志 0未屏蔽 1屏蔽
    asm_wrmsr(APIC_LVT_THERMAL_SENSOR_MSR,0x10022);

    //性能计数器LVT寄存器 bit0-7中断号，bit8-10投递模式000 fixed, bit16屏蔽标志 0未屏蔽 1屏蔽
    asm_wrmsr(APIC_LVT_PERF_COUNTER_MSR,0x10023);

    //本地中断LINT0寄存器 bit0-7中断号，bit8-10投递模式000 fixed, bit13电平触发极性0高电平触发 1低电平触发,bit15触发模式0边沿 1电平，bit16屏蔽标志 0未屏蔽 1屏蔽
    asm_wrmsr(APIC_LVT_LINT0_MSR,0x10024);

    //APIC_LVT_LINT1_MSR bit0-7中断号，bit8-10投递模式000 fixed, bit13电平触发极性0高电平触发 1低电平触发, bit15触发模式0边沿 1电平，bit16屏蔽标志 0未屏蔽 1屏蔽
    asm_wrmsr(APIC_LVT_LINT1_MSR,0x10025);

    //错误LVT寄存器 bit0-7中断号，bit16屏蔽标志 0未屏蔽 1屏蔽
    asm_wrmsr(APIC_LVT_ERROR_MSR,0x10026);
}

void enable_apic_time (uint64 time,uint32 model,uint32 ivt){

    uint32 model_ivt = model | ivt;
    //定时器LVT寄存器 bit0-7中断向量号,bit16屏蔽标志 0未屏蔽 1屏蔽,bit17 18 00/一次计数 01/周期计数 10/TSC-Deadline
    asm_wrmsr(APIC_LVT_TIMER_MSR,model_ivt);


    if(model == APIC_TSC_DEADLINE){
        uint64 cur_tsc= asm_rdtsc();
        uint64 timestamp=cur_tsc + time;
        asm_wrmsr(IA32_TSC_DEADLINE,timestamp);
    } else {
        //分频配置寄存器 bit0 bit1 bit3 0:2 1:4 2:8 3:16 8:32 9:64 0xA:128 0xB:1
        asm_wrmsr(APIC_DIVIDE_CONFIG_MSR, 0xB);
        //初始计数寄存器
        asm_wrmsr(APIC_INITIAL_COUNT_MSR, time);
    }

}