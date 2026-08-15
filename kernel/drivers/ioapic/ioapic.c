#include "ioapic.h"
#include "../../include/acpi.h"
#include "../../include/slub.h"
#include "../../x64/cpu.h"
#include "../../include/printk.h"
#include "../../include/vmm.h"
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
                ioapic_dev.ioapic_hw_res = iomap(ioapic_dev.phys_addr,4096,PAGE_4K_SIZE,PAGE_ROOT_RW_UC_4K );
                color_printk(GREEN, BLACK, "IOAPIC pa:%#lx va:%#lx \n", ioapic_dev.phys_addr,ioapic_dev.ioapic_hw_res);
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


    //region 初始化ioapic
    //索引寄存器0xFEC00000 32bit bit0-7
    //数据寄存器0xFEC00010 32bit
    //EOI寄存器0xFEC00040 32bit bit0-7
    //索引0 ioapic id寄存器 读写 32bit bit24-27
    //索引1 ioapic版本寄存器 读 32bit bit0-7apic版本 bit16-23 +1可用rte寄存器数
    //索引0x10-0x11 中断投递寄存器0 读写 0x10低32bit 0x11高32bit bit0-7中断号 bit16中断屏蔽位 bit56-63 local apic id
    //...
    //索引0x3E-0x3F 中断投递寄存器23 读写
    //endregion
    *ioapic_address.ioregsel=IO_APIC_TBL0_LOW32;
    *ioapic_address.iowin=0x10030;
    *ioapic_address.ioregsel=IO_APIC_TBL0_HIGH32;        //主8259A中断
    *ioapic_address.iowin=0;

    *ioapic_address.ioregsel=IO_APIC_TBL1_LOW32;
    *ioapic_address.iowin=0x31;
    *ioapic_address.ioregsel=IO_APIC_TBL1_HIGH32;        //ps2键盘中断
    *ioapic_address.iowin=0;

    *ioapic_address.ioregsel=IO_APIC_TBL2_LOW32;
    *ioapic_address.iowin=0x10032;
    *ioapic_address.ioregsel=IO_APIC_TBL2_HIGH32;        //8254定时器0/HPTE定时器0
    *ioapic_address.iowin=0;

    *ioapic_address.ioregsel=IO_APIC_TBL3_LOW32;
    *ioapic_address.iowin=0x10000;
    *ioapic_address.ioregsel=IO_APIC_TBL3_HIGH32;        //串口2中断
    *ioapic_address.iowin=0;

    *ioapic_address.ioregsel=IO_APIC_TBL4_LOW32;
    *ioapic_address.iowin=0x10000;
    *ioapic_address.ioregsel=IO_APIC_TBL4_HIGH32;        //串口1中断
    *ioapic_address.iowin=0;

    *ioapic_address.ioregsel=IO_APIC_TBL5_LOW32;
    *ioapic_address.iowin=0x10000;
    *ioapic_address.ioregsel=IO_APIC_TBL5_HIGH32;        //并口2中断
    *ioapic_address.iowin=0;

    *ioapic_address.ioregsel=IO_APIC_TBL6_LOW32;
    *ioapic_address.iowin=0x10000;
    *ioapic_address.ioregsel=IO_APIC_TBL6_HIGH32;        //软驱中断
    *ioapic_address.iowin=0;

    *ioapic_address.ioregsel=IO_APIC_TBL7_LOW32;
    *ioapic_address.iowin=0x10000;
    *ioapic_address.ioregsel=IO_APIC_TBL7_HIGH32;        //并口1中断
    *ioapic_address.iowin=0;

    *ioapic_address.ioregsel=IO_APIC_TBL8_LOW32;
    *ioapic_address.iowin=0x10033;
    *ioapic_address.ioregsel=IO_APIC_TBL8_HIGH32;        //CMOS RTC中断/HPTE定时器1
    *ioapic_address.iowin=0;

//    *ioapic_address.ioregsel=IO_APIC_TBL9_LOW32;
//    *ioapic_address.iowin=0x10039;
//    *ioapic_address.ioregsel=IO_APIC_TBL9_HIGH32;        //无
//    *ioapic_address.iowin=0;
//
//    *ioapic_address.ioregsel=IO_APIC_TBL10_LOW32;
//    *ioapic_address.iowin=0x1003A;
//    *ioapic_address.ioregsel=IO_APIC_TBL10_HIGH32;       //无
//    *ioapic_address.iowin=0;

    *ioapic_address.ioregsel=IO_APIC_TBL11_LOW32;
    *ioapic_address.iowin=0x10034;
    *ioapic_address.ioregsel=IO_APIC_TBL11_HIGH32;        //HPTE 定时器2
    *ioapic_address.iowin=0;

    *ioapic_address.ioregsel=IO_APIC_TBL12_LOW32;
    *ioapic_address.iowin=0x10035;
    *ioapic_address.ioregsel=IO_APIC_TBL12_HIGH32;        //ps2鼠标 /HPET定时器3
    *ioapic_address.iowin=0;

    *ioapic_address.ioregsel=IO_APIC_TBL13_LOW32;
    *ioapic_address.iowin=0x10036;
    *ioapic_address.ioregsel=IO_APIC_TBL13_HIGH32;        //FERR/DMA
    *ioapic_address.iowin=0;

    *ioapic_address.ioregsel=IO_APIC_TBL14_LOW32;
    *ioapic_address.iowin=0x10037;
    *ioapic_address.ioregsel=IO_APIC_TBL14_HIGH32;        //主SATA中断
    *ioapic_address.iowin=0;

    *ioapic_address.ioregsel=IO_APIC_TBL15_LOW32;
    *ioapic_address.iowin=0x10038;
    *ioapic_address.ioregsel=IO_APIC_TBL15_HIGH32;        //从SATA中断
    *ioapic_address.iowin=0;

    while (1);
}
