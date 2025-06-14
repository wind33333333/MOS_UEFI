#include "acpi.h"
#include "uefi.h"
#include "ioapic.h"
#include "hpet.h"
#include "printk.h"
#include "cpu.h"
#include "ap.h"
#include "apic.h"
#include "memblock.h"
#include "vmalloc.h"
#include "vmm.h"
#include "xhci.h"
#include "pcie.h"


UINT32 *apic_id_table; //apic_id_table

void scan_pcie(UINT64 ecam_base, UINT8 start_bus) {
    UINT8 bus = start_bus;
    for (UINT8 dev = 0; dev < 32; dev++) {
        for (UINT8 func = 0; func < 8; func++) {
            pcie_config_space_t *pcie_config_space = (pcie_config_space_t *) (
                ecam_base + (bus << 20) + (dev << 15) + (func << 12));
            if (pcie_config_space->header.vendor_id == 0xFFFF && func == 0) break;
            if (pcie_config_space->header.vendor_id == 0xFFFF) continue;
            if (pcie_config_space->header.header_type & 1) {
                //type1 pcie桥
                UINT32 *class_code = &pcie_config_space->header.class_code;
                color_printk(
                    GREEN,BLACK, "bus:%d dev:%d func:%d vorend_id:%#lx device_id:%#lx class_code:%#lx\n", bus,
                    dev, func, pcie_config_space->header.vendor_id, pcie_config_space->header.device_id,
                    *class_code & 0xFFFFFF);
                scan_pcie(ecam_base, pcie_config_space->header.type1.secondary_bus);
            } else {
                //type0 终端设备
                UINT32 *class_code = &pcie_config_space->header.class_code;
                color_printk(
                    GREEN,BLACK, "bus:%d dev:%d func:%d vorend_id:%#lx device_id:%#lx class_code:%#lx\n", bus,
                    dev, func, pcie_config_space->header.vendor_id, pcie_config_space->header.device_id,
                    *class_code & 0xFFFFFF);
                if ((pcie_config_space->header.header_type & 0x80) == 0) break;
            }
        }
    }
}

INIT_TEXT void init_acpi(void) {
    madt_t *madt;
    hpett_t *hpett;
    mcfg_t *mcfg;

    //初始化ap_boot_loader_adderss
    ap_boot_loader_address = (UINT64) pa_to_va(memblock.reserved.region[0].base);

    //region XSDT中找出各个ACPI表的指针
    xsdt_t *xsdt = boot_info->rsdp->xsdt_address;
    UINT32 xsdt_count = (xsdt->acpi_header.length - sizeof(acpi_header_t)) / sizeof(UINT32 *);
    for (UINT32 i = 0; i < xsdt_count; i++) {
        switch (*xsdt->entry[i]) {
            case 0x43495041: //"APIC 指针"
                madt = (madt_t *) xsdt->entry[i];
                break;
            case 0x54455048: //"HPET 指针"
                hpett = (hpett_t *) xsdt->entry[i];
                break;
            case 0x4746434D: //"MCFG 指针"
                mcfg = (mcfg_t *) xsdt->entry[i];
                break;
        }
    }
    //endregion

    //region MADT初始化
    UINT32 apic_id_index = 0;
    madt_header_t *madt_entry = (madt_header_t *) &madt->entry;
    UINT64 madt_endaddr = (UINT64) madt + madt->acpi_header.length;
    while ((UINT64) madt_entry < madt_endaddr) {
        switch (madt_entry->type) {
            case 0: //APIC ID
                apic_entry_t *apic_entry = (apic_entry_t *) madt_entry;
                if (apic_entry->flags & 1) {
                    color_printk(GREEN, BLACK, "apic_id:%d proc_id:%d flags:%d\n", apic_entry->apic_id,
                                 apic_entry->processor_id, apic_entry->flags);
                    ((UINT32 *) ap_boot_loader_address)[apic_id_index] = apic_entry->apic_id;
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

    //mcfg初始化
    mcfg_entry_t *mcfg_entry = &mcfg->entry;
    UINT32 mcfg_count = (mcfg->acpi_header.length - sizeof(acpi_header_t) - sizeof(mcfg->reserved)) / sizeof(
                            mcfg_entry_t);
    for (UINT32 j = 0; j < mcfg_count; j++) {
        color_printk(GREEN,BLACK, "PCIE BaseAddr:%#lX Segment:%d StartBus:%d EndBus:%d\n",
                     mcfg_entry[j].base_address,
                     mcfg_entry[j].pci_segment, mcfg_entry[j].start_bus, mcfg_entry[j].end_bus);
    }

    scan_pcie(mcfg_entry->base_address, 0);


    pcie_config_space_t *pcie_xhci = 0xE0010000;
    UINT64 *xhci_bar0 = &pcie_xhci->header.type0.bar[0];
    UINT64 bak = *xhci_bar0;
    *xhci_bar0 = 0xFFFFFFFFFFFFFFFFUL;
    *xhci_bar0 = bak;
    xhci_cap_regs_t *xchi_cap = iomap(bak & ~0xF, 0x4000,PAGE_4K_SIZE,PAGE_ROOT_RW_WC_4K);
    xhci_op_regs_t *xhci_op_regs = xchi_cap->caplength + (UINT64) xchi_cap;
    msi_x_capability_t *cap = 0xE0010000 + pcie_xhci->header.type0.cap_ptr;
    msi_x_table_entry_t *msi_x_table_entry = (UINT64) xchi_cap + (cap->table_offset_bir & ~0x3);
    msi_x_table_entry_t *msi;
    for (UINT16 i = 0; i < 20; i++) {
        msi = &msi_x_table_entry[i];
        msi->msg_addr_hi = 0xFF;
        msi->msg_addr_lo = 0xEE;
        msi->msg_data = 8;
        msi->vector_control = 0;
    }


    //移动apic id到内核空间
    apic_id_table = (UINT32 *) pa_to_va(
        bitmap_alloc_pages(PAGE_4K_ALIGN(cpu_info.logical_processors_number<<2) >> PAGE_4K_SHIFT));
    mem_set((void *) apic_id_table, 0x0,PAGE_4K_ALIGN(cpu_info.logical_processors_number<<2));
    for (UINT32 i = 0; i < cpu_info.logical_processors_number; i++) {
        apic_id_table[i] = ((UINT32 *) ap_boot_loader_address)[i];
    }
}


INIT_TEXT UINT32 apicid_to_cpuid(UINT32 apic_id) {
    for (UINT32 i = 0; i < cpu_info.logical_processors_number; i++) {
        if (apic_id == apic_id_table[i])
            return i;
    }
    return 0xFFFFFFFF;
}

INIT_TEXT UINT32 cpuid_to_apicid(UINT32 cpu_id) {
    return apic_id_table[cpu_id];
}
