#include "xhci.h"
#include "moslib.h"
#include "apic.h"
#include "pcie.h"
#include "printk.h"
#include "slub.h"
#include "vmalloc.h"
#include "vmm.h"

#define TRB_COUNT 256        //trb个数

#define XHCI_CMD_RS (1 << 0)
#define XHCI_CMD_HCRST (1 << 1)
#define XHCI_CMD_INTE (1 << 2)
#define XHCI_CMD_HSEE (1 << 3)
#define XHCI_CMD_LHCRST (1 << 7)
#define XHCI_CMD_CSS (1 << 8)
#define XHCI_CMD_CRS (1 << 9)
#define XHCI_CMD_EWE (1 << 10)
#define XHCI_CMD_EU3S (1 << 11)

#define XHCI_STS_HCH (1 << 0)
#define XHCI_STS_HSE (1 << 2)
#define XHCI_STS_EINT (1 << 3)
#define XHCI_STS_PCD (1 << 4)
#define XHCI_STS_SSS (1 << 8)
#define XHCI_STS_RSS (1 << 9)
#define XHCI_STS_SRE (1 << 10)
#define XHCI_STS_CNR (1 << 11)
#define XHCI_STS_HCE (1 << 12)

#define XHCI_PORTSC_CCS (1 << 0)
#define XHCI_PORTSC_PED (1 << 1)
#define XHCI_PORTSC_OCA (1 << 3)
#define XHCI_PORTSC_PR (1 << 4)
#define XHCI_PORTSC_PLS_SHIFT 5
#define XHCI_PORTSC_PLS_MASK 0xf
#define XHCI_PORTSC_PP (1 << 9)
#define XHCI_PORTSC_SPEED_SHIFT 10
#define XHCI_PORTSC_SPEED_MASK 0xf
#define XHCI_PORTSC_SPEED_FULL (1 << 10)
#define XHCI_PORTSC_SPEED_LOW (2 << 10)
#define XHCI_PORTSC_SPEED_HIGH (3 << 10)
#define XHCI_PORTSC_SPEED_SUPER (4 << 10)
#define XHCI_PORTSC_PIC_SHIFT 14
#define XHCI_PORTSC_PIC_MASK 0x3
#define XHCI_PORTSC_LWS (1 << 16)
#define XHCI_PORTSC_CSC (1 << 17)
#define XHCI_PORTSC_PEC (1 << 18)
#define XHCI_PORTSC_WRC (1 << 19)
#define XHCI_PORTSC_OCC (1 << 20)
#define XHCI_PORTSC_PRC (1 << 21)
#define XHCI_PORTSC_PLC (1 << 22)
#define XHCI_PORTSC_CEC (1 << 23)
#define XHCI_PORTSC_CAS (1 << 24)
#define XHCI_PORTSC_WCE (1 << 25)
#define XHCI_PORTSC_WDE (1 << 26)
#define XHCI_PORTSC_WOE (1 << 27)
#define XHCI_PORTSC_DR (1 << 30)
#define XHCI_PORTSC_WPR (1 << 31)

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
    cmd_ring->parameter = cmd_trb->parameter;
    cmd_ring->status = cmd_trb->status;
    cmd_ring->control = cmd_trb->control | (xhci_regs->cmd_ring[TRB_COUNT-1].control&TRB_CYCLE);
    return 0;
}

//读事件环
int xhci_read_evt_ring (xhci_regs_t *xhci_regs,xhci_trb_t *evt_trb) {
    xhci_trb_t *evt_ring = &xhci_regs->evt_ring[xhci_regs->evt_idx];
    evt_trb->parameter = evt_ring->parameter;
    evt_trb->status = evt_ring->status;
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
    transfer_ring[TRB_COUNT-1].parameter = va_to_pa(transfer_ring);
    transfer_ring[TRB_COUNT-1].control = TRB_TYPE_LINK | TRB_TOGGLE_CYCLE | TRB_CYCLE;

    //配置设备上下文
    xhci_input_context32_t *input_context = kzalloc(sizeof(xhci_input_context32_t));
    input_context->add_context = 0x3;                                                       // 启用 Slot Context 和 Endpoint 0 Context
    input_context->drop_context = 0x0;
    input_context->dev_ctx.slot.reg0 = 1<<27;
    input_context->dev_ctx.slot.reg1 = port_number<<16;
    UINT64 tr = va_to_pa(transfer_ring)|TRB_CYCLE;
    color_printk(GREEN,BLACK,"tr:%lx\n",tr);
    input_context->dev_ctx.ep[0].tr_dequeue_pointer = tr;
    //input_context->dev_ctx.ep[0].tr_dequeue_pointer = va_to_pa(transfer_ring)|TRB_CYCLE;
    //input_context->dev_ctx.ep[0].reg0 = 1;
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
int get_device_descriptor(xhci_regs_t *xhci_regs, UINT32 slot_number) {
    usb_device_descriptor_t *dev_desc = kzalloc(sizeof(usb_device_descriptor_t));
    xhci_device_context32_t *dev_ctx = pa_to_va(xhci_regs->dcbaap[slot_number]);
    xhci_trb_t *transfer_ring = pa_to_va(dev_ctx->ep[0].tr_dequeue_pointer & ~0xFULL);

    // Setup TRB
    usb_setup_packet_t setup = {0x80, 0x06, 0x0100, 0x0000, 8};  // 统一为8
    color_printk(GREEN,BLACK,"transfer_ring:%lx\n",transfer_ring);
    transfer_ring[0].parameter = *(UINT64*)&setup;  // 完整 8 字节
    while (1);
    transfer_ring[0].status = 8;  // TRB Length=8 (Setup 阶段长度)
    transfer_ring[0].control = TRB_TYPE_SETUP | TRB_IDT | (3 << 16) | TRB_CHAIN | TRB_IOC | TRB_CYCLE;  // TRT=3 (IN), Chain, IO

    // Data TRB
    transfer_ring[1].parameter = va_to_pa(dev_desc);
    transfer_ring[1].status = 8;  // 匹配 w_length
    transfer_ring[1].control = TRB_TYPE_DATA | (1 << 16) | TRB_CHAIN | TRB_IOC | TRB_CYCLE;

    // Status TRB
    transfer_ring[2].parameter = 0;
    transfer_ring[2].status = 0;
    transfer_ring[2].control = TRB_TYPE_STATUS | TRB_IOC | TRB_CYCLE;

    // 响铃
    xhci_ring_doorbell(xhci_regs,slot_number,1);

    color_printk(GREEN,BLACK,"bcd_usb:%x id_v:%x id_p:%x\n",dev_desc->bcdUSB,dev_desc->idVendor,dev_desc->idProduct);

    while (1);
}

INIT_TEXT void init_xhci(void) {
    pcie_dev_t *xhci_dev = pcie_dev_find(XHCI_CLASS_CODE);      //找xhci设备
    pcie_bar_set(xhci_dev,0);                                         //初始化bar0寄存器
    pcie_msi_intrpt_set(xhci_dev);                                       //初始化msi中断

    xhci_dev->private = kzalloc(sizeof(xhci_regs_t));                //设备私有数据空间申请一块内存，存放xhci相关信息
    xhci_regs_t *xhci_regs = xhci_dev->private;
    xhci_regs->cap = xhci_dev->bar[0];                                  //xhci能力寄存器基地址
    xhci_regs->op = xhci_dev->bar[0] + xhci_regs->cap->cap_length;      //xhci操作寄存器基地址
    xhci_regs->rt = xhci_dev->bar[0] + xhci_regs->cap->rtsoff;          //xhci运行时寄存器基地址
    xhci_regs->db = xhci_dev->bar[0] + xhci_regs->cap->dboff;           //xhci门铃寄存器基地址

    xhci_regs->op->usbcmd &= ~XHCI_CMD_RS;  //停止xhci
    while (!(xhci_regs->op->usbsts & XHCI_STS_HCH)) pause();
    xhci_regs->op->usbcmd |= XHCI_CMD_HCRST;  //复位xhci
    while (xhci_regs->op->usbcmd & XHCI_CMD_HCRST) pause();
    while (xhci_regs->op->usbcmd & XHCI_CMD_EU3S) pause();

    UINT32 max_slots = xhci_regs->cap->hcsparams1&0xff;
    xhci_regs->dcbaap = kzalloc(max_slots<<3);        //分配设备上下文插槽内存,最大插槽数量*8字节内存
    xhci_regs->op->dcbaap = va_to_pa(xhci_regs->dcbaap);  //把设备上下文基地址数组表的物理地址写入寄存器
    xhci_regs->op->config = max_slots;                    //把最大插槽数量写入寄存器

    xhci_regs->cmd_ring = kzalloc(TRB_COUNT*sizeof(xhci_trb_t));                            //分配命令环空间256* sizeof(xhci_trb_t) = 4K
    xhci_regs->cmd_ring[TRB_COUNT-1].parameter = va_to_pa(xhci_regs->cmd_ring);                //命令环最后一个trb指向环首地址
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

    xhci_regs->op->usbcmd |= XHCI_CMD_RS; //启动xhci

    color_printk(GREEN,BLACK,"Xhci Version:%x BAR0 MMIO:%#lx MSI-X:%d MaxSlots:%d MaxIntrs:%d MaxPorts:%d CS:%d AC64:%d USBcmd:%#x USBsts:%#x PageSize:%d iman:%#x imod:%#x\n",xhci_regs->cap->hciversion,(UINT64)xhci_dev->pcie_config_space->type0.bar[0]&~0x1f|(UINT64)xhci_dev->pcie_config_space->type0.bar[1]<<32,xhci_dev->msi_x_flags,xhci_regs->cap->hcsparams1&0xFF,xhci_regs->cap->hcsparams1>>8&0x7FF,xhci_regs->cap->hcsparams1>>24,xhci_regs->cap->hccparams1>>2&1,xhci_regs->cap->hccparams1&1,xhci_regs->op->usbcmd,xhci_regs->op->usbsts,xhci_regs->op->pagesize<<12,xhci_regs->rt->intr_regs[0].iman,xhci_regs->rt->intr_regs[0].imod);
    color_printk(GREEN,BLACK,"crcr:%#lx dcbaap:%#lx erstba[0]:%#lx erdp[0]:%#lx erstsz:%d config:%d \n",xhci_regs->op->crcr,xhci_regs->op->dcbaap,xhci_regs->rt->intr_regs[0].erstba,xhci_regs->rt->intr_regs[0].erdp,xhci_regs->rt->intr_regs[0].erstsz,xhci_regs->op->config);

    xhci_cap_t *supported_protocol = xhci_cap_find(xhci_regs,2);
    color_printk(GREEN,BLACK,"USB%x.%x port_info:%#x port_slot_type:%#x\n",supported_protocol->supported_protocol.protocol_ver>>24,supported_protocol->supported_protocol.protocol_ver>>16&0xFF,supported_protocol->supported_protocol.port_info,supported_protocol->supported_protocol.protocol_slot_type);
    for (UINT32 i=0;i < supported_protocol->supported_protocol.port_info>>28;i++) {
        color_printk(GREEN,BLACK,"pro_speed:%#x\n",supported_protocol->supported_protocol.protocol_speed[i]);
    }

    UINT32 i = 0;
    UINT64 j = 0;
    while (TRUE) {
        if ((xhci_regs->op->portregs[i].portsc & 0xE1) == 0xE1) {
            color_printk(GREEN,BLACK,"port_id:%d portsc:%x portpmsc:%x portli:%x porthlpmc:%x j:%ld \n",i+1,xhci_regs->op->portregs[i].portsc,xhci_regs->op->portregs[i].portpmsc,xhci_regs->op->portregs[i].portli,xhci_regs->op->portregs[i].porthlpmc,j);
            UINT32 portsc = xhci_regs->op->portregs[i].portsc;
            portsc &= ~(0xF<<10);
            xhci_regs->op->portregs[i].portsc = portsc;
            while (!(xhci_regs->op->portregs[i].portsc == 1<<1)) pause();
            color_printk(GREEN,BLACK,"port_id:%d portsc:%x portpmsc:%x portli:%x porthlpmc:%x j:%ld \n",i+1,xhci_regs->op->portregs[i].portsc,xhci_regs->op->portregs[i].portpmsc,xhci_regs->op->portregs[i].portli,xhci_regs->op->portregs[i].porthlpmc,j);
            break;
        }
        i++;
        if (i == xhci_regs->cap->hcsparams1>>24) i = 0;
        j++;
    }

    //遍历端口，分配插槽和设备地址
    /*UINT32 slot_id;
    for (UINT32 i = 0; i < xhci_regs->cap->hcsparams1>>24; i++) {
        color_printk(GREEN,BLACK,"port_id:%d portsc:%x portpmsc:%x portli:%x porthlpmc:%x \n",i,xhci_regs->op->portregs[i].portsc,xhci_regs->op->portregs[i].portpmsc,xhci_regs->op->portregs[i].portli,xhci_regs->op->portregs[i].porthlpmc);
        if (xhci_regs->op->portregs[i].portsc & 1) {
            color_printk(GREEN,BLACK,"port_id:%d portsc:%x portpmsc:%x portli:%x porthlpmc:%x \n",i,xhci_regs->op->portregs[i].portsc,xhci_regs->op->portregs[i].portpmsc,xhci_regs->op->portregs[i].portli,xhci_regs->op->portregs[i].porthlpmc);
            // slot_id = xhci_enable_slot(xhci_regs);
            // xhci_address_device(xhci_regs,slot_id,i+1);
            color_printk(GREEN,BLACK,"port:%d slot_id:%d\n",i+1,slot_id);
            break;
        }
        if (i == (xhci_regs->cap->hcsparams1>>24)-1) i = 0;
    }*/


    while (1);

}