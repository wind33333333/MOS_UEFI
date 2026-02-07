#include "xhci.h"
#include "printk.h"
#include "pcie.h"
#include "slub.h"
#include "vmm.h"
#include "driver.h"
#include "vmalloc.h"
#include "usb.h"

//写入input上文
void xhci_input_context_add(xhci_input_context_t *input_ctx,void *from_ctx, uint32 ctx_size, uint32 ep_num) {
    void* to_ctx = (uint8*)input_ctx + ctx_size * (ep_num + 1);
    asm_mem_cpy(from_ctx,to_ctx,ctx_size);
    input_ctx->input_ctx32.control.add_context |= 1 << ep_num;
}

//读取input上下文
void xhci_context_read(xhci_device_context_t *dev_context,void* to_ctx,uint32 ctx_size, uint32 ep_num) {
    void* from_ctx = (uint8*) dev_context + ctx_size * ep_num;
    asm_mem_cpy(from_ctx,to_ctx,ctx_size);
}

//命令环/传输环入队列
int xhci_ring_enqueue(xhci_ring_t *ring, trb_t *trb) {
    if (ring->index >= TRB_COUNT - 1) {
        link_trb(&ring->ring_base[TRB_COUNT - 1], va_to_pa(ring->ring_base), ring->status_c);
        ring->index = 0;
        ring->status_c ^= TRB_FLAG_CYCLE;
    }

    ring->ring_base[ring->index].member0 = trb->member0;
    ring->ring_base[ring->index].member1 = trb->member1 | ring->status_c;
    ring->index++;
    return 0;
}

//事件环出队列
int xhci_ering_dequeue(xhci_controller_t *xhci_controller, trb_t *evt_trb) {
    xhci_ring_t *event_ring = &xhci_controller->event_ring;
    while ((event_ring->ring_base[event_ring->index].member1 & TRB_FLAG_CYCLE) == event_ring->status_c) {
        evt_trb->member0 = event_ring->ring_base[event_ring->index].member0;
        evt_trb->member1 = event_ring->ring_base[event_ring->index].member1;
        event_ring->index++;
        if (event_ring->index >= TRB_COUNT) {
            event_ring->index = 0;
            event_ring->status_c ^= TRB_FLAG_CYCLE;
        }
        xhci_controller->rt_reg->intr_regs[0].erdp =
                va_to_pa(&event_ring->ring_base[event_ring->index]) | XHCI_ERDP_EHB;
    }
    return 0;
}

//分配插槽
uint8 xhci_enable_slot(usb_dev_t *usb_dev) {
    trb_t trb;
    enable_slot_com_trb(&trb);
    xhci_ring_enqueue(&usb_dev->xhci_controller->cmd_ring, &trb);
    xhci_ring_doorbell(usb_dev->xhci_controller, 0, 0);
    timing();
    xhci_ering_dequeue(usb_dev->xhci_controller, &trb);
    if ((trb.member1 >> 42 & 0x3F) == 33 && trb.member1 >> 56) {
        return trb.member1 >> 56;
    }
    return -1;
}

//设置设备地址
void xhci_address_device(usb_dev_t *usb_dev) {
    xhci_controller_t *xhci_controller = usb_dev->xhci_controller;
    //分配设备插槽上下文内存
    usb_dev->dev_context = kzalloc(align_up(sizeof(xhci_device_context_t), xhci_controller->align_size));
    xhci_controller->dcbaap[usb_dev->slot_id] = va_to_pa(usb_dev->dev_context);
    //初始化控制
    xhci_ring_init(&usb_dev->ep0, xhci_controller->align_size);
    //配置设备上下文
    xhci_input_context_t *input_ctx = kzalloc(align_up(sizeof(xhci_input_context_t), xhci_controller->align_size));
    slot64_t slot_ctx = {0};
    slot_ctx.route_speed = 1 << 27 | (xhci_controller->op_reg->portregs[usb_dev->port_id - 1].portsc & 0x3C00) << 10;
    slot_ctx.latency_hub = usb_dev->port_id << 16;
    slot_ctx.parent_info = 0;
    slot_ctx.addr_status = 0;
    xhci_input_context_add(input_ctx, &slot_ctx,xhci_controller->dev_ctx_size,0); // 启用 Slot Context

    ep64_t ep_ctx = {0};
    ep_ctx.ep_config = 0;
    ep_ctx.ep_type_size = 4 << 3 | 8 << 16 | 3 << 1;
    ep_ctx.tr_dequeue_ptr = va_to_pa(usb_dev->ep0.ring_base) | 1;
    ep_ctx.trb_payload = 0;
    xhci_input_context_add(input_ctx, &ep_ctx,xhci_controller->dev_ctx_size, 1); //Endpoint 0 Context

    trb_t trb;
    addr_dev_com_trb(&trb, va_to_pa(input_ctx), usb_dev->slot_id);
    xhci_ring_enqueue(&xhci_controller->cmd_ring, &trb);
    xhci_ring_doorbell(xhci_controller, 0, 0);
    timing();
    xhci_ering_dequeue(xhci_controller, &trb);
    kfree(input_ctx);
}

//xhic扩展能力搜索
uint8 xhci_ecap_find(xhci_controller_t *xhci_controller,void **ecap_arr,uint8 cap_id) {
    uint32 offset = xhci_controller->cap_reg->hccparams1 >> 16;
    uint32 *ecap = (uint32*) xhci_controller->cap_reg;
    uint8 count = 0;
    while (offset) {
        ecap = (void*)ecap + (offset << 2);
        if ((*ecap & 0xFF) == cap_id) {
            ecap_arr[count++] = ecap;
        };
        offset = (*ecap >> 8) & 0xFF;
    }
    return count;
}

//xhci设备探测初始化驱动
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
    while (!(xhci_controller->op_reg->usbsts & XHCI_STS_HCH)) asm_pause();
    xhci_controller->op_reg->usbcmd |= XHCI_CMD_HCRST; //复位xhci
    while (xhci_controller->op_reg->usbcmd & XHCI_CMD_HCRST) asm_pause();
    while (xhci_controller->op_reg->usbsts & XHCI_STS_CNR) asm_pause();

    /*计算xhci内存对齐边界*/
    xhci_controller->align_size = PAGE_4K_SIZE << asm_tzcnt(xhci_controller->op_reg->pagesize);

    /*设备上下文字节数*/
    xhci_controller->dev_ctx_size = 32 << ((xhci_controller->cap_reg->hccparams1 & HCCP1_CSZ) >> 2);

    /*初始化设备上下文*/
    xhci_controller->max_slots = xhci_controller->cap_reg->hcsparams1 & 0xff;
    xhci_controller->dcbaap = kzalloc(align_up((xhci_controller->max_slots + 1) << 3, xhci_controller->align_size));
    //分配设备上下文插槽内存,最大插槽数量(插槽从1开始需要+1)*8字节内存
    xhci_controller->op_reg->dcbaap = va_to_pa(xhci_controller->dcbaap); //把设备上下文基地址数组表的物理地址写入寄存器
    xhci_controller->op_reg->config = xhci_controller->max_slots; //把最大插槽数量写入寄存器

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

    xhci_controller->major_bcd = xhci_controller->cap_reg->hciversion >> 8;          //xhci主版本
    xhci_controller->minor_bcd = xhci_controller->cap_reg->hciversion & 0xFF;        //xhci次版本
    xhci_controller->max_ports = xhci_controller->cap_reg->hcsparams1 >> 24;         //xhci最大端口数
    xhci_controller->max_intrs = xhci_controller->cap_reg->hcsparams1 >> 8 & 0x7FF;  //xhci最大中断数

    /*获取协议支持能力*/
    xhci_ecap_supported_protocol *ecap_spc_arr[16];
    xhci_controller->spc_count = xhci_ecap_find(xhci_controller,ecap_spc_arr, 2);
    xhci_controller->spc = kzalloc(sizeof(xhci_spc_t)*xhci_controller->spc_count);
    xhci_controller->port_to_spc = kmalloc(xhci_controller->max_ports);
    asm_mem_set(xhci_controller->port_to_spc, 0xFF, xhci_controller->max_ports);
    for (uint8 i = 0; i < xhci_controller->spc_count; i++) {
        xhci_spc_t *spc = &xhci_controller->spc[i];
        xhci_ecap_supported_protocol *spc_ecap = ecap_spc_arr[i];
        spc->major_bcd = spc_ecap->protocol_ver >> 24;
        spc->minor_bcd = spc_ecap->protocol_ver >> 16 & 0xFF;
        *(uint32*)spc->name = *(uint32*)spc_ecap->name;
        spc->name[3] = 0;
        spc->proto_defined = spc_ecap->port_info >> 16 & 0x1FF;
        spc->port_first = spc_ecap->port_info & 0xFF;
        spc->port_count = spc_ecap->port_info >> 8 & 0xFF;
        spc->slot_type = spc_ecap->protocol_slot_type & 0xF;
        spc->psi_count = spc_ecap->port_info >> 28 & 0xF;
        if (spc->psi_count) {
            uint32 *psi = kzalloc(sizeof(uint32)*spc->psi_count);
            spc->psi = psi;
            for (uint8 j = 0; j < spc->psi_count; j++) {
                psi[j] = spc_ecap->protocol_speed[j];
            }
        }
        for (uint8 j = spc->port_first-1; j < spc->port_first-1 + spc->port_count; j++) {
            xhci_controller->port_to_spc[j] = i;
        }
    }


    color_printk(GREEN,BLACK,"XHCI Version:%x.%x MaxSlots:%d MaxIntrs:%d MaxPorts:%d Dev_Ctx_Size:%d AlignSize:%d USBcmd:%#x USBsts:%#x    \n",
        xhci_controller->major_bcd, xhci_controller->minor_bcd,xhci_controller->max_slots,xhci_controller->max_intrs,xhci_controller->max_ports,
        xhci_controller->dev_ctx_size,xhci_controller->align_size,xhci_controller->op_reg->usbcmd, xhci_controller->op_reg->usbsts);

    for (uint8 i = 0; i < xhci_controller->spc_count; i++) {
        xhci_spc_t *spc = &xhci_controller->spc[i];
        color_printk(GREEN,BLACK,"spc%d %s%x.%x port_first:%d port_count:%d psi_count:%d    \n",i,spc->name,spc->major_bcd,spc->minor_bcd,spc->port_first,spc->port_count,spc->psi_count);
    }

    /*启动xhci*/
    xhci_controller->op_reg->usbcmd |= XHCI_CMD_RS;

    timing();

    usb_dev_scan(xhci_dev);

    color_printk(GREEN,BLACK, "\nUSBcmd:%#x  USBsts:%#x", xhci_controller->op_reg->usbcmd,
                 xhci_controller->op_reg->usbsts);

}

void xhci_remove(pcie_dev_t *xhci_dev) {

}

//xhci驱动初始化
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
