#include "acpi.h"

__attribute__((section(".init_text"))) void acpiInit(unsigned char bspFlags) {
    if (bspFlags) {
        RSDP *rsdp = (RSDP *) 0xe0000;
        RSDT *rsdt = (RSDT *) 0;
        MADT *madt = (MADT *) 0;
        HPET *hpet = (HPET *) 0;
        MCFG *mcfg = (MCFG *) 0;

        for (; rsdp < 0x100000; rsdp = (unsigned long *) rsdp + 2) {
            if (*(unsigned long *) rsdp == 0x2052545020445352) {  //'RSD PTR '
                rsdt = (RSDT *) rsdp->RsdtAddress;
                break;
            }
        }

        for (unsigned int i = 0; i < (rsdt->Length - 36) / 4; i++) {
            switch (*(unsigned int *) rsdt->Entry[i]) {
                case 0x43495041:        //"APIC"
                    madt = (MADT *) rsdt->Entry[i];
                    break;
                case 0x54455048:        //"HPET"
                    hpet = (HPET *) rsdt->Entry[i];
                    hpet_attr.baseaddr = (unsigned long)LADDR_TO_HADDR(hpet->BaseAddressUpper);
                    break;
                case 0x4746434D:        //"MCFG"
                    mcfg = (MCFG *) rsdt->Entry[i];
                    break;
            }
        }

        IOAPIC *ioapic = 0;
        InterruptSourceOverride *isr = 0;
        unsigned int j =0;
        for (unsigned int i = 0; i < ((madt->Length - 44) / 2); i++) {
            if ((madt->Header[i].Type == 1) && (madt->Header[i].Length == 0xC)) {
                ioapic = (IOAPIC *) &madt->Header[i];
                ioapic_baseaddr = LADDR_TO_HADDR((unsigned int *) ioapic->ioapic_address);
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

        color_printk(YELLOW, BLACK, "HPET: %#018lX \tHPET ADDR: %#018lX\n", hpet, hpet_attr.baseaddr);

        color_printk(YELLOW, BLACK, "MCFG: %#018lX \n", mcfg);

        for (int i = 0; i < 24; ++i) {
            if ((irq_to_gsi[i].IRQ == 0x0) && (irq_to_gsi[i].GSI == 0x0))
                break;
            color_printk(YELLOW, BLACK, "IRQ#%d -> GSI#%d\n", irq_to_gsi[i].IRQ,
                         irq_to_gsi[i].GSI);

        }

        return;
    }
}