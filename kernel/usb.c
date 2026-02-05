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
    xhci_ring_enqueue(&usb_dev->ep0, &trb);
    // Data TRB
    data_stage_trb(&trb, va_to_pa(dev_desc), 8, trb_in);
    xhci_ring_enqueue(&usb_dev->ep0, &trb);
    // Status TRB
    status_stage_trb(&trb, enable_ioc, trb_out);
    xhci_ring_enqueue(&usb_dev->ep0, &trb);

    xhci_ring_doorbell(xhci_controller, usb_dev->slot_id, 1);
    timing();
    xhci_ering_dequeue(xhci_controller, &trb);

    //更新端点0的最大包
    uint32 max_packe_size = dev_desc->usb_version >= 0x300
                                ? 1 << dev_desc->max_packet_size0
                                : dev_desc->max_packet_size0;
    xhci_input_context_t *input_ctx = kzalloc(align_up(sizeof(xhci_input_context_t), xhci_controller->align_size));
    ep64_t ep_ctx;
    xhci_context_read(usb_dev->dev_context, &ep_ctx, xhci_controller->dev_ctx_size, 1);
    ep_ctx.ep_type_size = 4 << 3 | max_packe_size << 16 | 3 << 1;
    xhci_input_context_add(input_ctx, &ep_ctx, xhci_controller->dev_ctx_size, 1);
    evaluate_context_com_trb(&trb, va_to_pa(input_ctx), usb_dev->slot_id);
    xhci_ring_enqueue(&xhci_controller->cmd_ring, &trb);
    xhci_ring_doorbell(xhci_controller, 0, 0);
    timing();
    xhci_ering_dequeue(xhci_controller, &trb);
    kfree(input_ctx);

    //第二次获取整个设备描述符
    setup_stage_trb(&trb, setup_stage_device, setup_stage_norm, setup_stage_in, usb_req_get_descriptor, 0x100, 0,
                    sizeof(usb_device_descriptor_t), 8,
                    in_data_stage);
    xhci_ring_enqueue(&usb_dev->ep0, &trb);
    // Data TRB
    data_stage_trb(&trb, va_to_pa(dev_desc), sizeof(usb_device_descriptor_t), trb_in);
    xhci_ring_enqueue(&usb_dev->ep0, &trb);
    // Status TRB
    status_stage_trb(&trb, enable_ioc, trb_out);
    xhci_ring_enqueue(&usb_dev->ep0, &trb);

    xhci_ring_doorbell(xhci_controller, usb_dev->slot_id, 1);
    timing();
    xhci_ering_dequeue(xhci_controller, &trb);

    usb_dev->dev_desc = dev_desc;
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
    xhci_ring_enqueue(&usb_dev->ep0, &trb);
    // Data TRB
    data_stage_trb(&trb, va_to_pa(config_desc), 9, trb_in);
    xhci_ring_enqueue(&usb_dev->ep0, &trb);
    // Status TRB
    status_stage_trb(&trb, enable_ioc, trb_out);
    xhci_ring_enqueue(&usb_dev->ep0, &trb);

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
    xhci_ring_enqueue(&usb_dev->ep0, &trb);
    // Data TRB
    data_stage_trb(&trb, va_to_pa(config_desc), config_desc_length, trb_in);
    xhci_ring_enqueue(&usb_dev->ep0, &trb);
    // Status TRB
    status_stage_trb(&trb, enable_ioc, trb_out);
    xhci_ring_enqueue(&usb_dev->ep0, &trb);

    // 响铃
    xhci_ring_doorbell(xhci_controller, usb_dev->slot_id, 1);
    timing();
    xhci_ering_dequeue(xhci_controller, &trb);

    usb_dev->config_desc = config_desc;
    return 0;
}

//获取字符串描述符
int usb_get_string_descriptor(usb_dev_t *usb_dev) {
    xhci_controller_t *xhci_controller = usb_dev->xhci_controller;
    trb_t trb;

    usb_string_descriptor_t *language_desc = kzalloc(8);
    //获取语言ID描述符
    setup_stage_trb(&trb, setup_stage_device, setup_stage_norm, setup_stage_in, usb_req_get_descriptor, 0x300, 0, 8, 8,
                    in_data_stage);
    xhci_ring_enqueue(&usb_dev->ep0, &trb);
    // Data TRB
    data_stage_trb(&trb, va_to_pa(language_desc), 8, trb_in);
    xhci_ring_enqueue(&usb_dev->ep0, &trb);
    // Status TRB
    status_stage_trb(&trb, enable_ioc, trb_out);
    xhci_ring_enqueue(&usb_dev->ep0, &trb);

    // 响铃
    xhci_ring_doorbell(xhci_controller, usb_dev->slot_id, 1);
    timing();
    xhci_ering_dequeue(xhci_controller, &trb);

    uint16 language_id;
    if (language_desc->head.descriptor_type == USB_STRING_DESCRIPTOR) {
        language_id = language_desc->string[0];
        usb_dev->language_desc = language_desc;
    }else {
        language_id = 0x0409;
        kfree(language_desc);
    }

    //默认设备都支持美式英语
    uint8 string_index[3] = {
        usb_dev->dev_desc->manufacturer_index, usb_dev->dev_desc->product_index,
        usb_dev->dev_desc->serial_number_index
    };
    usb_string_descriptor_t *string_desc[3];
    uint8 *string_ascii[3];
    usb_string_descriptor_t *string_desc_head = kzalloc(8);

    //获取制造商/产品型号/序列号字符串描述符
    for (uint8 i = 0; i < 3; i++) {
        if (string_index[i]) {
            //第一次先获取长度
            setup_stage_trb(&trb, setup_stage_device, setup_stage_norm, setup_stage_in, usb_req_get_descriptor,
                            0x300 | string_index[i], language_id, 2, 8,
                            in_data_stage);
            xhci_ring_enqueue(&usb_dev->ep0, &trb);
            // Data TRB
            data_stage_trb(&trb, va_to_pa(string_desc_head), 2, trb_in);
            xhci_ring_enqueue(&usb_dev->ep0, &trb);
            // Status TRB
            status_stage_trb(&trb, enable_ioc, trb_out);
            xhci_ring_enqueue(&usb_dev->ep0, &trb);

            // 响铃
            xhci_ring_doorbell(xhci_controller, usb_dev->slot_id, 1);
            timing();
            xhci_ering_dequeue(xhci_controller, &trb);

            //分配内存
            uint8 string_desc_length = string_desc_head->head.length;
            string_desc[i] = kzalloc(string_desc_length);

            //第二次先正式获取字符串描述符N
            setup_stage_trb(&trb, setup_stage_device, setup_stage_norm, setup_stage_in, usb_req_get_descriptor,
                            0x300 | string_index[i], language_id, string_desc_length, 8,
                            in_data_stage);
            xhci_ring_enqueue(&usb_dev->ep0, &trb);
            // Data TRB
            data_stage_trb(&trb, va_to_pa(string_desc[i]), string_desc_length, trb_in);
            xhci_ring_enqueue(&usb_dev->ep0, &trb);
            // Status TRB
            status_stage_trb(&trb, enable_ioc, trb_out);
            xhci_ring_enqueue(&usb_dev->ep0, &trb);

            // 响铃
            xhci_ring_doorbell(xhci_controller, usb_dev->slot_id, 1);
            timing();
            xhci_ering_dequeue(xhci_controller, &trb);

            //解析字符串描述符
            uint8 string_ascii_length = (string_desc_length-2)/2;
            string_ascii[i] = kzalloc(string_ascii_length+1);
            utf16le_to_ascii(string_desc[i]->string,string_ascii[i],string_ascii_length);
        }else {
            string_desc[i] = NULL;
        }
    }

    usb_dev->manufacturer_desc = string_desc[0];
    usb_dev->product_desc = string_desc[1];
    usb_dev->serial_number_desc = string_desc[2];
    usb_dev->manufacturer = string_ascii[0];
    usb_dev->product = string_ascii[1];
    usb_dev->serial_number = string_ascii[2];
    color_printk(GREEN,BLACK,"manufacturer: %s   \n",usb_dev->manufacturer);
    color_printk(GREEN,BLACK,"product: %s   \n",usb_dev->product);
    color_printk(GREEN,BLACK,"serial: %s   \n",usb_dev->serial_number);
    kfree(string_desc_head);
    return 0;
}

//激活usb配置
int usb_set_config(usb_dev_t *usb_dev) {
    xhci_controller_t *xhci_controller = usb_dev->xhci_controller;
    trb_t trb;

    setup_stage_trb(&trb, setup_stage_device, setup_stage_norm, setup_stage_out, usb_req_set_config,
                    usb_dev->config_desc->configuration_value, 0, 0, 8, no_data_stage);
    xhci_ring_enqueue(&usb_dev->ep0, &trb);

    status_stage_trb(&trb, enable_ioc, trb_in);
    xhci_ring_enqueue(&usb_dev->ep0, &trb);

    xhci_ring_doorbell(xhci_controller, usb_dev->slot_id, 1);
    timing();
    xhci_ering_dequeue(xhci_controller, &trb);
    return 0;
}

//激活接口
int usb_set_interface(usb_if_t *usb_if) {
    usb_dev_t *usb_dev = usb_if->usb_dev;
    xhci_controller_t *xhci_controller = usb_dev->xhci_controller;
    trb_t trb;

    setup_stage_trb(&trb, setup_stage_interface, setup_stage_norm, setup_stage_out, usb_req_set_interface,
                    usb_if->cur_alt->altsetting, usb_if->if_num, 0, 8, no_data_stage);
    xhci_ring_enqueue(&usb_dev->ep0, &trb);

    status_stage_trb(&trb, enable_ioc, trb_in);
    xhci_ring_enqueue(&usb_dev->ep0, &trb);

    xhci_ring_doorbell(xhci_controller, usb_dev->slot_id, 1);
    timing();
    xhci_ering_dequeue(xhci_controller, &trb);
    color_printk(RED,BLACK, "set_if_trb m0:%#lx m1:%#lx   \n", trb.member0, trb.member1);
    return 0;
}

//初始化端点
int usb_endpoint_init(usb_if_alt_t *if_alt) {
    usb_dev_t *usb_dev = if_alt->usb_if->usb_dev;
    xhci_controller_t *xhci_controller = usb_dev->xhci_controller;
    xhci_input_context_t *input_ctx = kzalloc(align_up(sizeof(xhci_input_context_t), xhci_controller->align_size));
    slot64_t slot_ctx = {0};
    ep64_t ep_ctx = {0};
    trb_t trb;
    //配置端点
    uint8 max_ep_num = 0;
    for (uint8 i = 0; i < if_alt->ep_count; i++) {
        usb_ep_t *ep_phy = &if_alt->eps[i];
        uint8 ep_num = ep_phy->ep_num;
        if (ep_num > max_ep_num) max_ep_num = ep_num;
        endpoint_t *ep_vir = &usb_dev->eps[ep_num];
        uint32 ep_config = 0;
        uint64 tr_dequeue_ptr = 0;
        if (ep_phy->max_streams) {
            ep_config = (ep_phy->max_streams << 10) | (1 << 15); // MaxPStreams，LSA=1，如果使用线性数组（可选，根据实现）
            // 有流：分配Stream Context Array和per-stream rings
            uint32 streams_count = 1 << ep_phy->max_streams;
            uint32 streams_ctx_array_count = 1 << (ep_phy->max_streams + 1);
            xhci_stream_ctx_t *stream_ctx_array = kzalloc(streams_ctx_array_count * sizeof(xhci_stream_ctx_t));
            xhci_ring_t *stream_rings = kzalloc(streams_ctx_array_count * sizeof(xhci_ring_t)); //streams0 保留内存需要对齐;
            ep_vir->stream_rings = stream_rings;
            ep_vir->streams_count = streams_count;

            for (uint32 s = 1; s <= streams_count; s++) {
                // Stream ID从1开始
                xhci_ring_init(&stream_rings[s], xhci_controller->align_size);
                stream_ctx_array[s].tr_dequeue = va_to_pa(stream_rings[s].ring_base) | 1 | 1 << 1;
                stream_ctx_array[s].reserved = 0;
            }
            // Stream ID 0保留，通常设为0或无效
            stream_ctx_array[0].tr_dequeue = 0;
            stream_ctx_array[0].reserved = 0;
            tr_dequeue_ptr = va_to_pa(stream_ctx_array);
        } else {
            // 无流：单个Transfer Ring
            xhci_ring_init(&ep_vir->transfer_ring, xhci_controller->align_size);
            tr_dequeue_ptr = va_to_pa(ep_vir->transfer_ring.ring_base) | 1; // DCS=1
            ep_config = 0;
        }
        ep_ctx.ep_config = ep_config;
        ep_ctx.ep_type_size = ep_phy->ep_type << 3 | ep_phy->max_packet << 16 | ep_phy->max_burst << 8 | 3 << 1;
        ep_ctx.tr_dequeue_ptr = tr_dequeue_ptr;
        ep_ctx.trb_payload = 0;
        xhci_input_context_add(input_ctx, &ep_ctx, xhci_controller->dev_ctx_size, ep_num);
    }

    //配置slot
    slot_ctx.route_speed = (max_ep_num << 27) | (
                               (xhci_controller->op_reg->portregs[usb_dev->port_id - 1].portsc & 0x3C00) << 10);
    slot_ctx.latency_hub = usb_dev->port_id << 16;
    slot_ctx.parent_info = 0;
    slot_ctx.addr_status = 0;
    xhci_input_context_add(input_ctx, &slot_ctx, xhci_controller->dev_ctx_size, 0);

    config_endpoint_com_trb(&trb, va_to_pa(input_ctx), usb_dev->slot_id);
    xhci_ring_enqueue(&xhci_controller->cmd_ring, &trb);
    xhci_ring_doorbell(xhci_controller, 0, 0);
    timing();
    xhci_ering_dequeue(xhci_controller, &trb);
    kfree(input_ctx);
    return 0;
}

//匹配驱动id
static inline usb_id_t *usb_match_id(usb_if_t *usb_if, driver_t *drv) {
    usb_id_t *id_table = drv->id_table;
    uint8 if_class = usb_if->cur_alt->if_class;
    uint8 if_protocol = usb_if->cur_alt->if_protocol;
    uint8 if_subclass = usb_if->cur_alt->if_subclass;
    for (; id_table->if_class || id_table->if_protocol || id_table->if_subclass; id_table++) {
        if (id_table->if_class == if_class && id_table->if_protocol == if_protocol && id_table->if_subclass ==
            if_subclass)
            return id_table;
    }
    return NULL;
}

//usb总线层设备驱动匹配
int usb_bus_match(device_t *dev, driver_t *drv) {
    if (dev->type != &usb_if_type) return FALSE;
    usb_if_t *usb_if = CONTAINER_OF(dev, usb_if_t, dev);
    usb_id_t *id = usb_match_id(usb_if, drv);
    return id ? 1 : 0;
}

//usb总线层探测初始化回调
int usb_bus_probe(device_t *dev) {
}

//usb总线层卸载在回调
void usb_bus_remove(device_t *dev) {
}

//usb驱动层探测初始化回调
int usb_drv_probe(device_t *dev) {
    usb_if_t *usb_if = CONTAINER_OF(dev, usb_if_t, dev);
    usb_drv_t *usb_if_drv = CONTAINER_OF(dev->drv, usb_drv_t, drv);
    usb_id_t *id = usb_match_id(usb_if,dev->drv);
    usb_if_drv->probe(usb_if, id);
    return 0;
}

//usb驱动层卸载回调
void usb_drv_remove(device_t *dev) {
}

//注册usb接口
static inline void usb_if_register(usb_if_t *usb_if) {
    device_register(&usb_if->dev);
}

//注册usb设备
static inline void usb_dev_register(usb_dev_t *usb_dev) {
    device_register(&usb_dev->dev);
}

//注册usb驱动
void usb_drv_register(usb_drv_t *usb_drv) {
    usb_drv->drv.bus = &usb_bus_type;
    usb_drv->drv.probe = usb_drv_probe;
    usb_drv->drv.remove = usb_drv_remove;
    driver_register(&usb_drv->drv);
}

//解析端点
int usb_parse_endpoints(usb_dev_t *usb_dev, usb_if_alt_t *if_alt) {
    usb_ep_t *cur_ep = NULL;
    usb_descriptor_head *desc_head = usb_get_next_desc(&if_alt->if_desc->head);
    uint8 ep_idx = 0;
    void *cfg_end = usb_cfg_end(usb_dev->config_desc);
    while (desc_head < cfg_end && desc_head->descriptor_type != USB_INTERFACE_DESCRIPTOR) {
        if (desc_head->descriptor_type == USB_ENDPOINT_DESCRIPTOR) {
            usb_endpoint_descriptor_t *ep_desc = (usb_endpoint_descriptor_t *) desc_head;
            cur_ep = &if_alt->eps[ep_idx++];
            cur_ep->ep_num = ((ep_desc->endpoint_address & 0xF) << 1) | (ep_desc->endpoint_address >> 7);
            cur_ep->ep_type = ((ep_desc->endpoint_address & 0x80) >> 5) + (ep_desc->attributes & 3); //计算端点传输类型
            cur_ep->max_packet = ep_desc->max_packet_size & 0x07FF;
            cur_ep->mult = (ep_desc->max_packet_size >> 11) & 0x3;
            cur_ep->interval = ep_desc->interval;
            cur_ep->max_burst = 0;
            cur_ep->max_streams = 0;
            cur_ep->bytes_per_interval = 0;
            cur_ep->extras_desc = NULL;
        } else if (desc_head->descriptor_type == USB_SUPERSPEED_COMPANION_DESCRIPTOR) {
            usb_superspeed_companion_descriptor_t *ss_desc = (usb_superspeed_companion_descriptor_t *) desc_head;
            cur_ep->max_burst = ss_desc->max_burst;
            cur_ep->bytes_per_interval = ss_desc->bytes_per_interval;
            cur_ep->max_streams = ss_desc->attributes & 0x1F;
        } else {
            if (cur_ep && !cur_ep->extras_desc) cur_ep->extras_desc = desc_head; //仅保存扫描到的第一条其他类型描述符
        };
        desc_head = usb_get_next_desc(desc_head);
    }
    return 0;
}

//usb接口创建并注册总线
int usb_if_create_register(usb_dev_t *usb_dev) {
    uint8 alt_count[256]; //每个接口的替用接口数量
    usb_if_t *usb_if_map[256]; //usb_if临时缓存区
    uint8 fill_idx[256]; //下一个alts计数

    mem_set(alt_count, 0, sizeof(alt_count));
    mem_set(usb_if_map, 0, sizeof(usb_if_map));
    mem_set(fill_idx, 0, sizeof(fill_idx));

    //给接口分配内存
    usb_dev->interfaces_count = 0;
    usb_dev->interfaces = kzalloc(sizeof(usb_if_t) * usb_dev->config_desc->num_interfaces);

    //统计每个接口的替用接口数量
    usb_interface_descriptor_t *if_desc = (usb_interface_descriptor_t *) usb_dev->config_desc;
    void *cfg_end = usb_cfg_end(usb_dev->config_desc);
    while (if_desc < cfg_end) {
        if (if_desc->head.descriptor_type == USB_INTERFACE_DESCRIPTOR) {
            alt_count[if_desc->interface_number]++;
        }
        if_desc = usb_get_next_desc(&if_desc->head);
    }

    //解析alt_cout分配usb_if_alt内存
    for (uint32 i = 0; i < 256; i++) {
        if (alt_count[i]) {
            usb_if_t *usb_if = &usb_dev->interfaces[usb_dev->interfaces_count++];
            usb_if->if_num = i;
            usb_if->alt_count = alt_count[i];
            usb_if->alts = kzalloc(sizeof(usb_if_alt_t) * usb_if->alt_count);
            usb_if->usb_dev = usb_dev;
            usb_if->dev.type = &usb_if_type;
            usb_if->dev.parent = &usb_dev->dev;
            usb_if->dev.bus = &usb_bus_type;
            usb_if_map[i] = usb_if; //把usb_if缓存在usb_if_map中
        }
    }

    //填充每个usb_if_alt
    if_desc = (usb_interface_descriptor_t *) usb_dev->config_desc;
    while (if_desc < cfg_end) {
        if (if_desc->head.descriptor_type == USB_INTERFACE_DESCRIPTOR) {
            usb_if_t *usb_if = usb_if_map[if_desc->interface_number];
            uint8 idx = fill_idx[if_desc->interface_number]++;
            usb_if_alt_t *if_alt = &usb_if->alts[idx];
            if_alt->usb_if = usb_if;
            if_alt->if_desc = if_desc;
            if_alt->altsetting = if_desc->alternate_setting;
            if_alt->if_class = if_desc->interface_class;
            if_alt->if_subclass = if_desc->interface_subclass;
            if_alt->if_protocol = if_desc->interface_protocol;
            if_alt->ep_count = if_desc->num_endpoints;
            if_alt->eps = kzalloc(if_alt->ep_count * sizeof(usb_ep_t)); //给端点分配内存
            /* 可选：此处不解析端点，延后到 probe；或预解析以便 match/probe 快速使用 */
            /* usb_parse_alt_endpoints(usb_dev, alt); */
            usb_parse_endpoints(usb_dev, if_alt);
        }
        if_desc = usb_get_next_desc(&if_desc->head);
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
    usb_get_string_descriptor(usb_dev); //获取字符串描述符
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
    for (uint8 i = 0; i < xhci_controller->max_ports; i++) {
        if ((xhci_controller->op_reg->portregs[i].portsc & XHCI_PORTSC_CCS) && xhci_controller->op_reg->portregs[i].
            portsc & (XHCI_PORTSC_CSC | XHCI_PORTSC_PRC)) {
            //检测端口是否有设备
            uint8 spc_idx = xhci_controller->port_to_spc[i];
            if (xhci_controller->spc[spc_idx].major_bcd < 0x3) {
                //usb2.0
                uint32 pr = XHCI_PORTSC_PR | XHCI_PORTSC_PP;
                xhci_controller->op_reg->portregs[i].portsc = pr;
                timing();
                xhci_ering_dequeue(xhci_controller, &trb);
            }
            //usb3.x
            while (!(xhci_controller->op_reg->portregs[i].portsc & XHCI_PORTSC_PED)) pause();
            usb_dev_t *usb_dev = usb_dev_create(xhci_dev, i);
            usb_dev_register(usb_dev);
            usb_if_create_register(usb_dev);
            color_printk(GREEN, BLACK, "ports[%d]:%#x    \n", i + 1, xhci_controller->op_reg->portregs[i].portsc);
            uint32 w1c = xhci_controller->op_reg->portregs[i].portsc;
            w1c &= (XHCI_PORTSC_W1C_MASK | XHCI_PORTSC_PP);
            xhci_controller->op_reg->portregs[i].portsc = w1c;
            color_printk(GREEN, BLACK, "ports[%d]:%#x    \n", i + 1, xhci_controller->op_reg->portregs[i].portsc);
            timing();
        }
    }
}
