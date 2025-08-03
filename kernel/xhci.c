#include "xhci.h"

#include "apic.h"
#include "pcie.h"
#include "printk.h"
#include "slub.h"
#include "vmalloc.h"

INIT_TEXT void init_xhci(void) {
    pcie_dev_t *xhci_dev = find_pcie_dev(XHCI_CLASS_CODE);
    init_pcie_bar(xhci_dev,0);
    init_pcie_msi_intrpt(xhci_dev);

    xhci_dev->private_data = kmalloc(sizeof(xhci_regs_t));
    xhci_regs_t *xhci_regs = xhci_dev->private_data;
    xhci_regs->cap = xhci_dev->bar[0];
    xhci_regs->op = xhci_dev->bar[0] + xhci_regs->cap->cap_length;
    xhci_regs->runtime = xhci_dev->bar[0] + xhci_regs->cap->rtsoff;
    xhci_regs->doorbells = xhci_dev->bar[0] + xhci_regs->cap->dboff;

    color_printk(GREEN,BLACK,"Xhci Version:%x MaxSlots:%d MaxIntrs:%d MaxPorts:%d ContextSize:%d USBcmd:%#x USBsts:%#x PageSize:%d MSI-X:%d\n",xhci_regs->cap->xhci_version,xhci_regs->cap->hcsparams1&0xFF,xhci_regs->cap->hcsparams1>>8&0xFF,xhci_regs->cap->hcsparams1>>24,xhci_regs->cap->hccparams1>>2&1,xhci_regs->op->usbcmd,xhci_regs->op->usbsts,xhci_regs->op->pagesize<<12,xhci_dev->msi_x_flags);
    color_printk(GREEN,BLACK,"MsgAdd:%#x%x MsgData:%#x\n",xhci_dev->msi.msg_addr_hi,xhci_dev->msi.msg_addr_lo,xhci_dev->msi.msg_data);
    while (1);

}