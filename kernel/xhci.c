#include "xhci.h"
#include "moslib.h"
#include "apic.h"
#include "pcie.h"
#include "printk.h"
#include "slub.h"
#include "vmalloc.h"
#include "vmm.h"


typedef struct {
    UINT8 b_request_type;
    UINT8 b_request;
    UINT16 w_value;
    UINT16 w_index;
    UINT16 w_length;
} __attribute__((packed)) usb_setup_packet_t;

typedef struct {
    UINT8 bLength;
    UINT8 bDescriptorType;
    UINT16 bcdUSB;
    UINT8 bDeviceClass;
    UINT8 bDeviceSubClass;
    UINT8 bDeviceProtocol;
    UINT8 bMaxPacketSize0;
    UINT16 idVendor;
    UINT16 idProduct;
    UINT16 bcdDevice;
    UINT8 iManufacturer;
    UINT8 iProduct;
    UINT8 iSerialNumber;
    UINT8 bNumConfigurations;
} __attribute__((packed)) usb_device_descriptor_t;

typedef struct {
    UINT8 bLength;
    UINT8 bDescriptorType;
    UINT16 wTotalLength;
    UINT8 bNumInterfaces;
    UINT8 bConfigurationValue;
    UINT8 iConfiguration;
    UINT8 bmAttributes;
    UINT8 bMaxPower;
} __attribute__((packed)) usb_config_descriptor_t;

typedef struct {
    UINT8 bLength;
    UINT8 bDescriptorType;
    UINT8 bInterfaceNumber;
    UINT8 bAlternateSetting;
    UINT8 bNumEndpoints;
    UINT8 bInterfaceClass;
    UINT8 bInterfaceSubClass;
    UINT8 bInterfaceProtocol;
    UINT8 iInterface;
} __attribute__((packed)) usb_interface_descriptor_t;


xhci_cap_t *xhci_cap_find(xhci_regs_t *xhci_reg, UINT8 cap_id) {
    UINT32 offset = xhci_reg->cap->hccparams1 >> 16;
    while (offset) {
        xhci_cap_t *xhci_cap = (void *) xhci_reg->cap + (offset << 2);
        if ((xhci_cap->cap_id & 0xFF) == cap_id) return xhci_cap;
        offset = (xhci_cap->next_ptr >> 8) & 0xFF;
    }
    return NULL;
}

//响铃
static inline void xhci_ring_doorbell(xhci_regs_t *xhci_regs, UINT8 db_number, UINT32 value) {
    xhci_regs->db[db_number] = value;
}

//trb入队列
int xhci_queue(xhci_trb_t **queue_ptr, xhci_trb_t *trb) {
    xhci_trb_t *ring_base = (xhci_trb_t*)((UINT64)*queue_ptr & ~(TRB_COUNT*sizeof(xhci_trb_t) - 1));
    if (*queue_ptr >= ring_base+TRB_COUNT-1) {
        *queue_ptr = ring_base;
        (*queue_ptr)[TRB_COUNT-1].control ^= TRB_CYCLE;
    }
    (*queue_ptr)->parameter = trb->parameter;
    (*queue_ptr)->status = trb->status;
    (*queue_ptr)->control = trb->control | (*queue_ptr)[TRB_COUNT-1].control&TRB_CYCLE;
    (*queue_ptr)++;
    return 0;
}

//读事件环
int xhci_read_evt_queue_ptr(xhci_regs_t *xhci_regs, xhci_trb_t *evt_trb) {
    while ((xhci_regs->evt_queue_ptr->control&TRB_CYCLE) == xhci_regs->event_c) {
        evt_trb->parameter = xhci_regs->evt_queue_ptr->parameter;
        evt_trb->status = xhci_regs->evt_queue_ptr->status;
        evt_trb->control = xhci_regs->evt_queue_ptr->control;
        xhci_trb_t *event_base = (xhci_trb_t*)((UINT64)xhci_regs->evt_queue_ptr & ~(TRB_COUNT*sizeof(xhci_trb_t) - 1));
        xhci_regs->evt_queue_ptr++;
        if (xhci_regs->evt_queue_ptr >= event_base+TRB_COUNT) {
            xhci_regs->evt_queue_ptr = event_base;
            xhci_regs->event_c ^= TRB_CYCLE;
        }
        xhci_regs->rt->intr_regs->erdp = va_to_pa(xhci_regs->evt_queue_ptr) | XHCI_ERDP_EHB;
    }
    return 0;
}

//分配插槽
static inline UINT32 xhci_enable_slot(xhci_regs_t *xhci_regs) {
    xhci_trb_t trb = {
        0,
        0,
        TRB_ENABLE_SLOT
    };
    xhci_queue(&xhci_regs->cr_queue_ptr, &trb);
    xhci_ring_doorbell(xhci_regs, 0, 0);

    // UINT64 count = 20000000;
    // while (count--) pause();

    xhci_read_evt_queue_ptr(xhci_regs, &trb);
    if ((trb.control >> 10 & 0x3F) == 33 && trb.control >> 24) {
        return trb.control >> 24 & 0xFF;
    }
    return -1;
}

//设置设备地址
void xhci_address_device(xhci_regs_t *xhci_regs, UINT32 slot_number, UINT32 port_number,UINT32 speed) {
    //分配设备插槽上下文内存
    xhci_regs->dcbaap[slot_number] = va_to_pa(kzalloc(align_up(sizeof(xhci_device_context64_t),xhci_regs->align_size)));

    //分配传输环内存
    xhci_regs->ep0_tr_queue_ptr = kzalloc(align_up(TRB_COUNT * sizeof(xhci_trb_t),xhci_regs->align_size));
    xhci_regs->ep0_tr_queue_ptr[TRB_COUNT - 1].parameter = va_to_pa(xhci_regs->ep0_tr_queue_ptr);
    xhci_regs->ep0_tr_queue_ptr[TRB_COUNT - 1].control = TRB_TYPE_LINK | TRB_TOGGLE_CYCLE | TRB_CYCLE;

    //配置设备上下文
    xhci_input_context64_t *input_context = kzalloc(align_up(sizeof(xhci_input_context64_t),xhci_regs->align_size));
    if (xhci_regs->cap->hccparams1 & HCCP1_CSZ) {
        input_context->add_context = 0x3; // 启用 Slot Context 和 Endpoint 0 Context
        input_context->drop_context = 0x0;
        input_context->dev_ctx.slot.reg0 = 1 << 27 | speed<<20;
        input_context->dev_ctx.slot.reg1 = port_number << 16;
        input_context->dev_ctx.ep[0].tr_dequeue_pointer = va_to_pa(xhci_regs->ep0_tr_queue_ptr) | TRB_CYCLE;
        input_context->dev_ctx.ep[0].reg0 = 1;
        input_context->dev_ctx.ep[0].reg1 = 4 << 3 | 64 << 16;
    }else {
        xhci_input_context32_t *input_context32 = (xhci_input_context32_t*)input_context;
        input_context32->add_context = 0x3; // 启用 Slot Context 和 Endpoint 0 Context
        input_context32->drop_context = 0x0;
        input_context32->dev_ctx.slot.reg0 = 1 << 27 | speed<<20;
        input_context32->dev_ctx.slot.reg1 = port_number << 16;
        input_context32->dev_ctx.ep[0].tr_dequeue_ptr = va_to_pa(xhci_regs->ep0_tr_queue_ptr) | TRB_CYCLE;
        input_context32->dev_ctx.ep[0].reg0 = 1;
        input_context32->dev_ctx.ep[0].reg1 = 4 << 3 | 64 << 16;
    }

    xhci_trb_t trb = {
        va_to_pa(input_context),
        0,
        TRB_ADDRESS_DEVICE | slot_number << 24
    };
    xhci_queue(&xhci_regs->cr_queue_ptr, &trb);
    xhci_ring_doorbell(xhci_regs, 0, 0);

    // UINT64 count = 20000000;
    // while (count--) pause();

    xhci_read_evt_queue_ptr(xhci_regs, &trb);
    kfree(input_context);
}

//获取设备描述符
int get_device_descriptor(xhci_regs_t *xhci_regs, UINT32 slot_number) {
    usb_device_descriptor_t *dev_desc = kzalloc(sizeof(usb_device_descriptor_t));
    xhci_device_context32_t *dev_ctx = pa_to_va(xhci_regs->dcbaap[slot_number]);
    xhci_trb_t *transfer_ring = pa_to_va(dev_ctx->ep[0].tr_dequeue_ptr & ~0xFULL);

    // Setup TRB
    usb_setup_packet_t setup = {0x80, 0x06, 0x0100, 0x0000, 8}; // 统一为8
    color_printk(GREEN,BLACK, "transfer_ring:%lx\n", transfer_ring);
    transfer_ring[0].parameter = *(UINT64 *) &setup; // 完整 8 字节
    transfer_ring[0].status = 8; // TRB Length=8 (Setup 阶段长度)
    transfer_ring[0].control = TRB_TYPE_SETUP | TRB_IDT | (3 << 16) | TRB_CHAIN | TRB_IOC | TRB_CYCLE;
    // TRT=3 (IN), Chain, IO

    // Data TRB
    transfer_ring[1].parameter = va_to_pa(dev_desc);
    transfer_ring[1].status = 8; // 匹配 w_length
    transfer_ring[1].control = TRB_TYPE_DATA | (1 << 16) | TRB_CHAIN | TRB_IOC | TRB_CYCLE;

    // Status TRB
    transfer_ring[2].parameter = 0;
    transfer_ring[2].status = 0;
    transfer_ring[2].control = TRB_TYPE_STATUS | TRB_IOC | TRB_CYCLE;

    // 响铃
    xhci_ring_doorbell(xhci_regs, slot_number, 1);

    color_printk(GREEN,BLACK, "bcd_usb:%x id_v:%x id_p:%x\n", dev_desc->bcdUSB, dev_desc->idVendor,
                 dev_desc->idProduct);

    while (1);
}

INIT_TEXT void init_xhci(void) {
    pcie_dev_t *xhci_dev = pcie_dev_find(XHCI_CLASS_CODE); //找xhci设备
    pcie_bar_set(xhci_dev, 0); //初始化bar0寄存器
    pcie_msi_intrpt_set(xhci_dev); //初始化msi中断
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

    /*初始化设备上下文*/
    UINT32 max_slots = xhci_regs->cap->hcsparams1 & 0xff;
    xhci_regs->dcbaap = kzalloc(align_up((max_slots+1)<<3,xhci_regs->align_size)); //分配设备上下文插槽内存,最大插槽数量(插槽从1开始需要+1)*8字节内存
    xhci_regs->op->dcbaap = va_to_pa(xhci_regs->dcbaap); //把设备上下文基地址数组表的物理地址写入寄存器
    xhci_regs->op->config = max_slots;                   //把最大插槽数量写入寄存器

    /*初始化命令环*/
    xhci_regs->cr_queue_ptr = kzalloc(align_up(TRB_COUNT * sizeof(xhci_trb_t),xhci_regs->align_size)); //分配命令环空间256* sizeof(xhci_trb_t) = 4K
    xhci_regs->cr_queue_ptr[TRB_COUNT - 1].parameter = va_to_pa(xhci_regs->cr_queue_ptr); //命令环最后一个trb指向环首地址
    xhci_regs->cr_queue_ptr[TRB_COUNT - 1].control = TRB_TYPE_LINK | TRB_TOGGLE_CYCLE | TRB_CYCLE; //命令环最后一个trb设置位link
    xhci_regs->op->crcr = va_to_pa(xhci_regs->cr_queue_ptr) | TRB_CYCLE; //命令环物理地址写入crcr寄存器，置位rcs

    /*初始化事件环*/
    xhci_regs->event_c = TRB_CYCLE;
    xhci_erst_t *erstba = kmalloc(align_up(sizeof(xhci_erst_t),xhci_regs->align_size)); //分配单事件环段表内存64字节
    xhci_regs->evt_queue_ptr = kzalloc(align_up(TRB_COUNT * sizeof(xhci_trb_t),xhci_regs->align_size)); //分配事件环空间256* sizeof(xhci_trb_t) = 4K
    erstba->ring_seg_base = va_to_pa(xhci_regs->evt_queue_ptr); //段表中写入事件环物理地址
    erstba->ring_seg_size = TRB_COUNT;    //事件环最大trb个数
    erstba->reserved = 0;
    xhci_regs->rt->intr_regs[0].erstsz = 1; //设置单事件环段
    xhci_regs->rt->intr_regs[0].erstba = va_to_pa(erstba); //事件环段表物理地址写入寄存器
    xhci_regs->rt->intr_regs[0].erdp = va_to_pa(xhci_regs->evt_queue_ptr); //事件环物理地址写入寄存器

    /*初始化暂存器缓冲区*/
    UINT32 spb_number = (xhci_regs->cap->hcsparams2 & 0x1f<<21)>>16 | xhci_regs->cap->hcsparams2>>27;
    if (spb_number) {
        UINT64 *spb_array = kzalloc(align_up(spb_number<<3,xhci_regs->align_size)); //分配暂存器缓冲区指针数组
        for (UINT32 i = 0; i < spb_number; i++) {
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
        "Xhci Version:%x.%x USB%x.%x BAR0 MMIO:%#lx MSI-X:%d MaxSlots:%d MaxIntrs:%d MaxPorts:%d CTS:%d AC64:%d USBcmd:%#x USBsts:%#x AlignSize:%d iman:%#x imod:%#x crcr:%#lx dcbaap:%#lx erstba:%#lx erdp0:%#lx\n",
        xhci_regs->cap->hciversion >> 8, xhci_regs->cap->hciversion & 0xFF,
        sp_cap->supported_protocol.protocol_ver >> 24, sp_cap->supported_protocol.protocol_ver >> 16 & 0xFF,
        va_to_pa(xhci_dev->bar[0]), xhci_dev->msi_x_flags, xhci_regs->cap->hcsparams1 & 0xFF, xhci_regs->cap->hcsparams1 >> 8 & 0x7FF,
        xhci_regs->cap->hcsparams1 >> 24, xhci_regs->cap->hccparams1 >> 2 & 1, xhci_regs->cap->hccparams1 & 1,
        xhci_regs->op->usbcmd, xhci_regs->op->usbsts, xhci_regs->align_size, xhci_regs->rt->intr_regs[0].iman,
        xhci_regs->rt->intr_regs[0].imod,va_to_pa(xhci_regs->cr_queue_ptr), xhci_regs->op->dcbaap, xhci_regs->rt->intr_regs[0].erstba,
                 xhci_regs->rt->intr_regs[0].erdp);

    // UINT64 count = 20000000;
    // while (count--) pause();

    xhci_trb_t trb;

    //遍历初始化端口，分配插槽和设备地址
    for (UINT32 i = 0; i < xhci_regs->cap->hcsparams1 >> 24; i++) {
        if (xhci_regs->op->portregs[i].portsc & XHCI_PORTSC_CCS) {
            if ((xhci_regs->op->portregs[i].portsc>>XHCI_PORTSC_PLS_SHIFT&XHCI_PORTSC_PLS_MASK) == XHCI_PLS_POLLING) { //usb2.0协议版本
                xhci_regs->op->portregs[i].portsc |= XHCI_PORTSC_PR;
                // UINT64 count = 20000000;
                // while (count--) pause();
                xhci_read_evt_queue_ptr(xhci_regs, &trb);
            }
            //usb3.x以上协议版本
            while (!(xhci_regs->op->portregs[i].portsc & XHCI_PORTSC_PED)) pause();
            UINT32 slot_id = xhci_enable_slot(xhci_regs);
            xhci_address_device(xhci_regs, slot_id, i + 1,xhci_regs->op->portregs[i].portsc>>10&0xF);
            color_printk(GREEN,BLACK, "port_id:%#x slot_id:%#x portsc:%#x portpmsc:%#x portli:%#x porthlpmc:%#x \n", i+1,slot_id,
              xhci_regs->op->portregs[i].portsc, xhci_regs->op->portregs[i].portpmsc,
              xhci_regs->op->portregs[i].portli, xhci_regs->op->portregs[i].porthlpmc);
        }
    }

    color_printk(GREEN,BLACK,"\nUSBcmd:%#x  USBsts:%#x",xhci_regs->op->usbcmd, xhci_regs->op->usbsts);
    while (1);
}