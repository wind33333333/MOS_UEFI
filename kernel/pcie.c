#include "pcie.h"
#include "acpi.h"
#include "printk.h"

void pcie_scan(UINT64 ecam_base, UINT8 bus) {
    for (UINT8 dev = 0; dev < 32; dev++) {
        for (UINT8 func = 0; func < 8; func++) {
            pcie_config_space_t *pcie_config_space = (pcie_config_space_t *) (
                ecam_base + (bus << 20) + (dev << 15) + (func << 12));
            if (pcie_config_space->vendor_id == 0xFFFF && func == 0) break;
            if (pcie_config_space->vendor_id == 0xFFFF) continue;
            if (pcie_config_space->header_type & 1) {
                //type1 pcie桥
                UINT32 *class_code = &pcie_config_space->class_code;
                color_printk(
                    GREEN,BLACK, "bus:%d dev:%d func:%d vendor_id:%#lx device_id:%#lx class_code:%#lx\n", bus,
                    dev, func, pcie_config_space->vendor_id, pcie_config_space->device_id,*class_code & 0xFFFFFF);
                pcie_scan(ecam_base, pcie_config_space->type1.secondary_bus);
            } else {
                //type0 终端设备
                UINT32 *class_code = &pcie_config_space->class_code;
                color_printk(
                    GREEN,BLACK, "bus:%d dev:%d func:%d vendor_id:%#lx device_id:%#lx class_code:%#lx\n", bus,
                    dev, func, pcie_config_space->vendor_id, pcie_config_space->device_id,*class_code & 0xFFFFFF);
                if ((pcie_config_space->header_type & 0x80) == 0) break;
            }
        }
    }
}

INIT_TEXT void init_pcie(void) {
    //mcfg初始化
    mcfg_entry_t *mcfg_entry = &mcfg->entry;
    UINT32 mcfg_count = (mcfg->acpi_header.length - sizeof(acpi_header_t) - sizeof(mcfg->reserved)) / sizeof(
                            mcfg_entry_t);
    for (UINT32 i = 0; i < mcfg_count; i++) {
        color_printk(GREEN,BLACK, "ECAM base:%#lX Segment:%d StartBus:%d EndBus:%d\n",
                     mcfg_entry[i].base_address,mcfg_entry[i].pci_segment, mcfg_entry[i].start_bus, mcfg_entry[i].end_bus);
        pcie_scan(mcfg_entry[i].base_address,0);
    }

}