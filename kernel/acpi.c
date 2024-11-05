#include "acpi.h"
#include "uefi.h"
#include "ioapic.h"
#include "hpet.h"
#include "memory.h"
#include "printk.h"
#include "cpu.h"

UINT32 apic_id_table[1024];   //apic_id_table

__attribute__((section(".init_text"))) void init_acpi(void) {
    mem_set((void*)apic_id_table,0x0,1024*sizeof(UINT32));
    madt_t *madt;
    hpett_t *hpett;
    mcfg_t *mcfg;

    //region XSDT中找出各个ACPI表的指针
    xsdt_t *xsdt = boot_info->rsdp->xsdt_address;
    for (UINT32 i = 0; i < ((xsdt->acpi_header.length - sizeof(acpi_header_t)) / sizeof(UINT32 *)); i++) {
        switch (*xsdt->entry[i]) {
            case 0x43495041://"APIC 指针"
                madt = (madt_t *) xsdt->entry[i];
                break;
            case 0x54455048://"HPET 指针"
                hpett = (hpett_t *) xsdt->entry[i];
                break;
            case 0x4746434D://"MCFG 指针"
                mcfg = (mcfg_t *) xsdt->entry[i];
                break;
        }
    }
    //endregion

    //region MADT初始化
    UINT32 apic_id_index=0;
    madt_header_t *madt_entry = (madt_header_t *)&madt->entry;
    while((UINT64)madt_entry < ((UINT64)madt+madt->acpi_header.length)){
        switch(madt_entry->type) {
            case 0://APIC ID
                if(((apic_entry_t*)madt_entry)->apic_id != 255){
                    apic_id_table[apic_id_index]=((apic_entry_t *) madt_entry)->apic_id;
                    apic_id_index++;
                    cpu_info.logical_processors_number++;
                }
                break;

            case 1://ioapic
                ioapic_baseaddr = (UINT32 *) LADDR_TO_HADDR(
                        ((ioapic_entry_t *) madt_entry)->ioapic_address);
                color_printk(RED, BLACK, "IOAPIC Addr:%#lX\n",
                             ((ioapic_entry_t *) madt_entry)->ioapic_address);
                break;

            case 2://中断重定向
                color_printk(RED, BLACK, "IRQ#%d -> GSI#%d\n",
                             ((interrupt_source_override_entry_t *) madt_entry)->irq_source,
                             ((interrupt_source_override_entry_t *) madt_entry)->global_system_interrupt);
                break;

            case 3://不可屏蔽中断
                color_printk(RED,BLACK,"non-maskable interrupt:%d\n",((nmi_source_entry_t *) madt_entry)->global_interrupt);
                break;
            case 4://apic nmi引脚
                color_printk(RED, BLACK, "APIC NMI ApicID:%#lX LINT:%d\n",
                             ((apic_nmi_entry_t *) madt_entry)->apic_id,
                             ((apic_nmi_entry_t *) madt_entry)->lint);
                break;

            case 5://64位local apic地址
                color_printk(RED,BLACK,"64-bit local apic address:%#lX\n",((apic_address_override_entry_t *)madt_entry)->apic_address);
                break;

            case 9://X2APIC ID
                color_printk(RED,BLACK,"X2APIC ID:%d\n",((x2apic_entry_t*)madt_entry)->x2apic_id);
                break;

            case 10://X2APIC不可屏蔽中断
                color_printk(RED,BLACK,"X2APIC NMI X2ApicID:%#lX LINT:%d\n",
                             ((x2apic_nmi_entry_t *) madt_entry)->x2apic_id,
                             ((x2apic_nmi_entry_t *) madt_entry)->lint);
                break;

            case 13://多处理器唤醒
                color_printk(RED,BLACK,"Multiprocessor Wakeup Address:%#lX\n",((multiprocessor_wakeup_entry_t *)madt_entry)->mailbox_address);
                break;
        }
        madt_entry=(madt_header_t *)((UINT64)madt_entry+madt_entry->length);
    }
    //endregion

    //hpet初始化
    hpet.address = (UINT64) LADDR_TO_HADDR(hpett->acpi_generic_adderss.address);
    color_printk(RED,BLACK,"HPET MiniMumTick:%d Number:%d SpaceID:%d BitWidth:%d BiteOffset:%d AccessSize:%d Address:%#lX\n",hpett->minimum_tick,hpett->hpet_number,hpett->acpi_generic_adderss.space_id,hpett->acpi_generic_adderss.bit_width,hpett->acpi_generic_adderss.bit_offset,hpett->acpi_generic_adderss.access_size,hpett->acpi_generic_adderss.address);

    //mcfg初始化
    mcfg_entry_t *mcfg_entry=(mcfg_entry_t *)&mcfg->entry;
    for(UINT32 j=0;j<((mcfg->acpi_header.length-sizeof(acpi_header_t)-sizeof(UINT64))/sizeof(mcfg_entry_t));j++){
        color_printk(RED,BLACK,"PCIE BaseAddr:%#lX Segment:%d StartBus:%d EndBus:%d\n",mcfg_entry[j].base_address,mcfg_entry[j].pci_segment,mcfg_entry[j].start_bus,mcfg_entry[j].end_bus);
    }

    return;
}

__attribute__((section(".init_text"))) UINT32 apicid_to_cpuid(UINT32 apic_id){
    for(UINT32 i=0;i<cpu_info.logical_processors_number;i++){
        if(apic_id==apic_id_table[i])
            return i;
    }
    return 0xFFFFFFFF;
}

__attribute__((section(".init_text"))) UINT32 cpuid_to_apicid(UINT32 cpu_id){
    return apic_id_table[cpu_id];
}

