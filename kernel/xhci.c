#include "xhci.h"
#include "moslib.h"
#include "apic.h"
#include "pcie.h"
#include "printk.h"
#include "slub.h"
#include "vmalloc.h"
#include "vmm.h"

#define trbs_count 256
#define TRB_TYPE_LINK        (0x06<<10)
#define TRB_CYCLE            (1 << 0)
#define TRB_TOGGLE_CYCLE     (1 << 1)
#define TRB_CHAIN            (1 << 9)

xhci_cap_t *find_xhci_cap(xhci_regs_t *xhci_reg,UINT8 cap_id) {
    UINT32 offset = xhci_reg->cap->hccparams1>>16;
    while (offset) {
        xhci_cap_t *xhci_cap = (void*)xhci_reg->cap + (offset<<2);
        if ((xhci_cap->cap_id&0xFF) == cap_id) return xhci_cap;
        offset = (xhci_cap->next_ptr>>8)&0xFF;
    }
    return NULL;
}

INIT_TEXT void init_xhci(void) {
    pcie_dev_t *xhci_dev = find_pcie_dev(XHCI_CLASS_CODE);
    init_pcie_bar(xhci_dev,0);
    init_pcie_msi_intrpt(xhci_dev);

    xhci_dev->private_data = kmalloc(sizeof(xhci_regs_t));
    xhci_regs_t *xhci_regs = xhci_dev->private_data;
    xhci_regs->cap = xhci_dev->bar[0];                          //xhci能力寄存器基地址
    xhci_regs->op = xhci_dev->bar[0] + xhci_regs->cap->cap_length;      //xhci操作寄存器基地址
    xhci_regs->runtime = xhci_dev->bar[0] + xhci_regs->cap->rtsoff;     //xhci运行时寄存器基地址
    xhci_regs->doorbells = xhci_dev->bar[0] + xhci_regs->cap->dboff;    //xhci门铃寄存器基地址

    xhci_regs->crcr_ptr = kzalloc(trbs_count*16);          //分配命令环寄存器内存
    xhci_regs->dcbaap_ptr32 = kzalloc((xhci_regs->cap->hcsparams1&0xff)<<3); //分配设备上下文寄存器最大插槽数量*8字节内存
    for (UINT32 i = 0; i < (xhci_regs->cap->hcsparams1&0xff); i++) {
        xhci_regs->dcbaap_ptr32[i] = va_to_pa(kzalloc(sizeof(xhci_device_context_32_t)));
    }

    xhci_regs->erstba_ptr = kmalloc((1<<(xhci_regs->cap->hccparams2>>4&0xf)) * sizeof(xhci_erst_entry) + 64); //分配事件环段数量*事件环段结构内存，对齐64字节
    xhci_regs->erdp_ptr = kzalloc(trbs_count*16);  //分配事件环空间256*16

    xhci_regs->erstba_ptr[0].ring_seg_base_addr = va_to_pa(xhci_regs->erdp_ptr);
    xhci_regs->erstba_ptr[0].ring_seg_size = trbs_count;
    xhci_regs->erstba_ptr[0].reserved = 0;

    xhci_regs->crcr_ptr[3].parameter1 = va_to_pa(xhci_regs->crcr_ptr);
    xhci_regs->crcr_ptr[3].control = TRB_TYPE_LINK | TRB_TOGGLE_CYCLE;

    xhci_regs->op->usbcmd &= ~1;  //停止xhci
    while (!(xhci_regs->op->usbsts & 1)) pause();
    xhci_regs->op->usbcmd |= 2;  //复位xhci
    while (xhci_regs->op->usbcmd & 2) pause();
    while (xhci_regs->op->usbcmd & 0x800) pause();

    xhci_regs->op->config = xhci_regs->cap->hcsparams1&0xFF;
    xhci_regs->op->dcbaap = va_to_pa(xhci_regs->dcbaap_ptr);
    xhci_regs->op->crcr = va_to_pa(xhci_regs->crcr_ptr)|1;
    xhci_regs->runtime->intr_regs[0].erstsz = 1<<(xhci_regs->cap->hccparams2>>4&0xf);
    xhci_regs->runtime->intr_regs[0].erstba = va_to_pa(xhci_regs->erstba_ptr);
    xhci_regs->runtime->intr_regs[0].erdp = va_to_pa(xhci_regs->erdp_ptr);
    xhci_regs->op->usbcmd |= 1; //启动xhci

    // 开中断
    xhci_regs->runtime->intr_regs[0].iman |= 1;

    xhci_regs->crcr_ptr[0].control = 23<<10 | TRB_CYCLE;
    xhci_regs->crcr_ptr[1].control = 23<<10 | TRB_CYCLE;
    xhci_regs->crcr_ptr[2].control = 23<<10 | TRB_CYCLE;
    xhci_regs->crcr_ptr[3].control |= TRB_CYCLE;
    xhci_regs->doorbells->doorbell[0] = 0;


    color_printk(GREEN,BLACK,"Xhci Version:%x BAR0 MMIO:%#lx MSI-X:%d MaxSlots:%d MaxIntrs:%d MaxPorts:%d CS:%d AC64:%d USBcmd:%#x USBsts:%#x PageSize:%d iman:%#x imod:%#x\n",xhci_regs->cap->hciversion,(UINT64)xhci_dev->pcie_config_space->type0.bar[0]&~0x1f|(UINT64)xhci_dev->pcie_config_space->type0.bar[1]<<32,xhci_dev->msi_x_flags,xhci_regs->cap->hcsparams1&0xFF,xhci_regs->cap->hcsparams1>>8&0x7FF,xhci_regs->cap->hcsparams1>>24,xhci_regs->cap->hccparams1>>2&1,xhci_regs->cap->hccparams1&1,xhci_regs->op->usbcmd,xhci_regs->op->usbsts,xhci_regs->op->pagesize<<12,xhci_regs->runtime->intr_regs[0].iman,xhci_regs->runtime->intr_regs[0].imod);
    color_printk(GREEN,BLACK,"crcr:%#lx dcbaap:%#lx erstba[0]:%#lx erdp[0]:%#lx erstsz:%d config:%d \n",xhci_regs->op->crcr,xhci_regs->op->dcbaap,xhci_regs->runtime->intr_regs[0].erstba,xhci_regs->runtime->intr_regs[0].erdp,xhci_regs->runtime->intr_regs[0].erstsz,xhci_regs->op->config);

    while (1);

}