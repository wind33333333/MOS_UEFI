#include "acpi.h"

__attribute__((section(".init_text"))) void acpiInit(UINT8 bspFlags) {
    if (bspFlags) {
        RSDP *rsdp = (RSDP *) 0xe0000;
        RSDT *rsdt = (RSDT *) 0;
        MADT *madt = (MADT *) 0;
        HPET *hpet = (HPET *) 0;
        MCFG *mcfg = (MCFG *) 0;

        for (; rsdp < 0x100000; rsdp = (UINT64 *) rsdp + 2) {
            if (*(UINT64 *) rsdp == 0x2052545020445352) {  //'RSD PTR '
                rsdt = (RSDT *) rsdp->RsdtAddress;
                break;
            }
        }

        for (UINT32 i = 0; i < (rsdt->Length - 36) / 4; i++) {
            switch (*(UINT32 *) rsdt->Entry[i]) {
                case 0x43495041:        //"APIC"
                    madt = (MADT *) rsdt->Entry[i];
                    break;
                case 0x54455048:        //"HPET"
                    hpet = (HPET *) rsdt->Entry[i];
                    hpet_attr.baseaddr = (UINT64)LADDR_TO_HADDR(hpet->BaseAddressUpper);
                    break;
                case 0x4746434D:        //"MCFG"
                    mcfg = (MCFG *) rsdt->Entry[i];
                    break;
            }
        }

        IOAPIC *ioapic = 0;
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

        colorPrintK(YELLOW, BLACK, "RSDP: %#018lX \tRSDT: %#018lX\n", rsdp,
                     rsdt);
        colorPrintK(YELLOW, BLACK, "APIC: %#018lX \tIOAPIC ADDR: %#018lX\n", madt,
                     ioapic_baseaddr);

        colorPrintK(YELLOW, BLACK, "HPET: %#018lX \tHPET ADDR: %#018lX\n", hpet, hpet_attr.baseaddr);

        colorPrintK(YELLOW, BLACK, "MCFG: %#018lX \n", mcfg);

        for (int i = 0; i < 24; ++i) {
            if ((irq_to_gsi[i].IRQ == 0x0) && (irq_to_gsi[i].GSI == 0x0))
                break;
            colorPrintK(YELLOW, BLACK, "IRQ#%d -> GSI#%d\n", irq_to_gsi[i].IRQ,
                         irq_to_gsi[i].GSI);

        }

        return;
    }
}