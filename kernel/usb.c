#include "usb.h"
#include "slub.h"
#include "vmm.h"
#include "printk.h"
#include "pcie.h"

//======================================= 传输环命令 ===========================================================

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
    xhci_trb_comp_code_e comp_code = xhci_wait_for_event(xhcd, 0,wait_trb_pa, 30000000, NULL);

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
            xhci_cmd_set_tr_deq_ptr(xhcd, slot_id, ep_dci, &udev->eps[ep_dci]->transfer_ring);

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

    xhci_ring_t *uc_ring = &udev->ep0.transfer_ring;

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

    setup_ptr = xhci_ring_enqueue(uc_ring, &tr_trb);

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

        data_ptr = xhci_ring_enqueue(uc_ring, &tr_trb);
    }

    // ==========================================================
    // 阶段 3：组装 Status TRB
    // ==========================================================
    asm_mem_set(&tr_trb, 0, sizeof(xhci_trb_t));
    tr_trb.status_stage.type = XHCI_TRB_TYPE_STATUS_STAGE;
    tr_trb.status_stage.chain = TRB_CHAIN_DISABLE;
    tr_trb.status_stage.ioc = TRB_IOC_ENABLE;
    tr_trb.status_stage.dir = (length == 0 || usb_req_dir == USB_DIR_OUT) ? TRB_DIR_IN : TRB_DIR_OUT; // ★ 核心逻辑 2：Status 阶段的方向必须是相反的！

    status_ptr = xhci_ring_enqueue(uc_ring ,&tr_trb);


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

//获取描述符
int32 usb_get_desc(usb_dev_t *udev,void *desc_buf,uint16 length,usb_desc_type_e desc_type,uint8 desc_idx, uint16 req_idx) {
    usb_req_pkg_t req_pkg = {0};

    // 注意：获取描述符的目标并不总是 Device！
    // 如果获取的是普通描述符，发给 Device；如果是接口特定的(如 HID)，必须发给 Interface！
    req_pkg.recipient = desc_type == USB_DESC_TYPE_HID_REPORT ? USB_RECIP_INTERFACE : USB_RECIP_DEVICE;
    req_pkg.req_type = USB_REQ_TYPE_STANDARD;
    req_pkg.dtd = USB_DIR_IN;
    req_pkg.request = USB_REQ_GET_DESCRIPTOR;
    req_pkg.value = desc_type<<8 | desc_idx;
    req_pkg.index = req_idx;
    req_pkg.length = length;

    usb_control_msg(udev,&req_pkg,desc_buf);
    return 0;
}

//激活配置
int usb_set_cfg(usb_dev_t *udev,uint8 cfg_value) {
    usb_req_pkg_t req_pkg = {0};
    req_pkg.recipient = USB_RECIP_DEVICE;
    req_pkg.req_type = USB_REQ_TYPE_STANDARD;
    req_pkg.dtd = USB_DIR_OUT;
    req_pkg.request = USB_REQ_SET_CONFIGURATION;
    req_pkg.value = cfg_value;
    req_pkg.index = 0;
    req_pkg.length = 0;

    usb_control_msg(udev,&req_pkg,NULL);
    return 0;
}

//激活接口
int usb_set_if(usb_dev_t *udev,uint8 if_num,uint8 alt_num) {
    usb_req_pkg_t req_pkg = {0};
    req_pkg.recipient = USB_RECIP_INTERFACE;
    req_pkg.req_type = USB_REQ_TYPE_STANDARD;
    req_pkg.dtd = USB_DIR_OUT;
    req_pkg.request = USB_REQ_SET_INTERFACE;
    req_pkg.value = alt_num;
    req_pkg.index = if_num;
    req_pkg.length = 0;

    usb_control_msg(udev,&req_pkg,NULL);
    return 0;
}



//=============================================================================================================


//获取usb配置描述符
int usb_get_cfg_desc(usb_dev_t *udev) {
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
int usb_get_string_desc(usb_dev_t *udev) {

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

//============================================== 上下文操作函数 ===========================================================

/**
 * @brief [内部工具] 统一根据推演状态，更新 Slot 的 context_entries
 */
static void usb_update_context_entries(usb_dev_t *udev) {
    xhci_input_ctx_t *input_ctx = udev->input_ctx;

    // ★ 核心算法：计算出“事务成功后”的投影位图 (忽略 Slot 本身 Bit 0)
    uint32 projected_map = (udev->active_ep_map | input_ctx->add_context_flags)
                           & ~input_ctx->drop_context_flags;

    uint8 new_max_dci = 31 - asm_lzcnt32(projected_map);

    xhci_slot_ctx_t *input_slot_ctx = xhci_get_input_ctx_entry(udev->xhcd, input_ctx, 0);
    if (input_slot_ctx->context_entries != new_max_dci) {
        input_slot_ctx->context_entries = new_max_dci;
        input_ctx->add_context_flags |= (1 << 0); // 声明 Slot 图纸被涂改
    }
}

void usb_ep_cpy(usb_dev_t *udev, usb_ep_t *new_ep) {
    xhci_ep_ctx_t *input_ep_ctx = xhci_get_input_ctx_entry(udev->xhcd, udev->input_ctx, new_ep->ep_dci);
    input_ep_ctx->mult = new_ep->mult;
    input_ep_ctx->max_pstreams = new_ep->max_streams;
    input_ep_ctx->lsa = new_ep->lsa;
    input_ep_ctx->interval = new_ep->interval;
    input_ep_ctx->max_esit_payload_hi = (new_ep->max_esit_payload>>16)&0xFF;

    input_ep_ctx->cerr = new_ep->cerr;
    input_ep_ctx->ep_type = new_ep->ep_type;
    input_ep_ctx->hid = new_ep->hid;
    input_ep_ctx->max_burst_size = new_ep->max_burst;
    input_ep_ctx->max_packet_size = new_ep->max_packet_size;

    input_ep_ctx->tr_dequeue_ptr = new_ep->trq_phys_addr;

    input_ep_ctx->average_trb_length = new_ep->average_trb_length;
    input_ep_ctx->max_esit_payload_lo = new_ep->max_esit_payload&0xFFFF;
}

/**
 * @brief 开启一个事务：将硬件真实状态克隆到软件图纸上
 */
void usb_tx_begin(usb_dev_t *udev) {
    // 1. 物理清零管控中心 (Input Control Context，占 1 个 ctx_size)
    // 彻底消灭上一次下发命令残留的 Add/Drop 幽灵标志位
    xhci_input_ctx_t *input_ctx = udev->input_ctx;
    input_ctx->add_context_flags = 0;
    input_ctx->drop_context_flags = 0;

    // 2. 完美的移花接木：将主板维护的 Device Context 拷贝到 Input Context 的数据区
    // 注意偏移量：Input Context 从第 1 个条目开始，才是 Slot 和 EP
    void *sw_ctx = xhci_get_input_ctx_entry(udev->xhcd,input_ctx,0);

    // 拷贝 32 个 Context (1 个 Slot + 31 个 EP)
    asm_mem_cpy(udev->dev_ctx, sw_ctx, udev->xhcd->ctx_size * 32);
}

/**
 * @brief [纯内存] 准备增加一个端点
 */
void usb_prep_add_ep(usb_dev_t *udev, usb_ep_t *new_ep) {
    // 1. 打上新增标记
    udev->input_ctx->add_context_flags |= (1 << new_ep->ep_dci);

    // 2. 动态推高 context_entries
    usb_update_context_entries(udev);

    // 3.拷贝
    usb_ep_cpy(udev,new_ep);

}

/**
 * @brief [纯内存] 准备删除一个端点
 */
void usb_prep_drop_ep(usb_dev_t *udev, uint8 dci) {
    // 1. 打上死刑标记
    udev->input_ctx->drop_context_flags |= (1 << dci);

    // 2. ★ 使用你的 O(1) 汇编魔法，算出删除后的新 context_entries
    usb_update_context_entries(udev);
}

/**
 * @brief [纯内存] 准备微调端点参数 (仅限 Evaluate Context 使用)
 * @note 绝不能置位 Drop Flag！常用于修正 EP0 的包长。
 */
void usb_prep_eval_ep(usb_dev_t *udev, usb_ep_t *new_ep) {
    // 1. 仅打上添加(微调)标记
    udev->input_ctx->add_context_flags |= (1 << new_ep->ep_dci);

    // 2. 拷贝新参数覆写图纸
    usb_ep_cpy(udev, new_ep);

}

//重建端点
void usb_prep_reconfig_ep(usb_dev_t *udev, usb_ep_t *new_ep) {
    // 1. 打上死刑标记
    udev->input_ctx->drop_context_flags |= (1 << new_ep->ep_dci);

    // 2. 标记添加
    udev->input_ctx->add_context_flags |= (1 << new_ep->ep_dci);

    // 3.拷贝
    usb_ep_cpy(udev,new_ep);
}

/**
 * @brief [纯内存] 准备初始化 Slot Context 基座 (专用于 Address Device 创世阶段)
 * @param udev       目标 USB 设备
 * @param port_speed 设备的物理连接速度
 */
void usb_prep_init_slot(usb_dev_t *udev) {
    // 1. 获取 Slot 图纸
    xhci_slot_ctx_t *input_slot_ctx = xhci_get_input_ctx_entry(udev->xhcd, udev->input_ctx, 0);

    // 2. 填入初始物理属性
    input_slot_ctx->port_speed = udev->port_speed;
    input_slot_ctx->root_hub_port_num = udev->port_id; // 精确锁定根集线器端口

    // 3. 打上涂改标记 (Bit 0 特权)
    udev->input_ctx->add_context_flags |= (1 << 0);
}

/**
 * @brief [纯内存] 准备微调 Slot 上下文 (仅限 Evaluate Context 使用)
 * @note 绝不能置位 Drop Flag (Slot 不能被 Drop，只能 Disable)！常用于设置 Hub 属性、省电参数或中断目标迁移。
 * @param udev 目标 USB 设备 (里面包含了软件层刚解析出的新全局属性)
 */
void usb_prep_eval_slot(usb_dev_t *udev) {
    xhci_input_ctx_t *input_ctx = udev->input_ctx;

    // 1. 核心指令：打上 Slot 图纸被涂改的标记 (Bit 0 专属特权)
    input_ctx->add_context_flags |= (1 << 0);

    // 2. 拿到 Slot Context 的图纸指针
    xhci_slot_ctx_t *input_slot_ctx = xhci_get_input_ctx_entry(udev->xhcd, input_ctx, 0);

    // 3. 涂改图纸：将 udev 软件对象里新挖掘出的全局属性，同步给硬件

    // 场景 A：设备身份觉醒 (发现它是个 Hub)
    if (udev->is_hub) {
        input_slot_ctx->is_hub = 1;                     // 宣告 Hub 身份
        input_slot_ctx->num_ports = udev->hub_num_ports;// 填入它有多少个下行端口
        input_slot_ctx->mtt = udev->hub_mtt;            // 多事务翻译器支持
        input_slot_ctx->tt_think_time = udev->hub_ttt;  // 翻译器思考时间
    }

    // 场景 B：多核中断负载均衡 (IRQ Routing)
    // 比如系统的 IRQ Balancer 决定把这个 U 盘的中断交给 CPU 3 处理
    if (udev->interrupter_target != input_slot_ctx->interrupter_target) {
        input_slot_ctx->interrupter_target = udev->interrupter_target;
    }

    // 场景 C：深度休眠电源管理 (LPM)
    // 更新最大退出延迟容忍度 (Max Exit Latency)
    input_slot_ctx->max_exit_latency = udev->max_exit_latency;
}


/**
 * @brief [物理通信] 统一事务提交引擎：下发命令，等待判决，同步状态
 * @param udev     目标 USB 设备
 * @param cmd_type 事务指令类型
 * @return 0 表示成功，非 0 表示硬件拒绝并已回滚
 */
int32 usb_commit_tx(usb_dev_t *udev, usb_tx_cmd_e cmd_type) {
    xhci_input_ctx_t *input_ctx = udev->input_ctx;
    int32 ret = -1;

    // ==========================================
    // 阶段 1：根据指令类型，扣动对应的物理硬件扳机
    // ==========================================
    switch (cmd_type) {
        case USB_TX_CMD_ADDR_DEV:
            ret = xhci_cmd_addr_dev(udev->xhcd, udev->slot_id, input_ctx);
            break;

        case USB_TX_CMD_EVAL_CTX:
            ret = xhci_cmd_eval_ctx(udev->xhcd, input_ctx, udev->slot_id);
            break;

        case USB_TX_CMD_CFG_EP:
            ret = xhci_cmd_cfg_ep(udev->xhcd, input_ctx, udev->slot_id, 0); // DC = 0
            break;

        case USB_TX_CMD_DECFG_ALL:
            // 注意：Deconfigure 模式下，主板会直接无视 input_ctx 的内容
            ret = xhci_cmd_cfg_ep(udev->xhcd, input_ctx, udev->slot_id, 1); // DC = 1 (核武器开启)
            break;

        default:
            return -1; // 非法指令
    }

    // ==========================================
    // 阶段 2：硬件裁决 (完美回滚机制)
    // ==========================================
    if (ret != 0) {
        // 主板拒绝了本次事务 (比如带宽不足、参数非法)
        // 软件层面的图纸 (Input Context) 随便它脏，我们根本不关心。
        // 因为真实的硬件账本没变，我们只需保持 active_ep_map 原样直接 return，即实现完美回滚！
        return ret;
    }

    // ==========================================
    // 阶段 3：事务成功，软件真理同步 (Shadow State Sync)
    // ==========================================
    if (cmd_type == USB_TX_CMD_DECFG_ALL) {
        // ★ 格式化特例：硬件已经把 EP1~31 全杀光了，软件必须强制同步
        // 仅保留 Slot (Bit 0) 和 EP0 (Bit 1) 存活
        udev->active_ep_map = (1 << 0) | (1 << 1);
    } else {
        // ★ 常规增量同步：根据图纸里的 Drop 和 Add 标志位，精确更新存活位图
        udev->active_ep_map &= ~input_ctx->drop_context_flags;
        udev->active_ep_map |= input_ctx->add_context_flags;
    }

    return 0; // 事务完美落地！
}

//===================================================================================================================

/**
 * @brief 阶段 1：分配设备上下文，配置 Slot 和 EP0，并赋予物理地址
 * @param udev USB 设备对象
 * @return int32 0 表示成功，-1 表示失败
 */
int32 usb_enable_slot_ep0(usb_dev_t *udev) {
    xhci_hcd_t *xhcd = udev->xhcd;

    //启用插槽
    xhci_cmd_enable_slot(xhcd,&udev->slot_id); //启用插槽

    //分配设备上下文
    uint8 ctx_size = xhcd->ctx_size;
    udev->dev_ctx = kzalloc_dma(XHCI_DEVICE_CONTEXT_COUNT * ctx_size);
    xhcd->dcbaap[udev->slot_id] = va_to_pa(udev->dev_ctx);

    //分配input上下文
    udev->input_ctx = kzalloc_dma(XHCI_INPUT_CONTEXT_COUNT * ctx_size);

    //给端点0分配传输环内存,挂载到 O(1) 路由表
    usb_ep_t *ep0 = &udev->ep0;
    xhci_ring_init(&ep0->transfer_ring);
    udev->eps[1] = ep0;

    // --- 计算初始 Max Packet Size ---
    udev->port_speed = xhci_get_port_speed(xhcd, udev->port_id);
    uint32 mps = (udev->port_speed >= XHCI_PORTSC_SPEED_SUPER) ? 512 :
                 (udev->port_speed == XHCI_PORTSC_SPEED_HIGH)  ? 64  : 8;

    // 端点0信息
    ep0->ep_dci = 1;
    ep0->cerr = 3;
    ep0->ep_type = 4; // Control Endpoint
    ep0->max_packet_size = mps;
    ep0->average_trb_length = 8;
    ep0->trq_phys_addr = va_to_pa(ep0->transfer_ring.ring_base) | 1;

    // ---下发命令 ---
    usb_tx_begin(udev);
    usb_prep_init_slot(udev);
    usb_prep_add_ep(udev,ep0);
    usb_commit_tx(udev,USB_TX_CMD_ADDR_DEV);

    return 0;
}

/**
 * @brief 阶段 2：通过 EP0 获取设备描述符，并动态修正全速设备的 MPS
 * @param udev USB 设备对象
 * @return int32 0 表示成功
 */
int32 usb_get_dev_desc(usb_dev_t *udev) {
    xhci_hcd_t *xhcd = udev->xhcd;
    uint8 port_speed = xhci_get_port_speed(xhcd, udev->port_id);

    // 分配设备描述符的 DMA 内存
    usb_dev_desc_t *dev_desc = kzalloc_dma(sizeof(usb_dev_desc_t));

    // ============================
    // 全速设备 (FS) 的 8 字节刺探与修正逻辑
    // ============================
    if (port_speed == XHCI_PORTSC_SPEED_FULL) {

        // 探针：只拿前 8 字节
        usb_get_desc(udev, dev_desc, 8, USB_DESC_TYPE_DEVICE, 0, 0);

        if (dev_desc->max_packet_size0 != 8) {
            usb_ep_t *ep0 = udev->eps[1];
            ep0->max_packet_size = dev_desc->max_packet_size0;
            usb_tx_begin(udev);
            usb_prep_eval_ep(udev,ep0);
            usb_commit_tx(udev,USB_TX_CMD_EVAL_CTX);
        }
    }

    // ============================
    // 获取完整的 18 字节设备描述符
    // ============================
    usb_get_desc(udev, dev_desc, sizeof(usb_dev_desc_t), USB_DESC_TYPE_DEVICE, 0, 0);

    // 挂载到内核对象树上
    udev->dev_desc = dev_desc;

    return 0;
}

device_type_t usb_dev_type = {"usb-dev"};
device_type_t usb_if_type = {"usb-if"};

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



//注册usb驱动
void usb_drv_register(usb_drv_t *usb_drv) {
    usb_drv->drv.bus = &usb_bus_type;
    usb_drv->drv.probe = usb_drv_probe;
    usb_drv->drv.remove = usb_drv_remove;
    driver_register(&usb_drv->drv);
}


/**
 * @brief [工业级 O(N) 单次扫描] 解析 USB 接口下的所有端点及其伴随描述符
 * @note  采用 "Base + Upgrade (打底+覆写)" 模式，完美兼容 USB 2.0 与 USB 3.0
 * @param usb_dev 设备对象指针
 * @param if_alt  当前正在解析的备用接口对象指针
 * @return 0 表示成功
 */
int32 usb_parse_ep_desc(usb_if_alt_t *if_alt) {
    usb_ep_t *cur_ep = NULL;
    usb_desc_head *desc_head = usb_get_next_desc(&if_alt->if_desc->head);
    uint8 ep_idx = 0;
    void *cfg_end = usb_cfg_end(if_alt->usb_if->udev->config_desc);
    uint8 ep_type = 0;

    // 严密防御：限定搜索范围绝对不能超出整个配置描述符，且遇到下一个接口即停止
    while ((desc_head < cfg_end) && (desc_head->desc_type != USB_DESC_TYPE_INTERFACE)) {

        if (desc_head->desc_type == USB_DESC_TYPE_ENDPOINT) {
            // ================================================================
            // 阶段 1：解析标准端点描述符 (提取基础信息 + USB 2.0 规格打底)
            // ================================================================
            usb_ep_desc_t *ep_desc = (usb_ep_desc_t *) desc_head;
            cur_ep = &if_alt->eps[ep_idx++];

            // 物理端点号转换 (如 EP1 IN 转换为 DCI=3)
            cur_ep->ep_dci = epaddr_to_epdci(ep_desc->endpoint_address);

            // 将方向位 (Bit 7) 与 传输类型 (Bits 1:0) 完美无缝映射到 xHCI 要求的 1~7 号物理类型
            cur_ep->ep_type = ((ep_desc->endpoint_address & 0x80) >> 5) + (ep_desc->attributes & 3);

            // 屏蔽高位控制信息，仅保留真实的 wMaxPacketSize 基础包长
            cur_ep->max_packet_size = ep_desc->max_packet_size & 0x07FF;

            // 预提取 USB 2.0 高速高带宽设备的突发乘数 (Mult)
            // 若设备是 USB 3.0 等时端点，此值会在稍后的伴随描述符中被覆写
            cur_ep->mult = (ep_desc->max_packet_size >> 11) & 0x3;
            cur_ep->interval = ep_desc->interval;

            // 初始化 USB 3.0 专属扩展字段为 0，防止野指针/脏数据
            cur_ep->max_burst = 0;
            cur_ep->max_streams = 0;
            cur_ep->bytes_per_interval = 0;
            cur_ep->extras_desc = NULL;

            // --- ★ 策略隔离 (Policy Isolation) ---
            // USB Core 必须保持中立，绝不擅自开启高阶特性。
            // 线性流(LSA)与设备控制权(HID)必须默认关闭，留给具体业务驱动按需开启
            cur_ep->lsa = 0;
            cur_ep->hid = 0;

            // --- ★ 衍生参数计算 (基于当前 USB 2.0 认知的保守底稿) ---
            // 计算生死攸关的带宽极限 (max_esit_payload)
            ep_type =  ep_desc->attributes & 3;
            if (ep_type == XHCI_EP_TYPE_ISOCH || ep_type == XHCI_EP_TYPE_INTR) {
                    // USB 2.0 经典公式：最大包长 * (突发乘数 + 1)
                    cur_ep->max_esit_payload = cur_ep->max_packet_size * (cur_ep->mult + 1);
            } else if (ep_type == XHCI_EP_TYPE_BULK) {
                    // Bulk 与 Control 端点吃 PCIe 闲置带宽，严禁申请固定带宽
                    cur_ep->max_esit_payload = 0;
            }

            // --- ★ 启发式 DMA 搬运长度估算 (average_trb_length) ---
            // 提前给主板内部 SRAM 缓存分配器提供情报
            if (ep_type  == XHCI_EP_TYPE_BULK) {
                // 黄金魔法值：3072 (3 个 USB 3.0 数据包)
                // 完美平衡了 PCIe 连续突发读取效率与主板内部 FIFO 资源的消耗
                cur_ep->average_trb_length = 3072;
            } else if (ep_type == XHCI_EP_TYPE_INTR) {
                // 中断端点数据极小，暗示硬件只分配最小所需缓存即可
                cur_ep->average_trb_length = cur_ep->max_packet_size;
            } else if (ep_type == XHCI_EP_TYPE_ISOCH){
                // 等时端点使用周期满载值作为平均值
                cur_ep->average_trb_length = cur_ep->max_esit_payload;
            }

        } else if (desc_head->desc_type == USB_DESC_TYPE_SS_ENDPOINT_COMPANION) {
            // ================================================================
            // 阶段 2：解析 USB 3.0 伴随描述符 (覆盖升级 USB 3.0 高阶参数)
            // ================================================================

            // 致命防雷：防止劣质 U 盘固件错排描述符顺序导致内核蓝屏
            if (!cur_ep) {
                desc_head = usb_get_next_desc(desc_head);
                continue;
            }

            usb_ss_comp_desc_t *ss_desc = (usb_ss_comp_desc_t *) desc_head;
            cur_ep->max_burst = ss_desc->max_burst;
            cur_ep->bytes_per_interval = ss_desc->bytes_per_interval;

            // ★ 物理隔离法则：伴随描述符的 bmAttributes 在不同端点下意义截然不同
            if (ep_type == XHCI_EP_TYPE_BULK) {
                // Bulk 阵营：提取最大支持的并发流数量 (Streams)
                cur_ep->max_streams = ss_desc->attributes & 0x1F;
            }
            else if (ep_type == XHCI_EP_TYPE_ISOCH) {
                // Isochronous 阵营：提取真实乘数，原地覆写掉第一阶段的 USB 2.0 伪值
                cur_ep->mult = ss_desc->attributes & 0x03;
            }
            else if (ep_type == XHCI_EP_TYPE_INTR) {
                // Interrupt 阵营：规范铁律要求此字段为保留位。强行清零，防止主板报 Parameter Error
                cur_ep->mult = 0;
                cur_ep->max_streams = 0;
            }

            // --- ★ 衍生参数升级 (检测到 USB 3.0 特性，覆写旧带宽军令状) ---
            if (ep_type == XHCI_EP_TYPE_INTR || ep_type == XHCI_EP_TYPE_ISOCH) {
                if (cur_ep->bytes_per_interval > 0) {
                    // 硬件直给：直接用出厂标定的周期诉求，替换掉 USB 2.0 的计算公式
                    cur_ep->max_esit_payload = cur_ep->bytes_per_interval;

                    // 同步升级 DMA 启发估算值
                    if (ep_type == XHCI_EP_TYPE_ISOCH) {
                        cur_ep->average_trb_length = cur_ep->max_esit_payload;
                    }
                }
            }

        } else {
            // ================================================================
            // 阶段 3：收集未知/类专属描述符 (Class-Specific)
            // ================================================================
            // USB-Core 作为总线层看不懂这些私有业务协议（如 UVC 分辨率、UAC 音频采样率）
            // 仅保留第一块描述符的指针。挂载业务驱动时，由驱动自行强转指针去解析。
            if (cur_ep && !cur_ep->extras_desc) {
                cur_ep->extras_desc = desc_head;
            }
        }

        // 游标推进，扫描下一个描述符
        desc_head = usb_get_next_desc(desc_head);
    }

    return 0; // O(N) 一气呵成，当前接口下所有端点图纸绘制完毕！
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
            usb_if->udev = usb_dev;
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
            usb_parse_ep_desc(if_alt);
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
    usb_dev->xhcd = xhcd;
    usb_dev->port_id = port_id;
    usb_enable_slot_ep0(usb_dev); //启用slot 和 ep0
    usb_get_dev_desc(usb_dev);
    usb_get_cfg_desc(usb_dev);    //获取配置描述符
    usb_get_string_desc(usb_dev); //获取字符串描述符
    usb_set_cfg(usb_dev,usb_dev->config_desc->configuration_value); //激活配置

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
    uint8 spc_idx = xhcd->port_to_spc[port_id-1];

    // ==========================================
    // 阶段 1：复位区分协议usb2.0需要设置pr手动复位，3.0全自动
    // ==========================================
    if (xhcd->spc[spc_idx].major_bcd < 0x3) {
        // --- [USB 2.0 专属逻辑：手动触发复位] ---
        portsc = xhci_read_portsc(xhcd,port_id);
        portsc &= ~XHCI_PORTSC_W1C_MASK;
        portsc |= XHCI_PORTSC_PR; // 下发 Port Reset
        xhci_write_portsc(xhcd,port_id,portsc);

        // 挂起等待主板硬件完成复位电平发送，并返回 Event TRB
        xhci_wait_for_event(xhcd, 0, port_id, 30000000, NULL);
    }


    // ==========================================
    // 阶段 2：确认端口已使能 (PED = 1)
    // ==========================================
    uint32 timeout = 30000000;
    while (timeout--) {
        portsc = xhci_read_portsc(xhcd, port_id);
        // 1. PR (Bit 4) 必须归零，表示硬件不再强拉复位电平！
        // 2. PED (Bit 1) 必须置 1，表示端口真正被启用了！
        if (((portsc & XHCI_PORTSC_PR) == 0) && ((portsc & XHCI_PORTSC_PED) != 0)) {
            break; // 完美命中！跳出循环
        }
        asm_pause();
    }

    if (timeout == 0) {
        return -1; // 复位失败
    }

    // ==========================================
    // 阶段 3：清理状态位
    // ==========================================
    portsc = xhci_read_portsc(xhcd, port_id);
    portsc = (portsc & (XHCI_PORTSC_CSC | XHCI_PORTSC_PEC |
                                   XHCI_PORTSC_WRC | XHCI_PORTSC_OCC |
                                   XHCI_PORTSC_PRC | XHCI_PORTSC_PLC |
                                   XHCI_PORTSC_CEC)) | (portsc & ~XHCI_PORTSC_W1C_MASK);
    xhci_write_portsc(xhcd, port_id, portsc);

    return 0; // 复位成功
}

//usb设备初始化
void usb_dev_scan(xhci_hcd_t *xhcd){

    //等待硬件完成端口初始化
    /*uint32 times = 20000000;
    while (times--) {
        asm_pause();
    }*/

    for (uint8 i = 0; i < xhcd->max_ports; i++) {
        uint8 port_id = i+1;
        uint32 portsc = xhci_read_portsc(xhcd,port_id);

        // 检测是否有设备连接 (CCS) 并且发生了状态变化 (CSC)
        //if ((portsc & XHCI_PORTSC_CCS) && (portsc & XHCI_PORTSC_CSC))
        if (portsc & XHCI_PORTSC_CCS ) {//目前采用轮训等待方式暂时只要ccs置为就进行初始化
            if (xhci_port_reset(xhcd, port_id) == 0) {
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
