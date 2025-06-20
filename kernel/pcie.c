#include "pcie.h"
#include "acpi.h"

void pcie_scan(UINT64 ecam_base, UINT8 bus) {
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
                pcie_scan(ecam_base, pcie_config_space->header.type1.secondary_bus);
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

void init_pcie(void) {
    //mcfg初始化
    mcfg_entry_t *mcfg_entry = &mcfg->entry;
    UINT32 mcfg_count = (mcfg->acpi_header.length - sizeof(acpi_header_t) - sizeof(mcfg->reserved)) / sizeof(
                            mcfg_entry_t);
    for (UINT32 j = 0; j < mcfg_count; j++) {
        color_printk(GREEN,BLACK, "PCIE BaseAddr:%#lX Segment:%d StartBus:%d EndBus:%d\n",
                     mcfg_entry[j].base_address,
                     mcfg_entry[j].pci_segment, mcfg_entry[j].start_bus, mcfg_entry[j].end_bus);
    }
}