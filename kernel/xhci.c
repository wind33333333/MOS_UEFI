#include "xhci.h"
#include "pcie.h"
#include "printk.h"
#include "slub.h"
#include "vmalloc.h"

INIT_TEXT void init_xhci(void) {
    pcie_dev_t *xhci_dev = find_pcie_dev(0x0C0330);
    disable_msi_x(xhci_dev->pcie_config_space);
    xhci_dev->bar[0] = set_bar(xhci_dev->pcie_config_space,0);
    xhci_dev->msi_x_table = get_msi_x_table(xhci_dev);

    xhci_cap_regs_t *xhci_cap = xhci_dev->bar[0];
}