#include "usb.h"
#include "slub.h"
#include "vmm.h"
#include "printk.h"
#include "pcie.h"

device_type_t usb_dev_type = {"usb-dev"};
device_type_t usb_if_type = {"usb-if"};

//获取usb设备描述符
int usb_get_device_descriptor(usb_dev_t *usb_dev) {
    xhci_controller_t *xhci_controller = usb_dev->xhci_controller;
    usb_device_descriptor_t *dev_desc = kzalloc(align_up(sizeof(usb_device_descriptor_t), 64));

    //第一次先获取设备描述符前8字节，拿到max_pack_size后更新端点1，再重新获取描述符。
    trb_t trb;
    // Setup TRB
    setup_stage_trb(&trb, setup_stage_device, setup_stage_norm, setup_stage_in, usb_req_get_descriptor, 0x100, 0, 8, 8,
                    in_data_stage);
    xhci_ring_enqueue(&usb_dev->control_ring, &trb);
    // Data TRB
    data_stage_trb(&trb, va_to_pa(dev_desc), 8, trb_in);
    xhci_ring_enqueue(&usb_dev->control_ring, &trb);
    // Status TRB
    status_stage_trb(&trb, enable_ioc, trb_out);
    xhci_ring_enqueue(&usb_dev->control_ring, &trb);

    xhci_ring_doorbell(xhci_controller, usb_dev->slot_id, 1);
    timing();
    xhci_ering_dequeue(xhci_controller, &trb);

    //更新端点0的最大包
    uint32 max_packe_size = dev_desc->usb_version >= 0x300
                                ? 1 << dev_desc->max_packet_size0
                                : dev_desc->max_packet_size0;
    xhci_input_context_t *input_ctx = kzalloc(align_up(sizeof(xhci_input_context_t), xhci_controller->align_size));
    ep64_t ep_ctx;
    xhci_context_read(usb_dev->dev_context, &ep_ctx,xhci_controller->dev_ctx_size, 1);
    ep_ctx.ep_type_size = EP_TYPE_CONTROL | max_packe_size << 16;
    xhci_input_context_add(input_ctx,&ep_ctx, xhci_controller->dev_ctx_size, 1);
    evaluate_context_com_trb(&trb, va_to_pa(input_ctx), usb_dev->slot_id);
    xhci_ring_enqueue(&xhci_controller->cmd_ring, &trb);
    xhci_ring_doorbell(xhci_controller, 0, 0);
    timing();
    xhci_ering_dequeue(xhci_controller, &trb);
    kfree(input_ctx);

    //第二次获取整个设备描述符
    setup_stage_trb(&trb, setup_stage_device, setup_stage_norm, setup_stage_in, usb_req_get_descriptor, 0x100, 0, 18, 8,
                    in_data_stage);
    xhci_ring_enqueue(&usb_dev->control_ring, &trb);
    // Data TRB
    data_stage_trb(&trb, va_to_pa(dev_desc), 18, trb_in);
    xhci_ring_enqueue(&usb_dev->control_ring, &trb);
    // Status TRB
    status_stage_trb(&trb, enable_ioc, trb_out);
    xhci_ring_enqueue(&usb_dev->control_ring, &trb);

    xhci_ring_doorbell(xhci_controller, usb_dev->slot_id, 1);
    timing();
    xhci_ering_dequeue(xhci_controller, &trb);

    usb_dev->usb_dev_desc = dev_desc;
    return 0;
}

//获取usb配置描述符
int usb_get_config_descriptor(usb_dev_t *usb_dev) {
    xhci_controller_t *xhci_controller = usb_dev->xhci_controller;
    usb_config_descriptor_t *config_desc = kzalloc(align_up(sizeof(usb_config_descriptor_t), 64));

    //第一次先获取配置描述符前9字节
    trb_t trb;
    setup_stage_trb(&trb, setup_stage_device, setup_stage_norm, setup_stage_in, usb_req_get_descriptor, 0x200, 0, 9, 8,
                    in_data_stage);
    xhci_ring_enqueue(&usb_dev->control_ring, &trb);
    // Data TRB
    data_stage_trb(&trb, va_to_pa(config_desc), 9, trb_in);
    xhci_ring_enqueue(&usb_dev->control_ring, &trb);
    // Status TRB
    status_stage_trb(&trb, enable_ioc, trb_out);
    xhci_ring_enqueue(&usb_dev->control_ring, &trb);

    // 响铃
    xhci_ring_doorbell(xhci_controller, usb_dev->slot_id, 1);
    timing();
    xhci_ering_dequeue(xhci_controller, &trb);

    //第二次从配置描述符中得到总长度获取整个配置描述符
    uint16 config_desc_length = config_desc->total_length;
    kfree(config_desc);
    config_desc = kzalloc(align_up(config_desc_length, 64));

    setup_stage_trb(&trb, setup_stage_device, setup_stage_norm, setup_stage_in, usb_req_get_descriptor, 0x200, 0,
                    config_desc_length, 8, in_data_stage);
    xhci_ring_enqueue(&usb_dev->control_ring, &trb);
    // Data TRB
    data_stage_trb(&trb, va_to_pa(config_desc), config_desc_length, trb_in);
    xhci_ring_enqueue(&usb_dev->control_ring, &trb);
    // Status TRB
    status_stage_trb(&trb, enable_ioc, trb_out);
    xhci_ring_enqueue(&usb_dev->control_ring, &trb);

    // 响铃
    xhci_ring_doorbell(xhci_controller, usb_dev->slot_id, 1);
    timing();
    xhci_ering_dequeue(xhci_controller, &trb);

    usb_dev->usb_config_desc = config_desc;
    return 0;
}

//激活usb配置
int usb_set_config(usb_dev_t *usb_dev) {
    xhci_controller_t *xhci_controller = usb_dev->xhci_controller;
    trb_t trb;

    setup_stage_trb(&trb, setup_stage_device, setup_stage_norm, setup_stage_out, usb_req_set_config,
                    usb_dev->usb_config_desc->configuration_value, 0, 0, 8, no_data_stage);
    xhci_ring_enqueue(&usb_dev->control_ring, &trb);

    status_stage_trb(&trb, enable_ioc, trb_in);
    xhci_ring_enqueue(&usb_dev->control_ring, &trb);

    xhci_ring_doorbell(xhci_controller, usb_dev->slot_id, 1);
    timing();
    xhci_ering_dequeue(xhci_controller, &trb);
    return 0;
}

//设置备用设置
int usb_set_interface(usb_dev_t *usb_dev, int64 if_num, int64 alt_num) {
    xhci_controller_t *xhci_controller = usb_dev->xhci_controller;
    trb_t trb;

    setup_stage_trb(&trb, setup_stage_interface, setup_stage_norm, setup_stage_out, usb_req_set_interface,
                    alt_num, if_num, 0, 8, no_data_stage);
    xhci_ring_enqueue(&usb_dev->control_ring, &trb);

    status_stage_trb(&trb, enable_ioc, trb_in);
    xhci_ring_enqueue(&usb_dev->control_ring, &trb);

    xhci_ring_doorbell(xhci_controller, usb_dev->slot_id, 1);
    timing();
    xhci_ering_dequeue(xhci_controller, &trb);
    color_printk(RED,BLACK, "set_if_trb m0:%#lx m1:%#lx   \n", trb.member0, trb.member1);
    return 0;
}


//创建usb设备
usb_dev_t *usb_dev_create(pcie_dev_t *xhci_dev, uint32 port_id) {
    usb_dev_t *usb_dev = kzalloc(sizeof(usb_dev_t));
    usb_dev->xhci_controller = xhci_dev->dev.drv_data;
    usb_dev->port_id = port_id + 1;
    usb_dev->slot_id = xhci_enable_slot(usb_dev); //启用插槽
    xhci_address_device(usb_dev); //设置设备地址
    usb_get_device_descriptor(usb_dev); //获取设备描述符
    usb_get_config_descriptor(usb_dev); //获取配置描述符
    usb_set_config(usb_dev); //激活配置
    usb_dev->dev.type = &usb_dev_type;
    usb_dev->dev.parent = &xhci_dev->dev;
    usb_dev->dev.bus = &usb_bus_type;
    return usb_dev;
}

//注册usb设备
void usb_dev_register(usb_dev_t *usb_dev) {
    device_register(&usb_dev->dev);
}

//usb设备初始化
void usb_dev_scan(pcie_dev_t *xhci_dev) {
    xhci_controller_t *xhci_controller = xhci_dev->dev.drv_data;
    trb_t trb;
    uint8 max_ports = xhci_controller->cap_reg->hcsparams1>>24; //支持的端口数
    for (uint32 i = 0; i < max_ports; i++) {
        if (xhci_controller->op_reg->portregs[i].portsc & XHCI_PORTSC_CCS) { //检测端口是否有设备
            if ((xhci_controller->op_reg->portregs[i].portsc>>XHCI_PORTSC_SPEED_SHIFT&XHCI_PORTSC_SPEED_MASK) < XHCI_PORTSC_SPEED_SUPER) {
                //usb2.0
                xhci_controller->op_reg->portregs[i].portsc |= XHCI_PORTSC_PR;
                timing();
                xhci_ering_dequeue(xhci_controller, &trb);
                }
            //usb3.x
            while (!(xhci_controller->op_reg->portregs[i].portsc & XHCI_PORTSC_PED)) pause();
            usb_dev_t *usb_dev = usb_dev_create(xhci_dev, i);
            usb_dev_register(usb_dev);
        }
    }
}