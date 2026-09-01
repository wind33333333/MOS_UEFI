#include "ioapic.h"
#include "../../include/acpi.h"
#include "../../include/slub.h"
#include "../../x64/cpu.h"
#include "../../include/printk.h"
#include "../include/vmalloc.h"

ioapic_devive_t ioapic_dev;


// 基础读写原语
static inline uint32 ioapic_read(struct ioapic_devive_t *dev, uint8 index) {
    ioapic_hw_res_t *ioapic_hw_res = dev->ioapic_hw_res;
    uint32 val;
    //spin_lock(&ioapic_lock);
    ioapic_hw_res->reg_sel = index;         // 1. 选中内部寄存器索引
    val = ioapic_hw_res->reg_win;           // 2. 读出数据
    //spin_unlock(&ioapic_lock);
    return val;
}

static inline void ioapic_write(struct ioapic_devive_t *dev, uint8 index, uint32 val) {
    ioapic_hw_res_t *ioapic_hw_res = dev->ioapic_hw_res;
    //spin_lock(&ioapic_lock);
    ioapic_hw_res->reg_sel = index;         // 1. 选中内部寄存器索引
    ioapic_hw_res->reg_win = val;           // 2. 写入数据
    //spin_unlock(&ioapic_lock);
}

// -------------------------------------------------------------
// 高级抽象：配置指定引脚的 RTE (Redirection Table Entry)
// -------------------------------------------------------------
void ioapic_write_rte(struct ioapic_devive_t *dev, uint8 pin, ioapic_rte_t rte) {
    // 架构师防线：防止越界访问
    if (pin >= dev->max_intr_entries) {
        color_printk(RED, BLACK, "[IOAPIC] Error: Pin %d exceeds max entries %d\n",
                     pin, dev->max_intr_entries);
        return;
    }

    // RTE 的索引计算公式：低32位索引 = 0x10 + 2*pin, 高32位 = 0x10 + 2*pin + 1
    uint8 low_idx = 0x10 + (pin * 2);
    uint8 high_idx = 0x10 + (pin * 2) + 1;

    // 经典内核踩坑点：为了防止在写入过程中触发幽灵中断，必须先屏蔽(Mask)，再写值！

    // 1. 构造一个先屏蔽的下半部分
    ioapic_rte_t temp_rte = rte;
    temp_rte.bits.mask = 1;

    // 2. 先写入低 32 位（此时中断已被屏蔽）
    ioapic_write(dev, low_idx, temp_rte.dwords.low_dword);

    // 3. 再写入高 32 位（配置目标 CPU 等）
    ioapic_write(dev, high_idx, rte.dwords.high_dword);

    // 4. 最后重新写入低 32 位（解除屏蔽，如果用户传入的 rte.bits.mask 是 0 的话）
    ioapic_write(dev, low_idx, rte.dwords.low_dword);
}


// IOAPIC 内部寄存器偏移
#define IOAPIC_REG_ID      0x00
#define IOAPIC_REG_VER     0x01

// -------------------------------------------------------------
// 禁用指定 IOAPIC 的所有引脚中断
// -------------------------------------------------------------
void ioapic_disable_all_interrupts(struct ioapic_devive_t *dev) {
    ioapic_dev.ioapic_hw_res = ioremap(ioapic_dev.phys_addr,4096);

    // 1. 读取版本寄存器 (0x01)
    uint32 ver = ioapic_read(dev, IOAPIC_REG_VER);

    // 2. 解析最大引脚数
    // Intel 规定：Version 寄存器的 Bit 16~23 存放的是 "Maximum Redirection Entry"
    // 注意：硬件返回的是 (总数 - 1)，比如返回 23，说明有 24 个引脚 (0~23)
    uint8 max_entries = ((ver >> 16) & 0xFF) + 1;

    // 顺手把正确的数量存入我们的设备结构体中
    dev->max_intr_entries = max_entries;

    color_printk(GREEN, BLACK, "IOAPIC Disabling all %d pa:%#lx va:%#lx \n",max_entries, ioapic_dev.phys_addr,ioapic_dev.ioapic_hw_res);


    // 3. 遍历每一个引脚，下达“封口令”
    for (uint8 i = 0; i < max_entries; i++) {
        ioapic_rte_t rte;

        // 先将整个 64 位清零，这会清除之前可能遗留的电平极性和投递状态
        rte.value = 0;

        // 核心：设置 Mask 位 (Bit 16 = 1) -> 屏蔽中断！
        rte.bits.mask = 1;

        // 架构师防御性编程：
        // 即使中断被屏蔽，我们也把默认触发模式切回安全的“边沿触发(Edge)”，
        // 并且给一个合法的默认向量号（比如 0x20），防止某些存在 Bug 的芯片乱发信号。
        rte.bits.trigger_mode = 0; // 0 = Edge
        rte.bits.vector = 0x00;

        // 写入硬件
        ioapic_write_rte(dev, i, rte);
    }

}



INIT_TEXT void init_ioapic(void) {
    //从madt表中获取关键数据
    madt_t *madt = acpi_get_table('CIPA');
    madt_header_t *madt_entry = (madt_header_t *) &madt->entry;
    uint64 madt_endaddr = (uint64) madt + madt->acpi_header.length;
    uint32 apic_id_index = 0;
    apic_id_table = kzalloc(4096);
    while ((uint64) madt_entry < madt_endaddr) {
        switch (madt_entry->type) {
            case 0: //APIC ID
                apic_entry_t *apic_entry = (apic_entry_t *) madt_entry;
                if (apic_entry->flags & 1) {
                    color_printk(GREEN, BLACK, "Apic_id:%d Proc_id:%d Flags:%d\n", apic_entry->apic_id,
                                 apic_entry->processor_id, apic_entry->flags);
                    apic_id_table[apic_id_index] = apic_entry->apic_id;
                    apic_id_index++;
                    cpu_info.logical_processors_number++;
                }
                break;
            case 1: //ioapic
                ioapic_entry_t *ioapic_entry = (ioapic_entry_t *) madt_entry;
                ioapic_dev.phys_addr = ioapic_entry->ioapic_address;
                break;
            case 2: //中断重定向
                interrupt_source_override_entry_t *iso_entry = (interrupt_source_override_entry_t *) madt_entry;
                color_printk(GREEN, BLACK, "IRQ#%d -> GSI#%d\n", iso_entry->irq_source,
                             iso_entry->global_system_interrupt);
                break;
            case 3: //不可屏蔽中断
                nmi_source_entry_t *nmi_source_entry = (nmi_source_entry_t *) madt_entry;
                color_printk(GREEN,BLACK, "non-maskable interrupt:%d\n", nmi_source_entry->global_interrupt);
                break;
            case 4: //apic nmi引脚
                apic_nmi_entry_t *apic_nmi_entry = (apic_nmi_entry_t *) madt_entry;
                //color_printk(GREEN, BLACK, "APIC NMI ApicID:%#lX LINT:%d\n", apic_nmi_entry->apic_id,apic_nmi_entry->lint);
                break;
            case 5: //64位local apic地址
                apic_address_override_entry_t *apic_addr_override_entry = (apic_address_override_entry_t *)
                        madt_entry;
                color_printk(GREEN,BLACK, "64-bit local apic address:%#lX\n",
                             apic_addr_override_entry->apic_address);
                break;
            case 9: //X2APIC ID
                x2apic_entry_t *x2apic_entry = (x2apic_entry_t *) madt_entry;
                color_printk(GREEN, BLACK, "x2apic_id:%d proc_id:%d flags:%d\n", x2apic_entry->x2apic_id,
                             x2apic_entry->processor_id, x2apic_entry->flags);
                apic_id_table[apic_id_index] = apic_entry->apic_id;
                apic_id_index++;
                cpu_info.logical_processors_number++;
                break;
            case 10: //X2APIC不可屏蔽中断
                x2apic_nmi_entry_t *x2apic_nmi_entry = (x2apic_nmi_entry_t *) madt_entry;
                color_printk(RED,BLACK, "X2APIC NMI X2ApicID:%#lX LINT:%d\n", x2apic_nmi_entry->x2apic_id,
                             x2apic_nmi_entry->lint);
                break;
            case 13: //多处理器唤醒
                multiprocessor_wakeup_entry_t *mult_proc_wakeup_entry = (multiprocessor_wakeup_entry_t *)
                        madt_entry;
                color_printk(RED,BLACK, "Multiprocessor Wakeup Address:%#lX\n",
                             mult_proc_wakeup_entry->mailbox_address);
                break;
        }
        madt_entry = (madt_header_t *) ((uint64) madt_entry + madt_entry->length);
    }


    //禁用8259A
    asm_io_out8(0x21,0xff);     //禁用主8259A
    asm_io_out8(0xA1,0xff);     //禁用从8259A

    asm_io_out8(0x43,0x30);
    asm_io_out8(0x40,0);
    asm_io_out8(0x40,0);        //禁用8054计时器0

    asm_io_out8(0x43,0x70);
    asm_io_out8(0x41,0);
    asm_io_out8(0x41,0);        //禁用8054计时器1

    asm_io_out8(0x43,0xB0);
    asm_io_out8(0x42,0);
    asm_io_out8(0x42,0);        //禁用8054计时器2

    ioapic_disable_all_interrupts(&ioapic_dev);

}
