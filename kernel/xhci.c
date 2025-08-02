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
    xhci_dev->msi_x_control = get_msi_x_control(xhci_dev);
    xhci_dev->msi_x_table = get_msi_x_table(xhci_dev);
    xhci_dev->msi_x_pba_offset = get_pda_table(xhci_dev);
    UINT64 msg_addr = rdmsr(IA32_APIC_BASE_MSR) & ~0xFFFUL;
    xhci_dev->msi_x_table[0].msg_addr_lo = (UINT32)msg_addr;
    xhci_dev->msi_x_table[0].msg_addr_hi = (UINT32)(msg_addr >> 32);
    xhci_dev->msi_x_table[0].msg_data = 0x40;
    xhci_dev->msi_x_table[0].vector_control = 0;
    color_printk(GREEN,BLACK,"msg_addr_lo:%#x msg_addr_hi%#x\n",xhci_dev->msi_x_table[0].msg_addr_lo,xhci_dev->msi_x_table[0].msg_addr_hi);

    xhci_regs_t xhci_regs;
    xhci_regs.cap = xhci_dev->bar[0];
    xhci_regs.op = xhci_dev->bar[0] + xhci_regs.cap->cap_length;
    xhci_regs.runtime = xhci_dev->bar[0] + xhci_regs.cap->rtsoff;
    xhci_regs.doorbells = xhci_dev->bar[0] + xhci_regs.cap->dboff;

    color_printk(GREEN,BLACK,"Xhci Version:%x MaxSlots:%d MaxIntrs:%d MaxPorts:%d ContextSize:%d USBcmd:%#x USBsts:%#x PageSize:%d\n",xhci_regs.cap->xhci_version,xhci_regs.cap->hcsparams1&0xFF,xhci_regs.cap->hcsparams1>>8&0xFF,xhci_regs.cap->hcsparams1>>24,xhci_regs.cap->hccparams1>>2&1,xhci_regs.op->usbcmd,xhci_regs.op->usbsts,xhci_regs.op->pagesize<<12);
    while (1);

}