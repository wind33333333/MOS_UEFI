#include "xhci.h"

#include "apic.h"
#include "pcie.h"
#include "printk.h"
#include "slub.h"
#include "vmalloc.h"

INIT_TEXT void init_xhci(void) {
    pcie_dev_t *xhci_dev = find_pcie_dev(XHCI_CLASS_CODE);
    xhci_dev->bar[0] = init_pcie_dev_bar(xhci_dev,0);
    UINT64 msg_addr = rdmsr(IA32_APIC_BASE_MSR) & ~0xFFFUL;
    if (find_pcie_cap(xhci_dev,msi_x_e)) {
        color_printk(GREEN,BLACK,"XHCI MSI-X Support!\n");
        xhci_dev->msi_x.control = get_msi_x_control(xhci_dev);
        xhci_dev->msi_x.irq_table = get_msi_x_table(xhci_dev);
        xhci_dev->msi_x.pba_table = get_pda_table(xhci_dev);
        disable_msi_x(xhci_dev);
        xhci_dev->msi_x.irq_table[0].msg_addr_lo = (UINT32)msg_addr;
        xhci_dev->msi_x.irq_table[0].msg_addr_hi = (UINT32)(msg_addr >> 32);
        xhci_dev->msi_x.irq_table[0].msg_data = 0x40;
        xhci_dev->msi_x.irq_table[0].vector_control = 0;
    }else {
        color_printk(RED,BLACK,"XHCI MSI-X Not Support!\n");
    }
    if (find_pcie_cap(xhci_dev,msi_e)) {
        color_printk(GREEN,BLACK,"XHCI MSI Support!\n");
        xhci_dev->msi.control = get_msi_control(xhci_dev);
        xhci_dev->msi.addr_l = get_msi_addrl(xhci_dev);
        xhci_dev->msi.addr_h = get_msi_addrh(xhci_dev);
        xhci_dev->msi.data = get_msi_data(xhci_dev);
        disable_msi(xhci_dev);
        xhci_dev->msi.addr_l = (UINT32)msg_addr;
        xhci_dev->msi.addr_h = (UINT32)(msg_addr >> 32);
        xhci_dev->msi.data = 0x40;
        color_printk(GREEN,BLACK,"msg_addr:%#lx msg_addr_lo:%#x msg_addr_hi:%#x msg_data:%#x\n",msg_addr,xhci_dev->msi.addr_l,xhci_dev->msi.addr_h,xhci_dev->msi.data);
    }else {
        color_printk(RED,BLACK,"XHCI MSI Not Support!\n");
    }


    xhci_regs_t xhci_regs;
    xhci_regs.cap = xhci_dev->bar[0];
    xhci_regs.op = xhci_dev->bar[0] + xhci_regs.cap->cap_length;
    xhci_regs.runtime = xhci_dev->bar[0] + xhci_regs.cap->rtsoff;
    xhci_regs.doorbells = xhci_dev->bar[0] + xhci_regs.cap->dboff;

    color_printk(GREEN,BLACK,"Xhci Version:%x MaxSlots:%d MaxIntrs:%d MaxPorts:%d ContextSize:%d USBcmd:%#x USBsts:%#x PageSize:%d\n",xhci_regs.cap->xhci_version,xhci_regs.cap->hcsparams1&0xFF,xhci_regs.cap->hcsparams1>>8&0xFF,xhci_regs.cap->hcsparams1>>24,xhci_regs.cap->hccparams1>>2&1,xhci_regs.op->usbcmd,xhci_regs.op->usbsts,xhci_regs.op->pagesize<<12);
    while (1);

}