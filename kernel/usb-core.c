#include "usb-core.h"
#include "slub.h"
#include "vmm.h"
#include "printk.h"
#include "pcie.h"
#include "errno.h"

//======================================= 传输环命令 ===========================================================

/**
 * @brief 纯物理层的主板级 STALL 抢救 (只救主板，不救 U 盘)
 * @return int32 0 表示主板端点恢复成功，负数表示主板硬件已物理级死亡
 */
static inline int32 xhci_recover_stall(usb_dev_t *udev, uint8 ep_dci) {
    int32 posix_err;
    color_printk(YELLOW, BLACK, "[xHCI CPR] Executing Host-side STALL recovery for DCI=%d\n", ep_dci);

    // 抢救第 1 步：主板级解挂
    posix_err = xhci_cmd_reset_ep(udev->xhcd, udev->slot_id, ep_dci);
    if (posix_err < 0) {
        color_printk(RED, BLACK, "[xHCI CPR FATAL] Reset EP Command failed: %d\n", posix_err);
        return posix_err; // 抢救失败，主板拒绝解挂！
    }

    // 抢救第 2 步：重置出队指针
    posix_err = xhci_cmd_set_tr_deq_ptr(udev->xhcd, udev->slot_id, ep_dci, udev->ueps[ep_dci]->ring_arr);
    if (posix_err < 0) {
        color_printk(RED, BLACK, "[xHCI CPR FATAL] Set TR Deq Ptr Command failed: %d\n", posix_err);
        return posix_err; // 指针重置失败，传输环彻底报废！
    }

    return 0; // 主板端点抢救成功，可以接受新一轮的投递了
}


/**
 * @brief [内联执行器] 处理 EP0 控制传输 (三阶段)
 */
static inline uint64 xhci_submit_control_transfer(usb_urb_t *urb, xhci_submit_ring_t *ring, uint8 wants_ioc) {
    uint64 last_trb_pa = 0;
    xhci_trb_t ctl_trb;
    uint16 length  = urb->setup_packet->length;
    uint8  req_dir = urb->setup_packet->dtd; // USB_DIR_IN 或 USB_DIR_OUT

    // [阶段 1: Setup TRB]
    ctl_trb.raw[0] = 0;
    ctl_trb.raw[1] = 0;
    asm_mem_cpy(urb->setup_packet, &ctl_trb, 8); // 拷贝 8 字节控制包
    ctl_trb.setup_stage.trb_tr_len = 8;//steup trb必须8字节
    ctl_trb.setup_stage.idt        = TRB_IDT_ENABLE;//Immediate Data 必须为 1
    ctl_trb.setup_stage.type       = XHCI_TRB_TYPE_SETUP_STAGE;
    ctl_trb.setup_stage.chain      = TRB_CHAIN_DISABLE;
    ctl_trb.setup_stage.ioc        = TRB_IOC_DISABLE;// Setup 阶段不触发中断

    if (length == 0) {
        ctl_trb.setup_stage.trt = TRB_TRT_NO_DATA;
    } else if (req_dir == USB_DIR_IN) {
        ctl_trb.setup_stage.trt = TRB_TRT_IN_DATA;
    } else {
        ctl_trb.setup_stage.trt = TRB_TRT_OUT_DATA;
    }
    xhci_submit_ring_enq(ring, &ctl_trb);

    // [阶段 2: Data TRB]
    if (length != 0 && urb->transfer_buf != NULL) {
        ctl_trb.raw[0] = 0;
        ctl_trb.raw[1] = 0;
        ctl_trb.data_stage.data_buf_ptr = va_to_pa(urb->transfer_buf);
        ctl_trb.data_stage.tr_len       = length;
        ctl_trb.data_stage.type         = XHCI_TRB_TYPE_DATA_STAGE;
        ctl_trb.data_stage.dir          = req_dir;
        ctl_trb.data_stage.chain        = TRB_CHAIN_DISABLE;
        ctl_trb.data_stage.ioc          = TRB_IOC_DISABLE;
        xhci_submit_ring_enq(ring, &ctl_trb);
    }

    // [阶段 3: Status TRB]
    ctl_trb.raw[0] = 0;
    ctl_trb.raw[1] = 0;
    ctl_trb.status_stage.type  = XHCI_TRB_TYPE_STATUS_STAGE;
    ctl_trb.status_stage.chain = TRB_CHAIN_DISABLE;
    ctl_trb.status_stage.ioc   = wants_ioc;
    ctl_trb.status_stage.dir   = (length == 0 || req_dir == USB_DIR_OUT) ? TRB_DIR_IN : TRB_DIR_OUT;

    last_trb_pa = xhci_submit_ring_enq(ring, &ctl_trb);
    return last_trb_pa;
}

/**
 * @brief [内联执行器] 处理 Bulk/Interrupt 普通传输 (大块切片与 ZLP)
 */
static inline uint64 xhci_submit_normal_transfer(usb_urb_t *urb, xhci_submit_ring_t *ring, uint8 wants_ioc) {
    uint64 last_trb_pa = 0;
    xhci_trb_t normal_trb;
    uint32 left_len = urb->transfer_len;

    uint64 current_pa = va_to_pa(urb->transfer_buf);

    uint16 max_packet = urb->ep->max_packet_size;
    if (max_packet == 0) max_packet = 512;

    uint8 needs_zlp = (urb->transfer_flags & URB_ZERO_PACKET) &&
                      (urb->transfer_len > 0) &&
                      ((urb->transfer_len % max_packet) == 0);

    normal_trb.raw[0] = 0;
    normal_trb.raw[1] = 0;
    normal_trb.normal.trb_type = XHCI_TRB_TYPE_NORMAL;
    normal_trb.normal.int_target = 0;

    while (left_len > 0) {
        uint32 space_to_boundary = 0x10000 - (current_pa & 0xFFFF);
        uint8  has_more_data = (left_len > space_to_boundary);
        uint32 chunk_len = has_more_data ? space_to_boundary : left_len;

        normal_trb.normal.data_buf_ptr = current_pa;
        normal_trb.normal.trb_tr_len   = chunk_len;
        // ★ 修复：如果数据没发完，或者虽然数据发完了但必须跟一个 ZLP，Chain 都必须为 1
        normal_trb.normal.chain = has_more_data || needs_zlp;
        // ★ 修复：全村唯一的 IOC 只能在绝对的最后一块 TRB 上点亮 (防双重中断风暴)
        normal_trb.normal.ioc   = (!has_more_data && !needs_zlp) ? wants_ioc : 0;

        last_trb_pa = xhci_submit_ring_enq(ring, &normal_trb);

        current_pa += chunk_len;
        left_len   -= chunk_len;
    }
    // 🎁 4. 极客彩蛋追加：精准下发 ZLP 空车厢
    if (needs_zlp) {
        normal_trb.normal.data_buf_ptr = current_pa; // 指针停在哪无所谓，长度为 0
        normal_trb.normal.trb_tr_len   = 0;          // 核心：0 字节载荷！
        normal_trb.normal.chain        = 0;          // 绝对的最后一环，拉断链条
        normal_trb.normal.ioc          = wants_ioc;  // 👑 赋予这节空车厢唤醒 CPU 的权利

        last_trb_pa = xhci_submit_ring_enq(ring, &normal_trb);
    }

    return last_trb_pa;
}

/**
 * @brief xHCI 终极大一统提交引擎 (URB 核心调度器)
 * 职责：解析纯逻辑的 URB 面单，将其翻译为 xHCI 硬件认识的 TRB 物理切片，
 * 压入对应的传输环，精确处理 ZLP 与中断边界，并最终敲响物理门铃。
 * * @param urb 已经由上层驱动填好的 URB 标准电子面单
 * @return int 0 表示提交成功，负数表示失败
 */
int32 usb_submit_urb(usb_urb_t *urb) {
    // ==========================================================
    // 1. 终极防御：面单防呆校验
    // ==========================================================
    if (!urb || !urb->udev || !urb->ep) {
        return -EINVAL; // (22) Invalid argument: 面单要素不全
    }

    // 如果是普通传输且没有数据，也没有强行要求发 0 字节包，直接视为成功返回
    if (urb->setup_packet == NULL && urb->transfer_len == 0 &&
        !(urb->transfer_flags & URB_ZERO_PACKET)) {
        return 0;
    }

    // ==========================================================
    // 2. 自动解包：提取物理寻址信息
    // ==========================================================
    xhci_hcd_t  *xhcd    = urb->udev->xhcd;
    uint8       slot_id  = urb->udev->slot_id;
    usb_ep_t    *ep = urb->ep;
    xhci_submit_ring_t *ring = &ep->ring_arr[urb->stream_id];

    // 计算真实的敲门铃目标值 (DCI + Stream ID 偏移)
    uint32 db_target = ep->ep_dci | ((uint32)urb->stream_id << 16);

    // 👑 标志位翻译哲学：默认开启中断，除非显式指定了 URB_NO_INTERRUPT
    uint8 wants_ioc = !(urb->transfer_flags & URB_NO_INTERRUPT);

    uint64 last_trb_pa = 0;

    // ==========================================================
    // 3. 多态路由分发 (根据端点描述符的硬件属性进行精准打击)
    // ==========================================================

    //加锁
    uint64 cpu_flags;
    spin_lock_irqsave(&ring->ring_lock, &cpu_flags);

    // 从端点上下文中提取真实的端点类型 (枚举时解析到的)
    uint8 usb_trans_type = ep->ep_type & 3;
    switch (usb_trans_type) {
        // 👑 路由 1：控制传输 (专属的三阶段状态机)
        case USB_EP_TYPE_CONTROL:
            if (urb->setup_packet == NULL) return -EINVAL; // 控制传输必须有 Setup 包
            last_trb_pa = xhci_submit_control_transfer(urb, ring, wants_ioc);
            break;

            // 👑 路由 2：同步传输 (摄像头/麦克风/声卡)
        case USB_EP_TYPE_ISOCH:
            // 以后写多媒体驱动时，在这里挂载 Isoch TRB 引擎
            // last_trb_pa = xhci_submit_isoc_transfer(urb, ring, wants_ioc);
            color_printk(RED, BLACK, "ISOC Transfer not implemented yet!\n");
            return -ENOSYS; // (38) Function not implemented: 驱动暂未实现该功能

            // 👑 路由 3：批量传输 (U盘) & 中断传输 (键鼠)
            // 它们在 xHCI 底层完全共享 Normal TRB 切片大循环！
        case USB_EP_TYPE_BULK:
        case USB_EP_TYPE_INTR:
            last_trb_pa = xhci_submit_normal_transfer(urb, ring, wants_ioc);
            break;

        default:
            color_printk(RED, BLACK, "Unknown Endpoint Type!\n");
            return -EPROTO; // (71) Protocol error: 端点描述符解析出错了
    }

    // ==========================================================
    // 3. 状态回填供上层同步等待使用
    // ==========================================================
    urb->last_trb_pa = last_trb_pa;
    urb->status = -EINPROGRESS; // (115) Operation now in progress: 异步操作已入队，正在执行！
    urb->is_done = FALSE;    // 🌟 初始化为未完成

    if (!(urb->transfer_flags & URB_NO_INTERRUPT)) {
        // 需要中断：挂入链表，等 ISR 叫醒
        list_add_tail(&ring->pending_list, &urb->node);
    }

    //解锁
    spin_unlock_irqrestore(&ring->ring_lock, cpu_flags);

    // ==========================================================
    // ★ 终极一击：精确敲响对应端点 / Stream 的物理门铃！
    // ==========================================================
    xhci_ring_doorbell(xhcd, slot_id, db_target);

    return 0;
}

/**
 * @brief 动态分配一个纯净的 URB 面单
 * @return usb_urb_t* 成功返回指针，失败返回 NULL
 */
usb_urb_t *usb_alloc_urb(void) {
    // 1. 从内核堆内存中申请一块空间
    usb_urb_t *urb = (usb_urb_t *)kzalloc(sizeof(usb_urb_t));
    if (urb == NULL) {
        color_printk(RED, BLACK, "USB Core: Failed to allocate URB!\n");
        return NULL;
    }

    // 以后如果引入了引用计数 (kref) 或自旋锁，也会在这里初始化

    return urb;
}

/**
 * @brief 安全销毁一个 URB 面单，并智能回收载荷内存
 * @param urb 需要销毁的 URB 指针
 */
void usb_free_urb(usb_urb_t *urb) {
    if (urb == NULL) return; // 防御性拦截

    // 👑 架构师彩蛋：智能内存托管 (对应 Linux 的 URB_FREE_BUFFER)
    // 如果上层驱动在提交时打了这个标志，USB Core 会在销毁 URB 时，
    // “顺手”把挂载的数据缓冲区也给释放掉，极大减轻上层驱动的内存管理心智负担！
    if (urb->transfer_flags & URB_FREE_BUFFER) {
        if (urb->transfer_buf != NULL) {
            kfree(urb->transfer_buf);
            urb->transfer_buf = NULL;
        }
    }

    // 彻底释放 URB 面单本身的内存
    kfree(urb);
}

/**
 * @brief 快速初始化 控制传输 (Control Transfer) URB
 */
void usb_fill_control_urb(usb_urb_t *urb,
                                        usb_dev_t *udev,
                                        usb_ep_t *ep,
                                        usb_setup_packet_t *setup_packet,
                                        void *transfer_buf,
                                        uint32 transfer_len) {
    urb->udev         = udev;
    urb->ep           = ep;       // 控制端点通常传 1
    urb->stream_id    = 0;            // 控制传输没有 Stream
    urb->setup_packet = setup_packet; // 必填：8字节协议头
    urb->transfer_buf = transfer_buf;
    urb->transfer_len = transfer_len;

    // 默认标志位设为 0 (表示开启中断、不开启ZLP等标准行为)
    urb->transfer_flags = 0;
    urb->status         = 0;
    urb->actual_length  = 0;
}

/**
 * @brief 快速初始化 批量传输 (Bulk Transfer) URB
 */
void usb_fill_bulk_urb(usb_urb_t *urb,
                                     usb_dev_t *udev,
                                     usb_ep_t *ep,
                                     void *transfer_buf,
                                     uint32 transfer_len) {
    urb->udev         = udev;
    urb->ep           = ep;
    urb->stream_id    = 0;
    urb->setup_packet = NULL;         // 👑 核心标志：批量传输绝对没有 Setup 包
    urb->transfer_buf = transfer_buf;
    urb->transfer_len = transfer_len;

    urb->transfer_flags = 0;
    urb->status         = 0;
    urb->actual_length  = 0;
}


/**
 * @brief 同步发送控制传输 (USB Core 的核心枢纽)
 * @return int32 0 表示成功，负数表示 POSIX 标准错误码
 */
int32 usb_control_msg_sync(usb_dev_t *udev, usb_setup_packet_t *setup_pkg, void *data_buf) {
    // 1. 动态申请 URB 面单
    usb_urb_t *urb = usb_alloc_urb();
    if (!urb) {
        return -ENOMEM; // ★ POSIX 修正：内存耗尽
    }

    // 2. 使用填单助手，一键压制参数
    // 注意：这里的 udev->eps[1] 对应的是控制端点 EP0 (DCI=1)
    usb_fill_control_urb(urb, udev, udev->ueps[1], setup_pkg, data_buf, setup_pkg->length);

    // 3. 将面单抛给底层调度引擎
    int32 posix_err = usb_submit_urb(urb);

    while (urb->is_done == FALSE) {
        asm_pause();
    }

    // 4. 过河拆桥：任务完成，彻底销毁 URB 面单！
    usb_free_urb(urb);

    // ★ POSIX 修正：拒绝粗暴的 return -1，完美透传底层状态！
    return posix_err;
}



/* @param udev   USB 设备上下文
 * @param ep_dci xHCI 的端点上下文索引 (DCI)
 * @param is_set USB_REQ_CLEAR_FEATURE(解锁)， USB_REQ_SET_FEATURE(上锁)
 * @return int32 0 表示成功，负数表示底层 POSIX 错误
 */
int32 usb_ep_halt_control(usb_dev_t *udev, uint8 ep_dci, usb_request_e halt_action) {
    usb_setup_packet_t setup_pkg = {0};

    // 组装标准 Setup 包
    setup_pkg.recipient = USB_RECIP_ENDPOINT;
    setup_pkg.req_type  = USB_REQ_TYPE_STANDARD;
    setup_pkg.dtd       = USB_DIR_OUT;           // 方向：主机发往设备

    // ★ 核心差异点：根据 halt_action 动态切换指令
    setup_pkg.request   = halt_action;

    setup_pkg.value     = USB_FEATURE_ENDPOINT_HALT;
    setup_pkg.index     = epdci_to_epaddr(ep_dci);
    setup_pkg.length    = 0;

    // 直接通过 EP0 发送控制传输并透传错误码
    return usb_control_msg_sync(udev, &setup_pkg, NULL);
}



/**
 * @brief 获取描述符
 * @return int32 获取成功返回 0，失败务必返回 POSIX 错误码 (调用者必须检查！)
 */
int32 usb_get_desc(usb_dev_t *udev, void *desc_buf, uint16 length, usb_desc_type_e desc_type, uint8 desc_idx, uint16 req_idx) {
    usb_setup_packet_t setup_pkg = {0};

    setup_pkg.recipient = desc_type == USB_DESC_TYPE_HID_REPORT ? USB_RECIP_INTERFACE : USB_RECIP_DEVICE;
    setup_pkg.req_type  = USB_REQ_TYPE_STANDARD;
    setup_pkg.dtd       = USB_DIR_IN;
    setup_pkg.request   = USB_REQ_GET_DESCRIPTOR;
    setup_pkg.value     = (desc_type << 8) | desc_idx;
    setup_pkg.index     = req_idx;
    setup_pkg.length    = length;

    // ★ POSIX 修正：坚决不能丢弃获取描述符的状态！
    return usb_control_msg_sync(udev, &setup_pkg, desc_buf);
}


/**
 * @brief 激活配置
 */
int32 usb_set_cfg(usb_dev_t *udev, uint8 cfg_value) {
    usb_setup_packet_t setup_pkg = {0};
    setup_pkg.recipient = USB_RECIP_DEVICE;
    setup_pkg.req_type  = USB_REQ_TYPE_STANDARD;
    setup_pkg.dtd       = USB_DIR_OUT;
    setup_pkg.request   = USB_REQ_SET_CONFIGURATION;
    setup_pkg.value     = cfg_value;
    setup_pkg.index     = 0;
    setup_pkg.length    = 0;

    // ★ POSIX 修正：透传错误
    return usb_control_msg_sync(udev, &setup_pkg, NULL);
}


/**
 * @brief 激活接口
 */
int32 usb_set_if(usb_dev_t *udev, uint8 if_num, uint8 alt_num) {
    usb_setup_packet_t setup_pkg = {0};
    setup_pkg.recipient = USB_RECIP_INTERFACE;
    setup_pkg.req_type  = USB_REQ_TYPE_STANDARD;
    setup_pkg.dtd       = USB_DIR_OUT;
    setup_pkg.request   = USB_REQ_SET_INTERFACE;
    setup_pkg.value     = alt_num;
    setup_pkg.index     = if_num;
    setup_pkg.length    = 0;

    // ★ POSIX 修正：透传错误
    return usb_control_msg_sync(udev, &setup_pkg, NULL);
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
    input_ep_ctx->max_pstreams = new_ep->enable_streams_exp;
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
void usb_tx_add_ep(usb_dev_t *udev, usb_ep_t *new_ep) {
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
void usb_tx_drop_ep(usb_dev_t *udev, usb_ep_t *ep) {
    // 1. 打上死刑标记
    udev->input_ctx->drop_context_flags |= (1 << ep->ep_dci);

    // 2. ★ 使用你的 O(1) 汇编魔法，算出删除后的新 context_entries
    ctx_update_entries(udev);

}

/**
 * @brief [纯内存] 准备微调端点参数 (仅限 Evaluate Context 使用)
 * @note 绝不能置位 Drop Flag！常用于修正 EP0 的包长。
 */
void usb_tx_eval_ep(usb_dev_t *udev, usb_ep_t *new_ep) {
    // 1. 仅打上添加(微调)标记
    udev->input_ctx->add_context_flags |= (1 << new_ep->ep_dci);

    // 2. 拷贝新参数覆写图纸
    ctx_ep_copy(udev, new_ep);

}

/**
 * @brief [纯内存] 重建/热更新端点参数 (用于开启多流等核心属性变更)
 * @note 必须由 Configure Endpoint Command 提交。硬件会先拆除旧端点，再同屏无缝建立新端点。
 */
void usb_tx_reconfig_ep(usb_dev_t *udev, usb_ep_t *new_ep) {
    // 1. 打上死刑标记 (告诉硬件：旧的图纸作废了)
    udev->input_ctx->drop_context_flags |= (1 << new_ep->ep_dci);

    // 2. 打上新生标记 (告诉硬件：在同一个位置，按新图纸原地重建)
    udev->input_ctx->add_context_flags |= (1 << new_ep->ep_dci);

    // 4. 拷贝包含新属性 (如 max_streams 等) 的图纸
    ctx_ep_copy(udev, new_ep);
}

/**
 * @brief [纯内存] 准备初始化 Slot Context 基座 (专用于 Address Device 创世阶段)
 * @param udev       目标 USB 设备
 * @param port_speed 设备的物理连接速度
 */
static void usb_tx_init_slot(usb_dev_t *udev) {
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
void usb_tx_eval_slot(usb_dev_t *udev) {
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
int32 usb_tx_commit(usb_dev_t *udev, usb_tx_cmd_e cmd_type) {
    xhci_input_ctx_t *input_ctx = udev->input_ctx;
    int32 ret = 0;

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
            // ★ POSIX 修正：调用者传入了驱动不支持的指令宏，属于极其严重的传参错误
            color_printk(RED, BLACK, "xHCI: Invalid commit command type: %d\n", cmd_type);
            return -EINVAL;
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

// 给端点分配环 (终极统一抽象版)
static int32 alloc_ep_ring(usb_ep_t *ep) {
    uint64 tr_dequeue_ptr;

    //根据类型非配环长度
    uint32 ring_size = 64;
    uint8 usb_trans_type = ep->ep_type & 3;
    switch (usb_trans_type) {
        case USB_EP_TYPE_CONTROL:
            ring_size = 64;
            break;
        case USB_EP_TYPE_ISOCH:
            ring_size = 1024;
            break;
        case USB_EP_TYPE_BULK:
            ring_size = 512;
            break;
        case USB_EP_TYPE_INTR:
            ring_size = 64;
    }

    // 安全边界截断
    uint32 streams_exp = ep->enable_streams_exp;

    if (streams_exp) {
        // ==========================================================
        // 👑 情况 B：流模式 (Stream Mode - 适用于 USB 3.0 UAS 等)
        // ==========================================================
        uint32 streams_count = (1 << streams_exp)+1;           // 流环的第0个不能用，所以需要：2^4 +1 = 17
        uint32 streams_array_count = 1 << (streams_exp + 1);     // 硬件要求流数组需要按倍数对齐，例如streams_exp=4,则2^5 = 32

        // 1. 给硬件 DMA 读的上下文数组 (必须 16 字节对齐)
        xhci_stream_ctx_t *streams_ctx_array = kzalloc_dma(streams_array_count * sizeof(xhci_stream_ctx_t));
        // 记录上下文，方便后续释放内存
        ep->streams_ctx_array = streams_ctx_array;

        // 2. ★ 核心重构：给软件管理的统一环数组 (分配 N+1 个)
        // 索引 0 闲置防越界，索引 1~N 对应真实的 Stream ID
        ep->ring_arr = kzalloc(streams_count * sizeof(xhci_submit_ring_t));

        // 更新逻辑状态
        ep->lsa = 1; // 线性流数组标志
        ep->hid = 1; // 主机初始化禁用标志

        // 初始化每一个流环
        for (uint32 s = 1; s < streams_count; s++) {
            // 对数组中的每一个环进行物理分配
            xhci_alloc_submit_ring(&ep->ring_arr[s],ring_size);

            // 将分配好的环的物理地址，写入硬件要求的 Context 数组中
            // SCT=1 (Primary TRB Ring: bit 1~3), DCS=1 (bit 0)
            streams_ctx_array[s].tr_dequeue = va_to_pa(ep->ring_arr[s].ring_base) | (1 << 1) | 1;
            streams_ctx_array[s].reserved = 0;
        }

        // Stream ID 0 在硬件规范中是保留的，必须清零
        streams_ctx_array[0].tr_dequeue = 0;
        streams_ctx_array[0].reserved = 0;

        // 硬件 Endpoint Context 需要的是整个 Stream Context 数组的首地址
        tr_dequeue_ptr = va_to_pa(streams_ctx_array);

    } else {
        // ==========================================================
        // 👑 情况 A：非流模式 (No-Stream Mode - 经典 Transfer Ring)
        // ==========================================================

        // 1. ★ 核心重构：给软件管理的统一环数组 (仅分配 1 个)
        ep->ring_arr = kzalloc(sizeof(xhci_submit_ring_t));

        // 2. 分配并初始化这唯一的环 (它就是 rings[0])
        xhci_alloc_submit_ring(&ep->ring_arr[0],ring_size);

        // 更新逻辑状态
        ep->lsa = 0;
        ep->hid = 0;

        // 硬件 Endpoint Context 直接要这个单一环的物理首地址，DCS=1
        tr_dequeue_ptr = va_to_pa(ep->ring_arr[0].ring_base) | 1;
    }

    // ==========================================================
    // 终极赋值：写回端点上下文结构体
    // ==========================================================
    ep->cerr = 3;
    ep->trq_phys_addr = tr_dequeue_ptr; // 供后续写入 xHCI 硬件的 Endpoint Context

    return 0;
}



// 释放端点环 (终极统一抽象版)
static int32 free_ep_ring(usb_ep_t *ep) {
    // 0. 防御性拦截：如果根本没分配过，直接返回
    if (ep == NULL || ep->ring_arr == NULL) {
        return -EINVAL;
    }

    if (ep->enable_streams_exp > 0) {
        // ==========================================================
        // 👑 情况 B：流模式 (Stream Mode) 的销毁
        // ==========================================================

        // 1. 释放每一个具体的流环 (TRB 物理内存)
        uint32 enable_streams_count = (1<<ep->enable_streams_exp)+1;
        for (uint32 s = 1; s < enable_streams_count; s++) {
            xhci_free_submit_ring(&ep->ring_arr[s]);
        }

        // 2. 释放专供硬件读取的 DMA 上下文数组
        if (ep->streams_ctx_array != NULL) {
            kfree(ep->streams_ctx_array); // ★ 修复：流模式特有的 DMA 内存释放
            ep->streams_ctx_array = NULL;
        }

    } else {
        // ==========================================================
        // 👑 情况 A：非流模式 (No-Stream Mode) 的销毁
        // ==========================================================

        // 释放那唯一的普通传输环
        xhci_free_submit_ring(&ep->ring_arr[0]);
    }

    // ==========================================================
    // ★ 终极统一回收：拆除统一调度层
    // ==========================================================
    // 无论是流模式分配的 N+1 个元素的数组，还是非流模式分配的 1 个元素的数组，
    // 它们都是用 kzalloc 申请的，最后在这里一刀切释放！
    kfree(ep->ring_arr);
    ep->ring_arr = NULL;

    // 清理端点逻辑状态，恢复出厂设置，防止悬空指针引发 Use-After-Free
    ep->enable_streams_exp = 0; // 如果你结构体里还没删干净，顺手清一下
    ep->lsa = 0;
    ep->hid = 0;
    ep->trq_phys_addr = 0;

    return 0; // 成功释放
}

/**
 * @brief 切换 USB 备用接口 (Linux 严谨基线版)
 * @param new_alt 上层驱动通过 find_alt 系列函数搜索到的目标图纸句柄
 * @return int32  0 表示成功，非 0 表示失败
 */
int32 usb_switch_alt_if(usb_if_alt_t *new_alt) {
    // 1. 终极防御：如果搜索函数返回了 NULL，或者这是一个脏指针，直接拦截！
    if (new_alt == NULL || new_alt->uif == NULL) return -EINVAL;

    int32 posix = 0;

    // 2. 顺藤摸瓜：通过你的反向指针，直接拉出上层接口和设备对象！
    usb_if_t *uif = new_alt->uif;
    usb_dev_t *udev = uif->udev;

    // 注意：设备刚刚插入进行 Config 初始化时，cur_alt 是 NULL，必须防范
    usb_if_alt_t *old_alt = uif->cur_uif_alt;

    // 3. 性能优化：如果想切换的就是当前正在用的，直接光速返回
    if (old_alt == new_alt) return -EINVAL;

    // 开启 xHCI 底层硬件事务
    usb_tx_begin(udev);

    // ==========================================================
    // 阶段 1：[纸上谈兵] 圈出要 Drop 的旧端点
    // ==========================================================
    if (old_alt != NULL) {
        for (uint8 i = 0; i < old_alt->uep_count; i++) {
            usb_tx_drop_ep(udev, &old_alt->ueps[i]);
        }
    }

    // ==========================================================
    // 阶段 2：[预分配] 为新端点画图纸并分配内存 (★ 核心架构重构)
    // ==========================================================
    for (uint8 i = 0; i < new_alt->uep_count; i++) {
        usb_ep_t *ep = &new_alt->ueps[i];

        // 👑 Linux 架构铁律：两级火箭分离！
        // 接口切换是底层总线的职责，底层绝不越俎代庖去开启动态流。
        // 一律将 num_streams 强行阉割为 0，按最基础的 Bulk 模式建立物理通道。
        // 满血多流的升级任务，必须留给上层 UAS 驱动事后去调用 usb_alloc_streams！
        ep->enable_streams_exp = 0;

        // ★ OOM 防御：分配环可能因为物理 DMA 内存耗尽而失败
        posix = alloc_ep_ring(ep);
        if (posix < 0) {
            color_printk(RED, BLACK, "USB: OOM allocating rings during Alt setting!\n");
            // 局部回滚：释放刚才循环里已经分配成功的前几个端点
            for (uint8 j = 0; j < i; j++) free_ep_ring(&new_alt->ueps[j]);
            return posix;
        }

        // 准备好图纸
        usb_tx_add_ep(udev, ep);
    }

    // ==========================================================
    // 阶段 3：一锤定音！向 xHCI 提交图纸，等待硬件裁决
    // ==========================================================
    posix = usb_tx_commit(udev, USB_TX_CMD_CFG_EP);
    if (posix < 0) {
        color_printk(RED, BLACK, "xHCI: Switch AltSetting failed, hardware rejected!\n");
        // 主板拒绝了这份图纸，销毁刚刚新分配的所有内存，安全退出
        for (uint8 i = 0; i < new_alt->uep_count; i++) free_ep_ring(&new_alt->ueps[i]);
        return posix;
    }

    // ==========================================================
    // ★ 阶段 4：[防竞态] 提前挂载新路由！(兵马未动，粮草先行)
    // 此时新通道在主板端已通，但外设还未切换。先把接收器架好，防漏包！
    // ==========================================================
    for (uint8 i = 0; i < new_alt->uep_count; i++) {
        usb_ep_t *ep = &new_alt->ueps[i];
        udev->ueps[ep->ep_dci] = ep;
    }

    // ==========================================================
    // ★ 阶段 5：主板软件均就绪，正式通过 EP0 通知物理 U 盘切换频道！
    // ==========================================================
    posix = usb_set_if(udev, uif->if_num, new_alt->altsetting);
    if (posix < 0) {
        color_printk(RED, BLACK, "USB: Device rejected Set Interface command!\n");

        // ！！！极品回滚逻辑 (完美修复版) ！！！

        // 1. 软件路由撤销 (把刚才挂上去的新路由摘下来)
        for (uint8 i = 0; i < new_alt->uep_count; i++) {
            udev->ueps[new_alt->ueps[i].ep_dci] = NULL;
        }

        // ★ 修复：重新挂载老端点的路由，否则系统再也找不到旧端点通信了！
        if (old_alt != NULL) {
            for (uint8 i = 0; i < old_alt->uep_count; i++) {
                udev->ueps[old_alt->ueps[i].ep_dci] = &old_alt->ueps[i];
            }
        }

        // 2. 释放新分配的废弃环内存
        for (uint8 i = 0; i < new_alt->uep_count; i++) free_ep_ring(&new_alt->ueps[i]);

        // 3. 将主板硬件强制回滚到旧状态 (反向 Drop 新的，Add 旧的)
        usb_tx_begin(udev);
        for (uint8 i = 0; i < new_alt->uep_count; i++) usb_tx_drop_ep(udev, &new_alt->ueps[i]);
        if (old_alt != NULL) {
            for (uint8 i = 0; i < old_alt->uep_count; i++) usb_tx_add_ep(udev, &old_alt->ueps[i]);
        }
        int32 tx_err = usb_tx_commit(udev, USB_TX_CMD_CFG_EP);
        if (tx_err < 0) {
            color_printk(RED,BLACK, "xHCI: Device rejected Committed Config Command!\n");
            return tx_err;
        }
        return posix; // 经过这一套抢救，操作系统和设备毫发无伤地回到了切换前的健康状态！
    }

    // ==========================================================
    // 阶段 6：[过河拆桥] 切换彻底成功，可以安全收缴旧端点的尸体了
    // ==========================================================
    if (old_alt != NULL) {
        for (uint8 i = 0; i < old_alt->uep_count; i++) {
            usb_ep_t *ep = &old_alt->ueps[i];
            // 只在路由没有被新端点(同 DCI)覆盖的情况下，才去清空它
            if (udev->ueps[ep->ep_dci] == ep) {
                udev->ueps[ep->ep_dci] = NULL;
            }
            free_ep_ring(ep); // 彻底释放旧物理内存
        }
    }

    // 状态机正式翻页
    uif->cur_uif_alt = new_alt;

    return 0;
}


/**
 * @brief 为 USB 端点组协商并分配流(Streams)模式物理内存
 * @param udev 外设上下文
 * @param eps 需要开启流模式的端点数组 (比如 UAS 的 Data IN 和 Data OUT)
 * @param eps_count 端点数量
 * @param expected_streams_exp 期望的流指数 (并发量 = 2^exp)
 * * @return int32
 * < 0 : 底层 POSIX 错误码 (如 -EINVAL, -ENOMEM)，表示配置彻底失败。
 * == 0: 降级为传统普通模式 (主板或外设不支持，或入参期望为0)。
 * > 0 : 成功开启流模式，返回最终多方妥协后的流指数 (Stream Exponent)。
 */
int32 usb_enable_streams(usb_dev_t *udev, usb_ep_t **eps, uint8 eps_count, uint8 expected_streams_exp) {
    if (!udev || !eps || eps_count == 0) return -EINVAL;
    if (expected_streams_exp == 0) return 0;

    int32 posix_err = 0;

    // ==========================================================
    // 阶段 1：疯狂的“最短板”算计 (The Short-Board Evaluation)
    // ==========================================================

    // 1. xHCI最大流和期望最大流取极小值
    uint8 mini_streams_exp = udev->xhcd->max_streams_exp;
    if (mini_streams_exp > expected_streams_exp) {
        mini_streams_exp = expected_streams_exp;
    }

    // 2. [探底外设]：遍历所有传入的端点，寻找流能力最低的那一个
    for (uint8 i = 0; i < eps_count; i++) {
        usb_ep_t *ep = eps[i];

        // 读取端点描述符里解析出的硬件原始流指数极限
        uint8 ep_max_streams_exp = ep->max_streams_exp;

        if (ep_max_streams_exp == 0) {
            // 致命短板：只要这几个核心端点里有一个完全不支持流，全体降级！
            color_printk(YELLOW, BLACK, "USB: EP %d does not support streams. Fallback to 2.0\n", ep->ep_dci);
            return 0;
        }

        if (mini_streams_exp > ep_max_streams_exp) {
            mini_streams_exp = ep_max_streams_exp; // 动态更新最短板
        }
    }

    // 此时，mini_streams_exp 已经代表了“主板、外设、期望”三方妥协后的最终并发极限！

    // ==========================================================
    // 阶段 2：底层硬件“多流升级” (xHCI 规范强制要求的两段式提交)
    // ==========================================================

    // ----------------------------------------------------------
    // 🚀 事务一：彻底拆除旧端点 (迫使其进入 Disabled 状态)
    // ----------------------------------------------------------
    usb_tx_begin(udev);

    for (uint8 i = 0; i < eps_count; i++) {
        usb_ep_t *ep = eps[i];
        usb_tx_drop_ep(udev, ep); // 仅仅打上 Drop 标记
    }

    // 提交事务一：主板收到后，会正式解除对这些端点旧物理内存的占用！
    posix_err = usb_tx_commit(udev, USB_TX_CMD_CFG_EP);
    if (posix_err < 0) {
        color_printk(RED, BLACK, "xHCI: Failed to drop endpoints for stream setup!\n");
        return posix_err;
    }

    // ----------------------------------------------------------
    // 🚀 事务二：以多流姿态原地复活 (从 Disabled 切换回 Running)
    // ----------------------------------------------------------
    usb_tx_begin(udev);

    for (uint8 i = 0; i < eps_count; i++) {
        usb_ep_t *ep = eps[i];

        // 1. 此时硬件已经彻底放手，旧内存绝对安全了！放心 Free！
        posix_err = free_ep_ring(ep);
        if (posix_err < 0) {
            return  posix_err;
        }

        // 2. 状态机翻页：正式赋予端点流能力参数
        ep->enable_streams_exp = mini_streams_exp;

        // 3. 重新分配物理内存：alloc_ep_ring 会根据 enable_streams_exp 分配 2^(N+1) 个流环
        posix_err = alloc_ep_ring(ep);
        if (posix_err < 0) {
            color_printk(RED, BLACK, "USB: OOM allocating stream rings!\n");
            // 注意：此时端点处于 Disabled，若 OOM，整个设备只能重新复位或降级
            return posix_err;
        }

        // 4. 将带有流参数的新端点加入图纸 (仅仅打上 Add 标记)
        usb_tx_add_ep(udev, ep);
    }

    // 提交事务二：主板看到端点从 Disabled 醒来，且带有多流参数，完美接受！
    posix_err = usb_tx_commit(udev, USB_TX_CMD_CFG_EP);
    if (posix_err < 0) {
        color_printk(RED, BLACK, "xHCI: Failed to add endpoints with streams!\n");
        return posix_err;
    }

    return mini_streams_exp; // 返回协商后的最终指数，供上层分配 Tag
}


//===================================================== 解析描述符非配资源 ============================================

//给备用接口的所有端点分配环
static inline int32 enable_alt_if (usb_if_alt_t *if_alt) {
    usb_dev_t *udev = if_alt->uif->udev;

    int32 posix_err = 0;

    // ★ 开启事务，拿出一张空白的 Configure Endpoint 申请表
    usb_tx_begin(udev);

    // 配置该接口下的所有端点
    for (uint8 i = 0; i < if_alt->uep_count; i++) {
        usb_ep_t *ep = &if_alt->ueps[i];
        uint8 ep_dci = ep->ep_dci;

        // 指针放到udev.eps中，建立全局 DCI 快速索引
        udev->ueps[ep_dci] = ep;

        //给端点分配环
        posix_err = alloc_ep_ring(ep);
        if (posix_err < 0) {
            return posix_err;
        }

        // ★ 将端点挂载到 Input Context 申请表中
        usb_tx_add_ep(udev, ep);
    }

    // ★ 扣动扳机！将申请表通过 Command Ring 提交给主板，并敲响门铃
    posix_err = usb_tx_commit(udev, USB_TX_CMD_CFG_EP);
    if (posix_err < 0) {
        return posix_err;
    }

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
    cur_ep->max_streams_exp = 0;
    cur_ep->bytes_per_interval = 0;
    cur_ep->extras_desc = NULL;
    cur_ep->lsa = 0;
    cur_ep->hid = 0;

    cur_ep->ring_arr = NULL;
    cur_ep->streams_ctx_array = NULL;
    cur_ep->enable_streams_exp = 0;

    // --- ★ 衍生参数与 DMA 启发值联合推导 (基于 USB 2.0 规格底稿) ---
    switch (usb_trans_type) {
        case USB_EP_TYPE_ISOCH:
            // Isochronous 阵营：音视频流，要求极高的周期吞吐量
            cur_ep->max_esit_payload = cur_ep->max_packet_size * (cur_ep->mult + 1);
            // 等时流永远是满载发送，直接使用最大周期负荷作为平均值
            cur_ep->average_trb_length = cur_ep->max_esit_payload;
            break;

        case USB_EP_TYPE_BULK:
            // Bulk 阵营：吃总线闲置带宽，无固定 ESIT 周期限制
            cur_ep->max_esit_payload = 0;
            // 黄金魔法值：3072 (3 个 USB 3.0 数据包) 完美平衡 PCIe 突发与硬件 FIFO
            cur_ep->average_trb_length = 3072;
            break;

        case USB_EP_TYPE_INTR:
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
        case USB_EP_TYPE_BULK:
            // Bulk 阵营：提取最大支持的并发流数量 (Streams)
            cur_ep->max_streams_exp = ss_desc->attributes & 0x1F;
            break;

        case USB_EP_TYPE_ISOCH:
            // Isochronous 阵营：提取真实乘数，原地覆写掉第一阶段的 USB 2.0 伪值
            cur_ep->mult = ss_desc->attributes & 0x03;

            // 衍生参数升级：直接用硬件出厂标定的周期诉求，替换掉 USB 2.0 的计算公式
            if (cur_ep->bytes_per_interval > 0) {
                cur_ep->max_esit_payload = cur_ep->bytes_per_interval;
                cur_ep->average_trb_length = cur_ep->max_esit_payload; // 同步升级 DMA 估算值
            }
            break;

        case USB_EP_TYPE_INTR:
            // Interrupt 阵营：规范铁律要求伴随属性为保留位。强行清零防止主板报错
            cur_ep->mult = 0;
            cur_ep->max_streams_exp = 0;

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
static inline int32 alt_if_desc_parse(usb_if_alt_t *if_alt) {
    usb_ep_t *cur_ep = NULL;
    usb_desc_head *desc_head = usb_get_next_desc(&if_alt->if_desc->head);
    uint8 ep_idx = 0;

    void *cfg_end = usb_cfg_end(if_alt->uif->udev->config_desc);

    // 严密防御：限定搜索范围绝对不能超出整个配置描述符
    while ((desc_head < cfg_end) && (desc_head->desc_type != USB_DESC_TYPE_INTERFACE)) {

        if (desc_head->desc_type == USB_DESC_TYPE_ENDPOINT) {
            // 防缓冲区溢出！恶意的描述符数量不能超过声明的数量
            if (ep_idx >= if_alt->uep_count) {
                break;
            }

            // 阶段 1：分发给标准解析器
            cur_ep = &if_alt->ueps[ep_idx++];
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
    udev->uif_count = 0;
    udev->uifs = kzalloc(sizeof(usb_if_t) * udev->config_desc->num_interfaces);
    if (!udev->uifs) return -1;

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
            usb_if_t *usb_if = &udev->uifs[udev->uif_count++];
            usb_if->if_num = i;
            usb_if->if_alt_count = alt_count[i];
            usb_if->uif_alts = kzalloc(sizeof(usb_if_alt_t) * usb_if->if_alt_count);

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
static inline int32 if_desc_parse(usb_dev_t *udev, usb_if_t **usb_if_map) {
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
            usb_if_alt_t *if_alt = &usb_if->uif_alts[idx];

            // 填充业务属性
            if_alt->uif = usb_if;
            if_alt->if_desc = if_desc;
            if_alt->altsetting = if_desc->alternate_setting;
            if_alt->if_class = if_desc->interface_class;
            if_alt->if_subclass = if_desc->interface_subclass;
            if_alt->if_protocol = if_desc->interface_protocol;
            if_alt->uep_count = if_desc->num_endpoints;

            // 为该 alt 分配端点内存，并触发底层解析引擎
            if (if_alt->uep_count > 0) {
                if_alt->ueps = kzalloc(if_alt->uep_count * sizeof(usb_ep_t));
                alt_if_desc_parse(if_alt);
            }
        }
        if_desc = (usb_if_desc_t *)usb_get_next_desc(&if_desc->head);
    }

    // =================================================================
    // 阶段 B：图纸绘制完毕，开始向主板申请硬件 DMA 高速公路
    // =================================================================
    for (uint32 i = 0; i < udev->uif_count; i++) {
        usb_if_t *usb_if = &udev->uifs[i];

        if (usb_if != NULL) {
            // 默认锁定 alt 0 备用接口 (包含极其严密的兜底容错逻辑)
            usb_if_alt_t *alt0 = usb_find_alt_by_num(usb_if, 0);
            usb_if->cur_uif_alt = alt0 ? alt0 : &usb_if->uif_alts[0];

            // 呼叫主板：分配 Transfer Ring 并下发 Configure Endpoint
            enable_alt_if(usb_if->cur_uif_alt);
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
    if_desc_parse(udev, usb_if_map);


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
    xhcd->udevs[udev->slot_id] = udev;

    //分配input上下文
    udev->input_ctx = kzalloc_dma(XHCI_INPUT_CONTEXT_COUNT * ctx_size);

    //挂载到 O(1) 路由表
    usb_ep_t *ep0 = &udev->uep0;
    udev->ueps[1] = ep0;  //警告端点0为eps1，方便后续通过ep-dci查找端点。

    // --- 计算初始 Max Packet Size ---
    udev->port_speed = xhci_get_port_speed(xhcd, udev->port_id);
    uint32 mps = (udev->port_speed >= XHCI_PORTSC_SPEED_SUPER) ? 512 :
                 (udev->port_speed == XHCI_PORTSC_SPEED_HIGH)  ? 64  : 8;

    //填充端点0
    ep0->ep_dci = 1;
    ep0->cerr = 3;
    ep0->ep_type = 4; // Control Endpoint
    ep0->max_packet_size = mps;
    ep0->average_trb_length = mps;
    ep0->max_streams_exp = 0;
    ep0->enable_streams_exp = 0;
    alloc_ep_ring(ep0);

    // ---下发命令 ---
    usb_tx_begin(udev);
    usb_tx_init_slot(udev);
    usb_tx_add_ep(udev,ep0);
    usb_tx_commit(udev,USB_TX_CMD_ADDR_DEV);

    return 0;
}

/**
 * @brief 阶段 2：通过 EP0 获取设备描述符，并动态修正全速设备的 MPS
 * @param udev USB 设备对象
 * @return int32 0 表示成功
 */
static inline int32 get_dev_desc(usb_dev_t *udev) {


    // 分配设备描述符的 DMA 内存
    usb_dev_desc_t *dev_desc = kzalloc_dma(sizeof(usb_dev_desc_t));

    // ============================
    // 全速设备 (FS) 的 8 字节刺探与修正逻辑
    // ============================
    if (udev->port_speed == XHCI_PORTSC_SPEED_FULL) {

        // 探针：只拿前 8 字节
        usb_get_desc(udev, dev_desc, 8, USB_DESC_TYPE_DEVICE, 0, 0);

        if (dev_desc->max_packet_size0 != 8) {
            usb_ep_t *ep0 = udev->ueps[1];
            ep0->max_packet_size = dev_desc->max_packet_size0;
            usb_tx_begin(udev);
            usb_tx_eval_ep(udev,ep0);
            usb_tx_commit(udev,USB_TX_CMD_EVAL_CTX);
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
    uint32 times = 30000000;
    while (times--) {

    }
    // if ( xhci_wait_for_event(xhcd, 0,XHCI_TRB_TYPE_PORT_STATUS_CHG ,port_id,0,0, 30000000, NULL) == XHCI_COMP_TIMEOUT) {
    //     return -1; // 超时失败
    // }

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


//端口插入设备处理
int32 xhci_handle_port_connection (xhci_hcd_t *xhcd,uint8 port_id) {
        color_printk(GREEN,BLACK,"portsc:%#x       \n",xhci_read_portsc(xhcd, port_id));
        if (usb_port_init(xhcd, port_id) == 0) {
            color_printk(GREEN,BLACK,"portsc:%#x       \n",xhci_read_portsc(xhcd, port_id));
            usb_dev_t *usb_dev = usb_dev_create(xhcd, port_id);
            usb_dev_register(usb_dev);
            usb_if_create(usb_dev);
            usb_if_register(usb_dev);
        } else {
            // 如果复位失败，比如劣质 U 盘无法响应，直接跳过，保护操作系统不挂死
            color_printk(YELLOW, BLACK, "[xHCI] Ignored faulty device on port %d.\n", port_id);
        }
}

//端口拔出设备处理
int32 xhci_handle_port_disconnection(xhci_hcd_t *xhcd,uint8 port_id) {

    color_printk(YELLOW,BLACK,"[xHCI] disconnection port %d.\n]",port_id);

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
        if (portsc & XHCI_PORTSC_CCS ) {//目前采用轮训等待方式暂时只要ccs置为就进行初始化
            xhci_handle_port_connection(xhcd, port_id);
        }
    }
}

//======================================= 驱动======================================

//匹配驱动id
static inline usb_id_t *usb_match_id(usb_if_t *usb_if, driver_t *drv) {
    usb_id_t *id_table = drv->id_table;
    uint8 if_class = usb_if->cur_uif_alt->if_class;
    uint8 if_protocol = usb_if->cur_uif_alt->if_protocol;
    uint8 if_subclass = usb_if->cur_uif_alt->if_subclass;
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
