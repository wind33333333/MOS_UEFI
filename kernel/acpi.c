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

        for (UINT32 i = 0; i < ((xsdt->acpi_header.length - sizeof(acpi_header_t)) / sizeof(UINT32 *)); i++) {
            if(*xsdt->entry[i]==0x43495041) {//"APIC"
                madt = (madt_t *) xsdt->entry[i];
                madt_header_t *madt_entry = (madt_header_t *)&madt->entry;
                while((UINT64)madt_entry < ((UINT64)madt+madt->acpi_header.length)){
                    if(madt_entry->type == 0){//local apic
                        madt_entry=(madt_header_t *)((UINT64)madt_entry+((apic_entry_t *)madt_entry)->madt_header.length);
                    }else if (madt_entry->type == 1) {//ioapic
                        ioapic_baseaddr=(UINT32 *)LADDR_TO_HADDR(((ioapic_entry_t *)madt_entry)->ioapic_address);
                        color_printk(RED,BLACK,"IOAPIC Addr:%#lX\n",((ioapic_entry_t *)madt_entry)->ioapic_address);
                        madt_entry=(madt_header_t *)((UINT64)madt_entry+((ioapic_entry_t *)madt_entry)->madt_header.length);
                    }else if(madt_entry->type == 2){//中断重定向
                        color_printk(RED,BLACK,"IRQ#%d -> GSI#%d\n",((interrupt_source_override_entry_t *)madt_entry)->irq_source,((interrupt_source_override_entry_t *)madt_entry)->global_system_interrupt);
                        madt_entry=(madt_header_t *)((UINT64)madt_entry+((interrupt_source_override_entry_t *)madt_entry)->madt_header.length);
                    }else if(madt_entry->type == 4){//nmi中断
                        color_printk(RED,BLACK,"NMI ApicProcessorID:%#lX LINT:%d\n",((nmi_entry_t *)madt_entry)->acpi_processor_id,((nmi_entry_t *)madt_entry)->lint);
                        madt_entry=(madt_header_t *)((UINT64)(madt_entry+((nmi_entry_t *)madt_entry)->madt_header.length));
                    }
                }
            }else if(*xsdt->entry[i]==0x54455048) {//"HPET"
                hpett = (hpett_t *) xsdt->entry[i];
                hpet.address = (UINT64) LADDR_TO_HADDR(hpett->acpi_generic_adderss.address);
                color_printk(RED,BLACK,"HPET MiniMumTick:%d Number:%d SpaceID:%d BitWidth:%d BiteOffset:%d AccessSize:%d Address:%#lX\n",hpett->minimum_tick,hpett->hpet_number,hpett->acpi_generic_adderss.space_id,hpett->acpi_generic_adderss.bit_width,hpett->acpi_generic_adderss.bit_offset,hpett->acpi_generic_adderss.access_size,hpett->acpi_generic_adderss.address);
            }else if(*xsdt->entry[i]==0x4746434D){//"MCFG"
                    mcfg = (mcfg_t *) xsdt->entry[i];
                    mcfg_entry_t *mcfg_entry=(mcfg_entry_t *)&mcfg->entry;
                for(UINT32 i=0;i<((mcfg->acpi_header.length-sizeof(acpi_header_t)-sizeof(UINT64))/sizeof(mcfg_entry_t));i++){
                    color_printk(RED,BLACK,"PCIE BaseAddr:%#lX Segment:%d StartBus:%d EndBus:%d\n",mcfg_entry[i].base_address,mcfg_entry[i].pci_segment,mcfg_entry[i].start_bus,mcfg_entry[i].end_bus);
                }
            }
        }
    }
    return;
}