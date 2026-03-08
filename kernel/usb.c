#include "usb.h"
#include "slub.h"
#include "vmm.h"
#include "printk.h"
#include "pcie.h"

device_type_t usb_dev_type = {"usb-dev"};
device_type_t usb_if_type = {"usb-if"};

//获取usb设备描述符
int usb_get_device_descriptor(usb_dev_t *udev) {
    xhci_hcd_t *xhcd = udev->xhcd;
    usb_dev_desc_t *dev_desc = kzalloc_dma(sizeof(usb_dev_desc_t));

    uint8 ctx_size = xhcd->ctx_size;

    //获取端口速率，全速端口先获取设备描述符前8字节得到max_packte_size修正端点0
    uint8 port_speed = xhci_get_port_speed(xhcd,udev->port_id);
    if (port_speed == XHCI_PORTSC_SPEED_FULL) {
        //第一次先获取设备描述符前8字节，拿到max_pack_size后更新端点1，再重新获取描述符。
        usb_get_desc(udev, dev_desc, 8,USB_DESC_TYPE_DEVICE,0,0);

        //配置input_ctx
        xhci_input_ctrl_ctx_t *input_ctx = kzalloc_dma(XHCI_INPUT_CONTEXT_COUNT*ctx_size);
        xhci_ep_ctx_t *cur_ep0_ctx = xhci_get_ctx_addr(udev,1);
        xhci_ep_ctx_t *input_ctx_ep0 = xhci_get_input_ctx_addr(xhcd,input_ctx,1);
        asm_mem_cpy(cur_ep0_ctx,input_ctx_ep0,sizeof(xhci_ep_ctx_t));
        input_ctx_ep0->max_packet_size = dev_desc->max_packet_size0;
        input_ctx->add_context_flags |= 1<<1;

        xhci_cmd_eval_ctx(xhcd,input_ctx,udev->slot_id);
        kfree(input_ctx);
    }
    //获取完整设备描述符
    usb_get_desc(udev, dev_desc, sizeof(usb_dev_desc_t),USB_DESC_TYPE_DEVICE,0,0);
    udev->dev_desc = dev_desc;
    return 0;
}

//获取usb配置描述符
int usb_get_config_descriptor(usb_dev_t *udev) {
    usb_cfg_desc_t *config_desc = kzalloc_dma(sizeof(usb_cfg_desc_t));

    //第一次先获取配置描述符前9字节
    usb_get_desc(udev, config_desc, 9,USB_DESC_TYPE_CONFIG,0,0);

    //第二次从配置描述符中得到总长度获取整个配置描述符
    uint16 config_desc_length = config_desc->total_length;
    kfree(config_desc);

    config_desc = kzalloc_dma(config_desc_length);

    usb_get_desc(udev, config_desc,config_desc_length, USB_DESC_TYPE_CONFIG,0,0);

    udev->config_desc = config_desc;
    return 0;
}

//获取字符串描述符
int usb_get_string_descriptor(usb_dev_t *udev) {

    usb_desc_head *desc_head = kzalloc_dma(2);

    //获取语言ID描述符
    uint16 language_id;
    usb_get_desc(udev, desc_head, 2, USB_DESC_TYPE_STRING, 0, 0);    // 刺探：只拿 2 字节的头部
    usb_string_desc_t *language_desc = kzalloc_dma(desc_head->length);    // 分配真实长度的 DMA 内存

    // 正式拉取
    usb_get_desc(udev, language_desc, desc_head->length, USB_DESC_TYPE_STRING, 0, 0);
    if (language_desc->head.desc_type == USB_DESC_TYPE_STRING) {
        language_id = language_desc->string[0];
        udev->language_desc = language_desc;
    }else {
        language_id = 0x0409;
        udev->language_desc = 0;
        kfree(language_desc);
    }


    //默认设备都支持美式英语
    uint8 string_index[3] = {
        udev->dev_desc->manufacturer_index, udev->dev_desc->product_index,udev->dev_desc->serial_number_index
    };
    usb_string_desc_t *string_desc[3];
    uint8 *string_ascii[3];

    //获取制造商/产品型号/序列号字符串描述符
    for (uint8 i = 0; i < 3; i++) {
        if (string_index[i]) {
            //第一次先获取长度
            usb_get_desc(udev,desc_head,2,USB_DESC_TYPE_STRING,string_index[i],language_id);

            //分配内存
            string_desc[i] = kzalloc_dma(desc_head->length);

            //第二次先正式获取字符串描述符N
            usb_get_desc(udev,string_desc[i],desc_head->length,USB_DESC_TYPE_STRING,string_index[i],language_id);

            //解析字符串描述符
            uint8 string_ascii_length = (desc_head->length-2)/2;
            string_ascii[i] = kzalloc(string_ascii_length+1);
            utf16le_to_ascii(string_desc[i]->string,string_ascii[i],string_ascii_length);
        }else {
            string_desc[i] = NULL;
        }
    }

    udev->manufacturer_desc = string_desc[0];
    udev->product_desc = string_desc[1];
    udev->serial_number_desc = string_desc[2];
    udev->manufacturer = string_ascii[0];
    udev->product = string_ascii[1];
    udev->serial_number = string_ascii[2];
    kfree(desc_head);
    return 0;
}


//配置slot和ep0上下文
void usb_setup_slot_ep0_ctx(usb_dev_t *udev) {
    xhci_hcd_t *xhcd = udev->xhcd;

    uint8 ctx_size = xhcd->ctx_size;
    //分配设备插槽上下文内存
    udev->dev_ctx = kzalloc(XHCI_DEVICE_CONTEXT_COUNT*ctx_size);
    xhcd->dcbaap[udev->slot_id] = va_to_pa(udev->dev_ctx);

    //usb控制传输环初始化
    xhci_ring_t *uc_ring = &udev->eps[0].transfer_ring;
    xhci_ring_init(uc_ring);

    //分配输入上下文空间
    xhci_input_ctrl_ctx_t *input_ctx = kzalloc(XHCI_INPUT_CONTEXT_COUNT*ctx_size);

    //获取端口速率
    uint8 port_speed = (xhcd->op_reg->portregs[udev->port_id - 1].portsc >> 10) & 0x0F;

    //配置slot_ctx
    xhci_slot_ctx_t *slot_ctx = xhci_get_input_ctx_addr(xhcd, input_ctx,0);
    asm_mem_set(slot_ctx,0,sizeof(xhci_slot_ctx_t));
    slot_ctx->port_speed = port_speed;
    slot_ctx->context_entries = 1;
    slot_ctx->context_entries = udev->port_id;
    input_ctx->add_context_flags |= 1<<0;

    uint32 max_packet_size = 8; // 默认给 8
    // ★ 核心修复：使用 >= 4，一举拿下所有未来超高速设备！
    if (port_speed >= XHCI_PORTSC_SPEED_SUPER ) {
        // 涵盖 4(SS), 5(SSP), 6(SSP Gen2x2) 等所有现代超高速设备
        max_packet_size = 512;
    } else if (port_speed == XHCI_PORTSC_SPEED_HIGH) {
        // 涵盖 3(HS), 标准 USB 2.0 高速设备
        max_packet_size = 64;
    } else {
        // 涵盖 1(FS), 2(LS), 极其古老的 USB 1.1 设备
        // 在正式读取设备描述符前，8 字节是 USB 1.1 的绝对安全保底值
        max_packet_size = 8;
    }

    //配置端点0上下文（控制传输端点）
    xhci_ep_ctx_t *ep_ctx = xhci_get_input_ctx_addr(xhcd, input_ctx,1);
    asm_mem_set(ep_ctx,0,sizeof(xhci_ep_ctx_t));
    ep_ctx->cerr = 3;
    ep_ctx->ep_type = 4;
    ep_ctx->max_packet_size = max_packet_size;
    ep_ctx->tr_dequeue_ptr = va_to_pa(uc_ring->ring_base) | 1;
    input_ctx->add_context_flags |= 1<<1;

    xhci_cmd_addr_dev(xhcd,udev->slot_id,input_ctx);

    kfree(input_ctx);
}


//初始化端点
int usb_endpoint_init(usb_if_alt_t *if_alt) {
    usb_dev_t *udev = if_alt->usb_if->usb_dev;
    xhci_hcd_t *xhcd = udev->xhcd;
    xhci_input_ctrl_ctx_t *input_ctx = kzalloc(XHCI_INPUT_CONTEXT_COUNT*xhcd->ctx_size);

    //配置端点
    uint8 max_ep_num = 0;
    for (uint8 i = 0; i < if_alt->ep_count; i++) {
        usb_ep_t *ep_phy = &if_alt->eps[i];
        uint8 ep_dci = ep_phy->ep_dci;
        if (ep_dci > max_ep_num) max_ep_num = ep_dci;
        endpoint_t *ep_vir = &udev->eps[ep_dci];
        uint64 tr_dequeue_ptr = 0;
        uint32 max_streams = ep_phy->max_streams > MAX_STREAMS ? MAX_STREAMS : ep_phy->max_streams;
        if (max_streams) {
            // 有流：分配Stream Context Array和per-stream rings
            uint32 streams_count = 1 << max_streams;
            uint32 streams_ctx_array_count = 1 << (max_streams + 1);
            xhci_stream_ctx_t *stream_ctx_array = kzalloc(streams_ctx_array_count * sizeof(xhci_stream_ctx_t));
            xhci_ring_t *stream_rings = kzalloc(streams_ctx_array_count * sizeof(xhci_ring_t)); //streams0 保留内存需要对齐;
            ep_vir->stream_rings = stream_rings;
            ep_vir->streams_count = streams_count;

            for (uint32 s = 1; s <= streams_count; s++) {
                // Stream ID从1开始
                xhci_ring_init(&stream_rings[s]);
                stream_ctx_array[s].tr_dequeue = va_to_pa(stream_rings[s].ring_base) | 1 | 1 << 1;
                stream_ctx_array[s].reserved = 0;
            }
            // Stream ID 0保留，通常设为0或无效
            stream_ctx_array[0].tr_dequeue = 0;
            stream_ctx_array[0].reserved = 0;
            tr_dequeue_ptr = va_to_pa(stream_ctx_array);
        } else {
            // 无流：单个Transfer Ring
            xhci_ring_init(&ep_vir->transfer_ring);
            tr_dequeue_ptr = va_to_pa(ep_vir->transfer_ring.ring_base) | 1; // DCS=1
        }

        xhci_ep_ctx_t *ep_ctx = xhci_get_input_ctx_addr(xhcd,input_ctx,ep_dci);
        ep_ctx->ep_type = ep_phy->ep_type;
        ep_ctx->cerr = 3;
        ep_ctx->max_packet_size = ep_phy->max_packet;
        ep_ctx->max_burst_size = ep_phy->max_burst;
        ep_ctx->max_pstreams = max_streams;
        ep_ctx->lsa = 1;
        ep_ctx->tr_dequeue_ptr = tr_dequeue_ptr;
        input_ctx->add_context_flags |= 1<<ep_dci;

    }
    //配置slot
    xhci_slot_ctx_t *slot_ctx = xhci_get_input_ctx_addr(xhcd,input_ctx,0);
    slot_ctx->port_speed = xhci_get_port_speed(xhcd,udev->port_id);
    slot_ctx->context_entries = max_ep_num;
    slot_ctx->root_hub_port_num = udev->port_id;
    input_ctx->add_context_flags |= 1<<0;

    xhci_cmd_cfg_ep(xhcd,input_ctx,udev->slot_id,0);

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
    usb_desc_head *desc_head = usb_get_next_desc(&if_alt->if_desc->head);
    uint8 ep_idx = 0;
    void *cfg_end = usb_cfg_end(usb_dev->config_desc);
    while ((desc_head < cfg_end) && (desc_head->desc_type != USB_DESC_TYPE_INTERFACE)) {
        if (desc_head->desc_type == USB_DESC_TYPE_ENDPOINT) {
            usb_ep_desc_t *ep_desc = (usb_ep_desc_t *) desc_head;
            cur_ep = &if_alt->eps[ep_idx++];
            cur_ep->ep_dci = epaddr_to_epdci(ep_desc->endpoint_address);
            cur_ep->ep_type = ((ep_desc->endpoint_address & 0x80) >> 5) + (ep_desc->attributes & 3); //计算端点传输类型
            cur_ep->max_packet = ep_desc->max_packet_size & 0x07FF;
            cur_ep->mult = (ep_desc->max_packet_size >> 11) & 0x3;
            cur_ep->interval = ep_desc->interval;
            cur_ep->max_burst = 0;
            cur_ep->max_streams = 0;
            cur_ep->bytes_per_interval = 0;
            cur_ep->extras_desc = NULL;
        } else if (desc_head->desc_type == USB_DESC_TYPE_SS_ENDPOINT_COMPANION) {
            usb_ss_comp_desc_t *ss_desc = (usb_ss_comp_desc_t *) desc_head;
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

    asm_mem_set(alt_count, 0, sizeof(alt_count));
    asm_mem_set(usb_if_map, 0, sizeof(usb_if_map));
    asm_mem_set(fill_idx, 0, sizeof(fill_idx));

    //给接口分配内存
    usb_dev->interfaces_count = 0;
    usb_dev->interfaces = kzalloc(sizeof(usb_if_t) * usb_dev->config_desc->num_interfaces);

    //统计每个接口的替用接口数量
    usb_if_desc_t *if_desc = (usb_if_desc_t *) usb_dev->config_desc;
    void *cfg_end = usb_cfg_end(usb_dev->config_desc);
    while (if_desc < cfg_end) {
        if (if_desc->head.desc_type == USB_DESC_TYPE_INTERFACE ) {
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
    if_desc = (usb_if_desc_t *) usb_dev->config_desc;
    while (if_desc < cfg_end) {
        if (if_desc->head.desc_type == USB_DESC_TYPE_INTERFACE ) {
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
usb_dev_t *usb_dev_create(xhci_hcd_t *xhcd, uint32 port_id) {
    usb_dev_t *usb_dev = kzalloc(sizeof(usb_dev_t));
    usb_dev->port_id = port_id;
    xhci_cmd_enable_slot(xhcd,&usb_dev->slot_id); //启用插槽
    usb_setup_slot_ep0_ctx(usb_dev); //设置设备地址
    usb_get_device_descriptor(usb_dev); //获取设备描述符
    usb_get_config_descriptor(usb_dev); //获取配置描述符
    usb_get_string_descriptor(usb_dev); //获取字符串描述符
    usb_set_config(usb_dev); //激活配置

    usb_dev->dev.type = &usb_dev_type;
    usb_dev->dev.parent = &xhcd->xdev->dev;
    usb_dev->dev.bus = &usb_bus_type;
    return usb_dev;
}

/**
 * @brief 对指定物理端口执行复位，并等待设备就绪 (使能)
 * @param xhcd     xHCI 控制器上下文
 * @param port_id 物理端口索引 (0-based)
 * @return int32   0 表示复位成功且端口已使能，-1 表示超时或硬件故障
 */
int32 xhci_port_reset(xhci_hcd_t *xhcd, uint8 port_id) {
    uint32 portsc;

    // 获取该端口对应的协议信息
    uint8 spc_idx = xhcd->port_to_spc[port_id];

    // ==========================================
    // 阶段 1：区分协议执行复位握手与清理
    // ==========================================
    if (xhcd->spc[spc_idx].major_bcd < 0x3) {
        // --- [USB 2.0 专属逻辑：手动触发复位] ---
        portsc = xhci_read_portsc(xhcd,port_id);
        portsc &= ~XHCI_PORTSC_W1C_MASK;
        portsc |= XHCI_PORTSC_PR; // 下发 Port Reset
        xhci_write_portsc(xhcd,port_id,portsc);

        // 挂起等待主板硬件完成复位电平发送，并返回 Event TRB
        xhci_wait_for_event(xhcd, 0, port_id, 0x2000000, NULL);
    }
    // --- [USB 3.x 自动完成复位，usb2/usb3共用状态清理逻辑] ---
    portsc = xhci_read_portsc(xhcd,port_id);
    portsc &= ~XHCI_PORTSC_W1C_MASK;
    portsc |= XHCI_PORTSC_PRC | XHCI_PORTSC_PLC | XHCI_PORTSC_WRC | XHCI_PORTSC_CEC;
    xhci_write_portsc(xhcd,port_id,portsc);

    // ==========================================
    // 阶段 2：终极确认端口已使能 (PED = 1)
    // ==========================================
    uint32 timeout = 500000;
    while (((xhci_read_portsc(xhcd,port_id) & XHCI_PORTSC_PED) == 0) && timeout > 0) {
        asm_pause();
        timeout--;
    }

    if (timeout == 0) {
        color_printk(RED, BLACK, "[xHCI] Port %d Reset & Enable Timeout! Bad Device?\n", port_id);
        return -1; // 复位失败
    }

    color_printk(GREEN, BLACK, "[xHCI] Port %d successfully reset and enabled.\n", port_id);
    return 0; // 复位成功
}

//usb设备初始化
void usb_dev_scan(xhci_hcd_t *xhcd){
    for (uint8 i = 0; i < xhcd->max_ports; i++) {
        uint8 port_id = ++i;
        uint32 portsc = xhci_read_portsc(xhcd,port_id);

        // 检测是否有设备连接 (CCS) 并且发生了状态变化 (CSC)
        if ((portsc & XHCI_PORTSC_CCS) && (portsc & XHCI_PORTSC_CSC)) {

            // ★ 核心防御：看到插拔变化，立刻清理 CSC (阅后即焚)
            portsc &= ~XHCI_PORTSC_W1C_MASK;
            portsc |= XHCI_PORTSC_CSC;
            xhci_write_portsc(xhcd,port_id,portsc);

            // 调用独立的复位大炮，轰开物理层！
            if (xhci_port_reset(xhcd, i) == 0) {

                // 此时硬件环境绝对干净且安全，正式开始内核软件层面的设备构建
                usb_dev_t *usb_dev = usb_dev_create(xhcd, port_id);
                usb_dev_register(usb_dev);
                usb_if_create_register(usb_dev);

            } else {
                // 如果复位失败，比如劣质 U 盘无法响应，直接跳过，保护操作系统不挂死
                color_printk(YELLOW, BLACK, "[xHCI] Ignored faulty device on port %d.\n", i);
            }
        }
    }
}
