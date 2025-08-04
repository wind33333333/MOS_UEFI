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
    color_printk(GREEN,BLACK,"Xhci Version:%x MaxSlots:%d MaxIntrs:%d MaxPorts:%d ContextSize:%d USBcmd:%#x USBsts:%#x PageSize:%d MSI-X:%d\n",xhci_regs->cap->hciversion,xhci_regs->cap->hcsparams1&0xFF,xhci_regs->cap->hcsparams1>>8&0x7FF,xhci_regs->cap->hcsparams1>>24,xhci_regs->cap->hccparams1>>2&1,xhci_regs->op->usbcmd,xhci_regs->op->usbsts,xhci_regs->op->pagesize<<12,xhci_dev->msi_x_flags);

    xhci_regs->op->usbcmd &= ~1;
    color_printk(GREEN,BLACK,"Xhci Version:%x MaxSlots:%d MaxIntrs:%d MaxPorts:%d ContextSize:%d USBcmd:%#x USBsts:%#x PageSize:%d MSI-X:%d\n",xhci_regs->cap->hciversion,xhci_regs->cap->hcsparams1&0xFF,xhci_regs->cap->hcsparams1>>8&0x7FF,xhci_regs->cap->hcsparams1>>24,xhci_regs->cap->hccparams1>>2&1,xhci_regs->op->usbcmd,xhci_regs->op->usbsts,xhci_regs->op->pagesize<<12,xhci_dev->msi_x_flags);
    color_printk(GREEN,BLACK,"crcr:%#lx dcbaap:%#lx portsc[0]:%#x portpmsc[0]:%#x portli[0]:%#x porthlpmc[0]:%#x erstba[0]:%#lx erdp[0]:%#lx\n",xhci_regs->op->crcr,xhci_regs->op->dcbaap,xhci_regs->op->portregs[0].portsc,xhci_regs->op->portregs[0].portpmsc,xhci_regs->op->portregs[0].portli,xhci_regs->op->portregs[0].porthlpmc,xhci_regs->runtime->intr_regs[0].erstba,xhci_regs->runtime->intr_regs[0].erdp);
    xhci_regs->op->crcr = 0x100001UL;
    xhci_regs->op->dcbaap = 0xBBBBBAAAAAAA0000UL;
    // xhci_regs->op->portregs[0].portsc = 0xAAAAAAAAUL;
    // xhci_regs->op->portregs[0].portpmsc = 0xAAAAAAAAUL;
    // xhci_regs->op->portregs[0].portli = 0xAAAAAAAAUL;
    // xhci_regs->op->portregs[0].porthlpmc = 0xAAAAAAAAUL;
    xhci_regs->runtime->intr_regs[0].erstba = 0xAAAAAAAAAAAA0000UL;
    xhci_regs->runtime->intr_regs[0].erdp = 0xAAAAAAAAAAAA0000UL;
    color_printk(GREEN,BLACK,"crcr:%#lx dcbaap:%#lx portsc[0]:%#x portpmsc[0]:%#x portli[0]:%#x porthlpmc[0]:%#x erstba[0]:%#lx erdp[0]:%#lx\n",xhci_regs->op->crcr,xhci_regs->op->dcbaap,xhci_regs->op->portregs[0].portsc,xhci_regs->op->portregs[0].portpmsc,xhci_regs->op->portregs[0].portli,xhci_regs->op->portregs[0].porthlpmc,xhci_regs->runtime->intr_regs[0].erstba,xhci_regs->runtime->intr_regs[0].erdp);

    while (1);

}