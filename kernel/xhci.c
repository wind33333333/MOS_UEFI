#include "xhci.h"

#include "apic.h"
#include "pcie.h"
#include "printk.h"
#include "slub.h"
#include "vmalloc.h"

INIT_TEXT void init_xhci(void) {
    pcie_dev_t *xhci_dev = find_pcie_dev(0x0C0330);
    disable_msi_x(xhci_dev->pcie_config_space);
    xhci_dev->bar[0] = set_bar(xhci_dev->pcie_config_space,0);
    xhci_dev->msi_x_table = get_msi_x_table(xhci_dev);
    UINT64 msg_addr = rdmsr(IA32_APIC_BASE_MSR) & ~0xFFFUL;
    xhci_dev->msi_x_table[0].msg_addr_lo = msg_addr;
    xhci_dev->msi_x_table[0].msg_addr_hi = msg_addr >> 32;
    xhci_dev->msi_x_table[0].msg_data = 0x40;
    xhci_dev->msi_x_table[0].vector_control = 0;


}