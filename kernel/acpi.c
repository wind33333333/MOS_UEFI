#include "acpi.h"
#include "uefi.h"
#include "ioapic.h"
#include "hpet.h"
#include "memory.h"
#include "printk.h"

__attribute__((section(".init_text"))) void init_acpi(UINT8 bsp_flags) {
    if (bsp_flags) {
        xsdt_t *xsdt = boot_info->rsdp->xsdt_address;
        madt_t *madt;
        hpett_t *hpett;
        mcfg_t *mcfg;

        for (UINT32 i = 0; i < ((xsdt->header.length - sizeof(acpi_table_header_t)) / 4); i++) {
            if(*xsdt->entry[i]==0x43495041) {//"APIC"
                madt = (madt_t *) xsdt->entry[i];
                madt_header_t *madt_header = (madt_header_t *)&madt->madt_header;
                apic_entry_t *apic_entry;
                ioapic_entry_t *ioapic_entry;
                interrupt_source_override_entry_t *interrupt_source_override_entry;
                nmi_entry_t *nmi_source_entry;
                while((UINT64)madt_header < ((UINT64)madt+madt->header.length)){
                    if(madt_header->type == 0){//local apic
                        apic_entry=(apic_entry_t *)madt_header;
                        madt_header=(madt_header_t *)((UINT64)madt_header+apic_entry->madt_header.length);
                    }else if (madt_header->type == 1) {//ioapic
                        ioapic_entry=(ioapic_entry_t*)madt_header;
                        ioapic_baseaddr=(UINT32 *) LADDR_TO_HADDR(ioapic_entry->ioapic_address);
                        color_printk(RED,BLACK,"IOAPIC Addr:%#lX\n",ioapic_entry->ioapic_address);
                        madt_header=(madt_header_t *)((UINT64)madt_header+ioapic_entry->madt_header.length);
                    }else if(madt_header->type == 2){//中断重定向
                        interrupt_source_override_entry=(interrupt_source_override_entry_t *)madt_header;
                        color_printk(RED,BLACK,"IRQ#%d -> GSI#%d\n",interrupt_source_override_entry->irq_source,interrupt_source_override_entry->global_system_interrupt);
                        madt_header=(madt_header_t *)((UINT64)madt_header+interrupt_source_override_entry->madt_header.length);
                    }else if(madt_header->type == 4){//nmi中断
                        nmi_source_entry=(nmi_entry_t *)madt_header;
                        color_printk(RED,BLACK,"NMI ApicProcessorID:%#lX LINT:%d\n",nmi_source_entry->acpi_processor_id,nmi_source_entry->lint);
                        madt_header=(madt_header_t *)((UINT64)(madt_header+nmi_source_entry->madt_header.length));
                    }
                }
            }else if(*xsdt->entry[i]==0x54455048) {//"HPET"
                hpett = (hpett_t *) xsdt->entry[i];
                hpet.address = (UINT64) LADDR_TO_HADDR(hpett->address);
            }else if(*xsdt->entry[i]==0x4746434D){//"MCFG"
                    mcfg = (mcfg_t *) xsdt->entry[i];
            }
        }

        /*    IOAPIC *ioapic = 0;
            InterruptSourceOverride *isr = 0;
            UINT32 j =0;
            for (UINT32 i = 0; i < ((madt->Length - 44) / 2); i++) {
                if ((madt->Header[i].Type == 1) && (madt->Header[i].Length == 0xC)) {
                    ioapic = (IOAPIC *) &madt->Header[i];
                    ioapic_baseaddr = LADDR_TO_HADDR((UINT32 *) ioapic->ioapic_address);
                } else if ((madt->Header[i].Type == 2) && (madt->Header[i].Length == 0xA)) {
                    isr = (InterruptSourceOverride *) &madt->Header[i];
                    irq_to_gsi[j].IRQ = isr->Source;
                    irq_to_gsi[j].GSI = isr->GlobalSystemInterrupt;
                    j++;
                }
            }

            color_printk(YELLOW, BLACK, "RSDP: %#018lX \tRSDT: %#018lX\n", rsdp,
                         rsdt);
            color_printk(YELLOW, BLACK, "APIC: %#018lX \tIOAPIC ADDR: %#018lX\n", madt,
                         ioapic_baseaddr);

            color_printk(YELLOW, BLACK, "HPET: %#018lX \tHPET ADDR: %#018lX\n", hpet, hpet.address);

            color_printk(YELLOW, BLACK, "MCFG: %#018lX \n", mcfg);

            for (int i = 0; i < 24; ++i) {
                if ((irq_to_gsi[i].IRQ == 0x0) && (irq_to_gsi[i].GSI == 0x0))
                    break;
                color_printk(YELLOW, BLACK, "IRQ#%d -> GSI#%d\n", irq_to_gsi[i].IRQ,
                             irq_to_gsi[i].GSI);

            }*/
    }
    return;
}