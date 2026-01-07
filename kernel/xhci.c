#include "xhci.h"
#include "printk.h"
#include "pcie.h"
#include "slub.h"
#include "vmm.h"
#include "driver.h"
#include "vmalloc.h"
#include "usb.h"

////////////////////////////////////////////////////////////


//设置设备地址
void xhci_address_device(usb_dev_t *usb_dev) {
    xhci_controller_t *xhci_controller = usb_dev->dev.parent->drv_data;
    //分配设备插槽上下文内存
    usb_dev->dev_context = kzalloc(align_up(sizeof(xhci_device_context_t), xhci_controller->align_size));
    xhci_controller->dcbaap[usb_dev->slot_id] = va_to_pa(usb_dev->dev_context);
    //初始化控制
    xhci_ring_init(&usb_dev->control_ring, xhci_controller->align_size);
    //配置设备上下文
    xhci_input_context_t *input_ctx = kzalloc(align_up(sizeof(xhci_input_context_t), xhci_controller->align_size));
    xhci_slot_context_t slot_ctx;
    slot_ctx.reg0 = 1 << 27 | (xhci_controller->op_reg->portregs[usb_dev->port_id - 1].portsc & 0x3C00) << 10;
    slot_ctx.reg1 = usb_dev->port_id << 16;
    slot_ctx.reg2 = 0;
    slot_ctx.reg3 = 0;
    xhci_input_slot_context_add(input_ctx, xhci_controller->context_size, &slot_ctx); // 启用 Slot Context

    xhci_endpoint_context_t ep_ctx;
    ep_ctx.reg0 = 0;
    ep_ctx.reg1 = EP_TYPE_CONTROL | 8 << 16 | 3 << 1;
    ep_ctx.reg2 = va_to_pa(usb_dev->control_ring.ring_base) | 1;
    ep_ctx.reg3 = 0;
    xhci_input_endpoint_context_add(input_ctx, xhci_controller->context_size, 1, &ep_ctx); //Endpoint 0 Context

    trb_t trb;
    addr_dev_com_trb(&trb, va_to_pa(input_ctx), usb_dev->slot_id);
    xhci_ring_enqueue(&xhci_controller->cmd_ring, &trb);
    xhci_ring_doorbell(xhci_controller, 0, 0);
    timing();
    xhci_ering_dequeue(xhci_controller, &trb);
    kfree(input_ctx);
}




///////////////////////////////////////////////////////////

xhci_cap_t *xhci_cap_find(xhci_controller_t *xhci_reg, uint8 cap_id) {
    uint32 offset = xhci_reg->cap_reg->hccparams1 >> 16;
    while (offset) {
        xhci_cap_t *xhci_cap = (void *) xhci_reg->cap_reg + (offset << 2);
        if ((xhci_cap->cap_id & 0xFF) == cap_id) return xhci_cap;
        offset = (xhci_cap->next_ptr >> 8) & 0xFF;
    }
    return NULL;
}

//xhci设备初始化驱动
int xhci_probe(pcie_dev_t *xhci_dev,pcie_id_t* id) {
    xhci_dev->dev.drv_data = kzalloc(sizeof(xhci_controller_t)); //存放xhci相关信息
    xhci_controller_t *xhci_controller = xhci_dev->dev.drv_data;
    xhci_dev->bar[0].vaddr = iomap(xhci_dev->bar[0].paddr,xhci_dev->bar[0].size,PAGE_4K_SIZE,PAGE_ROOT_RW_UC_4K);

    /*初始化xhci寄存器*/
    xhci_controller->cap_reg = xhci_dev->bar[0].vaddr; //xhci能力寄存器基地址
    xhci_controller->op_reg = xhci_dev->bar[0].vaddr + xhci_controller->cap_reg->cap_length; //xhci操作寄存器基地址
    xhci_controller->rt_reg = xhci_dev->bar[0].vaddr + xhci_controller->cap_reg->rtsoff; //xhci运行时寄存器基地址
    xhci_controller->db_reg = xhci_dev->bar[0].vaddr + xhci_controller->cap_reg->dboff; //xhci门铃寄存器基地址

    /*停止复位xhci*/
    xhci_controller->op_reg->usbcmd &= ~XHCI_CMD_RS; //停止xhci
    while (!(xhci_controller->op_reg->usbsts & XHCI_STS_HCH)) pause();
    xhci_controller->op_reg->usbcmd |= XHCI_CMD_HCRST; //复位xhci
    while (xhci_controller->op_reg->usbcmd & XHCI_CMD_HCRST) pause();
    while (xhci_controller->op_reg->usbsts & XHCI_STS_CNR) pause();

    /*计算xhci内存对齐边界*/
    xhci_controller->align_size = PAGE_4K_SIZE << bsf(xhci_controller->op_reg->pagesize);

    /*设备上下文字节数*/
    xhci_controller->context_size = 32 << ((xhci_controller->cap_reg->hccparams1 & HCCP1_CSZ) >> 2);

    /*初始化设备上下文*/
    uint32 max_slots = xhci_controller->cap_reg->hcsparams1 & 0xff;
    xhci_controller->dcbaap = kzalloc(align_up((max_slots + 1) << 3, xhci_controller->align_size));
    //分配设备上下文插槽内存,最大插槽数量(插槽从1开始需要+1)*8字节内存
    xhci_controller->op_reg->dcbaap = va_to_pa(xhci_controller->dcbaap); //把设备上下文基地址数组表的物理地址写入寄存器
    xhci_controller->op_reg->config = max_slots; //把最大插槽数量写入寄存器

    /*初始化命令环*/
    xhci_ring_init(&xhci_controller->cmd_ring, xhci_controller->align_size);
    xhci_controller->op_reg->crcr = va_to_pa(xhci_controller->cmd_ring.ring_base) | 1; //命令环物理地址写入crcr寄存器，置位rcs

    /*初始化事件环*/
    xhci_ring_init(&xhci_controller->event_ring, xhci_controller->align_size);
    xhci_erst_t *erstba = kmalloc(align_up(sizeof(xhci_erst_t), xhci_controller->align_size)); //分配单事件环段表内存
    erstba->ring_seg_base = va_to_pa(xhci_controller->event_ring.ring_base); //段表中写入事件环物理地址
    erstba->ring_seg_size = TRB_COUNT; //事件环最大trb个数
    erstba->reserved = 0;
    xhci_controller->rt_reg->intr_regs[0].erstsz = 1; //设置单事件环段
    xhci_controller->rt_reg->intr_regs[0].erstba = va_to_pa(erstba); //事件环段表物理地址写入寄存器
    xhci_controller->rt_reg->intr_regs[0].erdp = va_to_pa(xhci_controller->event_ring.ring_base); //事件环物理地址写入寄存器

    /*初始化暂存器缓冲区*/
    uint32 spb_number = (xhci_controller->cap_reg->hcsparams2 & 0x1f << 21) >> 16 | xhci_controller->cap_reg->hcsparams2
                        >> 27;
    if (spb_number) {
        uint64 *spb_array = kzalloc(align_up(spb_number << 3, xhci_controller->align_size)); //分配暂存器缓冲区指针数组
        for (uint32 i = 0; i < spb_number; i++) spb_array[i] = va_to_pa(kzalloc(xhci_controller->align_size));
        //分配暂存器缓存区
        xhci_controller->dcbaap[0] = va_to_pa(spb_array); //暂存器缓存去数组指针写入设备上下写文数组0
    }

    /*启动xhci*/
    xhci_controller->op_reg->usbcmd |= XHCI_CMD_RS;

    /*获取协议支持能力*/
    xhci_cap_t *sp_cap = xhci_cap_find(xhci_controller, 2);

    color_printk(
        GREEN,BLACK,
        "Xhci Version:%x.%x USB%x.%x BAR0 MMIO:%#lx MSI-X:%d MaxSlots:%d MaxIntrs:%d MaxPorts:%d Context_Size:%d AC64:%d SPB:%d USBcmd:%#x USBsts:%#x AlignSize:%d iman:%#x imod:%#x crcr:%#lx dcbaap:%#lx erstba:%#lx erdp0:%#lx\n",
        xhci_controller->cap_reg->hciversion >> 8, xhci_controller->cap_reg->hciversion & 0xFF,
        sp_cap->supported_protocol.protocol_ver >> 24, sp_cap->supported_protocol.protocol_ver >> 16 & 0xFF,
        xhci_dev->bar[0].paddr, xhci_dev->msi_x_flags, xhci_controller->cap_reg->hcsparams1 & 0xFF,
        xhci_controller->cap_reg->hcsparams1 >> 8 & 0x7FF,
        xhci_controller->cap_reg->hcsparams1 >> 24, xhci_controller->cap_reg->hccparams1 >> 2 & 1,
        xhci_controller->context_size, spb_number,
        xhci_controller->op_reg->usbcmd, xhci_controller->op_reg->usbsts, xhci_controller->align_size,
        xhci_controller->rt_reg->intr_regs[0].iman,
        xhci_controller->rt_reg->intr_regs[0].imod, va_to_pa(xhci_controller->cmd_ring.ring_base),
        xhci_controller->op_reg->dcbaap, xhci_controller->rt_reg->intr_regs[0].erstba,
        xhci_controller->rt_reg->intr_regs[0].erdp);

    timing();

    usb_dev_scan(xhci_dev);

    color_printk(GREEN,BLACK, "\nUSBcmd:%#x  USBsts:%#x", xhci_controller->op_reg->usbcmd,
                 xhci_controller->op_reg->usbsts);
    while (1);
}

void xhci_remove(pcie_dev_t *xhci_dev) {

}

pcie_drv_t *xhci_drv_init(void) {
    pcie_drv_t *xhci_drv = kmalloc(sizeof(pcie_drv_t));
    pcie_id_t *id_table = kzalloc(sizeof(pcie_id_t)*2);
    id_table->class_code = XHCI_CLASS_CODE;
    xhci_drv->drv.name = "XHCI-driver";
    xhci_drv->drv.id_table = id_table;
    xhci_drv->probe = xhci_probe;
    xhci_drv->remove = xhci_remove;
    return xhci_drv;
}
