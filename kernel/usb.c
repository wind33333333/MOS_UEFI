#include "usb.h"
#include "slub.h"
#include "vmalloc.h"
#include "vmm.h"

//获取设备描述符
int get_device_descriptor(xhci_regs_t *xhci_regs, usb_dev_t* usb_dev) {
    usb_device_descriptor_t *dev_desc = kzalloc(sizeof(usb_device_descriptor_t));
    xhci_device_context32_t *dev_context32 = pa_to_va(xhci_regs->dcbaap[usb_dev->slot_id]);

    xhci_trb_t trb;
    // Setup TRB
    usb_setup_packet_t setup = {0x80, 0x06, 0x0100, 0x0000, 8}; // 统一为8
    trb.parameter = *(UINT64 *) &setup; // 完整 8 字节
    trb.status = 8; // TRB Length=8 (Setup 阶段长度)
    trb.control = TRB_TYPE_SETUP | TRB_IDT | (3 << 16) | TRB_CHAIN | TRB_IOC;
    xhci_ring_enqueue(&usb_dev->ep0_trans_ring, &trb);
    // TRT=3 (IN), Chain, IO

    // Data TRB
    trb.parameter = va_to_pa(dev_desc);
    trb.status = 8; // 匹配 w_length
    trb.control = TRB_TYPE_DATA | (1 << 16) | TRB_CHAIN | TRB_IOC;
    xhci_ring_enqueue(&usb_dev->ep0_trans_ring, &trb);

    // Status TRB
    trb.parameter = 0;
    trb.status = 0;
    trb.control = TRB_TYPE_STATUS | TRB_IOC;
    xhci_ring_enqueue(&usb_dev->ep0_trans_ring, &trb);

    // 响铃
    xhci_ring_doorbell(xhci_regs->db, usb_dev->slot_id, 1);

    timing();

    UINT32 max_packe_size = dev_desc->bcdUSB >= 0x300 ? 1<<dev_desc->bMaxPacketSize0:dev_desc->bMaxPacketSize0;

    //配置设备上下文
    xhci_input_context64_t *input_context = kzalloc(align_up(sizeof(xhci_input_context64_t),xhci_regs->align_size));
    if (xhci_regs->cap->hccparams1 & HCCP1_CSZ) {
        input_context->add_context = 0x2;
        input_context->drop_context = 0x0;
        input_context->dev_ctx.ep[0].tr_dequeue_ptr = dev_context32->ep[0].tr_dequeue_ptr;
        input_context->dev_ctx.ep[0].reg0 = dev_context32->ep[0].reg0;
        input_context->dev_ctx.ep[0].reg1 = 4 << 3 | max_packe_size<<16;
    }else {
        xhci_input_context32_t *input_context32 = (xhci_input_context32_t*)input_context;
        input_context32->add_context = 0x2;
        input_context32->drop_context = 0x0;
        input_context32->dev_ctx.ep[0].tr_dequeue_ptr = dev_context32->ep[0].tr_dequeue_ptr;
        input_context32->dev_ctx.ep[0].reg0 = dev_context32->ep[0].reg0;
        input_context32->dev_ctx.ep[0].reg1 = 4 << 3 | max_packe_size<<16;
    }

    trb.parameter = va_to_pa(input_context);
    trb.status = 0;
    trb.control =  usb_dev->slot_id << 24 | TRB_EVALUATE_CONTEXT;
    xhci_ring_enqueue(&xhci_regs->cmd_ring, &trb);
    xhci_ring_doorbell(xhci_regs->db, 0, 0);

    timing();

    xhci_ering_dequeue(xhci_regs, &trb);
    kfree(input_context);

    setup.w_length = 18;
    trb.parameter = *(UINT64 *) &setup; // 完整 8 字节
    trb.status = 8; // TRB Length=8 (Setup 阶段长度)
    trb.control = TRB_TYPE_SETUP | TRB_IDT | (3 << 16) | TRB_CHAIN | TRB_IOC;
    xhci_ring_enqueue(&usb_dev->ep0_trans_ring, &trb);
    // TRT=3 (IN), Chain, IO

    // Data TRB
    trb.parameter = va_to_pa(dev_desc);
    trb.status = 18; // 匹配 w_length
    trb.control = TRB_TYPE_DATA | (1 << 16) | TRB_CHAIN | TRB_IOC;
    xhci_ring_enqueue(&usb_dev->ep0_trans_ring, &trb);

    // Status TRB
    trb.parameter = 0;
    trb.status = 0;
    trb.control = TRB_TYPE_STATUS | TRB_IOC;
    xhci_ring_enqueue(&usb_dev->ep0_trans_ring, &trb);

    // 响铃
    xhci_ring_doorbell(xhci_regs->db, usb_dev->slot_id, 1);

    timing();

    color_printk(GREEN,BLACK, "port_id:%d slot_id:%d portsc:%#x bcd_usb:%#x id_v:%#x id_p:%#x MaxPZ:%d DevClass:%#x DevSubClass:%#x DevProt:%#x\n",usb_dev->port_id,usb_dev->slot_id, dev_desc->bcdUSB,xhci_regs->op->portregs[usb_dev->port_id-1].portsc, dev_desc->idVendor,
    dev_desc->idProduct,max_packe_size,dev_desc->bDeviceClass,dev_desc->bDeviceSubClass,dev_desc->bDeviceProtocol);
}