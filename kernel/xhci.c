#include "xhci.h"

#include "apic.h"
#include "pcie.h"
#include "printk.h"
#include "slub.h"
#include "vmalloc.h"

INIT_TEXT void init_xhci(void) {
    pcie_dev_t *xhci_dev = find_pcie_dev(XHCI_CLASS_CODE);
    disable_msi_x(xhci_dev);
    xhci_dev->bar[0] = set_bar(xhci_dev,0);
    xhci_dev->msi_x_table = get_msi_x_table(xhci_dev);
    UINT64 msg_addr = rdmsr(IA32_APIC_BASE_MSR) & ~0xFFFUL;
    xhci_dev->msi_x_table[0].msg_addr_lo = msg_addr;
    xhci_dev->msi_x_table[0].msg_addr_hi = msg_addr >> 32;
    xhci_dev->msi_x_table[0].msg_data = 0x40;
    xhci_dev->msi_x_table[0].vector_control = 0;

    xhci_regs_t *xhci_regs;
    xhci_regs->cap = xhci_dev->bar[0];
    xhci_regs->op = xhci_dev->bar[0] + xhci_regs->cap->cap_length;
    xhci_regs->runtime = xhci_dev->bar[0] + xhci_regs->cap->rtsoff;
    xhci_regs->doorbells = xhci_dev->bar[0] + xhci_regs->cap->dboff;

    color_printk(GREEN,BLACK,"hccparams1:%#lx",xhci_regs->cap->hccparams1);
    while (1);

}