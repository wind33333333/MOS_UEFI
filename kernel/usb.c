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

//注册usb接口
static inline void usb_if_register(usb_if_t* usb_if) {
    device_register(&usb_if->dev);
}

//注册usb设备
static inline void usb_dev_register(usb_dev_t *usb_dev) {
    device_register(&usb_dev->dev);
}

//匹配驱动id
static inline usb_id_t *usb_match_id(usb_if_t *usb_if,driver_t *drv) {
    usb_id_t *id_table = drv->id_table;
    for (;id_table->if_class || id_table->if_protocol || id_table->if_subclass;id_table++) {
        if (id_table->if_class==usb_if->cur_alt->if_class && id_table->if_protocol == usb_if->cur_alt->if_protocol && id_table->if_subclass==usb_if->cur_alt->if_subclass)
            return id_table;
    }
    return NULL;
}

//usb设备驱动匹配程序
int usb_bus_match(device_t* dev,driver_t* drv) {
    if (dev->type != &usb_if_type) return FALSE;
    usb_if_t* usb_if = CONTAINER_OF(dev,usb_if_t,dev);
    usb_id_t *id = usb_match_id(usb_if,drv);
    return id ? 1 : 0;
}

//usb设备探测初始化程序
int usb_bus_probe(device_t* dev) {

}

//usb设备删除程序
void usb_bus_remove(device_t* dev) {

}

//usb接口创建并注册总线
int usb_if_create_register(usb_dev_t *usb_dev) {
    uint8 alt_count[256];      //每个接口的替用接口数量
    usb_if_t *usb_if_map[256]; //usb_if临时缓存区
    uint8 fill_idx[256];       //下一个alts计数

    mem_set(alt_count, 0, sizeof(alt_count));
    mem_set(usb_if_map, 0, sizeof(usb_if_map));
    mem_set(fill_idx, 0, sizeof(fill_idx));

    //给接口分配内存
    usb_dev->interfaces_count = 0;
    usb_dev->interfaces = kzalloc(sizeof(usb_if_t)*usb_dev->usb_config_desc->num_interfaces);

    //统计每个接口的替用接口数量
    usb_interface_descriptor_t* if_desc = (usb_interface_descriptor_t*)usb_dev->usb_config_desc;
    void* cfg_end = usb_cfg_end(usb_dev->usb_config_desc);
    while (if_desc < cfg_end) {
        if (if_desc->head.descriptor_type == USB_INTERFACE_DESCRIPTOR) {
            alt_count[if_desc->interface_number]++;
        }
        if_desc = get_next_desc(&if_desc->head);
    }

    //遍历alt_cout创建usb_if 和 usb_if_alt结构
    for (uint32 i = 0; i < 256; i++) {
        if (alt_count[i]) {
            usb_if_t *usb_if = &usb_dev->interfaces[usb_dev->interfaces_count++];
            usb_if->if_num = i;
            usb_if->alt_count = alt_count[i];
            usb_if->alts = kzalloc(sizeof(usb_if_alt_t)*usb_if->alt_count);
            usb_if->usb_dev = usb_dev;
            usb_if->dev.type = &usb_if_type;
            usb_if->dev.parent = &usb_dev->dev;
            usb_if->dev.bus = &usb_bus_type;
            usb_if_map[i] = usb_if;     //把usb_if缓存在usb_if_map中
        }
    }

    //填充每个usb_if_alt
    if_desc = (usb_interface_descriptor_t*)usb_dev->usb_config_desc;
    while (if_desc < cfg_end) {
        if (if_desc->head.descriptor_type == USB_INTERFACE_DESCRIPTOR) {
            usb_if_t *usb_if = usb_if_map[if_desc->interface_number];
            uint8 idx = fill_idx[if_desc->interface_number]++;
            usb_if_alt_t *if_alt = &usb_if->alts[idx];
            if_alt->if_desc     = if_desc;
            if_alt->altsetting  = if_desc->alternate_setting;
            if_alt->if_class    = if_desc->interface_class;
            if_alt->if_subclass = if_desc->interface_subclass;
            if_alt->if_protocol = if_desc->interface_protocol;
            /* 可选：此处不解析端点，延后到 probe；或预解析以便 match/probe 快速使用 */
            /* usb_parse_alt_endpoints(usb_dev, alt); */
        }
        if_desc = get_next_desc(&if_desc->head);
    }

    /* 设置 cur_alt（优先 alt0，否则第一个），然后延迟注册（触发 match/probe） */
    for (uint32 i = 0; i < usb_dev->interfaces_count; i++) {
        usb_if_t *usb_if = &usb_dev->interfaces[i];
        if (usb_if) {
            usb_if_alt_t *alt0 = usb_find_alt_by_num(usb_if, 0);
            usb_if->cur_alt = alt0 ? alt0 : &usb_if->alts[0];

            /* 可选：只解析当前 alt 的端点，保证驱动 probe 一上来就能拿到 ep_count */
            /* usb_parse_alt_endpoints(usb_dev, uif->cur_alt); */

            /* 延迟注册：到这里 uif/alt 数据已完整 */
            usb_if_register(usb_if);
        }
    }
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
            usb_if_create_register(usb_dev);
        }
    }
}