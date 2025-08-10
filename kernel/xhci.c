#include "xhci.h"
#include "moslib.h"
#include "apic.h"
#include "pcie.h"
#include "printk.h"
#include "slub.h"
#include "vmalloc.h"
#include "vmm.h"

#define TRB_COUNT 256        //trb个数

#define TRB_TYPE_LINK        (0x06<<10) //连接trb
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
    pcie_dev_t *xhci_dev = find_pcie_dev(XHCI_CLASS_CODE);      //找xhci设备
    init_pcie_bar(xhci_dev,0);                                         //初始化bar0寄存器
    init_pcie_msi_intrpt(xhci_dev);                                       //初始化msi中断

    xhci_dev->private = kmalloc(sizeof(xhci_regs_t));                 //设备似有数据空间申请一块内存
    xhci_regs_t *xhci_regs = xhci_dev->private;
    xhci_regs->cap = xhci_dev->bar[0];                                  //xhci能力寄存器基地址
    xhci_regs->op = xhci_dev->bar[0] + xhci_regs->cap->cap_length;      //xhci操作寄存器基地址
    xhci_regs->rt = xhci_dev->bar[0] + xhci_regs->cap->rtsoff;          //xhci运行时寄存器基地址
    xhci_regs->db = xhci_dev->bar[0] + xhci_regs->cap->dboff;           //xhci门铃寄存器基地址

    xhci_regs->crcr = kzalloc(TRB_COUNT*sizeof(xhci_trb_t));                 //分配命令环寄存器内存 4K
    xhci_regs->crcr[TRB_COUNT-1].parameter1 = va_to_pa(xhci_regs->crcr);
    xhci_regs->crcr[TRB_COUNT-1].control = TRB_TYPE_LINK | TRB_TOGGLE_CYCLE;     //命令环最后一个trb设置位link

    UINT32 max_slots = xhci_regs->cap->hcsparams1&0xff;
    xhci_regs->dcbaap32 = kzalloc(max_slots<<3);     //分配设备上下文插槽内存,最大插槽数量*8字节内存
    for (UINT32 i = 0; i < max_slots; i++) {             //为每个插槽分配设备上下文内存
        xhci_regs->dcbaap32[i] = va_to_pa(kzalloc(sizeof(xhci_device_context32_t)));
    }

    xhci_regs->erstba = kmalloc(sizeof(xhci_erst_t)); //单事件环段表
    xhci_regs->erdp = kzalloc(TRB_COUNT*16);  //分配事件环空间256*16
    xhci_regs->erstba[0].ring_seg_base_addr = va_to_pa(xhci_regs->erdp);
    xhci_regs->erstba[0].ring_seg_size = TRB_COUNT;
    xhci_regs->erstba[0].reserved = 0;

    xhci_regs->op->usbcmd &= ~1;  //停止xhci
    while (!(xhci_regs->op->usbsts & 1)) pause();
    xhci_regs->op->usbcmd |= 2;  //复位xhci
    while (xhci_regs->op->usbcmd & 2) pause();
    while (xhci_regs->op->usbcmd & 0x800) pause();

    xhci_regs->op->config = xhci_regs->cap->hcsparams1&0xFF;
    xhci_regs->op->dcbaap = va_to_pa(xhci_regs->dcbaap32);
    xhci_regs->op->crcr = va_to_pa(xhci_regs->crcr)|1;
    xhci_regs->rt->intr_regs[0].erstsz = 1;    //设置单事件环段
    xhci_regs->rt->intr_regs[0].erstba = va_to_pa(xhci_regs->erstba);
    xhci_regs->rt->intr_regs[0].erdp = va_to_pa(xhci_regs->erdp);
    xhci_regs->op->usbcmd |= 1; //启动xhci

    color_printk(GREEN,BLACK,"Xhci Version:%x BAR0 MMIO:%#lx MSI-X:%d MaxSlots:%d MaxIntrs:%d MaxPorts:%d CS:%d AC64:%d USBcmd:%#x USBsts:%#x PageSize:%d iman:%#x imod:%#x\n",xhci_regs->cap->hciversion,(UINT64)xhci_dev->pcie_config_space->type0.bar[0]&~0x1f|(UINT64)xhci_dev->pcie_config_space->type0.bar[1]<<32,xhci_dev->msi_x_flags,xhci_regs->cap->hcsparams1&0xFF,xhci_regs->cap->hcsparams1>>8&0x7FF,xhci_regs->cap->hcsparams1>>24,xhci_regs->cap->hccparams1>>2&1,xhci_regs->cap->hccparams1&1,xhci_regs->op->usbcmd,xhci_regs->op->usbsts,xhci_regs->op->pagesize<<12,xhci_regs->runtime->intr_regs[0].iman,xhci_regs->runtime->intr_regs[0].imod);
    color_printk(GREEN,BLACK,"crcr:%#lx dcbaap:%#lx erstba[0]:%#lx erdp[0]:%#lx erstsz:%d config:%d \n",xhci_regs->op->crcr,xhci_regs->op->dcbaap,xhci_regs->runtime->intr_regs[0].erstba,xhci_regs->runtime->intr_regs[0].erdp,xhci_regs->runtime->intr_regs[0].erstsz,xhci_regs->op->config);

    while (1);

}