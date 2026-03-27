#include "usb-core.h"
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
    xhci_trb_comp_code_e comp_code = xhci_wait_for_event(xhcd, 0,XHCI_TRB_TYPE_TRANSFER_EVENT,wait_trb_pa,slot_id,ep_dci, 30000000, NULL);

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
    tr_trb.setup_stage.ioc = TRB_IOC_DISABLE;
    // 判断 TRT (Transfer Type)
    if (length == 0) {
        tr_trb.setup_stage.trt = TRB_TRT_NO_DATA;
    } else if (usb_req_pkg->dtd == USB_DIR_IN) {
        tr_trb.setup_stage.trt = TRB_TRT_IN_DATA;
    } else {
        tr_trb.setup_stage.trt = TRB_TRT_OUT_DATA;
    }

    xhci_ring_enqueue(uc_ring, &tr_trb);

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
        tr_trb.data_stage.ioc = TRB_IOC_DISABLE;   // 开启中断防雷

     xhci_ring_enqueue(uc_ring, &tr_trb);
    }

    // ==========================================================
    // 阶段 3：组装 Status TRB
    // ==========================================================
    asm_mem_set(&tr_trb, 0, sizeof(xhci_trb_t));
    tr_trb.status_stage.type = XHCI_TRB_TYPE_STATUS_STAGE;
    tr_trb.status_stage.chain = TRB_CHAIN_DISABLE;
    tr_trb.status_stage.ioc = TRB_IOC_ENABLE;
    tr_trb.status_stage.dir = (length == 0 || usb_req_dir == USB_DIR_OUT) ? TRB_DIR_IN : TRB_DIR_OUT; // ★ 核心逻辑 2：Status 阶段的方向必须是相反的！

    uint64 status_ptr = xhci_ring_enqueue(uc_ring ,&tr_trb);


    // ==========================================================
    // ★ 阶段 4：一锤定音，唤醒硬件！
    // 将 xhci_ring_doorbell 从 sync 函数中移出，放在这里执行。
    // xHC 硬件会像高铁一样连续压过 Setup -> Data -> Status。
    // ==========================================================
    xhci_ring_doorbell(xhcd, udev->slot_id, 1); // EP0 的 DCI 是 1


    // ==========================================================
    // 阶段 5：监听 Status 阶段
    // ==========================================================
    xhci_trb_comp_code_e comp_code = xhci_wait_transfer_comp(udev, 1, status_ptr);
    if (comp_code != XHCI_COMP_SUCCESS) {
        color_printk(RED, BLACK, "USB Control Msg failed! comp_code: %d\n", comp_code);
        return comp_code;
    }

    return comp_code;
}

/**
 * @brief xHCI 终极一键提交引擎 (切片入队 + 敲门铃)
 * @param xhcd      主机控制器指针
 * @param slot_id   设备槽位号
 * @param db_target 门铃目标值 (DCI 或 DCI | StreamID<<16)
 * @param ring      目标传输环
 * @param buf       虚拟内存首地址
 * @param len       总传输长度
 * @param ioc       是否在最后一块触发完成中断
 * @return uint64   整个传输块最后一个 TRB 的物理地址
 */
uint64 usb_submit_transfer(xhci_hcd_t *xhcd, uint8 slot_id, uint32 db_target,
                           xhci_ring_t *ring, void *buf, uint32 len, trb_ioc_e ioc) {
    if (len == 0) return 0; // 防御性拦截，无数据不敲门铃

    uint32 left_len = len;
    uint64 current_pa = va_to_pa(buf);
    uint64 last_trb_pa = 0;
    xhci_trb_t trb;

    // 🚀 初始化不变属性 (双 64 位极速清零)
    trb.raw[0] = 0;
    trb.raw[1] = 0;

    trb.normal.trb_type   = XHCI_TRB_TYPE_NORMAL;
    trb.normal.int_target = 0;

    // 🚀 极速无分支传输大循环
    while (left_len > 0) {
        uint32 space_to_boundary = 65536 - (current_pa & 0xFFFF);
        uint8 has_more = (left_len > space_to_boundary);
        uint32 chunk_len = has_more ? space_to_boundary : left_len;

        trb.normal.data_buf_ptr = current_pa;
        trb.normal.trb_tr_len   = chunk_len;
        trb.normal.chain = has_more;
        trb.normal.ioc   = (!has_more) & ioc;

        last_trb_pa = xhci_ring_enqueue(ring, &trb);

        current_pa += chunk_len;
        left_len   -= chunk_len;
    }

    // 👑 终极一步：图纸画完，精确敲响门铃！
    xhci_ring_doorbell(xhcd, slot_id, db_target);

    return last_trb_pa;
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

//============================================== 上下文操作函数 ===========================================================

/**
 * @brief [内部工具] 统一根据推演状态，更新 Slot 的 context_entries
 */
static void ctx_update_entries(usb_dev_t *udev) {
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

static void ctx_ep_copy(usb_dev_t *udev, usb_ep_t *new_ep) {
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
void ctx_tx_begin(usb_dev_t *udev) {
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
void ctx_prep_add_ep(usb_dev_t *udev, usb_ep_t *new_ep) {
    // 1. 打上新增标记
    udev->input_ctx->add_context_flags |= (1 << new_ep->ep_dci);

    // 2. 动态推高 context_entries
    ctx_update_entries(udev);

    // 3.拷贝
    ctx_ep_copy(udev,new_ep);

}

/**
 * @brief [纯内存] 准备删除一个端点
 */
void ctx_prep_drop_ep(usb_dev_t *udev, uint8 dci) {
    // 1. 打上死刑标记
    udev->input_ctx->drop_context_flags |= (1 << dci);

    // 2. ★ 使用你的 O(1) 汇编魔法，算出删除后的新 context_entries
    ctx_update_entries(udev);
}

/**
 * @brief [纯内存] 准备微调端点参数 (仅限 Evaluate Context 使用)
 * @note 绝不能置位 Drop Flag！常用于修正 EP0 的包长。
 */
void ctx_prep_eval_ep(usb_dev_t *udev, usb_ep_t *new_ep) {
    // 1. 仅打上添加(微调)标记
    udev->input_ctx->add_context_flags |= (1 << new_ep->ep_dci);

    // 2. 拷贝新参数覆写图纸
    ctx_ep_copy(udev, new_ep);

}

//重建端点
void ctx_prep_reconfig_ep(usb_dev_t *udev, usb_ep_t *new_ep) {
    // 1. 打上死刑标记
    udev->input_ctx->drop_context_flags |= (1 << new_ep->ep_dci);

    // 2. 标记添加
    udev->input_ctx->add_context_flags |= (1 << new_ep->ep_dci);

    // 3.拷贝
    ctx_ep_copy(udev,new_ep);
}

/**
 * @brief [纯内存] 准备初始化 Slot Context 基座 (专用于 Address Device 创世阶段)
 * @param udev       目标 USB 设备
 * @param port_speed 设备的物理连接速度
 */
static void ctx_prep_init_slot(usb_dev_t *udev) {
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
void ctx_prep_eval_slot(usb_dev_t *udev) {
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
int32 ctx_commit_tx(usb_dev_t *udev, usb_tx_cmd_e cmd_type) {
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

device_type_t usb_dev_type = {"usb-dev"};
device_type_t usb_if_type = {"usb-if"};

//给备端点分配环
static int32 alloc_ep_ring(usb_ep_t *ep) {

        uint64 tr_dequeue_ptr;
        uint32 max_streams = ep->max_streams > MAX_STREAMS ? MAX_STREAMS : ep->max_streams;

        if (max_streams) {
            // 有流：分配 Stream Context Array 和 per-stream rings
            uint32 streams_count = 1 << max_streams;                 // 例如：2^4 = 16
            uint32 streams_array_count = 1 << (max_streams + 1);     // 例如：2^5 = 32

            // 给硬件 DMA 读的上下文数组 (必须 16 字节对齐)
            xhci_stream_ctx_t *streams_ctx_array = kzalloc_dma(streams_array_count * sizeof(xhci_stream_ctx_t));
            // 给软件管理的传输环数组 (只需要 16 + 1 = 17 个)
            xhci_ring_t *streams_ring_array = kzalloc((streams_count + 1) * sizeof(xhci_ring_t));

            // ★ 修复 1：必须保存 DMA 数组指针，否则未来无法 kfree 回收！
            ep->streams_ctx_array = streams_ctx_array;
            ep->streams_ring_array = streams_ring_array;
            ep->enable_streams_count = streams_count;
            ep->lsa = 1;
            ep->hid = 1;

            for (uint32 s = 1; s <= streams_count; s++) {
                // Stream ID 从 1 开始
                xhci_alloc_ring(&streams_ring_array[s]);
                // SCT=1 (Primary TRB Ring: bit 1~3), DCS=1 (bit 0)
                streams_ctx_array[s].tr_dequeue = va_to_pa(streams_ring_array[s].ring_base) | (1 << 1) | 1;
                streams_ctx_array[s].reserved = 0;
            }
            // Stream ID 0 保留，硬件规定设为 0
            streams_ctx_array[0].tr_dequeue = 0;
            streams_ctx_array[0].reserved = 0;

            tr_dequeue_ptr = va_to_pa(streams_ctx_array);
        } else {
            // 无流：最经典的单个 Transfer Ring
            xhci_alloc_ring(&ep->transfer_ring);
            tr_dequeue_ptr = va_to_pa(ep->transfer_ring.ring_base) | 1; // DCS=1

            // ★ 修复 3：确保无流时状态干净
            ep->lsa = 0;
            ep->hid = 0;
        }

        ep->cerr = 3;
        ep->max_streams = max_streams;
        ep->trq_phys_addr = tr_dequeue_ptr; // 物理基址准备就绪

    return 0;
}

//释放端点环
static int32 free_ep_ring(usb_ep_t *ep) {
    uint32 enable_streams_count = ep->enable_streams_count;

    if (enable_streams_count) {
        xhci_ring_t *streams_ring_array = ep->streams_ring_array;
        // 释放流环
        for (uint32 s = 1; s <= enable_streams_count; s++) {
            xhci_free_ring(&streams_ring_array[s]);
        }
        kfree(ep->streams_ring_array);
        kfree(ep->streams_ctx_array); // ★ 修复：必须是 kfree_dma

        ep->streams_ring_array = NULL;
        ep->streams_ctx_array = NULL;
        ep->enable_streams_count = 0;
    } else {
        // 无流：最经典的单个 Transfer Ring
        xhci_free_ring(&ep->transfer_ring);
    }

    return 0; // ★ 修复：增加返回值
}


/**
 * @brief 切换 USB 备用接口 (极简句柄版)
 * @param new_alt 上层驱动通过 find_alt 系列函数搜索到的目标图纸句柄
 * @return int32  0 表示成功，非 0 表示失败
 */
int32 usb_switch_alt_if(usb_if_alt_t *new_alt) {
    // 1. 终极防御：如果搜索函数返回了 NULL，或者这是一个脏指针，直接拦截！
    if (new_alt == NULL || new_alt->uif == NULL) return -1;

    // 2. 顺藤摸瓜：通过你的反向指针，直接拉出上层接口和设备对象！
    usb_if_t *uif = new_alt->uif;
    usb_dev_t *udev = uif->udev;
    usb_if_alt_t *old_alt = uif->cur_alt;

    // 3. 性能优化：如果想切换的就是当前正在用的，直接光速返回
    if (old_alt == new_alt) return 0;


    ctx_tx_begin(udev);

    // ==========================================================
    // 阶段 1：[纸上谈兵] 圈出要 Drop 的旧端点
    // ==========================================================
    for (uint8 i = 0; i < old_alt->ep_count; i++) {
        ctx_prep_drop_ep(udev, old_alt->eps[i].ep_dci);
    }

    // ==========================================================
    // 阶段 2：[预分配] 为新端点画图纸并分配内存
    // ==========================================================
    for (uint8 i = 0; i < new_alt->ep_count; i++) {
        usb_ep_t *ep = &new_alt->eps[i];
        alloc_ep_ring(ep);
        ctx_prep_add_ep(udev, ep);
    }

    // ==========================================================
    // 阶段 3：一锤定音！向 xHCI 提交图纸，等待硬件裁决
    // ==========================================================
    if (ctx_commit_tx(udev, USB_TX_CMD_CFG_EP) != 0) {
        color_printk(RED, BLACK, "xHCI: Switch AltSetting failed, hardware rejected!\n");
        // 主板拒绝，销毁新分配的内存，安全退出
        for (uint8 i = 0; i < new_alt->ep_count; i++) free_ep_ring(&new_alt->eps[i]);
        return -1;
    }

    // ==========================================================
    // ★ 阶段 4：[防竞态] 提前挂载新路由！(兵马未动，粮草先行)
    // 此时新通道硬件已通，但外设还未切换。我们先把接收器架好，防漏包！
    // ==========================================================
    for (uint8 i = 0; i < new_alt->ep_count; i++) {
        usb_ep_t *ep = &new_alt->eps[i];
        udev->eps[ep->ep_dci] = ep;
    }

    // ==========================================================
    // ★ 阶段 5：主板软件均就绪，正式通知物理 U 盘切换频道！
    // ==========================================================
    if (usb_set_if(udev, uif->if_num, new_alt->altsetting) != 0) {
        color_printk(RED, BLACK, "USB: Device rejected Set Interface command!\n");

        // ！！！极品回滚逻辑 ！！！
        // 1. 软件路由撤销 (把刚才挂上去的新路由摘下来)
        for (uint8 i = 0; i < new_alt->ep_count; i++) udev->eps[new_alt->eps[i].ep_dci] = NULL;

        // 2. 释放新环内存
        for (uint8 i = 0; i < new_alt->ep_count; i++) free_ep_ring(&new_alt->eps[i]);

        // 3. 将主板硬件强制回滚到旧状态 (反向 Drop 新的，Add 旧的)
        ctx_tx_begin(udev);
        for (uint8 i = 0; i < new_alt->ep_count; i++) ctx_prep_drop_ep(udev, new_alt->eps[i].ep_dci);
        for (uint8 i = 0; i < old_alt->ep_count; i++) ctx_prep_add_ep(udev, &old_alt->eps[i]);
        ctx_commit_tx(udev, USB_TX_CMD_CFG_EP);

        return -1; // 经过这一套抢救，操作系统和设备完美回到切换前的健康状态！
    }

    // ==========================================================
    // 阶段 6：[过河拆桥] 切换彻底成功，可以安全收缴旧端点的尸体了
    // ==========================================================
    for (uint8 i = 0; i < old_alt->ep_count; i++) {
        usb_ep_t *ep = &old_alt->eps[i];
        if (udev->eps[ep->ep_dci] == ep) udev->eps[ep->ep_dci] = NULL; // 清除旧路由
        free_ep_ring(ep); // 彻底释放旧物理内存
    }

    // 状态机翻页
    uif->cur_alt = new_alt;

    return 0;
}


//给备用接口的所有端点分配环
static inline int32 enable_alt_if (usb_if_alt_t *if_alt) {
    usb_dev_t *udev = if_alt->uif->udev;

    // ★ 开启事务，拿出一张空白的 Configure Endpoint 申请表
    ctx_tx_begin(udev);

    // 配置该接口下的所有端点
    for (uint8 i = 0; i < if_alt->ep_count; i++) {
        usb_ep_t *ep = &if_alt->eps[i];
        uint8 ep_dci = ep->ep_dci;

        // 指针放到udev.eps中，建立全局 DCI 快速索引
        udev->eps[ep_dci] = ep;

        //给端点分配环
        alloc_ep_ring(ep);

        // ★ 将端点挂载到 Input Context 申请表中
        ctx_prep_add_ep(udev, ep);
    }

    // ★ 扣动扳机！将申请表通过 Command Ring 提交给主板，并敲响门铃
    ctx_commit_tx(udev, USB_TX_CMD_CFG_EP);

    return 0;
}


/**
 * @brief [内部辅助] 解析并装填 USB 标准端点参数 (USB 2.0 规格底稿)
 */
static inline void ep_desc_params(usb_ep_t *cur_ep, usb_ep_desc_t *ep_desc) {
    // 提取纯粹的 USB 传输类型 (0~3)
    uint8 usb_trans_type = ep_desc->attributes & 3;

    // 基础物理映射
    cur_ep->ep_dci = epaddr_to_epdci(ep_desc->endpoint_address);
    cur_ep->ep_type = ((ep_desc->endpoint_address & 0x80) >> 5) + usb_trans_type;
    cur_ep->max_packet_size = ep_desc->max_packet_size & 0x07FF;
    cur_ep->mult = (ep_desc->max_packet_size >> 11) & 0x3;
    cur_ep->interval = ep_desc->interval;

    // 清空高阶扩展字段 (防野指针)
    cur_ep->max_burst = 0;
    cur_ep->max_streams = 0;
    cur_ep->bytes_per_interval = 0;
    cur_ep->extras_desc = NULL;
    cur_ep->lsa = 0;
    cur_ep->hid = 0;

    // --- ★ 衍生参数与 DMA 启发值联合推导 (基于 USB 2.0 规格底稿) ---
    switch (usb_trans_type) {
        case XHCI_EP_TYPE_ISOCH:
            // Isochronous 阵营：音视频流，要求极高的周期吞吐量
            cur_ep->max_esit_payload = cur_ep->max_packet_size * (cur_ep->mult + 1);
            // 等时流永远是满载发送，直接使用最大周期负荷作为平均值
            cur_ep->average_trb_length = cur_ep->max_esit_payload;
            break;

        case XHCI_EP_TYPE_BULK:
            // Bulk 阵营：吃总线闲置带宽，无固定 ESIT 周期限制
            cur_ep->max_esit_payload = 0;
            // 黄金魔法值：3072 (3 个 USB 3.0 数据包) 完美平衡 PCIe 突发与硬件 FIFO
            cur_ep->average_trb_length = 3072;
            break;

        case XHCI_EP_TYPE_INTR:
            // Interrupt 阵营：要求极其严苛的周期性带宽保证
            cur_ep->max_esit_payload = cur_ep->max_packet_size * (cur_ep->mult + 1);
            // 数据量极小，暗示主板硬件只需分配最小 SRAM 缓存即可
            cur_ep->average_trb_length = cur_ep->max_packet_size;
            break;

        default:                 // 兜底 Control 及其它
            cur_ep->max_esit_payload = 0;
            cur_ep->average_trb_length = 8;
            break;
    }
}

/**
 * @brief [内部辅助] 解析并升级 USB 3.0 超高速伴随参数
 */
static inline void ss_desc_params(usb_ep_t *cur_ep, usb_ss_comp_desc_t *ss_desc) {
    // ★ 极客解码：直接从已经映射好的 xHCI 类型中，反向剥离出纯 USB 传输类型
    uint8 usb_trans_type = cur_ep->ep_type & 3;

    cur_ep->max_burst = ss_desc->max_burst;
    cur_ep->bytes_per_interval = ss_desc->bytes_per_interval;

    // ★ 物理隔离与参数覆写：基于端点类型的高内聚升级
    switch (usb_trans_type) {
        case XHCI_EP_TYPE_BULK:
            // Bulk 阵营：提取最大支持的并发流数量 (Streams)
            cur_ep->max_streams = ss_desc->attributes & 0x1F;
            break;

        case XHCI_EP_TYPE_ISOCH:
            // Isochronous 阵营：提取真实乘数，原地覆写掉第一阶段的 USB 2.0 伪值
            cur_ep->mult = ss_desc->attributes & 0x03;

            // 衍生参数升级：直接用硬件出厂标定的周期诉求，替换掉 USB 2.0 的计算公式
            if (cur_ep->bytes_per_interval > 0) {
                cur_ep->max_esit_payload = cur_ep->bytes_per_interval;
                cur_ep->average_trb_length = cur_ep->max_esit_payload; // 同步升级 DMA 估算值
            }
            break;

        case XHCI_EP_TYPE_INTR:
            // Interrupt 阵营：规范铁律要求伴随属性为保留位。强行清零防止主板报错
            cur_ep->mult = 0;
            cur_ep->max_streams = 0;

            // 衍生参数升级：只升级周期诉求带宽，中断端点的 average_trb_length 保持极小值不变
            if (cur_ep->bytes_per_interval > 0) {
                cur_ep->max_esit_payload = cur_ep->bytes_per_interval;
            }
            break;
    }
}

/**
 * @brief [工业级 O(N) 单次扫描] 解析 USB 接口下的所有端点及其伴随描述符
 */
static inline int32 alt_if_parse(usb_if_alt_t *if_alt) {
    usb_ep_t *cur_ep = NULL;
    usb_desc_head *desc_head = usb_get_next_desc(&if_alt->if_desc->head);
    uint8 ep_idx = 0;

    void *cfg_end = usb_cfg_end(if_alt->uif->udev->config_desc);

    // 严密防御：限定搜索范围绝对不能超出整个配置描述符
    while ((desc_head < cfg_end) && (desc_head->desc_type != USB_DESC_TYPE_INTERFACE)) {

        if (desc_head->desc_type == USB_DESC_TYPE_ENDPOINT) {
            // 防缓冲区溢出！恶意的描述符数量不能超过声明的数量
            if (ep_idx >= if_alt->ep_count) {
                break;
            }

            // 阶段 1：分发给标准解析器
            cur_ep = &if_alt->eps[ep_idx++];
            ep_desc_params(cur_ep, (usb_ep_desc_t *) desc_head);

        } else if (desc_head->desc_type == USB_DESC_TYPE_SS_ENDPOINT_COMPANION) {

            // 阶段 2：分发给 USB 3.0 覆写器
            if (cur_ep) {
                ss_desc_params(cur_ep, (usb_ss_comp_desc_t *) desc_head);
            }

        } else {

            // 阶段 3：收集类专属描述符
            if (cur_ep && cur_ep->extras_desc == NULL) {
                cur_ep->extras_desc = desc_head;
            }
        }

        // 游标推进，扫描下一个描述符
        desc_head = usb_get_next_desc(desc_head);
    }

    return 0; // O(N) 一气呵成！
}

/**
 * @brief [阶段 1] 扫描描述符，统计并分配接口与替用接口的内存
 * @param udev    USB 设备对象
 * @param alt_count  用于统计每个接口号对应的替用接口数量 (外部传入的栈数组)
 * @param usb_if_map 用于缓存接口指针的映射表 (外部传入的栈数组)
 * @return 0 成功，-1 内存分配失败或遭遇恶意描述符
 */
static inline int32 alloc_if(usb_dev_t *udev, uint8 *alt_count, usb_if_t **usb_if_map) {
    // 1. 根据配置描述符声明的接口数，分配顶层接口数组
    udev->interfaces_count = 0;
    udev->interfaces = kzalloc(sizeof(usb_if_t) * udev->config_desc->num_interfaces);
    if (!udev->interfaces) return -1;

    // 2. 第一次遍历：统计每个 interface_number 拥有多少个 alternate_setting
    usb_if_desc_t *if_desc = (usb_if_desc_t *)udev->config_desc;
    void *cfg_end = usb_cfg_end(udev->config_desc);

    while ((void *)if_desc < cfg_end) {
        if (if_desc->head.desc_type == USB_DESC_TYPE_INTERFACE) {
            alt_count[if_desc->interface_number]++;
        }
        if_desc = (usb_if_desc_t *)usb_get_next_desc(&if_desc->head);
    }

    // 3. 分配底层的 alt 数组，并初始化总线设备模型结构
    for (uint16 i = 0; i < 256; i++) {
        if (alt_count[i] > 0) {
            usb_if_t *usb_if = &udev->interfaces[udev->interfaces_count++];
            usb_if->if_num = i;
            usb_if->alt_count = alt_count[i];
            usb_if->alts = kzalloc(sizeof(usb_if_alt_t) * usb_if->alt_count);

            // 绑定设备模型拓扑
            usb_if->udev = udev;
            usb_if->dev.type = &usb_if_type;
            usb_if->dev.parent = &udev->dev;
            usb_if->dev.bus = &usb_bus_type;

            usb_if_map[i] = usb_if; // 缓存映射关系，留给下一阶段直接使用
        }
    }
    return 0;
}

/**
 * @brief [阶段 2] 填充替用接口参数，分配端点内存并触发解析
 */
/**
 * @brief [核心阶段] 解析所有接口与端点图纸，配置 xHCI 硬件环，并下发激活设备的指令
 * @param udev       USB 设备对象
 * @param usb_if_map 接口映射缓存表
 * @return 0 表示全线贯通成功，-1 表示设备拒绝激活
 */
static inline int32 if_parse(usb_dev_t *udev, usb_if_t **usb_if_map) {
    uint8 fill_idx[256];
    asm_mem_set(fill_idx, 0, sizeof(fill_idx)); // 用于记录每个接口当前填充到了第几个 alt

    usb_if_desc_t *if_desc = (usb_if_desc_t *)udev->config_desc;
    void *cfg_end = usb_cfg_end(udev->config_desc);

    // =================================================================
    // 阶段 A：纯软件遍历，精细化装填所有接口和端点的“图纸”参数
    // =================================================================
    while ((void *)if_desc < cfg_end) {
        if (if_desc->head.desc_type == USB_DESC_TYPE_INTERFACE) {
            uint8 if_num = if_desc->interface_number;
            usb_if_t *usb_if = usb_if_map[if_num];

            // 找到当前 alt 对应的槽位
            uint8 idx = fill_idx[if_num]++;
            usb_if_alt_t *if_alt = &usb_if->alts[idx];

            // 填充业务属性
            if_alt->uif = usb_if;
            if_alt->if_desc = if_desc;
            if_alt->altsetting = if_desc->alternate_setting;
            if_alt->if_class = if_desc->interface_class;
            if_alt->if_subclass = if_desc->interface_subclass;
            if_alt->if_protocol = if_desc->interface_protocol;
            if_alt->ep_count = if_desc->num_endpoints;

            // 为该 alt 分配端点内存，并触发底层解析引擎
            if (if_alt->ep_count > 0) {
                if_alt->eps = kzalloc(if_alt->ep_count * sizeof(usb_ep_t));
                alt_if_parse(if_alt);
            }
        }
        if_desc = (usb_if_desc_t *)usb_get_next_desc(&if_desc->head);
    }

    // =================================================================
    // 阶段 B：图纸绘制完毕，开始向主板申请硬件 DMA 高速公路
    // =================================================================
    for (uint32 i = 0; i < udev->interfaces_count; i++) {
        usb_if_t *usb_if = &udev->interfaces[i];

        if (usb_if != NULL) {
            // 默认锁定 alt 0 备用接口 (包含极其严密的兜底容错逻辑)
            usb_if_alt_t *alt0 = usb_find_alt_by_num(usb_if, 0);
            usb_if->cur_alt = alt0 ? alt0 : &usb_if->alts[0];

            // 呼叫主板：分配 Transfer Ring 并下发 Configure Endpoint
            enable_alt_if(usb_if->cur_alt);
        }
    }

    // =================================================================
    // 阶段 C：全线高速公路竣工，向物理 U 盘下发唯一的一次“总闸通电”指令
    // =================================================================
    int ret = usb_set_cfg(udev, udev->config_desc->configuration_value);
    if (ret != 0) {
        color_printk(RED, BLACK, "USB: Failed to Set Configuration! Device rejected.\n");
        return -1; // 通电失败，拒绝挂载！
    }

    return 0; // 软硬全线贯通！
}

/**
 * @brief 解析配置描述符，创建 USB 接口树并注册到系统总线
 */
static inline int32 usb_if_create(usb_dev_t *udev) {
    // 局部极速缓存区（放在栈上，函数退出自动销毁，零内存碎片）
    // ★ 修复：使用 uint32 彻底杜绝自增整数溢出
    uint8 alt_count[256];
    usb_if_t *usb_if_map[256];

    asm_mem_set(alt_count, 0, sizeof(alt_count));
    asm_mem_set(usb_if_map, 0, sizeof(usb_if_map));

    // =======================================================
    // 阶段 1：搭骨架 (盘点拓扑与分配内存)
    // =======================================================
    alloc_if(udev, alt_count, usb_if_map);

    // =======================================================
    // 阶段 2：填血肉 (解析接口与端点图纸)
    // =======================================================
    if_parse(udev, usb_if_map);


    return 0; // 接口树构建完毕，成功交接给业务层驱动！
}


/**
 * @brief 阶段 1：分配设备上下文，配置 Slot 和 EP0，并赋予物理地址
 * @param udev USB 设备对象
 * @return int32 0 表示成功，-1 表示失败
 */
static inline int32 enable_slot_ep0(usb_dev_t *udev) {
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
    xhci_alloc_ring(&ep0->transfer_ring);
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
    ep0->average_trb_length = mps;
    ep0->trq_phys_addr = va_to_pa(ep0->transfer_ring.ring_base) | 1;

    // ---下发命令 ---
    ctx_tx_begin(udev);
    ctx_prep_init_slot(udev);
    ctx_prep_add_ep(udev,ep0);
    ctx_commit_tx(udev,USB_TX_CMD_ADDR_DEV);

    return 0;
}

/**
 * @brief 阶段 2：通过 EP0 获取设备描述符，并动态修正全速设备的 MPS
 * @param udev USB 设备对象
 * @return int32 0 表示成功
 */
static inline int32 get_dev_desc(usb_dev_t *udev) {
    xhci_hcd_t *xhcd = udev->xhcd;

    // 分配设备描述符的 DMA 内存
    usb_dev_desc_t *dev_desc = kzalloc_dma(sizeof(usb_dev_desc_t));

    // ============================
    // 全速设备 (FS) 的 8 字节刺探与修正逻辑
    // ============================
    if (udev->port_speed == XHCI_PORTSC_SPEED_FULL) {

        // 探针：只拿前 8 字节
        usb_get_desc(udev, dev_desc, 8, USB_DESC_TYPE_DEVICE, 0, 0);

        if (dev_desc->max_packet_size0 != 8) {
            usb_ep_t *ep0 = udev->eps[1];
            ep0->max_packet_size = dev_desc->max_packet_size0;
            ctx_tx_begin(udev);
            ctx_prep_eval_ep(udev,ep0);
            ctx_commit_tx(udev,USB_TX_CMD_EVAL_CTX);
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

//获取usb配置描述符
static inline int get_cfg_desc(usb_dev_t *udev) {
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
static inline int get_string_desc(usb_dev_t *udev) {

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

//创建usb设备
static inline usb_dev_t *usb_dev_create(xhci_hcd_t *xhcd, uint32 port_id) {
    usb_dev_t *udev = kzalloc(sizeof(usb_dev_t));
    udev->xhcd = xhcd;
    udev->port_id = port_id;
    enable_slot_ep0(udev); //启用slot 和 ep0
    get_dev_desc(udev);    //获取设备描述符
    get_cfg_desc(udev);    //获取配置描述符
    get_string_desc(udev); //获取字符串描述符

    udev->dev.type = &usb_dev_type;
    udev->dev.parent = &xhcd->xdev->dev;
    udev->dev.bus = &usb_bus_type;
    return udev;
}

/**
 * @brief 内部辅助函数：无分支极速清理端口状态 (W1C 陷阱防御)
 */
static void usb_clear_port_change(xhci_hcd_t *xhcd, uint8 port_id, uint32 portsc) {
    // 1. 保护现场：将读到的数值中所有的 W1C 位强行置 0，防止误杀其他未处理的中断
    portsc &= ~XHCI_PORTSC_W1C_MASK;

    // 2. 暴力美学，全量清零：
    // 不分 2.0 和 3.0，把所有可能在复位/插入时产生的状态变化位一次性砸成 1！
    // 对于 2.0 端口，WRC 和 PLC 写 1 纯属空操作，极其安全。
    portsc |= XHCI_PORTSC_PRC |
              XHCI_PORTSC_CSC |
              XHCI_PORTSC_PEC |
              XHCI_PORTSC_WRC |
              XHCI_PORTSC_PLC;

    xhci_write_portsc(xhcd, port_id, portsc);
}

/**
 * @brief xHCI 硬核物理复位引擎 (支持运行时错误抢救 & 2.0 初始化)
 */
static int32 usb_port_reset(xhci_hcd_t *xhcd, uint8 port_id) {
    uint32 portsc = xhci_read_portsc(xhcd, port_id);
    if (!(portsc & XHCI_PORTSC_CCS)) return -1;

    //清除状态
    usb_clear_port_change(xhcd, port_id, portsc);
    portsc = xhci_read_portsc(xhcd, port_id); // 重新读取干净的状态

    uint8 spc_idx = xhcd->port_to_spc[port_id - 1];
    boolean is_usb3 = (xhcd->spc[spc_idx].major_bcd >= 0x03);

    // ---------------------------------------------------------
    // 【未来重构点】：这部分将成为 "Issue Reset" (动作下发)
    // ---------------------------------------------------------
    portsc &= ~XHCI_PORTSC_W1C_MASK;
    if (is_usb3 && (portsc & XHCI_PORTSC_PLS_MASK) == XHCI_PLS_INACTIVE) {
        portsc |= XHCI_PORTSC_WPR; // 暖复位抢救
    } else {
        portsc |= XHCI_PORTSC_PR;  // 热复位常规流程
    }
    xhci_write_portsc(xhcd, port_id, portsc);

    // ---------------------------------------------------------
    // 【未来重构点】：这部分将被替换为 "Thread Sleep / Yield" (挂起线程)
    // ---------------------------------------------------------
    if ( xhci_wait_for_event(xhcd, 0,XHCI_TRB_TYPE_PORT_STATUS_CHG ,port_id,0,0, 30000000, NULL) == XHCI_COMP_TIMEOUT) {
        return -1; // 超时失败
    }

    // ---------------------------------------------------------
    // 【未来重构点】：这部分将成为 "Bottom Half" (中断唤醒后的收尾)
    // ---------------------------------------------------------
    uint32 timeout = 3000000;
    while (timeout--) {
        portsc = xhci_read_portsc(xhcd, port_id);
        if ((portsc & XHCI_PORTSC_PR)==0 && (portsc & XHCI_PORTSC_WPR)==0 && (portsc & XHCI_PORTSC_PED)) break;
        asm_pause();
    }
    if (timeout == 0) return -1;

    // ★ 极其优雅的复用：直接调用刚才抽出来的清道夫函数！
    usb_clear_port_change(xhcd, port_id, portsc);

    return 0; // 彻底复位且使能成功！
}

/**
 * @brief xHCI 端口枚举初始化 (专供 Hub 线程在设备插入时调用)
 */
static int32 usb_port_init(xhci_hcd_t *xhcd, uint8 port_id) {
    uint32 portsc = xhci_read_portsc(xhcd, port_id);
    if (!(portsc & XHCI_PORTSC_CCS)) return -1;

    // ==========================================
    // USB 3.0 极速通道：硬件已搞定，清理现场后直接放行！
    // ==========================================
    if (portsc & XHCI_PORTSC_PED) {
        usb_clear_port_change(xhcd, port_id, portsc);
        return 0; // 成功！准备去分配 Address
    }

    // ==========================================
    // USB 2.0 或假死兜底：委托给硬核复位引擎
    // ==========================================
    return usb_port_reset(xhcd, port_id);
}

//usb设备初始化
void usb_dev_scan(xhci_hcd_t *xhcd){

    //等待硬件完成端口初始化
    // uint32 times = 20000000;
    // while (times--) {
    //     asm_pause();
    // }

    for (uint8 i = 0; i < xhcd->max_ports; i++) {
        uint8 port_id = i+1;
        uint32 portsc = xhci_read_portsc(xhcd,port_id);

        // 检测是否有设备连接 (CCS) 并且发生了状态变化 (CSC)
        //if ((portsc & XHCI_PORTSC_CCS) && (portsc & XHCI_PORTSC_CSC))
        if (portsc & XHCI_PORTSC_CCS ) {//目前采用轮训等待方式暂时只要ccs置为就进行初始化
            color_printk(GREEN,BLACK,"portsc:%#x       \n",xhci_read_portsc(xhcd, port_id));
            if (usb_port_init(xhcd, port_id) == 0) {
                color_printk(GREEN,BLACK,"portsc:%#x       \n",xhci_read_portsc(xhcd, port_id));
                usb_dev_t *usb_dev = usb_dev_create(xhcd, port_id);
                usb_dev_register(usb_dev);
                usb_if_create(usb_dev);
                usb_if_register(usb_dev);
            } else {
                // 如果复位失败，比如劣质 U 盘无法响应，直接跳过，保护操作系统不挂死
                color_printk(YELLOW, BLACK, "[xHCI] Ignored faulty device on port %d.\n", i);
            }
        }
    }
}

//======================================= 驱动======================================

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
