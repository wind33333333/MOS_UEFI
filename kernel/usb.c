#include "usb.h"
#include "slub.h"
#include "vmm.h"
#include "printk.h"
#include "pcie.h"

device_type_t usb_dev_type = {"usb-dev"};
device_type_t usb_if_type = {"usb-if"};

//端点转Dci
static inline uint8 epaddr_to_epdci(uint8 ep) {
    asm volatile(
        "rolb $1,%0"
        :"+q"(ep)
        :
        :"cc");
    return ep;
}

//Dci转端点
static inline uint8 epdci_to_epaddr(uint8 dci) {
    asm volatile(
        "rorb $1,%0"
        :"+q"(dci)
        :
        :"cc");
    return dci;

}


/**
 * @brief 同步传输等待完成码并精细化处理完成码 (Transfer 专属专科门诊)
 * @param udev        通用 USB 设备对象
 * @param ep_dci      目标端点的 DCI (Device Context Index, 1~31)
 * @param wait_trb_pa 预期要核对的 TRB 物理地址
 * @param timeout_us  超时时间 (微秒)
 * @return xhci_trb_comp_code_e 最终的完成码
 */
xhci_trb_comp_code_e xhci_wait_transfer_comp (usb_dev_t *udev, uint8 ep_dci, uint64 wait_trb_pa) {
    xhci_hcd_t *xhcd = udev->xhcd;
    uint8 slot_id = udev->slot_id;

    // ==========================================================
    // 第一关： 1. 挂起等待事件环 (Event Ring) 的回执
    // ==========================================================
    xhci_trb_comp_code_e comp_code = xhci_wait_for_event(xhcd, wait_trb_pa, 50000000, NULL);

    // ==========================================================
    // 2. 完美成功或可接受的短包 (直接放行)
    // ==========================================================
    if (comp_code == XHCI_COMP_SUCCESS) {
        return comp_code;
    }

    // 在 BOT (Bulk-Only Transport) 协议的 Data IN 阶段，
    // 设备实际发送的数据少于我们请求的长度是完全合法的（短包机制）
    if (comp_code == XHCI_COMP_SHORT_PACKET) {
        // 注意：后续实战中，这里需要通过 event->param (传输残余长度) 计算出真实收到的字节数
        color_printk(YELLOW, BLACK, "[xHCI] Transfer: Normal Short Packet (DCI=%d)\n", ep_dci);
        return comp_code;
    }

    // ==========================================================
    // 3. 第二关：急诊室 (通用大病拦截)
    // ==========================================================
    if (xhci_handle_common_error(comp_code, wait_trb_pa) == 1) {
        // 如果返回了 1，说明急诊室已经抢救或打印过致命日志了（如主机控制器挂掉、内存故障等）。
        // 直接把这个致命错误码向上抛给业务层！
        return comp_code;
    }

    // ==========================================================
    // 4. 第三关：专科门诊 (传输事件专属错误分析)
    // ==========================================================
    switch (comp_code) {
        // --- [极其致命：硬件 DMA 级错误] ---
        case XHCI_COMP_DATA_BUFFER_ERROR:
            // OS 提供的 DMA 物理地址非法，或者跨越了非连续的页边界
            color_printk(RED, BLACK, "[xHCI] FATAL: Data Buffer Error! Invalid or page-crossing DMA phys address (DCI=%d)\n", ep_dci);
            break;

        // --- [严重故障：端点状态变为 Halted (死锁)，必须进行软复位抢救] ---
        case XHCI_COMP_STALL_ERROR:
            // 设备主动拒绝服务 (比如 U盘不支持某个下发的 SCSI 命令)
            color_printk(RED, BLACK, "[xHCI] FAULT: STALL Error! Device rejected request, EP halted (DCI=%d)\n", ep_dci);
            // 抢救第 1 步：主板级解挂 (强制从 Halted 拽回 Stopped)
            xhci_cmd_reset_ep(xhcd, slot_id, ep_dci);
            // 抢救第 2 步：跨越“坏死”的 TRB 尸体
            xhci_cmd_set_tr_deq_ptr(xhcd, slot_id, ep_dci, &udev->eps[ep_dci - 1].transfer_ring);

            if (ep_dci > 1) {
                // 普通端点且是因为 STALL 挂起：必须发和解信
                color_printk(YELLOW, BLACK, "xHCI: Sending Clear Feature to unlock physical endpoint...\n");
                usb_clear_feature_halt(udev, ep_dci);
            } else {
                // EP0 的 STALL：靠下一个 Setup 包自动冲刷，什么都不用做
                color_printk(GREEN, BLACK, "xHCI: EP0 STALL CPR complete.\n");
            }
            break;

        case XHCI_COMP_USB_TRANSACTION_ERROR:
            // 物理线路上发生了 CRC 校验失败或响应超时
            color_printk(RED, BLACK, "[xHCI] FAULT: USB Transaction Error! Link timeout or CRC failure (DCI=%d)\n", ep_dci);
            break;

        case XHCI_COMP_BABBLE_ERROR:
            // 设备发疯，狂发数据覆盖了主机内存 (超出了 TRB 预期长度)
            color_printk(RED, BLACK, "[xHCI] FAULT: Babble Error! Device sent excess data (DCI=%d)\n", ep_dci);
            break;

        // --- [正常状态流转：传输环被内核软件主动刹车] ---
        case XHCI_COMP_STOPPED:
        case XHCI_COMP_STOPPED_LENGTH_INVALID:
        case XHCI_COMP_STOPPED_SHORT_PACKET:
            // 收到 Stop Endpoint 命令后的正常回执
            color_printk(GREEN, BLACK, "[xHCI] STATUS: Transfer Ring successfully halted by Stop EP Command (DCI=%d)\n", ep_dci);
            break;

        // --- [带宽与链路异常] ---
        case XHCI_COMP_BANDWIDTH_OVERRUN_ERROR:
            // 设备试图占用超额的总线带宽
            color_printk(RED, BLACK, "[xHCI] ERROR: Bandwidth Overrun! Device exceeded bus bandwidth (DCI=%d)\n", ep_dci);
            break;
        case XHCI_COMP_NO_PING_RESPONSE_ERROR:
            // USB 3.0 链路维护检测失败
            color_printk(YELLOW, BLACK, "[xHCI] WARN: No Ping Response on USB 3.0 Link (DCI=%d)\n", ep_dci);
            break;
        case XHCI_COMP_INCOMPATIBLE_DEVICE_ERROR:
            // 设备协议与主板不兼容
            color_printk(RED, BLACK, "[xHCI] FATAL: Incompatible Device Protocol (DCI=%d)\n", ep_dci);
            break;
        case XHCI_COMP_MAX_EXIT_LATENCY_TOO_LARGE:
            // 链路从 U1/U2 低功耗休眠状态唤醒失败
            color_printk(RED, BLACK, "[xHCI] ERROR: Failed to wake link from U1/U2 sleep (DCI=%d)\n", ep_dci);
            break;

        // --- [等时传输 (Isoch) 与 其他扩展协议专属] ---
        case XHCI_COMP_RING_UNDERRUN:
        case XHCI_COMP_RING_OVERRUN:
        case XHCI_COMP_MISSED_SERVICE_ERROR:
        case XHCI_COMP_ISOCH_BUFFER_OVERRUN:
            // 音视频设备实时传输时的丢帧或速率不匹配警告
            color_printk(YELLOW, BLACK, "[xHCI] WARN: Isoch rate mismatch or missed microframe (DCI=%d)\n", ep_dci);
            break;
        case XHCI_COMP_INVALID_STREAM_ID_ERROR:
            // [UAS 协议专属] 发送了非法的 Stream ID 导致多路复用失败
            color_printk(RED, BLACK, "[xHCI] ERROR: Invalid Stream ID sent [UAS Protocol] (DCI=%d)\n", ep_dci);
            break;
        case XHCI_COMP_SPLIT_TRANSACTION_ERROR:
            // USB 2.0 Hub 拆分事务 (Split Transaction) 失败
            color_printk(RED, BLACK, "[xHCI] ERROR: USB 2.0 Hub Split Transaction Error (DCI=%d)\n", ep_dci);
            break;

        // --- [漏网之鱼处理] ---
        default:
            color_printk(RED, BLACK, "[xHCI] UNKNOWN: Unhandled Transfer Completion Code: %d (DCI=%d)\n", comp_code, ep_dci);
            break;
    }

    return comp_code;
}

/**
 * 标准 USB 控制传输统一封装函数 (EP0 专属) - 结构体传参版
 * @param udev       USB 设备上下文
 * @param usb_req_pkg           指向 8 字节标准 Setup 结构体的指针
 * @param data_buf      数据缓冲区指针 (无数据阶段填 NULL)
 * @param timeout_us    超时时间 (微秒)
 * @return int32        0 表示成功，其他为错误码
 */
int32 usb_control_msg(usb_dev_t *udev, usb_req_pkg_t *usb_req_pkg, void *data_buf) {
    xhci_hcd_t *xhcd = udev->xhcd;
    xhci_trb_t tr_trb;
    uint64 setup_ptr, data_ptr = 0, status_ptr;
    xhci_trb_comp_code_e comp_code;

    uint16 length = usb_req_pkg->length;

    // 解析trb方向
    uint8 usb_req_dir = usb_req_pkg->dtd;

    // ==========================================================
    // 阶段 1：组装 Setup TRB
    // ==========================================================
    asm_mem_set(&tr_trb, 0, sizeof(xhci_trb_t));
    asm_mem_cpy(usb_req_pkg,&tr_trb,sizeof(usb_req_pkg_t)); //拷贝USB请求包到TRB前8字节中
    tr_trb.setup_stage.trb_tr_len = sizeof(usb_req_pkg_t); //steup trb必须8
    tr_trb.setup_stage.int_target = 0;     //中断号暂时统一设置0
    tr_trb.setup_stage.idt = TRB_IDT_ENABLE;   // setup trb 必须1
    tr_trb.setup_stage.type = XHCI_TRB_TYPE_SETUP_STAGE;
    tr_trb.setup_stage.chain = TRB_CHAIN_DISABLE;
    tr_trb.setup_stage.ioc = TRB_IOC_ENABLE;
    // 判断 TRT (Transfer Type)
    if (length == 0) {
        tr_trb.setup_stage.trt = TRB_TRT_NO_DATA;
    } else if (usb_req_pkg->dtd == USB_DIR_IN) {
        tr_trb.setup_stage.trt = TRB_TRT_IN_DATA;
    } else {
        tr_trb.setup_stage.trt = TRB_TRT_OUT_DATA;
    }

    setup_ptr = xhci_ring_enqueue(&udev->eps[0].transfer_ring, &tr_trb);

    // ==========================================================
    // 阶段 2：组装 Data TRB (如果有)
    // ==========================================================
    if (length != 0 && data_buf != NULL) {
        asm_mem_set(&tr_trb, 0, sizeof(xhci_trb_t));
        tr_trb.data_stage.data_buf_ptr = va_to_pa(data_buf); // 物理地址
        tr_trb.data_stage.tr_len = length;
        tr_trb.data_stage.type = XHCI_TRB_TYPE_DATA_STAGE;
        tr_trb.data_stage.dir = usb_req_dir;  //数据阶段方向和usb.dtd方向一致
        tr_trb.data_stage.chain = TRB_CHAIN_DISABLE; // 单个 Data TRB 必须为 0
        tr_trb.data_stage.ioc = TRB_IOC_ENABLE;   // 开启中断防雷

        data_ptr = xhci_ring_enqueue(&udev->eps[0].transfer_ring, &tr_trb);
    }

    // ==========================================================
    // 阶段 3：组装 Status TRB
    // ==========================================================
    asm_mem_set(&tr_trb, 0, sizeof(xhci_trb_t));
    tr_trb.status_stage.type = XHCI_TRB_TYPE_STATUS_STAGE;
    tr_trb.status_stage.chain = TRB_CHAIN_DISABLE;
    tr_trb.status_stage.ioc = TRB_IOC_ENABLE;
    tr_trb.status_stage.dir = (length == 0 || usb_req_dir == USB_DIR_OUT) ? TRB_DIR_IN : TRB_DIR_OUT; // ★ 核心逻辑 2：Status 阶段的方向必须是相反的！

    status_ptr = xhci_ring_enqueue(&udev->eps[0].transfer_ring, &tr_trb);


    // ==========================================================
    // ★ 阶段 4：一锤定音，唤醒硬件！
    // 将 xhci_ring_doorbell 从 sync 函数中移出，放在这里执行。
    // xHC 硬件会像高铁一样连续压过 Setup -> Data -> Status。
    // ==========================================================
    xhci_ring_doorbell(xhcd, udev->slot_id, 1); // EP0 的 DCI 是 1


    // ==========================================================
    // 阶段 5：步步为营的守护监听 (复用你写的完美错误处理函数)
    // ==========================================================
    // 监听 Setup 阶段
    comp_code = xhci_wait_transfer_comp(udev, 1, setup_ptr);
    if (comp_code != XHCI_COMP_SUCCESS) {
        color_printk(RED,BLACK,"usb control msg setup stage error comp_code:%d  \n",comp_code);
        return comp_code; // 如果 U 盘拒收命令，它会在这里 STALL 并被自动救活
    }

    // 监听 Data 阶段
    if (length != 0 && data_buf != NULL) {
        comp_code = xhci_wait_transfer_comp(udev, 1, data_ptr);
        if (comp_code != XHCI_COMP_SUCCESS) {
            color_printk(RED,BLACK,"usb control msg data stage error comp_code:%d  \n",comp_code);
            return comp_code;
        }
    }

    // 监听 Status 阶段
    comp_code = xhci_wait_transfer_comp(udev, 1, status_ptr);
    if (comp_code != XHCI_COMP_SUCCESS) {
        color_printk(RED,BLACK,"usb control msg data status error comp_code:%d  \n",comp_code);
        return comp_code;
    }

    return comp_code;
}

/**
 * 清除 USB 端点的 STALL/Halt 状态 (撬开大门)
 * @param udev      USB 设备上下文
 * @param ep_dci xHCI 的端点上下文索引 (DCI, 范围 2-31)
 */
int32 usb_clear_feature_halt(usb_dev_t *udev, uint8 ep_dci) {
    // 2. 组装 8 字节的标准 Setup 请求包
    usb_req_pkg_t req_pkg = {0};
    req_pkg.recipient = USB_RECIP_ENDPOINT;
    req_pkg.req_type = USB_REQ_TYPE_STANDARD;
    req_pkg.dtd = USB_DIR_OUT;
    req_pkg.request = USB_REQ_CLEAR_FEATURE;
    req_pkg.value = USB_FEATURE_ENDPOINT_HALT;
    req_pkg.index = epdci_to_epaddr(ep_dci);
    req_pkg.length = 0;

    usb_control_msg(udev,&req_pkg,NULL);

    return 0;
}

//获取usb设备描述符
int32 usb_get_dev_desc(usb_dev_t *udev,usb_dev_desc_t *dev_desc,uint16 length) {
    usb_req_pkg_t req_pkg = {0};
    req_pkg.recipient = USB_RECIP_DEVICE;
    req_pkg.req_type = USB_REQ_TYPE_STANDARD;
    req_pkg.dtd = USB_DIR_IN;
    req_pkg.request = USB_REQ_GET_DESCRIPTOR;
    req_pkg.value = USB_DESC_TYPE_DEVICE<<8 | 0;
    req_pkg.index = 0;
    req_pkg.length = length;

    usb_control_msg(udev,&req_pkg,dev_desc);
    return 0;
}

//==================================================================


//获取usb设备描述符
int usb_get_device_descriptor(usb_dev_t *udev) {
    xhci_hcd_t *xhcd = udev->xhcd;
    usb_dev_desc_t *dev_desc = kzalloc(align_up(sizeof(usb_dev_desc_t), 64));

    uint8 ctx_size = xhcd->ctx_size;

    //获取端口速率，全速端口先获取设备描述符前8字节得到max_packte_size修正端点0
    uint8 port_speed = (xhcd->op_reg->portregs[udev->port_id - 1].portsc >> 10) & 0x0F;
    if (port_speed == XHCI_PORTSC_SPEED_FULL) {
        //第一次先获取设备描述符前8字节，拿到max_pack_size后更新端点1，再重新获取描述符。
        usb_get_dev_desc(udev, dev_desc, 8);

        //配置input_ctx
        xhci_input_ctrl_ctx_t *input_ctx = kzalloc(XHCI_INPUT_CONTEXT_COUNT*ctx_size);
        xhci_ep_ctx_t *cur_ep0_ctx = xhci_get_ctx_addr(udev,1);
        xhci_ep_ctx_t *input_ctx_ep0 = xhci_get_input_ctx_addr(xhcd,input_ctx,1);
        asm_mem_cpy(cur_ep0_ctx,input_ctx_ep0,sizeof(xhci_ep_ctx_t));
        input_ctx_ep0->max_packet_size = dev_desc->max_packet_size0;
        input_ctx->add_context_flags |= 1<<1;

        xhci_cmd_eval_ctx(xhcd,input_ctx,udev->slot_id);
        kfree(input_ctx);
    }
    //获取完整设备描述符
    usb_get_dev_desc(udev, dev_desc, sizeof(usb_dev_desc_t));
    udev->dev_desc = dev_desc;
    return 0;
}

//获取usb配置描述符
int usb_get_config_descriptor(usb_dev_t *usb_dev) {
    xhci_hcd_t *xhcd = usb_dev->xhcd;
    usb_config_descriptor_t *config_desc = kzalloc(align_up(sizeof(usb_config_descriptor_t), 64));

    //第一次先获取配置描述符前9字节
    trb_t trb;
    setup_stage_trb(&trb, setup_stage_device, setup_stage_norm, setup_stage_in, usb_req_get_descriptor, 0x200, 0, 9, in_data_stage);
    xhci_ring_enqueue(&usb_dev->ep0, &trb);
    // Data TRB
    data_stage_trb(&trb, va_to_pa(config_desc), 9, trb_in);
    xhci_ring_enqueue(&usb_dev->ep0, &trb);
    // Status TRB
    status_stage_trb(&trb, ENABLE_IOC, trb_out);
    xhci_ring_enqueue(&usb_dev->ep0, &trb);

    // 响铃
    xhci_ring_doorbell(xhcd, usb_dev->slot_id, 1);
    timing();
    xhci_ering_dequeue(xhcd, &trb);

    //第二次从配置描述符中得到总长度获取整个配置描述符
    uint16 config_desc_length = config_desc->total_length;
    kfree(config_desc);
    config_desc = kzalloc(align_up(config_desc_length, 64));

    setup_stage_trb(&trb, setup_stage_device, setup_stage_norm, setup_stage_in, usb_req_get_descriptor, 0x200, 0,
                    config_desc_length, in_data_stage);
    xhci_ring_enqueue(&usb_dev->ep0, &trb);
    // Data TRB
    data_stage_trb(&trb, va_to_pa(config_desc), config_desc_length, trb_in);
    xhci_ring_enqueue(&usb_dev->ep0, &trb);
    // Status TRB
    status_stage_trb(&trb, ENABLE_IOC, trb_out);
    xhci_ring_enqueue(&usb_dev->ep0, &trb);

    // 响铃
    xhci_ring_doorbell(xhcd, usb_dev->slot_id, 1);
    timing();
    xhci_ering_dequeue(xhcd, &trb);

    usb_dev->config_desc = config_desc;
    return 0;
}

//获取字符串描述符
int usb_get_string_descriptor(usb_dev_t *usb_dev) {
    xhci_hcd_t *xhcd = usb_dev->xhcd;
    trb_t trb;

    usb_string_descriptor_t *language_desc = kzalloc(8);
    //获取语言ID描述符
    setup_stage_trb(&trb, setup_stage_device, setup_stage_norm, setup_stage_in, usb_req_get_descriptor, 0x300, 0, 8, in_data_stage);
    xhci_ring_enqueue(&usb_dev->ep0, &trb);
    // Data TRB
    data_stage_trb(&trb, va_to_pa(language_desc), 8, trb_in);
    xhci_ring_enqueue(&usb_dev->ep0, &trb);
    // Status TRB
    status_stage_trb(&trb, ENABLE_IOC, trb_out);
    xhci_ring_enqueue(&usb_dev->ep0, &trb);

    // 响铃
    xhci_ring_doorbell(xhcd, usb_dev->slot_id, 1);
    timing();
    xhci_ering_dequeue(xhcd, &trb);

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
                            0x300 | string_index[i], language_id, 2, in_data_stage);
            xhci_ring_enqueue(&usb_dev->ep0, &trb);
            // Data TRB
            data_stage_trb(&trb, va_to_pa(string_desc_head), 2, trb_in);
            xhci_ring_enqueue(&usb_dev->ep0, &trb);
            // Status TRB
            status_stage_trb(&trb, ENABLE_IOC, trb_out);
            xhci_ring_enqueue(&usb_dev->ep0, &trb);

            // 响铃
            xhci_ring_doorbell(xhcd, usb_dev->slot_id, 1);
            timing();
            xhci_ering_dequeue(xhcd, &trb);

            //分配内存
            uint8 string_desc_length = string_desc_head->head.length;
            string_desc[i] = kzalloc(string_desc_length);

            //第二次先正式获取字符串描述符N
            setup_stage_trb(&trb, setup_stage_device, setup_stage_norm, setup_stage_in, usb_req_get_descriptor,
                            0x300 | string_index[i], language_id, string_desc_length, in_data_stage);
            xhci_ring_enqueue(&usb_dev->ep0, &trb);
            // Data TRB
            data_stage_trb(&trb, va_to_pa(string_desc[i]), string_desc_length, trb_in);
            xhci_ring_enqueue(&usb_dev->ep0, &trb);
            // Status TRB
            status_stage_trb(&trb, ENABLE_IOC, trb_out);
            xhci_ring_enqueue(&usb_dev->ep0, &trb);

            // 响铃
            xhci_ring_doorbell(xhcd, usb_dev->slot_id, 1);
            timing();
            xhci_ering_dequeue(xhcd, &trb);

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
    kfree(string_desc_head);
    return 0;
}

//激活usb配置
int usb_set_config(usb_dev_t *usb_dev) {
    xhci_hcd_t *xhcd = usb_dev->xhcd;
    trb_t trb;

    setup_stage_trb(&trb, setup_stage_device, setup_stage_norm, setup_stage_out, usb_req_set_config,
                    usb_dev->config_desc->configuration_value, 0, 0, no_data_stage);
    xhci_ring_enqueue(&usb_dev->ep0, &trb);

    status_stage_trb(&trb, ENABLE_IOC, trb_in);
    xhci_ring_enqueue(&usb_dev->ep0, &trb);

    xhci_ring_doorbell(xhcd, usb_dev->slot_id, 1);
    timing();
    xhci_ering_dequeue(xhcd, &trb);
    return 0;
}

//激活接口
int usb_set_interface(usb_if_t *usb_if) {
    usb_dev_t *usb_dev = usb_if->usb_dev;
    xhci_hcd_t *xhcd = usb_dev->xhcd;
    trb_t trb;

    setup_stage_trb(&trb, setup_stage_interface, setup_stage_norm, setup_stage_out, usb_req_set_interface,
                    usb_if->cur_alt->altsetting, usb_if->if_num, 0, no_data_stage);

    uint64 setup_ptr = xhci_ring_enqueue(&usb_dev->ep0, &trb);

    status_stage_trb(&trb, ENABLE_IOC, trb_in);

    uint64 status_ptr = xhci_ring_enqueue(&usb_dev->ep0, &trb);

    xhci_ring_doorbell(xhcd, usb_dev->slot_id, 1);

    int32 comp_code = xhci_wait_for_completion(xhcd, setup_ptr, 20000000);
    if (comp_code == -1) {
        comp_code = xhci_wait_for_completion(xhcd, status_ptr, 20000000);
    }

    if (comp_code != XHCI_COMP_SUCCESS) {
        color_printk(RED,BLACK,"ep0 c:%#x t:%#x \n",usb_dev->dev_ctx->dev_ctx32.ep[0].ep_config,usb_dev->dev_ctx->dev_ctx32.ep[0].ep_type_size);
        color_printk(RED,BLACK,"usb set if error code:%#x   \n",comp_code);
        while (1);
    }

    return 0;
}

//配置slot和ep0上下文
void usb_setup_slot_ep0_ctx(usb_dev_t *udev) {
    xhci_hcd_t *xhcd = udev->xhcd;

    uint8 ctx_size = xhcd->ctx_size;
    uint32 align_size = xhcd->align_size;
    //分配设备插槽上下文内存
    udev->dev_ctx = kzalloc(align_up(XHCI_DEVICE_CONTEXT_COUNT*ctx_size,align_size));
    xhcd->dcbaap[udev->slot_id] = va_to_pa(udev->dev_ctx);

    //usb控制传输环初始化
    xhci_ring_t *uc_ring = &udev->eps[0].transfer_ring;
    xhci_ring_init(uc_ring, xhcd->align_size);

    //分配输入上下文空间
    xhci_input_ctrl_ctx_t *input_ctx = kzalloc(align_up(sizeof(XHCI_INPUT_CONTEXT_COUNT*ctx_size),align_size));

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

    xhci_cmd_address_device(xhcd,udev->slot_id,input_ctx);

    kfree(input_ctx);
}






//初始化端点
int usb_endpoint_init(usb_if_alt_t *if_alt) {
    usb_dev_t *usb_dev = if_alt->usb_if->usb_dev;
    xhci_hcd_t *xhcd = usb_dev->xhcd;
    xhci_input_context_t *input_ctx = kzalloc(align_up(sizeof(xhci_input_context_t), xhcd->align_size));
    slot64_t slot_ctx = {0};
    ep64_t ep_ctx = {0};
    trb_t trb;
    //配置端点
    uint8 max_ep_num = 0;
    for (uint8 i = 0; i < if_alt->ep_count; i++) {
        usb_ep_t *ep_phy = &if_alt->eps[i];
        uint8 ep_num = ep_phy->ep_dci;
        if (ep_num > max_ep_num) max_ep_num = ep_num;
        endpoint_t *ep_vir = &usb_dev->eps[ep_num];
        uint32 ep_config = 0;
        uint64 tr_dequeue_ptr = 0;
        uint32 max_streams = ep_phy->max_streams > MAX_STREAMS ? MAX_STREAMS : ep_phy->max_streams;
        if (max_streams) {
            ep_config = (max_streams << 10) | (1 << 15); // MaxPStreams，LSA=1，如果使用线性数组（可选，根据实现）
            // 有流：分配Stream Context Array和per-stream rings
            uint32 streams_count = 1 << max_streams;
            uint32 streams_ctx_array_count = 1 << (max_streams + 1);
            xhci_stream_ctx_t *stream_ctx_array = kzalloc(streams_ctx_array_count * sizeof(xhci_stream_ctx_t));
            xhci_ring_t *stream_rings = kzalloc(streams_ctx_array_count * sizeof(xhci_ring_t)); //streams0 保留内存需要对齐;
            ep_vir->stream_rings = stream_rings;
            ep_vir->streams_count = streams_count;

            for (uint32 s = 1; s <= streams_count; s++) {
                // Stream ID从1开始
                xhci_ring_init(&stream_rings[s], xhcd->align_size);
                stream_ctx_array[s].tr_dequeue = va_to_pa(stream_rings[s].ring_base) | 1 | 1 << 1;
                stream_ctx_array[s].reserved = 0;
            }
            // Stream ID 0保留，通常设为0或无效
            stream_ctx_array[0].tr_dequeue = 0;
            stream_ctx_array[0].reserved = 0;
            tr_dequeue_ptr = va_to_pa(stream_ctx_array);
        } else {
            // 无流：单个Transfer Ring
            xhci_ring_init(&ep_vir->transfer_ring, xhcd->align_size);
            tr_dequeue_ptr = va_to_pa(ep_vir->transfer_ring.ring_base) | 1; // DCS=1
            ep_config = 0;
        }
        ep_ctx.ep_config = ep_config;
        ep_ctx.ep_type_size = ep_phy->ep_type << 3 | ep_phy->max_packet << 16 | ep_phy->max_burst << 8 | 3 << 1;
        ep_ctx.tr_dequeue_ptr = tr_dequeue_ptr;
        ep_ctx.trb_payload = 0;
        xhci_input_context_add(input_ctx, &ep_ctx, xhcd->ctx_size, ep_num);
    }

    //配置slot
    slot_ctx.route_speed = (max_ep_num << 27) | (
                               (xhcd->op_reg->portregs[usb_dev->port_id - 1].portsc & 0x3C00) << 10);
    slot_ctx.latency_hub = usb_dev->port_id << 16;
    slot_ctx.parent_info = 0;
    slot_ctx.addr_status = 0;
    xhci_input_context_add(input_ctx, &slot_ctx, xhcd->ctx_size, 0);

    config_endpoint_com_trb(&trb, va_to_pa(input_ctx), usb_dev->slot_id);
    xhci_ring_enqueue(&xhcd->cmd_ring, &trb);
    xhci_ring_doorbell(xhcd, 0, 0);
    timing();
    xhci_ering_dequeue(xhcd, &trb);
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
            cur_ep->ep_dci = epaddr_to_epdci(ep_desc->endpoint_address);
            cur_ep->ep_type = ((ep_desc->endpoint_address & 0x80) >> 5) + (ep_desc->attributes & 3); //计算端点传输类型
            cur_ep->max_packet = ep_desc->max_packet_size & 0x07FF;
            cur_ep->mult = (ep_desc->max_packet_size >> 11) & 0x3;
            cur_ep->interval = ep_desc->interval;
            cur_ep->max_burst = 0;
            cur_ep->max_streams = 0;
            cur_ep->bytes_per_interval = 0;
            cur_ep->extras_desc = NULL;
        } else if (desc_head->descriptor_type == USB_SUPERSPEED_COMPANION_DESCRIPTOR) {
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
    usb_dev->xhcd = xhci_dev->dev.drv_data;
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
void usb_dev_scan(pcie_dev_t *xdev) {
    xhci_hcd_t *xhcd = xdev->dev.drv_data;
    trb_t trb;
    for (uint8 i = 0; i < xhcd->max_ports; i++) {
        if ((xhcd->op_reg->portregs[i].portsc & XHCI_PORTSC_CCS) && xhcd->op_reg->portregs[i].
            portsc & (XHCI_PORTSC_CSC | XHCI_PORTSC_PRC)) {
            //检测端口是否有设备
            uint8 spc_idx = xhcd->port_to_spc[i];
            if (xhcd->spc[spc_idx].major_bcd < 0x3) {
                //usb2.0
                uint32 pr = XHCI_PORTSC_PR | XHCI_PORTSC_PP;
                xhcd->op_reg->portregs[i].portsc = pr;
                timing();
                xhci_ering_dequeue(xhcd, &trb);
            }
            //usb3.x
            while (!(xhcd->op_reg->portregs[i].portsc & XHCI_PORTSC_PED)) asm_pause();
            usb_dev_t *usb_dev = usb_dev_create(xdev, i);
            usb_dev_register(usb_dev);
            usb_if_create_register(usb_dev);
            color_printk(GREEN, BLACK, "ports[%d]:%#x    \n", i + 1, xhcd->op_reg->portregs[i].portsc);
            uint32 w1c = xhcd->op_reg->portregs[i].portsc;
            w1c &= (XHCI_PORTSC_W1C_MASK | XHCI_PORTSC_PP);
            xhcd->op_reg->portregs[i].portsc = w1c;
            color_printk(GREEN, BLACK, "ports[%d]:%#x    \n", i + 1, xhcd->op_reg->portregs[i].portsc);
            timing();
        }
    }
}
