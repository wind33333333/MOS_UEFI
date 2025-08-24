#include "xhci.h"
#include "moslib.h"
#include "apic.h"
#include "pcie.h"
#include "printk.h"
#include "slub.h"
#include "vmalloc.h"
#include "vmm.h"

#define TRB_COUNT 256        //trb个数

#define TRB_TYPE_NORMAL      (1 << 10)
#define TRB_TYPE_SETUP       (2 << 10)
#define TRB_TYPE_DATA        (3 << 10)
#define TRB_TYPE_STATUS      (4 << 10)
#define TRB_IOC              (1 << 5) // Interrupt on Completion
#define TRB_TYPE_LINK        (0x06<<10) //连接trb
#define TRB_CYCLE            (1 << 0)
#define TRB_TOGGLE_CYCLE     (1 << 1)
#define TRB_CHAIN            (1 << 9)
// 建议增加宏
#define TRB_IDT             (1 << 6)     // Immediate Data
#define TRB_TRT_IN_DATA     (3 << 16)    // TRT=3: 有数据阶段，方向 IN
#define TRB_DIR_IN          (1 << 16)    // Data/Status TRB 的方向位（对 Status 则相反）
#define TRB_DIR             (1 << 16) // DIR for Data TRB

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

xhci_cap_t *xhci_cap_find(xhci_regs_t *xhci_reg,UINT8 cap_id) {
    UINT32 offset = xhci_reg->cap->hccparams1>>16;
    while (offset) {
        xhci_cap_t *xhci_cap = (void*)xhci_reg->cap + (offset<<2);
        if ((xhci_cap->cap_id&0xFF) == cap_id) return xhci_cap;
        offset = (xhci_cap->next_ptr>>8)&0xFF;
    }
    return NULL;
}

//响铃
static inline void xhci_ring_doorbell(xhci_regs_t *xhci_regs,UINT8 db_number,UINT32 value) {
    xhci_regs->db[db_number] = value;
}

//分配一个命令环trb
static inline xhci_trb_t *xhci_alloc_cmd_ring(xhci_regs_t *xhci_regs) {
    if (xhci_regs->cmd_idx >= TRB_COUNT-1) {
        xhci_regs->cmd_ring[TRB_COUNT-1].control ^= TRB_CYCLE;
        xhci_regs->cmd_idx =0;
    }
    xhci_trb_t *cmd_ring = &xhci_regs->cmd_ring[xhci_regs->cmd_idx];
    xhci_regs->cmd_idx++;
    return cmd_ring;
}

//写命令环
int xhci_write_cmd_ring(xhci_regs_t *xhci_regs,xhci_trb_t *cmd_trb) {
    xhci_trb_t *cmd_ring = xhci_alloc_cmd_ring(xhci_regs);
    cmd_ring->parameter1 = cmd_trb->parameter1;
    cmd_ring->parameter2 = cmd_trb->parameter2;
    cmd_ring->control = cmd_trb->control | (xhci_regs->cmd_ring[TRB_COUNT-1].control&TRB_CYCLE);
    return 0;
}

//读事件环
int xhci_read_evt_ring (xhci_regs_t *xhci_regs,xhci_trb_t *evt_trb) {
    xhci_trb_t *evt_ring = &xhci_regs->evt_ring[xhci_regs->evt_idx];
    evt_trb->parameter1 = evt_ring->parameter1;
    evt_trb->parameter2 = evt_ring->parameter2;
    evt_trb->control = evt_ring->control;
    if (xhci_regs->evt_idx >= TRB_COUNT-1) {
        xhci_regs->evt_idx = 0;
    }else {
        xhci_regs->evt_idx++;
    }
    xhci_regs->rt->intr_regs->erdp = va_to_pa(&xhci_regs->evt_ring[xhci_regs->evt_idx])|8;
    return 0;
}

//分配插槽
static inline UINT32 xhci_enable_slot(xhci_regs_t *xhci_regs) {
    xhci_trb_t trb ={
        0,
        0,
        TRB_ENABLE_SLOT
    };
    xhci_write_cmd_ring(xhci_regs, &trb);
    xhci_ring_doorbell(xhci_regs,0,0);
    xhci_read_evt_ring(xhci_regs, &trb);
    if ((trb.control >> 10 & 0x3F) == 33 && trb.control>>24) {
        return trb.control >> 24 & 0xFF;
    }
    return -1;
}

//设置设备地址
void xhci_address_device(xhci_regs_t *xhci_regs,UINT32 slot_number,UINT32 port_number) {
    //分配设备插槽上下文内存
    xhci_regs->dcbaap[slot_number] = va_to_pa(kzalloc(sizeof(xhci_device_context32_t)));

    //分配传输环内存
    xhci_trb_t *transfer_ring = kzalloc(TRB_COUNT * sizeof(xhci_trb_t));
    transfer_ring[TRB_COUNT-1].parameter1 = va_to_pa(transfer_ring);
    transfer_ring[TRB_COUNT-1].control = TRB_TYPE_LINK | TRB_TOGGLE_CYCLE;

    //配置设备上下文
    xhci_input_context32_t *input_context = kzalloc(sizeof(xhci_input_context32_t));
    input_context->add_context = 0x3;                                                       // 启用 Slot Context 和 Endpoint 0 Context
    input_context->drop_context = 0x0;
    input_context->dev_ctx.slot.reg0 = 1<<27;
    input_context->dev_ctx.slot.reg1 = port_number<<16;
    input_context->dev_ctx.ep[0].tr_dequeue_pointer = va_to_pa(transfer_ring)|TRB_CYCLE;
    input_context->dev_ctx.ep[0].reg1 = 4<<3 | 64<<16;

    xhci_trb_t trb ={
        va_to_pa(input_context),
        0,
        TRB_ADDRESS_DEVICE | slot_number<<24
    };
    xhci_write_cmd_ring(xhci_regs, &trb);
    xhci_ring_doorbell(xhci_regs,0,0);

    xhci_read_evt_ring(xhci_regs, &trb);

    kfree(input_context);
}

//获取设备描述符
int get_device_descriptor(xhci_regs_t *xhci_regs, UINT32 slot_number, void *buffer, UINT32 length) {
    usb_setup_packet_t *setup = kmalloc(sizeof(usb_setup_packet_t));
    setup->b_request_type = 0x80;
    setup->b_request = 0x06;
    setup->w_value = 0x100; // 允许传入，如 0x100 Device, 0x200 Config
    setup->w_index = 0x0;
    setup->w_length = length;

    xhci_device_context32_t *dev_ctx = pa_to_va(xhci_regs->dcbaap[slot_number]);
    xhci_trb_t *transfer_ring = pa_to_va(dev_ctx->ep[0].tr_dequeue_pointer & ~0xFULL);

    // 假设环从0开始，实际应跟踪 Enqueue Pointer
    transfer_ring[0].parameter1 = va_to_pa(setup);
    transfer_ring[0].parameter2 = 8;
    transfer_ring[0].control = TRB_TYPE_SETUP | TRB_CHAIN | TRB_CYCLE; // 加 Chain

    transfer_ring[1].parameter1 = va_to_pa(buffer);
    transfer_ring[1].parameter2 = length;
    transfer_ring[1].control = TRB_TYPE_DATA | TRB_CHAIN | TRB_DIR | TRB_CYCLE; // DIR=1 (IN), Chain

    transfer_ring[2].parameter1 = 0;
    transfer_ring[2].parameter2 = 0;
    transfer_ring[2].control = TRB_TYPE_STATUS | TRB_IOC | TRB_CYCLE; // 无 DIR for Status, 但对于 IN 传输 Status DIR=0

    // 触发门铃
    xhci_regs->db[slot_number] = 1;

}

INIT_TEXT void init_xhci(void) {
    pcie_dev_t *xhci_dev = find_pcie_dev(XHCI_CLASS_CODE);      //找xhci设备
    init_pcie_bar(xhci_dev,0);                                         //初始化bar0寄存器
    init_pcie_msi_intrpt(xhci_dev);                                       //初始化msi中断

    xhci_dev->private = kzalloc(sizeof(xhci_regs_t));                //设备私有数据空间申请一块内存，存放xhci相关信息
    xhci_regs_t *xhci_regs = xhci_dev->private;
    xhci_regs->cap = xhci_dev->bar[0];                                  //xhci能力寄存器基地址
    xhci_regs->op = xhci_dev->bar[0] + xhci_regs->cap->cap_length;      //xhci操作寄存器基地址
    xhci_regs->rt = xhci_dev->bar[0] + xhci_regs->cap->rtsoff;          //xhci运行时寄存器基地址
    xhci_regs->db = xhci_dev->bar[0] + xhci_regs->cap->dboff;           //xhci门铃寄存器基地址

    xhci_regs->op->usbcmd &= ~1;  //停止xhci
    while (!(xhci_regs->op->usbsts & 1)) pause();
    xhci_regs->op->usbcmd |= 2;  //复位xhci
    while (xhci_regs->op->usbcmd & 2) pause();
    while (xhci_regs->op->usbcmd & 0x800) pause();

    UINT32 max_slots = xhci_regs->cap->hcsparams1&0xff;
    xhci_regs->dcbaap = kzalloc(max_slots<<3);        //分配设备上下文插槽内存,最大插槽数量*8字节内存
    xhci_regs->op->dcbaap = va_to_pa(xhci_regs->dcbaap);  //把设备上下文基地址数组表的物理地址写入寄存器
    xhci_regs->op->config = max_slots;                    //把最大插槽数量写入寄存器

    xhci_regs->cmd_ring = kzalloc(TRB_COUNT*sizeof(xhci_trb_t));                            //分配命令环空间256* sizeof(xhci_trb_t) = 4K
    xhci_regs->cmd_ring[TRB_COUNT-1].parameter1 = va_to_pa(xhci_regs->cmd_ring);                //命令环最后一个trb指向环首地址
    xhci_regs->cmd_ring[TRB_COUNT-1].control = TRB_TYPE_LINK | TRB_TOGGLE_CYCLE |TRB_CYCLE;     //命令环最后一个trb设置位link
    xhci_regs->op->crcr = va_to_pa(xhci_regs->cmd_ring)|TRB_CYCLE;                              //命令环物理地址写入crcr寄存器，置位rcs

    xhci_erst_t *erstba =kmalloc(sizeof(xhci_erst_t));                          //分配单事件环段表内存64字节
    xhci_regs->evt_ring = kzalloc(TRB_COUNT*sizeof(xhci_trb_t));                //分配事件环空间256* sizeof(xhci_trb_t) = 4K
    erstba->ring_seg_base_addr = va_to_pa(xhci_regs->evt_ring);                     //段表中写入事件环物理地址
    erstba->ring_seg_size = TRB_COUNT;                                              //写入段表最大trb个数
    erstba->reserved = 0;
    xhci_regs->rt->intr_regs->erstsz = 1;                                           //设置单事件环段
    xhci_regs->rt->intr_regs->erstba = va_to_pa(erstba);                            //事件环段表物理地址写入寄存器
    xhci_regs->rt->intr_regs->erdp = va_to_pa(xhci_regs->evt_ring);                 //事件环物理地址写入寄存器

    xhci_regs->op->usbcmd |= 1; //启动xhci

    color_printk(GREEN,BLACK,"Xhci Version:%x BAR0 MMIO:%#lx MSI-X:%d MaxSlots:%d MaxIntrs:%d MaxPorts:%d CS:%d AC64:%d USBcmd:%#x USBsts:%#x PageSize:%d iman:%#x imod:%#x\n",xhci_regs->cap->hciversion,(UINT64)xhci_dev->pcie_config_space->type0.bar[0]&~0x1f|(UINT64)xhci_dev->pcie_config_space->type0.bar[1]<<32,xhci_dev->msi_x_flags,xhci_regs->cap->hcsparams1&0xFF,xhci_regs->cap->hcsparams1>>8&0x7FF,xhci_regs->cap->hcsparams1>>24,xhci_regs->cap->hccparams1>>2&1,xhci_regs->cap->hccparams1&1,xhci_regs->op->usbcmd,xhci_regs->op->usbsts,xhci_regs->op->pagesize<<12,xhci_regs->rt->intr_regs[0].iman,xhci_regs->rt->intr_regs[0].imod);
    color_printk(GREEN,BLACK,"crcr:%#lx dcbaap:%#lx erstba[0]:%#lx erdp[0]:%#lx erstsz:%d config:%d \n",xhci_regs->op->crcr,xhci_regs->op->dcbaap,xhci_regs->rt->intr_regs[0].erstba,xhci_regs->rt->intr_regs[0].erdp,xhci_regs->rt->intr_regs[0].erstsz,xhci_regs->op->config);

    //遍历端口，分配插槽和设备地址
    UINT32 slot_id;
    for (UINT32 i = 0; i < xhci_regs->cap->hcsparams1>>24; i++) {
        if (xhci_regs->op->portregs[i].portsc & 1) {
            slot_id = xhci_enable_slot(xhci_regs);
            xhci_address_device(xhci_regs,slot_id,i+1);
        }
    }

    usb_device_descriptor_t *dev_desc = kzalloc(sizeof(usb_device_descriptor_t));
    UINT8 *config_buf = kzalloc(256);
    get_device_descriptor(xhci_regs, slot_id, dev_desc, sizeof(dev_desc));
    get_device_descriptor(xhci_regs, slot_id, config_buf, 256);


    while (1);

}