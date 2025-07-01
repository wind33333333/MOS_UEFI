#include "acpi.h"
#include "uefi.h"
#include "ioapic.h"
#include "hpet.h"
#include "printk.h"
#include "cpu.h"
#include "ap.h"
#include "slub.h"
#include "vmalloc.h"
#include "vmm.h"
#include "xhci.h"
#include "pcie.h"

/*
 * 查找acpi表
 * 参数用法 talbe = 'TEPH' //hpet表
 * 返回acpi表的指针
 */
INIT_TEXT void *acpi_get_table(UINT32 table) {
    acpi_header_t **acpi_table = &boot_info->rsdp->xsdt_address->entry;
    UINT32 acpi_count = (boot_info->rsdp->xsdt_address->acpi_header.length - sizeof(acpi_header_t)) / sizeof(UINT32 *);
    for (UINT32 i = 0; i < acpi_count; i++) {
        if (acpi_table[i]->signature == table) return acpi_table[i];
    }
    return NULL;
}

INIT_TEXT void init_acpi(void) {
    madt_t *madt;
    hpett_t *hpett;

    //region MADT初始化
    madt_header_t *madt_entry = (madt_header_t *) &madt->entry;
    UINT64 madt_endaddr = (UINT64) madt + madt->acpi_header.length;
    UINT32 apic_id_index = 0;
    apic_id_table = kmalloc(4096);
    mem_set(apic_id_table, 0, 4096);
    while ((UINT64) madt_entry < madt_endaddr) {
        switch (madt_entry->type) {
            case 0: //APIC ID
                apic_entry_t *apic_entry = (apic_entry_t *) madt_entry;
                if (apic_entry->flags & 1) {
                    color_printk(GREEN, BLACK, "apic_id:%d proc_id:%d flags:%d\n", apic_entry->apic_id,
                                 apic_entry->processor_id, apic_entry->flags);
                    apic_id_table[apic_id_index] = apic_entry->apic_id;
                    apic_id_index++;
                    cpu_info.logical_processors_number++;
                }
                break;
            case 1: //ioapic
                ioapic_entry_t *ioapic_entry = (ioapic_entry_t *) madt_entry;
                ioapic_address.ioregsel = pa_to_va(ioapic_entry->ioapic_address);
                ioapic_address.iowin = (UINT32 *) ((UINT64) ioapic_address.ioregsel + 0x10);
                ioapic_address.eoi = (UINT32 *) ((UINT64) ioapic_address.ioregsel + 0x40);
                color_printk(GREEN, BLACK, "IOAPIC Addr:%#lX\n", ioapic_entry->ioapic_address);
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
                color_printk(GREEN, BLACK, "APIC NMI ApicID:%#lX LINT:%d\n", apic_nmi_entry->apic_id,
                             apic_nmi_entry->lint);
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
                ((UINT32 *) ap_boot_loader_address)[apic_id_index] = x2apic_entry->x2apic_id;
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
        madt_entry = (madt_header_t *) ((UINT64) madt_entry + madt_entry->length);
    }
    //endregion

    //hpet初始化
    hpet1.address = (UINT64) pa_to_va(hpett->acpi_generic_adderss.address);
    color_printk(
        GREEN,BLACK,
        "HPET MiniMumTick:%d Number:%d SpaceID:%d BitWidth:%d BiteOffset:%d AccessSize:%d Address:%#lX\n",
        hpett->minimum_tick, hpett->hpet_number, hpett->acpi_generic_adderss.space_id,
        hpett->acpi_generic_adderss.bit_width, hpett->acpi_generic_adderss.bit_offset,
        hpett->acpi_generic_adderss.access_size, hpett->acpi_generic_adderss.address);

}

