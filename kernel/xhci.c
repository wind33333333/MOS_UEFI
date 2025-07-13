#include "xhci.h"
#include "pcie.h"
#include "printk.h"
#include "slub.h"
#include "vmalloc.h"

INIT_TEXT void init_xhci(void) {
    pcie_dev_t *xhci_dev = pcie_find(0x0C0330);
    xhci_dev->msi_x_table = get_msi_x_table(xhci_dev->pcie_config_space);
}