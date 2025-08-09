#include "xhci.h"
#include "moslib.h"
#include "apic.h"
#include "pcie.h"
#include "printk.h"
#include "slub.h"
#include "vmalloc.h"
#include "vmm.h"

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

    xhci_regs->crcr_ptr = kzalloc(PAGE_4K_SIZE);
    xhci_regs->dcbaap_ptr = kzalloc((xhci_regs->cap->hcsparams1&0xff)<<3);
    xhci_regs->erstba_ptr = kmalloc((1<<(xhci_regs->cap->hccparams2>>4&0xf)) * sizeof(xhci_erst_entry) + 64);
    xhci_regs->erdp_ptr = kzalloc(256*16);
    xhci_regs->erstba_ptr[0].ring_seg_base_addr = va_to_pa(xhci_regs->erdp_ptr);
    xhci_regs->erstba_ptr[0].ring_seg_size = 256;
    xhci_regs->erstba_ptr[0].reserved = 0;


    xhci_regs->op->usbcmd &= ~1;
    while (!(xhci_regs->op->usbsts & 1)) pause();
    xhci_regs->op->usbcmd |= 2;
    while (xhci_regs->op->usbcmd & 2) pause();
    while (xhci_regs->op->usbcmd & 0x800) pause();

    xhci_regs->op->config = xhci_regs->cap->hcsparams1&0xFF;
    xhci_regs->op->dcbaap = va_to_pa(xhci_regs->dcbaap_ptr);
    xhci_regs->op->crcr = va_to_pa(xhci_regs->crcr_ptr)|1;
    xhci_regs->runtime->intr_regs[0].erstsz = 1<<(xhci_regs->cap->hccparams2>>4&0xf);
    xhci_regs->runtime->intr_regs[0].erstba = va_to_pa(xhci_regs->erstba_ptr);
    xhci_regs->runtime->intr_regs[0].erdp = va_to_pa(xhci_regs->erdp_ptr);
    xhci_regs->op->usbcmd |= 1;
    color_printk(GREEN,BLACK,"Xhci Version:%x BAR0 MMIO:%#lx MSI-X:%d MaxSlots:%d MaxIntrs:%d MaxPorts:%d CS:%d AC64:%d USBcmd:%#x USBsts:%#x PageSize:%d\n",xhci_regs->cap->hciversion,(UINT64)xhci_dev->pcie_config_space->type0.bar[0]&~0x1f|(UINT64)xhci_dev->pcie_config_space->type0.bar[1]<<32,xhci_dev->msi_x_flags,xhci_regs->cap->hcsparams1&0xFF,xhci_regs->cap->hcsparams1>>8&0x7FF,xhci_regs->cap->hcsparams1>>24,xhci_regs->cap->hccparams1>>2&1,xhci_regs->cap->hccparams1&1,xhci_regs->op->usbcmd,xhci_regs->op->usbsts,xhci_regs->op->pagesize<<12);
    color_printk(GREEN,BLACK,"crcr:%#lx dcbaap:%#lx portsc[0]:%#x portpmsc[0]:%#x portli[0]:%#x porthlpmc[0]:%#x erstba[0]:%#lx erdp[0]:%#lx\n",xhci_regs->op->crcr,xhci_regs->op->dcbaap,xhci_regs->op->portregs[0].portsc,xhci_regs->op->portregs[0].portpmsc,xhci_regs->op->portregs[0].portli,xhci_regs->op->portregs[0].porthlpmc,xhci_regs->runtime->intr_regs[0].erstba,xhci_regs->runtime->intr_regs[0].erdp);

    while (1);

}