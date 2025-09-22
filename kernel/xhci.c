#include "xhci.h"
#include "moslib.h"
#include "pcie.h"
#include "printk.h"
#include "slub.h"
#include "vmm.h"
#include "usb.h"

//usb设备全局链
list_head_t usb_dev_list;

xhci_cap_t *xhci_cap_find(xhci_regs_t *xhci_reg, uint8 cap_id) {
    uint32 offset = xhci_reg->cap->hccparams1 >> 16;
    while (offset) {
        xhci_cap_t *xhci_cap = (void *) xhci_reg->cap + (offset << 2);
        if ((xhci_cap->cap_id & 0xFF) == cap_id) return xhci_cap;
        offset = (xhci_cap->next_ptr >> 8) & 0xFF;
    }
    return NULL;
}

static inline uint8 get_sts_c(uint64 ptr) {
    return ptr & TRB_CYCLE;
}

static inline xhci_trb_t *get_queue_ptr(uint64 ptr) {
    return (xhci_trb_t *) (ptr & ~(TRB_CYCLE));
}

//命令环/传输环入队列
int xhci_ring_enqueue(xhci_ring_t *ring, xhci_trb_t *trb) {
    if (ring->index >= TRB_COUNT-1) {
        ring->index = 0;
        ring->status_c ^= TRB_CYCLE;
        ring->ring_base[TRB_COUNT-1].parameter = va_to_pa(ring->ring_base);
        ring->ring_base[TRB_COUNT-1].status = 0;
        ring->ring_base[TRB_COUNT-1].control = TRB_TYPE_LINK | TRB_TOGGLE_CYCLE | ring->status_c;
    }
    ring->ring_base[ring->index].parameter = trb->parameter;
    ring->ring_base[ring->index].status = trb->status;
    ring->ring_base[ring->index].control = trb->control | ring->status_c;
    ring->index++;
    return 0;
}

//事件环出队列
int xhci_ering_dequeue(xhci_regs_t *xhci_regs, xhci_trb_t *evt_trb) {
    xhci_ring_t *event_ring = &xhci_regs->event_ring;
    while ((event_ring->ring_base[event_ring->index].control & TRB_CYCLE) == event_ring->status_c) {
        evt_trb->parameter = event_ring->ring_base[event_ring->index].parameter;
        evt_trb->status = event_ring->ring_base[event_ring->index].status;
        evt_trb->control = event_ring->ring_base[event_ring->index].control;
        event_ring->index++;
        if (event_ring->index >= TRB_COUNT) {
            event_ring->index = 0;
            event_ring->status_c ^= TRB_CYCLE;
        }
        xhci_regs->rt->intr_regs[0].erdp = va_to_pa(&event_ring->ring_base[event_ring->index]) | XHCI_ERDP_EHB;
    }
    return 0;
}

//分配插槽
uint32 xhci_enable_slot(xhci_regs_t *xhci_regs) {
    xhci_trb_t trb = {
        0,
        0,
        TRB_ENABLE_SLOT
    };
    xhci_ring_enqueue(&xhci_regs->cmd_ring, &trb);
    xhci_ring_doorbell(xhci_regs->db, 0, 0);

    timing();

    xhci_ering_dequeue(xhci_regs, &trb);
    if ((trb.control >> 10 & 0x3F) == 33 && trb.control >> 24) {
        return trb.control >> 24 & 0xFF;
    }
    return -1;
}

//初始化命令环
int xhci_ring_init(xhci_ring_t *ring,uint32 align_size) {
    ring->ring_base = kzalloc(align_up(TRB_COUNT * sizeof(xhci_trb_t),align_size));
    ring->index = 0;
    ring->status_c = TRB_CYCLE;
    ring->ring_base[TRB_COUNT - 1].parameter = va_to_pa(ring->ring_base);
    ring->ring_base[TRB_COUNT - 1].status = 0;
    ring->ring_base[TRB_COUNT - 1].control = TRB_TYPE_LINK | TRB_TOGGLE_CYCLE | TRB_CYCLE;
}

//增加输入上下文
void xhci_input_context_add(xhci_input_context_t *input_ctx,uint32 ctx_size,uint32 ctx_number,xhci_context_t *from_ctx) {
    xhci_context_t *to_dev_ctx = (xhci_context_t*)((uint64)input_ctx + ctx_size*(ctx_number+1));
    to_dev_ctx->reg0 = from_ctx->reg0;
    to_dev_ctx->reg1 = from_ctx->reg1;
    to_dev_ctx->reg2 = from_ctx->reg2;
    to_dev_ctx->reg3 = from_ctx->reg3;
    input_ctx->input_ctx32.control.add_context |= 1<<ctx_number;
}

//读取设备上下文
void xhci_devctx_read(xhci_regs_t *xhci_regs,uint32 slot_id,uint32 ctx_number,xhci_context_t *to_ctx) {
    xhci_context_t *from_ctx = pa_to_va(xhci_regs->dcbaap[slot_id]);
    from_ctx = (xhci_context_t*)((uint64)from_ctx + xhci_regs->context_size*ctx_number);
    to_ctx->reg0 = from_ctx->reg0;
    to_ctx->reg1 = from_ctx->reg1;
    to_ctx->reg2 = from_ctx->reg2;
    to_ctx->reg3 = from_ctx->reg3;
}

//设置设备地址
void xhci_address_device(xhci_regs_t *xhci_regs, usb_dev_t *usb_dev) {
    //分配设备插槽上下文内存
    xhci_regs->dcbaap[usb_dev->slot_id] = va_to_pa(kzalloc(align_up(sizeof(xhci_device_context_t),xhci_regs->align_size)));

    //分配传输环内存
    xhci_ring_init(&usb_dev->trans_ring[0],xhci_regs->align_size);

    //配置设备上下文
    xhci_input_context_t *input_ctx = kzalloc(align_up(sizeof(xhci_input_context_t),xhci_regs->align_size));
    xhci_context_t ctx;
    ctx.reg0 = 1 << 27;
    ctx.reg1 = usb_dev->port_id << 16;
    ctx.reg2 = 0;
    ctx.reg3 = 0;
    xhci_input_context_add(input_ctx,xhci_regs->context_size,0,&ctx); // 启用 Slot Context

    ctx.reg0= 1;
    ctx.reg1 = EP_TYPE_CONTROL | 8 << 16;
    ctx.reg2 = va_to_pa(usb_dev->trans_ring[0].ring_base) | TRB_CYCLE;
    ctx.reg3 = 0;
    xhci_input_context_add(input_ctx,xhci_regs->context_size,1,&ctx); //Endpoint 0 Context

    xhci_trb_t trb = {
        va_to_pa(input_ctx),
        0,
        TRB_ADDRESS_DEVICE | usb_dev->slot_id << 24
    };
    xhci_ring_enqueue(&xhci_regs->cmd_ring, &trb);
    xhci_ring_doorbell(xhci_regs->db, 0, 0);

    timing();

    xhci_ering_dequeue(xhci_regs, &trb);
    kfree(input_ctx);
}

//配置端点
void xhci_config_endpoint(xhci_regs_t *xhci_regs,usb_dev_t *usb_dev) {
    xhci_input_context_t *input_ctx = kzalloc(align_up(sizeof(xhci_input_context_t),xhci_regs->align_size));
    xhci_context_t ctx;
    ctx.reg0 = (usb_dev->interface_desc->num_endpoints+1) << 27;
    ctx.reg1 = usb_dev->port_id << 16;
    ctx.reg2 = 0;
    ctx.reg3 = 0;
    xhci_input_context_add(input_ctx,xhci_regs->context_size,0,&ctx);    //更新slot

    for (uint8 i=0;i<usb_dev->interface_desc->num_endpoints;i++) {
        uint8 ac_shift = (usb_dev->endpoint_desc[i]->endpoint_address&0xF)<<1 | usb_dev->endpoint_desc[i]->endpoint_address>>7;
        uint8 tr_idx = ac_shift - 1;
        //分配传输环内存
        xhci_ring_init(&usb_dev->trans_ring[tr_idx],xhci_regs->align_size);

        //识别端点类型
        uint32 ep_type = 0;
        if (ac_shift & 1) {
            switch (usb_dev->endpoint_desc[i]->attributes) {
                case USB_EP_ISOCH:
                    ep_type = EP_TYPE_ISOCH_IN;
                    break;
                case USB_EP_BULK:
                    ep_type = EP_TYPE_BULK_IN;
                    break;
                case USB_EP_INTERRUPT:
                    ep_type = EP_TYPE_INTERRUPT_IN;
            }
        }else {
            switch (usb_dev->endpoint_desc[i]->attributes) {
                case USB_EP_ISOCH:
                    ep_type = EP_TYPE_ISOCH_OUT;
                    break;
                case USB_EP_BULK:
                    ep_type = EP_TYPE_BULK_OUT;
                    break;
                case USB_EP_INTERRUPT:
                    ep_type = EP_TYPE_INTERRUPT_OUT;
            }
        }

        ctx.reg0 = 1;
        ctx.reg1 = ep_type | usb_dev->endpoint_desc[i]->max_packet_size << 16;
        ctx.reg2 = va_to_pa(usb_dev->trans_ring[tr_idx].ring_base) | TRB_CYCLE;
        ctx.reg3 = 0;
        xhci_input_context_add(input_ctx,xhci_regs->context_size,ac_shift,&ctx);
    }

    xhci_trb_t trb = {
        va_to_pa(input_ctx),
        0,
        TRB_CONFIGURE_ENDPOINT | usb_dev->slot_id << 24
    };
    xhci_ring_enqueue(&xhci_regs->cmd_ring, &trb);

    xhci_ring_doorbell(xhci_regs->db, 0, 0);

    timing();

    xhci_ering_dequeue(xhci_regs, &trb);
    kfree(input_ctx);

}

//获取usb设备描述符
int get_usb_device_descriptor(xhci_regs_t *xhci_regs, usb_dev_t* usb_dev) {
    usb_device_descriptor_t *dev_desc = kzalloc(sizeof(usb_device_descriptor_t));
    //第一次先获取设备描述符前8字节，拿到max_pack_size后更新端点1，再重新获取描述符。
    xhci_trb_t trb;
    // Setup TRB
    usb_setup_packet_t setup = {0x80, USB_REQ_GET_DESCRIPTOR, 0x0100, 0x0000, 8}; // 统一为8
    trb.parameter = *(uint64 *) &setup; // 完整 8 字节
    trb.status = 8; // TRB Length=8 (Setup 阶段长度)
    trb.control = TRB_TYPE_SETUP | TRB_IDT | (3 << 16) | TRB_CHAIN;
    xhci_ring_enqueue(&usb_dev->trans_ring[0], &trb);

    // Data TRB
    trb.parameter = va_to_pa(dev_desc);
    trb.status = 8; // 匹配 w_length
    trb.control = TRB_TYPE_DATA | (1 << 16) | TRB_CHAIN;
    xhci_ring_enqueue(&usb_dev->trans_ring[0], &trb);

    // Status TRB
    trb.parameter = 0;
    trb.status = 0;
    trb.control = TRB_TYPE_STATUS | TRB_IOC;
    xhci_ring_enqueue(&usb_dev->trans_ring[0], &trb);

    // 响铃
    xhci_ring_doorbell(xhci_regs->db, usb_dev->slot_id, 1);

    timing();

    //更新端点0的最大包
    uint32 max_packe_size = dev_desc->usb_version >= 0x300 ? 1<<dev_desc->max_packet_size0:dev_desc->max_packet_size0;
    xhci_input_context_t *input_ctx = kzalloc(align_up(sizeof(xhci_input_context_t),xhci_regs->align_size));
    xhci_context_t ctx;
    xhci_devctx_read(xhci_regs,usb_dev->slot_id,1,&ctx);
    ctx.reg1 = EP_TYPE_CONTROL | max_packe_size<<16;
    xhci_input_context_add(input_ctx,xhci_regs->context_size,1,&ctx);

    trb.parameter = va_to_pa(input_ctx);
    trb.status = 0;
    trb.control =  usb_dev->slot_id << 24 | TRB_EVALUATE_CONTEXT;
    xhci_ring_enqueue(&xhci_regs->cmd_ring, &trb);
    xhci_ring_doorbell(xhci_regs->db, 0, 0);

    timing();

    xhci_ering_dequeue(xhci_regs, &trb);
    kfree(input_ctx);

    //第二次获取整个设备描述符
    setup.length = 18;
    trb.parameter = *(uint64 *) &setup; // 完整 8 字节
    trb.status = 8; // TRB Length=8 (Setup 阶段长度)
    trb.control = TRB_TYPE_SETUP | TRB_IDT | (3 << 16) | TRB_CHAIN;
    xhci_ring_enqueue(&usb_dev->trans_ring[0], &trb);

    // Data TRB
    trb.parameter = va_to_pa(dev_desc);
    trb.status = 18; // 匹配 w_length
    trb.control = TRB_TYPE_DATA | (1 << 16) | TRB_CHAIN;
    xhci_ring_enqueue(&usb_dev->trans_ring[0], &trb);

    // Status TRB
    trb.parameter = 0;
    trb.status = 0;
    trb.control = TRB_TYPE_STATUS;
    xhci_ring_enqueue(&usb_dev->trans_ring[0], &trb);

    // 响铃
    xhci_ring_doorbell(xhci_regs->db, usb_dev->slot_id, 1);

    timing();

    usb_dev->dev_desc = dev_desc;

    color_printk(GREEN,BLACK, "port_id:%d slot_id:%d portsc:%#x bcd_usb:%#x id_v:%#x id_p:%#x MaxPZ:%d DevClass:%#x DevSubClass:%#x DevProt:%#x\n",usb_dev->port_id,usb_dev->slot_id, dev_desc->usb_version,xhci_regs->op->portregs[usb_dev->port_id-1].portsc, dev_desc->vendor_id,
    dev_desc->product_id,max_packe_size,dev_desc->device_class,dev_desc->device_subclass,dev_desc->device_protocol);
}

//获取usb配置描述符
int get_usb_config_descriptor(xhci_regs_t *xhci_regs,usb_dev_t *usb_dev) {
    //第一次先获取配置描述符前9字节
    xhci_trb_t trb;
    usb_config_descriptor_t *config_desc = kmalloc(9);
    usb_setup_packet_t setup = {0x80, USB_REQ_GET_DESCRIPTOR, 0x0200, 0x0000, 9}; //9
    trb.parameter = *(uint64 *) &setup;
    trb.status = 8;
    trb.control = TRB_TYPE_SETUP | TRB_IDT | (3 << 16) | TRB_CHAIN;
    xhci_ring_enqueue(&usb_dev->trans_ring[0], &trb);

    // Data TRB
    trb.parameter = va_to_pa(config_desc);
    trb.status = 9;
    trb.control = TRB_TYPE_DATA | (1 << 16) | TRB_CHAIN;
    xhci_ring_enqueue(&usb_dev->trans_ring[0], &trb);

    // Status TRB
    trb.parameter = 0;
    trb.status = 0;
    trb.control = TRB_TYPE_STATUS | TRB_IOC;
    xhci_ring_enqueue(&usb_dev->trans_ring[0], &trb);

    // 响铃
    xhci_ring_doorbell(xhci_regs->db, usb_dev->slot_id, 1);

    timing();

    //第二次从配置描述符中得到总长度获取整个配置描述符
    setup.length = config_desc->total_length;
    kfree(config_desc);
    config_desc = kzalloc(setup.length);

    trb.parameter = *(uint64 *) &setup;
    trb.status = 8;
    trb.control = TRB_TYPE_SETUP | TRB_IDT | (3 << 16) | TRB_CHAIN;
    xhci_ring_enqueue(&usb_dev->trans_ring[0], &trb);

    // Data TRB
    trb.parameter = va_to_pa(config_desc);
    trb.status = setup.length;
    trb.control = TRB_TYPE_DATA | (1 << 16) | TRB_CHAIN;
    xhci_ring_enqueue(&usb_dev->trans_ring[0], &trb);

    // Status TRB
    trb.parameter = 0;
    trb.status = 0;
    trb.control = TRB_TYPE_STATUS | TRB_IOC;
    xhci_ring_enqueue(&usb_dev->trans_ring[0], &trb);

    // 响铃
    xhci_ring_doorbell(xhci_regs->db, usb_dev->slot_id, 1);

    timing();

    usb_config_descriptor_t *config_desc_end = (usb_config_descriptor_t*)((uint64)config_desc+config_desc->total_length);
    uint32 ep_idx = 0;
    while (config_desc < config_desc_end) {
        switch (config_desc->head.descriptor_type) {
            case USB_DESC_TYPE_CONFIGURATION:
                usb_dev->config_desc = config_desc;
                break;
            case USB_DESC_TYPE_STRING:
                usb_dev->string_desc = config_desc;
                break;
            case USB_DESC_TYPE_INTERFACE:
                usb_dev->interface_desc = config_desc;
                break;
            case USB_DESC_TYPE_ENDPOINT:
                usb_dev->endpoint_desc[ep_idx] = config_desc;
                ep_idx++;
                break;
            case USB_DESC_TYPE_HID:
                usb_dev->hid_desc = config_desc;
                break;
            case USB_DESC_TYPE_HUB:
                usb_dev->hub_desc = config_desc;
                break;
        }
        config_desc = (usb_config_descriptor_t*)((uint64)config_desc + config_desc->head.length);
    }
    return 0;
}

//创建usb设备
usb_dev_t *create_usb_dev(xhci_regs_t *xhci_regs,uint32 port_id) {
    usb_dev_t *usb_dev = kzalloc(sizeof(usb_dev_t));
    usb_dev->port_id = port_id+1;
    usb_dev->slot_id = xhci_enable_slot(xhci_regs);
    xhci_address_device(xhci_regs,usb_dev);
    get_usb_device_descriptor(xhci_regs,usb_dev);
    get_usb_config_descriptor(xhci_regs,usb_dev);
    xhci_config_endpoint(xhci_regs,usb_dev);
    list_add_head(&usb_dev_list,&usb_dev->list);
}


INIT_TEXT void init_xhci(void) {
    pcie_dev_t *xhci_dev = pcie_dev_find(XHCI_CLASS_CODE); //查找xhci设备
    pcie_bar_set(xhci_dev, 0); //初始化bar0寄存器
    pcie_msi_intrpt_set(xhci_dev); //初始化xhci msi中断
    xhci_dev->private = kzalloc(sizeof(xhci_regs_t)); //设备私有数据空间申请一块内存，存放xhci相关信息

    /*计算xhci寄存器*/
    xhci_regs_t *xhci_regs = xhci_dev->private;
    xhci_regs->cap = xhci_dev->bar[0]; //xhci能力寄存器基地址
    xhci_regs->op = xhci_dev->bar[0] + xhci_regs->cap->cap_length; //xhci操作寄存器基地址
    xhci_regs->rt = xhci_dev->bar[0] + xhci_regs->cap->rtsoff; //xhci运行时寄存器基地址
    xhci_regs->db = xhci_dev->bar[0] + xhci_regs->cap->dboff; //xhci门铃寄存器基地址

    /*停止复位xhci*/
    xhci_regs->op->usbcmd &= ~XHCI_CMD_RS; //停止xhci
    while (!(xhci_regs->op->usbsts & XHCI_STS_HCH)) pause();
    xhci_regs->op->usbcmd |= XHCI_CMD_HCRST; //复位xhci
    while (xhci_regs->op->usbcmd & XHCI_CMD_HCRST) pause();
    while (xhci_regs->op->usbsts & XHCI_STS_CNR) pause();

    /*计算xhci内存对齐边界*/
    xhci_regs->align_size = PAGE_4K_SIZE<<bsf(xhci_regs->op->pagesize);

    /*设备上下文字节数*/
    xhci_regs->context_size = 32 << ((xhci_regs->cap->hccparams1&HCCP1_CSZ)>>2);

    /*初始化设备上下文*/
    uint32 max_slots = xhci_regs->cap->hcsparams1 & 0xff;
    xhci_regs->dcbaap = kzalloc(align_up((max_slots+1)<<3,xhci_regs->align_size)); //分配设备上下文插槽内存,最大插槽数量(插槽从1开始需要+1)*8字节内存
    xhci_regs->op->dcbaap = va_to_pa(xhci_regs->dcbaap); //把设备上下文基地址数组表的物理地址写入寄存器
    xhci_regs->op->config = max_slots;                   //把最大插槽数量写入寄存器

    /*初始化命令环*/
    xhci_regs->cmd_ring.ring_base = kzalloc(align_up(TRB_COUNT * sizeof(xhci_trb_t),xhci_regs->align_size));//分配命令环空间256* sizeof(xhci_trb_t) = 4K
    xhci_regs->cmd_ring.index = 0;
    xhci_regs->cmd_ring.status_c = TRB_CYCLE;
    xhci_regs->cmd_ring.ring_base[TRB_COUNT - 1].parameter = va_to_pa(xhci_regs->cmd_ring.ring_base); //命令环最后一个trb指向环首地址
    xhci_regs->cmd_ring.ring_base[TRB_COUNT - 1].status = 0;
    xhci_regs->cmd_ring.ring_base[TRB_COUNT - 1].control = TRB_TYPE_LINK | TRB_TOGGLE_CYCLE | TRB_CYCLE; //命令环最后一个trb设置位link
    xhci_regs->op->crcr = va_to_pa(xhci_regs->cmd_ring.ring_base) | TRB_CYCLE; //命令环物理地址写入crcr寄存器，置位rcs

    /*初始化事件环*/
    xhci_regs->event_ring.ring_base = kzalloc(align_up(TRB_COUNT * sizeof(xhci_trb_t),xhci_regs->align_size)); //分配事件环空间256* sizeof(xhci_trb_t) = 4K
    xhci_regs->event_ring.index = 0;
    xhci_regs->event_ring.status_c = TRB_CYCLE;
    xhci_erst_t *erstba = kmalloc(align_up(sizeof(xhci_erst_t),xhci_regs->align_size)); //分配单事件环段表内存64字节
    erstba->ring_seg_base = va_to_pa(xhci_regs->event_ring.ring_base); //段表中写入事件环物理地址
    erstba->ring_seg_size = TRB_COUNT;    //事件环最大trb个数
    erstba->reserved = 0;
    xhci_regs->rt->intr_regs[0].erstsz = 1; //设置单事件环段
    xhci_regs->rt->intr_regs[0].erstba = va_to_pa(erstba); //事件环段表物理地址写入寄存器
    xhci_regs->rt->intr_regs[0].erdp = va_to_pa(xhci_regs->event_ring.ring_base); //事件环物理地址写入寄存器

    /*初始化暂存器缓冲区*/
    uint32 spb_number = (xhci_regs->cap->hcsparams2 & 0x1f<<21)>>16 | xhci_regs->cap->hcsparams2>>27;
    if (spb_number) {
        uint64 *spb_array = kzalloc(align_up(spb_number<<3,xhci_regs->align_size)); //分配暂存器缓冲区指针数组
        for (uint32 i = 0; i < spb_number; i++) {
            spb_array[i] = va_to_pa(kzalloc(xhci_regs->align_size));        //分配暂存器缓存区
        }
        xhci_regs->dcbaap[0] = va_to_pa(spb_array);                 //暂存器缓存去数组指针写入设备上下写文数组0
    }

    /*启动xhci*/
    xhci_regs->op->usbcmd |= XHCI_CMD_RS;

    /*获取协议支持能力*/
    xhci_cap_t *sp_cap = xhci_cap_find(xhci_regs, 2);

    color_printk(
        GREEN,BLACK,
        "Xhci Version:%x.%x USB%x.%x BAR0 MMIO:%#lx MSI-X:%d MaxSlots:%d MaxIntrs:%d MaxPorts:%d CTS:%d AC64:%d SPB:%d USBcmd:%#x USBsts:%#x AlignSize:%d iman:%#x imod:%#x crcr:%#lx dcbaap:%#lx erstba:%#lx erdp0:%#lx\n",
        xhci_regs->cap->hciversion >> 8, xhci_regs->cap->hciversion & 0xFF,
        sp_cap->supported_protocol.protocol_ver >> 24, sp_cap->supported_protocol.protocol_ver >> 16 & 0xFF,
        va_to_pa(xhci_dev->bar[0]), xhci_dev->msi_x_flags, xhci_regs->cap->hcsparams1 & 0xFF, xhci_regs->cap->hcsparams1 >> 8 & 0x7FF,
        xhci_regs->cap->hcsparams1 >> 24, xhci_regs->cap->hccparams1 >> 2 & 1, xhci_regs->cap->hccparams1 & 1,spb_number,
        xhci_regs->op->usbcmd, xhci_regs->op->usbsts, xhci_regs->align_size, xhci_regs->rt->intr_regs[0].iman,
        xhci_regs->rt->intr_regs[0].imod,va_to_pa(xhci_regs->cmd_ring.ring_base), xhci_regs->op->dcbaap, xhci_regs->rt->intr_regs[0].erstba,
                 xhci_regs->rt->intr_regs[0].erdp);

    timing();

    xhci_trb_t trb;

    //遍历初始化端口，分配插槽和设备地址
    for (uint32 i = 0; i < xhci_regs->cap->hcsparams1 >> 24; i++) {
        if (xhci_regs->op->portregs[i].portsc & XHCI_PORTSC_CCS) {
            if ((xhci_regs->op->portregs[i].portsc>>XHCI_PORTSC_PLS_SHIFT&XHCI_PORTSC_PLS_MASK) == XHCI_PLS_POLLING) { //usb2.0协议版本
                xhci_regs->op->portregs[i].portsc |= XHCI_PORTSC_PR;
                timing();
                xhci_ering_dequeue(xhci_regs,&trb);
            }
            //usb3.x以上协议版本
            while (!(xhci_regs->op->portregs[i].portsc & XHCI_PORTSC_PED)) pause();
            create_usb_dev(xhci_regs,i);
        }
    }

    color_printk(GREEN,BLACK,"\nUSBcmd:%#x  USBsts:%#x",xhci_regs->op->usbcmd, xhci_regs->op->usbsts);
    while (1);
}