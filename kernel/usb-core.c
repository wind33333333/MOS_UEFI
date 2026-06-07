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
    posix_err = xhci_cmd_set_tr_deq_ptr(udev->xhcd, udev->slot_id, ep_dci, udev->eps[ep_dci]->ring_arr);
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
 * @brief 核心控制传输枢纽 (大一统接口)
 * @param ... 散装参数映射到 Setup 包的 8 个字节
 */
int32 usb_control_msg(usb_dev_t *udev, void *data_buf,
                      usb_data_dir_e dtd, usb_req_type_e req_type, usb_recipient_e recipient,
                      usb_request_e request, uint16 value, uint16 index,uint16 length) {
    // 1. 在这里统一组装 Setup 包！
    usb_setup_packet_t setup_pkg = {
        .dtd       = dtd,
        .req_type  = req_type,
        .recipient = recipient,
        .request   = request,
        .value     = value,
        .index     = index,
        .length    = length
    };

    // 2. 动态申请 URB 面单
    usb_urb_t *urb = usb_alloc_urb();
    if (!urb) return -ENOMEM;

    // 3. 使用填单助手压制参数 (ep0 = udev->ueps[1])
    usb_fill_control_urb(urb, udev, udev->eps[1], &setup_pkg, data_buf, length);

    // 4. 将面单抛给底层调度引擎
    int32 posix_err = usb_submit_urb(urb);

    uint32 times = 0x7000000;
    while (urb->is_done == FALSE && times) {
        asm_pause();
        times--;
    }

    if (times == 0) {
        posix_err = ETIMEDOUT;
    }

    usb_free_urb(urb);
    return posix_err;
}


// ==========================================
// 📄 设备级描述符获取 API (直通控制传输枢纽)
// ==========================================

/**
 * @brief 获取设备描述符 (Device Descriptor)
 * @note 全局唯一，不需要索引，不需要语言 ID
 */
static inline int32 _usb_get_dev_desc(usb_dev_t *udev, void *buf, uint16 len) {
    return usb_control_msg(udev, buf,
                           USB_DIR_IN, USB_REQ_TYPE_STANDARD, USB_RECIP_DEVICE,
                           USB_REQ_GET_DESCRIPTOR, (USB_DESC_TYPE_DEVICE << 8) | 0, 0, len);
}

/**
 * @brief 获取配置描述符 (Configuration Descriptor)
 * @param config_index 配置的索引 (通常为 0，代表第 1 个配置)
 */
static inline int32 _usb_get_cfg_desc(usb_dev_t *udev, uint8 config_index, void *buf, uint16 len) {
    return usb_control_msg(udev, buf,
                           USB_DIR_IN, USB_REQ_TYPE_STANDARD, USB_RECIP_DEVICE,
                           USB_REQ_GET_DESCRIPTOR, (USB_DESC_TYPE_CONFIG << 8) | config_index, 0, len);
}

/**
 * @brief 获取字符串描述符 (String Descriptor)
 * @param string_index 设备描述符中指定的字符串索引 (如 iManufacturer)
 * @param lang_id      语言 ID (通常传入 0x0409 代表美式英语)
 */
static inline int32 _usb_get_string_desc(usb_dev_t *udev, uint8 string_index, uint16 lang_id, void *buf, uint16 len) {
    return usb_control_msg(udev, buf,
                           USB_DIR_IN, USB_REQ_TYPE_STANDARD, USB_RECIP_DEVICE,
                           USB_REQ_GET_DESCRIPTOR, (USB_DESC_TYPE_STRING << 8) | string_index, lang_id, len);
}

/**
 * @brief 获取 BOS 描述符 (Binary Object Store - USB 3.0+ 专属)
 * @note 全局唯一，不需要索引
 */
static inline int32 usb_get_bos_desc(usb_dev_t *udev, void *buf, uint16 len) {
    return usb_control_msg(udev, buf,
                           USB_DIR_IN, USB_REQ_TYPE_STANDARD, USB_RECIP_DEVICE,
                           USB_REQ_GET_DESCRIPTOR, (USB_DESC_TYPE_BOS << 8) | 0, 0, len);
}




//=============================================================================================================

//============================================== 上下文操作函数 ===========================================================

/*/**
 * @brief 开启一个事务：将硬件真实状态同步到input
 #1#
static inline void usb_ctx_sync(usb_dev_t *udev) {
    // 1. 物理清零管控中心 (Input Control Context，占 1 个 ctx_size)
    // 彻底消灭上一次下发命令残留的 Add/Drop 幽灵标志位
    input_ctrl_ctx_t *input_ctrl_ctx = udev->in_ctx;
    input_ctrl_ctx->add_context_flags = 0;
    input_ctrl_ctx->drop_context_flags = 0;

    // 2. 完美的移花接木：将主板维护的 Device Context 拷贝到 Input Context 的数据区
    // 注意偏移量：Input Context 从第 1 个条目开始，才是 Slot 和 EP
    uint8 ctx_size = udev->xhcd->ctx_size;
    void *input_ctx = xhci_get_input_ctx_entry(input_ctrl_ctx,0,ctx_size);

    // 3. 拷贝 32 个 Context (1 个 Slot + 31 个 EP)
    asm_mem_cpy(udev->out_ctx, input_ctx, ctx_size * 32);
}


/**
 * @brief [纯内存] 全量同步 Slot 上下文
 * @note 无论是创世(Address)、基建(CFG_EP)还是微调(EVAL_CTX)，统一调用此函数。
 * 硬件会根据下发的命令类型，自动提取它关心的字段，忽略不关心的字段。
 * @param udev 目标 USB 设备 (真理之源)
 #1#
static inline void usb_ctx_slot_sync(usb_dev_t *udev) {
    input_ctrl_ctx_t *input_ctrl_ctx = udev->in_ctx;

    // 1. 核心算法：算出投影位图并更新 context_entries
    uint32 projected_map = (udev->active_ep_map | input_ctrl_ctx->add_context_flags) & ~input_ctrl_ctx->drop_context_flags;
    udev->context_entries = 31 - asm_lzcnt32(projected_map);

    // 2. 强制打上 Slot 图纸被涂改的标记 (Bit 0 特权)
    // 既然是全量同步，Slot 必定被修改，直接置位
    input_ctrl_ctx->add_context_flags |= (1 << 0);

    // 3. 拿到 Slot Context 的图纸指针
    xhci_slot_ctx_t *slot = xhci_get_input_ctx_entry(input_ctrl_ctx, 0,udev->xhcd->ctx_size);

    // ===================================================================
    // 维度 A：基础物理与路由属性 (Address Device 创世阶段核心)
    // ===================================================================
    slot->route_string = udev->route_string;
    slot->speed = udev->psiv;
    slot->root_hub_port_num = udev->root_port_num;
    slot->parent_hub_slot_id = udev->parent_hub ? udev->parent_hub->slot_id : 0;
    slot->parent_port_num = udev->parent_port_num;
    slot->context_entries = udev->context_entries;

    // ===================================================================
    // 维度 B：集线器全局拓扑属性 (Configure Endpoint 基建阶段核心)
    // ===================================================================
    slot->is_hub = udev->is_hub;
    slot->num_ports = udev->hub_num_ports;
    slot->mtt = udev->hub_mtt;
    slot->tt_think_time = udev->hub_ttt;

    // ===================================================================
    // 维度 C：行政路由与电源管理 (Evaluate Context 微调阶段核心)
    // ===================================================================
    slot->interrupter_target = udev->interrupter_target;
    slot->max_exit_latency = udev->max_exit_latency;
}


//同步端点上下文属性
static inline void usb_ctx_ep_sync(usb_dev_t *udev, usb_ep_t *ep,usb_ctx_ep_op_e op) {
    input_ctrl_ctx_t *input_ctrl_ctx = udev->in_ctx;
    uint8 dci = ep->ep_dci;

    // 解析 Add 意图：只要是新建、微调、热重构，都必须置位并同步最新软件属性
    if (op == USB_CTX_EP_ADD || op == USB_CTX_EP_EVAL || op == USB_CTX_EP_RECONFIG) {
        input_ctrl_ctx->add_context_flags |= (1 << dci);

        xhci_ep_ctx_t *ep_ctx = xhci_get_input_ctx_entry(udev->in_ctx, dci,udev->xhcd->ctx_size);
        ep_ctx->mult = ep->mult;
        ep_ctx->max_pstreams = ep->enable_streams_exp;
        ep_ctx->lsa = ep->lsa;
        ep_ctx->interval = ep->interval;
        ep_ctx->max_esit_payload_hi = (ep->max_esit_payload>>16)&0xFF;
        ep_ctx->cerr = ep->cerr;
        ep_ctx->ep_type = ep->ep_type;
        ep_ctx->hid = ep->hid;
        ep_ctx->max_burst_size = ep->max_burst;
        ep_ctx->max_packet_size = ep->max_packet_size;
        ep_ctx->tr_dequeue_ptr = ep->trq_phys_addr;
        ep_ctx->average_trb_length = ep->average_trb_length;
        ep_ctx->max_esit_payload_lo = ep->max_esit_payload&0xFFFF;
    }

    // 解析 Drop 意图：只要是强拆、热重构，都必须置位，通知硬件回收旧资源
    if (op == USB_CTX_EP_DROP || op == USB_CTX_EP_RECONFIG) {
        input_ctrl_ctx->drop_context_flags |= (1 << dci);
    }

}


/**
 * @brief [物理通信] 统一事务提交引擎：下发命令，等待判决，同步状态
 * @param udev     目标 USB 设备
 * @param cmd_type 事务指令类型
 * @return 0 表示成功，非 0 表示硬件拒绝并已回滚
 #1#
static inline int32 usb_ctx_commit(usb_dev_t *udev, usb_ctx_cmd_e cmd_type) {
    input_ctrl_ctx_t *input_ctrl_ctx = udev->in_ctx;
    int32 ret = 0;

    // ==========================================
    // 阶段 1：根据指令类型，扣动对应的物理硬件扳机
    // ==========================================
    switch (cmd_type) {
        case USB_CTX_CMD_ADDR:
            ret = xhci_cmd_addr_dev(udev->xhcd, udev->slot_id, input_ctrl_ctx);
            break;

        case USB_CTX_CMD_EVAL:
            ret = xhci_cmd_eval_ctx(udev->xhcd, input_ctrl_ctx, udev->slot_id);
            break;

        case USB_CTX_CMD_CFG:
            ret = xhci_cmd_cfg_ep(udev->xhcd, input_ctrl_ctx, udev->slot_id, 0); // DC = 0
            break;

        case USB_CTX_CMD_DECFG_ALL:
            // 注意：Deconfigure 模式下，主板会直接无视 input_ctx 的内容
            ret = xhci_cmd_cfg_ep(udev->xhcd, input_ctrl_ctx, udev->slot_id, 1); // DC = 1 (核武器开启)
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
    if (cmd_type == USB_CTX_CMD_DECFG_ALL) {
        // ★ 格式化特例：硬件已经把 EP1~31 全杀光了，软件必须强制同步
        // 仅保留 Slot (Bit 0) 和 EP0 (Bit 1) 存活
        udev->active_ep_map = (1 << 0) | (1 << 1);
    } else {
        // ★ 常规增量同步：根据图纸里的 Drop 和 Add 标志位，精确更新存活位图
        udev->active_ep_map &= ~input_ctrl_ctx->drop_context_flags;
        udev->active_ep_map |= input_ctrl_ctx->add_context_flags;
    }

    return 0; // 事务完美落地！
}


/**
 * @brief [核心 API] 执行原子化的上下文事务
 * @param udev     目标 USB 设备
 * @param cmd_type 事务指令类型 (ADDR, EVAL, CFG, DECFG_ALL)
 * @param actions  端点操作意图数组 (可传 NULL)
 * @param count    操作意图的数量 (可传 0)
 * @param icc_env  ICC 审批环境 (仅限 CFG 命令使用，其它传 NULL)
 * @return 0 成功，非 0 失败
 #1#
int32 usb_ctx_execute(usb_dev_t *udev, usb_ctx_cmd_e cmd,usb_ctx_action_t *actions,uint8 action_count){
    // ==========================================
    // 阶段 1：自动开启事务 (绝对不会忘记物理清零)
    // ==========================================
    usb_ctx_sync(udev); // 内部执行: flags=0, memcpy(dev_ctx -> input_ctx)

    // ==========================================
    // 阶段 2：自动遍历推演端点 (代替手动的 ep_op)
    // ==========================================
    for (uint8 i = 0; i < action_count; i++) {
        usb_ctx_ep_sync(udev,actions[i].ep,actions[i].op);
    }

    // ==========================================
    // 阶段 3：惰性同步全局基座 (Slot & 算出最终 context_entries)
    // ==========================================
    usb_ctx_slot_sync(udev);

    // ==========================================
    // 阶段 4：智能防御与环境刻录 (隔离 CFG 的特殊逻辑)
    // ==========================================
    return usb_ctx_commit(udev,cmd);

}*/

//////////////////////////////////////////////////////////////////////

//获取 Input Context 数组中的指定条目
static inline void *usb_get_in_ctx_entry(input_ctrl_ctx_t *input_ctx, uint32 dci,uint8 ctx_size) {
    return (uint8 *)input_ctx + ctx_size * (dci + 1);
}


//获取 Device Context 数组中的指定条目
static inline void *usb_get_out_ctx_entry(void* out_ctx,uint32 dci,uint8 ctx_size) {
    return (uint8*)out_ctx + ctx_size * dci;
}

/**
 * @brief 开启一个事务：将硬件真实状态同步到input
 */
static void usb_ctx_in_sync(usb_dev_t *udev) {
    // 1. 物理清零管控中心 (Input Control Context，占 1 个 ctx_size)
    // 彻底消灭上一次下发命令残留的 Add/Drop 幽灵标志位
    input_ctrl_ctx_t *input_ctrl_ctx = udev->in_ctx;
    input_ctrl_ctx->add_context_flags = 0;
    input_ctrl_ctx->drop_context_flags = 0;

    // 2. 完美的移花接木：将主板维护的 Device Context 拷贝到 Input Context 的数据区
    // 注意偏移量：Input Context 从第 1 个条目开始，才是 Slot 和 EP
    uint8 ctx_size = udev->xhcd->ctx_size;
    void *in_ctx = usb_get_in_ctx_entry(input_ctrl_ctx,0,ctx_size);

    // 3. 拷贝 32 个 Context (1 个 Slot + 31 个 EP)
    asm_mem_cpy(udev->out_ctx, in_ctx, ctx_size * 32);
}

/**
 * @brief [纯内存] 全量同步 Slot 上下文
 * @note 无论是创世(Address)、基建(CFG_EP)还是微调(EVAL_CTX)，统一调用此函数。
 * 硬件会根据下发的命令类型，自动提取它关心的字段，忽略不关心的字段。
 * @param udev 目标 USB 设备 (真理之源)
 */
static void usb_ctx_slot_update(usb_dev_t *udev) {
    input_ctrl_ctx_t *input_ctrl_ctx = udev->in_ctx;

    // 1. 强制打上 Slot 图纸被涂改的标记 (Bit 0 特权)
    // 既然是全量同步，Slot 必定被修改，直接置位
    input_ctrl_ctx->add_context_flags |= (1 << 0);

    // 2. 核心算法：算出投影位图并更新 context_entries,先删后建
    uint32 projected_map = (udev->active_ep_map & ~input_ctrl_ctx->drop_context_flags) | input_ctrl_ctx->add_context_flags;
    udev->context_entries = 31 - asm_lzcnt32(projected_map);


    // 3. 拿到 Slot Context 的图纸指针
    xhci_slot_ctx_t *slot = usb_get_in_ctx_entry(input_ctrl_ctx, 0,udev->xhcd->ctx_size);

    // ===================================================================
    // 维度 A：基础物理与路由属性 (Address Device 创世阶段核心)
    // ===================================================================
    slot->route_string = udev->route_string;
    slot->speed = udev->psiv;
    slot->root_hub_port_num = udev->root_port_num;
    slot->parent_hub_slot_id = udev->parent_hub ? udev->parent_hub->slot_id : 0;
    slot->parent_port_num = udev->parent_port_num;
    slot->context_entries = udev->context_entries;

    // ===================================================================
    // 维度 B：集线器全局拓扑属性 (Configure Endpoint 基建阶段核心)
    // ===================================================================
    slot->is_hub = udev->is_hub;
    slot->num_ports = udev->hub_num_ports;
    slot->mtt = udev->hub_mtt;
    slot->tt_think_time = udev->hub_ttt;

    // ===================================================================
    // 维度 C：行政路由与电源管理 (Evaluate Context 微调阶段核心)
    // ===================================================================
    slot->interrupter_target = udev->interrupter_target;
    slot->max_exit_latency = udev->max_exit_latency;
}

//增加一个端点上下文
static void usb_ctx_ep_add(usb_dev_t *udev, usb_ep_t *ep) {
    input_ctrl_ctx_t *input_ctrl_ctx = udev->in_ctx;
    uint8 dci = ep->ep_dci;
    input_ctrl_ctx->add_context_flags |= (1 << dci);

    xhci_ep_ctx_t *ep_ctx = usb_get_in_ctx_entry(udev->in_ctx, dci,udev->xhcd->ctx_size);
    ep_ctx->mult = ep->mult;
    ep_ctx->max_pstreams = ep->enable_streams_exp;
    ep_ctx->lsa = ep->lsa;
    ep_ctx->interval = ep->interval;
    ep_ctx->max_esit_payload_hi = (ep->max_esit_payload>>16)&0xFF;
    ep_ctx->cerr = ep->cerr;
    ep_ctx->ep_type = ep->ep_type;
    ep_ctx->hid = ep->hid;
    ep_ctx->max_burst_size = ep->max_burst;
    ep_ctx->max_packet_size = ep->max_packet_size;
    ep_ctx->tr_dequeue_ptr = ep->trq_phys_addr;
    ep_ctx->average_trb_length = ep->average_trb_length;
    ep_ctx->max_esit_payload_lo = ep->max_esit_payload&0xFFFF;

}


//删除一个端点上下文
static inline void usb_ctx_ep_drop(usb_dev_t *udev, usb_ep_t *ep) {
    udev->in_ctx->drop_context_flags |= (1 << ep->ep_dci);
}


typedef enum : uint8 {
    USB_CTX_CMD_ADDR,    // 事务：分配地址 (无中生有创世)
    USB_CTX_CMD_EVAL,    // 事务：评估上下文 (微调参数，如 EP0 包长)
    USB_CTX_CMD_CFG,      // 事务：配置端点 (常规增删业务端点，DC=0)
    USB_CTX_CMD_DECFG_ALL    // 事务：格式化端点 (一键抹除所有业务端点，保留 EP0，DC=1)
} usb_ctx_cmd_e;

/**
 * @brief [物理通信] 统一事务提交引擎：下发命令，等待判决，同步状态
 * @param udev     目标 USB 设备
 * @param cmd_type 事务指令类型
 * @return 0 表示成功，非 0 表示硬件拒绝并已回滚
 */
static int32 usb_ctx_commit(usb_dev_t *udev, usb_ctx_cmd_e cmd_type) {
    input_ctrl_ctx_t *in_ctx = udev->in_ctx;
    int32 ret = 0;

    // 1. 发射物理指令
    switch (cmd_type) {
        case USB_CTX_CMD_ADDR:      ret = xhci_cmd_addr_dev(udev->xhcd, udev->slot_id, in_ctx); break;
        case USB_CTX_CMD_EVAL:      ret = xhci_cmd_eval_ctx(udev->xhcd, udev->slot_id, in_ctx); break;
        case USB_CTX_CMD_CFG:       ret = xhci_cmd_cfg_ep(udev->xhcd, udev->slot_id, in_ctx, 0); break;
        case USB_CTX_CMD_DECFG_ALL:     ret = xhci_cmd_cfg_ep(udev->xhcd, udev->slot_id, NULL, 1); break;
        default: return -EINVAL;
    }

    if (ret != 0) return ret; // 硬件拒绝，完美回滚

    // 2. 🌟 事务成功，真理对齐！(修复了你的状态丢失 Bug)
    if (cmd_type == USB_CTX_CMD_DECFG_ALL) {
        udev->active_ep_map = (1 << 0) | (1 << 1); // 仅留 Slot 和 EP0
        udev->context_entries = 1;                 // 瞬间格式化最大端点
    } else {
        udev->active_ep_map &= ~udev->in_ctx->drop_context_flags;
        udev->active_ep_map |= udev->in_ctx->add_context_flags;
    }

    return 0;
}


//地址分配 + EP0 初始化（创世）
static int32 usb_ctx_addr_dev(usb_dev_t *udev) {
    usb_ctx_in_sync(udev);
    usb_ctx_ep_add(udev,udev->eps[1]);
    usb_ctx_slot_update(udev);
    return usb_ctx_commit(udev,USB_CTX_CMD_ADDR);
}

//配置slot context
int32 usb_ctx_slot_cfg(usb_dev_t *udev) {
    usb_ctx_in_sync(udev);
    usb_ctx_slot_update(udev);
    return usb_ctx_commit(udev,USB_CTX_CMD_CFG);
}

//微调slot ep0 context
int32 usb_ctx_slot_ep0_eval(usb_dev_t *udev) {
    usb_ctx_in_sync(udev);
    usb_ctx_ep_add(udev,udev->eps[1]);
    usb_ctx_slot_update(udev);
    return usb_ctx_commit(udev,USB_CTX_CMD_EVAL);
}


//批量增删改业务端点
int32 usb_ctx_eps_cfg(usb_if_alt_t *drop_uif_alt,usb_if_alt_t *add_uif_alt) {

    usb_dev_t *udev = NULL;
    uint8 drop_num_ep = 0;
    uint8 add_num_ep = 0;

    // 1. 提取设备指针并进行防御性检查
    if (drop_uif_alt) {
        udev = drop_uif_alt->uif->udev;
        drop_num_ep = drop_uif_alt->if_desc->num_endpoints;
    }

    if (add_uif_alt) {
        // 🌟 防御性护城河：防止传入的两个接口不属于同一个设备
        if (udev != NULL && udev != add_uif_alt->uif->udev) {
            return -EINVAL;
        }

        udev = add_uif_alt->uif->udev;
        add_num_ep = add_uif_alt->if_desc->num_endpoints;
    }

    // 2. 基础校验
    if (udev == NULL) return -EINVAL;

    // 🌟 逻辑修正：如果新旧配置都没有业务端点，直接返回成功 (No-op)
    if (drop_num_ep == 0 && add_num_ep == 0) {
        return 0;
    }

    // 4. 准备 Input Context
    usb_ctx_in_sync(udev);

    // 5. Drop 旧端点
    for (uint8 i = 0; i < drop_num_ep; i++) {
        usb_ctx_ep_drop(udev,&drop_uif_alt->eps[i]);
    }

    // 6. Add 新端点
    for (uint8 i = 0; i < add_num_ep; i++) {
        usb_ctx_ep_add(udev,&add_uif_alt->eps[i]);
    }

    // 7. 更新 Slot Context（重新计算 context_entries）
    usb_ctx_slot_update(udev);

    // 8. 提交 Configure Endpoint 命令
    return usb_ctx_commit(udev,USB_CTX_CMD_CFG);
}

//批量清空业务端点
int32 usb_ctx_deconfigure_all(usb_dev_t *udev ) {
    return usb_ctx_commit(udev,USB_CTX_CMD_DECFG_ALL);
}



//===================================================================================================================

device_type_t usb_dev_type = {"usb-dev"};
device_type_t usb_if_type = {"usb-if"};

// 给端点分配环 (终极统一抽象版)
static int32 usb_alloc_ep_ring(usb_ep_t *ep) {
    uint64 tr_dequeue_ptr;

    // 安全边界截断
    uint32 streams_exp = ep->enable_streams_exp;

    if (streams_exp) {
        // ==========================================================
        // 👑 情况 B：流模式 (Stream Mode - 适用于 USB 3.0 UAS 等)
        // ==========================================================
        uint32 num_streams = (1 << streams_exp)+1;           // 流环的第0个不能用，所以需要：2^4 +1 = 17
        uint32 num_streams_array = 1 << (streams_exp + 1);     // 硬件要求流数组需要按倍数对齐，例如streams_exp=4,则2^5 = 32

        // 1. 给硬件 DMA 读的上下文数组 (必须 16 字节对齐)
        xhci_stream_ctx_t *streams_ctx_array = kzalloc_dma(num_streams_array * sizeof(xhci_stream_ctx_t));
        // 记录上下文，方便后续释放内存
        ep->streams_ctx_array = streams_ctx_array;

        // 2. ★ 核心重构：给软件管理的统一环数组 (分配 N+1 个)
        // 索引 0 闲置防越界，索引 1~N 对应真实的 Stream ID
        ep->ring_arr = kzalloc(num_streams * sizeof(xhci_submit_ring_t));

        // 更新逻辑状态
        ep->lsa = 1; // 线性流数组标志
        ep->hid = 1; // 主机初始化禁用标志

        // 初始化每一个流环
        for (uint32 s = 1; s < num_streams; s++) {
            // 对数组中的每一个环进行物理分配
            xhci_alloc_submit_ring(&ep->ring_arr[s],ep->ring_max_trbs);

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
        xhci_alloc_submit_ring(&ep->ring_arr[0],ep->ring_max_trbs);

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
static int32 usb_free_ep_ring(usb_ep_t *ep) {
    // 0. 防御性拦截：如果根本没分配过，直接返回
    if (ep == NULL || ep->ring_arr == NULL) {
        return -EINVAL;
    }

    if (ep->enable_streams_exp > 0) {
        // ==========================================================
        // 👑 情况 B：流模式 (Stream Mode) 的销毁
        // ==========================================================

        // 1. 释放每一个具体的流环 (TRB 物理内存)
        uint32 enable_num_streams = (1<<ep->enable_streams_exp)+1;
        for (uint32 s = 1; s < enable_num_streams; s++) {
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
 * @brief 在接口中寻找匹配指定 Class/SubClass/Protocol 的备用接口
 * @param uif      目标 USB 接口对象
 * @param class    匹配大类 (传 USB_MATCH_ANY 表示忽略该条件)
 * @param subclass 匹配子类 (传 USB_MATCH_ANY 表示忽略)
 * @param protocol 匹配协议 (传 USB_MATCH_ANY 表示忽略)
 * @return usb_if_alt_t* 找到的备用接口指针。未找到则返回 NULL。
 */
usb_if_alt_t* usb_find_alt_if(usb_if_t *uif, int16 class, int16 subclass, int16 protocol) {
    // 防野指针
    if (!uif || !uif->if_alts) return NULL;

    for (uint8 i = 0; i < uif->num_if_alts; i++) {
        usb_if_desc_t *if_desc = uif->if_alts[i].if_desc;

        // 注意：现在比较是 int16 级别，-1 和 0xFF(255) 是彻底不同的两个值！
        if (class != USB_MATCH_ANY && if_desc->interface_class != class) continue;
        if (subclass != USB_MATCH_ANY && if_desc->interface_subclass != subclass) continue;
        if (protocol != USB_MATCH_ANY && if_desc->interface_protocol != protocol) continue;

        return &uif->if_alts[i]; // 完美匹配！返回指针
    }
    return NULL; // 查找失败
}



/**
 * @brief 切换备用接口
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
    usb_if_alt_t *old_alt = uif->activity_if_alt;

    // 3. 性能优化：如果想切换的就是当前正在用的，直接光速返回
    if (old_alt == new_alt) return -EINVAL;

    // 2. 提前获取新端点数量
    uint8 new_num_eps = new_alt->if_desc->num_endpoints;

    // ==========================================================
    // 阶段 2：[预分配] 为新端点画图纸并分配内存 (★ 核心架构重构)
    // ==========================================================
    // 2. 提前获取新端点数量
    for (uint8 i = 0; i < new_num_eps; i++) {
        usb_ep_t *ep = &new_alt->eps[i];

        // ★ OOM 防御：分配环可能因为物理 DMA 内存耗尽而失败
        posix = usb_alloc_ep_ring(ep);
        if (posix < 0) {
            color_printk(RED, BLACK, "USB: OOM allocating rings during Alt setting!\n");
            // 局部回滚：释放刚才循环里已经分配成功的前几个端点
            for (uint8 j = 0; j < i; j++) usb_free_ep_ring(&new_alt->eps[j]);
            return posix;
        }
    }

    // ==========================================================
    // 阶段 3：一锤定音！向 xHCI 提交图纸，等待硬件裁决
    // ==========================================================
    posix = usb_ctx_eps_cfg(old_alt,new_alt);
    if (posix < 0) {
        color_printk(RED, BLACK, "xHCI: Switch AltSetting failed, hardware rejected!\n");
        // 主板拒绝了这份图纸，销毁刚刚新分配的所有内存，安全退出
        for (uint8 i = 0; i < new_num_eps; i++) usb_free_ep_ring(&new_alt->eps[i]);
        return posix;
    }

    // ==========================================================
    // ★ 阶段 4：[防竞态] 提前挂载新路由！(兵马未动，粮草先行)
    // 此时新通道在主板端已通，但外设还未切换。先把接收器架好，防漏包！
    // ==========================================================
    for (uint8 i = 0; i < new_num_eps; i++) {
        usb_ep_t *ep = &new_alt->eps[i];
        udev->eps[ep->ep_dci] = ep;
    }

    // ==========================================================
    // ★ 阶段 5：主板软件均就绪，正式通过 EP0 通知物理 U 盘切换频道！
    // ==========================================================
    posix = usb_set_if(udev, new_alt->if_desc->interface_number, new_alt->if_desc->alternate_setting);
    if (posix < 0) {
        color_printk(RED, BLACK, "USB: Device rejected Set Interface command!\n");

        // ！！！极品回滚逻辑 (完美修复版) ！！！

        // 1. 软件路由撤销 (把刚才挂上去的新路由摘下来)
        for (uint8 i = 0; i < new_num_eps; i++) {
            udev->eps[new_alt->eps[i].ep_dci] = NULL;
        }

        // ★ 修复：重新挂载老端点的路由，否则系统再也找不到旧端点通信了！
        for (uint8 i = 0; i < old_num_eps; i++) {
            udev->eps[old_alt->eps[i].ep_dci] = &old_alt->eps[i];
        }

        // 2. 释放新分配的废弃环内存
        for (uint8 i = 0; i < new_num_eps; i++) usb_free_ep_ring(&new_alt->eps[i]);

        // 3. 将主板硬件强制回滚到旧状态 (反向 Drop 新的，Add 旧的)
        usb_ctx_sync(udev);
        for (uint8 i = 0; i < new_num_eps; i++) usb_ctx_drop_ep(udev, &new_alt->eps[i]);
        if (old_alt != NULL) {
            for (uint8 i = 0; i < old_num_eps; i++) usb_ctx_add_ep(udev, &old_alt->eps[i]);
        }
        int32 tx_err = usb_ctx_commit(udev, USB_CTX_CMD_CFG);
        if (tx_err < 0) {
            color_printk(RED,BLACK, "xHCI: Device rejected Committed Config Command!\n");
            return tx_err;
        }
        return posix; // 经过这一套抢救，操作系统和设备毫发无伤地回到了切换前的健康状态！
    }

    // ==========================================================
    // 阶段 6：[过河拆桥] 切换彻底成功，可以安全收缴旧端点的尸体了
    // ==========================================================
    for (uint8 i = 0; i < old_num_eps; i++) {
        usb_ep_t *ep = &old_alt->eps[i];
        // 只在路由没有被新端点(同 DCI)覆盖的情况下，才去清空它
        if (udev->eps[ep->ep_dci] == ep) {
            udev->eps[ep->ep_dci] = NULL;
        }
        usb_free_ep_ring(ep); // 彻底释放旧物理内存
    }

    // 状态机正式翻页
    uif->activity_if_alt = new_alt;

    return 0;
}


/**
 * @brief [驱动层 API] 配置备用接口图纸的硬件资源诉求 (完美支持流与非流端点混编)
 * @note 必须在 usb_switch_alt_if 提交硬件前调用！
 * @return int32  最终成功协商出的流指数。如果为 0，表示全线降级为普通环。
 */
int32 usb_cfg_alt_if_resources(usb_if_alt_t *alt,uint8 want_streams_exp, uint32 want_trb_size) {
    if (!alt || !alt->uif->udev || !alt->if_desc) return -EINVAL;

    uint8 final_exp = 0;
    uint8 num_eps = alt->if_desc->num_endpoints;

    // =========================================================================
    // 阶段 1：【精准算计】只和“有能力开流”的端点进行博弈
    // =========================================================================
    if (want_streams_exp > 0) {
        uint8 host_max = alt->uif->udev->xhcd->max_streams_exp;
        final_exp = (host_max < want_streams_exp) ? host_max : want_streams_exp;

        for (uint8 i = 0; i < num_eps; i++) {
            uint8 ep_max = alt->eps[i].max_streams_exp;
            // 🌟 核心修复：放过它！不支持流的端点，不参与最短板算计
            if (ep_max == 0) continue;
            if (ep_max < final_exp) final_exp = ep_max;
        }

    }

    // =========================================================================
    // 阶段 2：【图纸重绘】流端点用多环，普通端点保单环，各取所需！
    // =========================================================================
    for (uint8 i = 0; i < num_eps; i++) {
        usb_ep_t *ep = &alt->eps[i];

        // 🌟 精准施策：只有硬件描述符里声明了能力的端点，才配得上 final_exp！
        // 那些 max_streams_exp == 0 的端点，将被乖乖清零，底层分配器会给它们建普通环。
        if (ep->max_streams_exp > 0) {
            ep->enable_streams_exp = final_exp;
        } else {
            ep->enable_streams_exp = 0;
        }

        // TRB 运力诉求可以一视同仁地覆盖 (普通环和流环都需要 TRB)
        ep->ring_max_trbs = want_trb_size;
    }

    return final_exp;
}


//===================================================== 解析描述符非配资源 ============================================

/**
 * @brief [内部辅助] 解析并装填 USB 标准端点参数 (USB 2.0 规格底稿)
 */
static inline void usb_ep_desc_params(usb_ep_t *cur_ep, usb_ep_desc_t *ep_desc) {
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
static inline void usb_ss_desc_params(usb_ep_t *cur_ep, usb_ss_comp_desc_t *ss_desc) {
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


//配置描述符结束地址
static inline void *usb_cfg_end(usb_cfg_desc_t *usb_config_desc)
{
    return (uint8*)usb_config_desc + usb_config_desc->total_length;
}



/**
 * @brief [终极精简版] 零碎片、一次性分配的 USB 描述符解析引擎
 * @param udev USB 设备对象
 * @return 0 表示成功，负数表示失败
 */
int32 usb_if_create(usb_dev_t *udev) {
    usb_cfg_desc_t *cfg_desc = udev->config_desc;
    if (!cfg_desc || cfg_desc->head.length < sizeof(usb_cfg_desc_t)) return -EINVAL;

    // =========================================================================
    // 阶段 1：[纯净统计] 只数人头，确认拓扑规模
    // =========================================================================
    uint32 total_alts = 0, total_eps = 0 ,num_ifs = 0;
    uint8 max_if_num = 0;

    usb_desc_head_t *desc_head = (usb_desc_head_t *)cfg_desc;
    void *cfg_end = (uint8 *)cfg_desc + cfg_desc->total_length;

    while ((void *)desc_head < cfg_end) {
        if (desc_head->length < 2) return -EINVAL; // 必不可少的防死锁

        if (desc_head->desc_type == USB_DESC_TYPE_INTERFACE) {
            total_alts++;
            uint8 if_num = ((usb_if_desc_t *)desc_head)->interface_number;
            if (if_num > max_if_num) max_if_num = if_num;
        }
        else if (desc_head->desc_type == USB_DESC_TYPE_ENDPOINT) {
            total_eps++;
        }
        desc_head = usb_get_next_desc(desc_head);
    }

    num_ifs = max_if_num + 1; // 真实接口数量

    // =========================================================================
    // 阶段 2 & 3：[切割地盘] 连续内存分配与总线拓扑绑定
    // =========================================================================
    uint32 mem_size = (num_ifs * sizeof(usb_if_t)) +
                      (total_alts * sizeof(usb_if_alt_t)) +
                      (total_eps * sizeof(usb_ep_t));

    void *mem_block = kzalloc(mem_size);
    if (!mem_block) return -ENOMEM;

    udev->ifs = (usb_if_t *)mem_block;
    usb_if_alt_t *alts_pool = (usb_if_alt_t *)(udev->ifs + num_ifs);
    usb_ep_t *eps_pool = (usb_ep_t *)(alts_pool + total_alts);

    // 绑定拓扑 (信任 kzalloc: num_if_alts 和 if_alts 已默认清零/为空，无需重复赋值)
    for (uint32 i = 0; i < num_ifs; i++) {
        udev->ifs[i].udev = udev;
        udev->ifs[i].dev.type = &usb_if_type;
        udev->ifs[i].dev.parent = &udev->dev;
        udev->ifs[i].dev.bus = &usb_bus_type;
    }

    // =========================================================================
    // 阶段 4：[血肉装填] 真 O(N) 状态机解析
    // =========================================================================
    desc_head = (usb_desc_head_t *)cfg_desc;
    usb_if_alt_t *cur_alt = NULL;
    usb_ep_t *cur_ep = NULL; // 🌟 核心优化：引入独立的端点游标

    while ((void *)desc_head < cfg_end) {
        if (desc_head->length < 2) break;

        switch (desc_head->desc_type) {
            case USB_DESC_TYPE_INTERFACE: {
                uint8 if_num = ((usb_if_desc_t *)desc_head)->interface_number;
                if (if_num < num_ifs) {
                    usb_if_t *cur_if = &udev->ifs[if_num];

                    cur_alt = alts_pool++;
                    if (cur_if->num_if_alts == 0) cur_if->if_alts = cur_alt;
                    cur_if->num_if_alts++;

                    cur_alt->uif = cur_if;
                    cur_alt->if_desc = (usb_if_desc_t *)desc_head;
                    cur_alt->eps = eps_pool;

                    cur_ep = NULL; // 🌟 跨入新接口辖区，清空当前端点游标！
                }
                break;
            }
            case USB_DESC_TYPE_ENDPOINT: {
                if (cur_alt) {
                    cur_ep = eps_pool++; // 🌟 获取当前端点实体
                    usb_ep_desc_params(cur_ep, (usb_ep_desc_t *)desc_head);
                }
                break;
            }
            case USB_DESC_TYPE_SS_ENDPOINT_COMPANION: {
                if (cur_ep) usb_ss_desc_params(cur_ep, (usb_ss_comp_desc_t *)desc_head);
                break;
            }
            default: {
                // 🌟 私有描述符挂载变得极其优雅，告别烧脑的指针地址比较
                if (cur_ep) {
                    if (!cur_ep->extras_desc) cur_ep->extras_desc = desc_head;
                    cur_ep->extras_len += desc_head->length;
                } else if (cur_alt) {
                    if (!cur_alt->extras_desc) cur_alt->extras_desc = desc_head;
                    cur_alt->extras_len += desc_head->length;
                }
                break;
            }
        }

        desc_head = usb_get_next_desc(desc_head);
    }

    return 0;
}


//注册usb接口
void usb_if_register(usb_dev_t *udev) {
    uint8 num_ifs = udev->config_desc->num_interfaces;
    for (uint32 i = 0; i < num_ifs; i++) {
        usb_if_t *usb_if = &udev->ifs[i];
        if (usb_if != NULL) {
            // 触发系统级的 match/probe (比如唤醒 bot.c 或 uas.c 驱动)
            device_register(&usb_if->dev);
        }
    }
}


/**
 * @brief 阶段 1：分配设备上下文，配置 Slot 和 EP0，并赋予物理地址
 * @param udev USB 设备对象
 * @return int32 0 表示成功，-1 表示失败
 */
static inline int32 usb_enable_slot_ep0(usb_dev_t *udev) {
    xhci_hcd_t *xhcd = udev->xhcd;

    //启用插槽
    int32 err = xhci_cmd_enable_slot(xhcd,udev->root_port_num,&udev->slot_id); //启用插槽
    if (err < 0) return err;

    //分配设备上下文
    uint8 ctx_size = xhcd->ctx_size;
    udev->out_ctx = kzalloc_dma(XHCI_DEVICE_CONTEXT_COUNT * ctx_size);
    xhcd->dcbaap[udev->slot_id] = va_to_pa(udev->out_ctx);
    xhcd->udevs[udev->slot_id] = udev;

    //分配input上下文
    udev->in_ctx = kzalloc_dma(XHCI_INPUT_CONTEXT_COUNT * ctx_size);

    //挂载到 O(1) 路由表
    usb_ep_t *uep0 = kzalloc(sizeof(usb_ep_t));
    udev->eps[1] = uep0;  //警告端点0为eps1，方便后续通过ep-dci查找端点。

    // --- 计算初始 Max Packet Size ---
    uint32 mps = (udev->port_speed >= USB_SPEED_SUPER) ? 512 :
                 (udev->port_speed == USB_SPEED_HIGH)  ? 64  : 8;

    //填充端点0
    uep0->ep_dci = 1;
    uep0->cerr = 3;
    uep0->ep_type = 4; // Control Endpoint
    uep0->max_packet_size = mps;
    uep0->average_trb_length = mps;
    uep0->max_streams_exp = 0;
    uep0->enable_streams_exp = 0;
    uep0->ring_max_trbs = 32;  //32个trb槽位就够了
    usb_alloc_ep_ring(uep0);

    // ---下发命令 ---
    err = usb_ctx_addr_dev(udev);
    return err;
}

/**
 * @brief 阶段 2：通过 EP0 获取设备描述符，并动态修正全速设备的 MPS
 * @param udev USB 设备对象
 * @return int32 0 表示成功
 */
static inline int32 usb_get_dev_desc(usb_dev_t *udev) {


    // 分配设备描述符的 DMA 内存
    usb_dev_desc_t *dev_desc = kzalloc_dma(sizeof(usb_dev_desc_t));

    // ============================
    // 全速设备 (FS) 的 8 字节刺探与修正逻辑
    // ============================
    if (udev->port_speed == USB_SPEED_FULL) {

        // 探针：只拿前 8 字节
        _usb_get_dev_desc(udev,dev_desc,8);

        if (dev_desc->max_packet_size0 != 8) {
            usb_ep_t *ep0 = udev->eps[1];
            ep0->max_packet_size = dev_desc->max_packet_size0;
            usb_ctx_slot_ep0_eval(udev);
        }
    }

    // ============================
    // 获取完整的 18 字节设备描述符
    // ============================
    _usb_get_dev_desc(udev,dev_desc,sizeof(usb_dev_desc_t));

    // 挂载到内核对象树上
    udev->dev_desc = dev_desc;

    return 0;
}

//获取usb配置描述符
static inline int usb_get_cfg_desc(usb_dev_t *udev) {
    usb_cfg_desc_t *config_desc = kzalloc_dma(sizeof(usb_cfg_desc_t));

    //第一次先获取配置描述符前9字节
    _usb_get_cfg_desc(udev,0,config_desc,9);

    //第二次从配置描述符中得到总长度获取整个配置描述符
    uint16 config_desc_length = config_desc->total_length;
    kfree(config_desc);

    config_desc = kzalloc_dma(config_desc_length);

    _usb_get_cfg_desc(udev,0,config_desc,config_desc_length);

    udev->config_desc = config_desc;
    return 0;
}

//获取字符串描述符
static inline int usb_get_string_desc(usb_dev_t *udev) {

    usb_desc_head_t *desc_head = kzalloc_dma(2);

    //获取语言ID描述符
    uint16 language_id;
    _usb_get_string_desc(udev,0,0,desc_head,2);
    usb_string_desc_t *language_desc = kzalloc_dma(desc_head->length);    // 分配真实长度的 DMA 内存

    // 正式拉取
    _usb_get_string_desc(udev,0,0,language_desc,desc_head->length);
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
            _usb_get_string_desc(udev,string_index[i],language_id,desc_head,2);

            //分配内存
            string_desc[i] = kzalloc_dma(desc_head->length);

            //第二次先正式获取字符串描述符N
            _usb_get_string_desc(udev,string_index[i],language_id,string_desc[i],desc_head->length);

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
usb_dev_t *usb_dev_create(xhci_hcd_t *xhcd, usb_dev_t *parent_hub,uint32 port_num) {
    usb_dev_t *udev = kzalloc(sizeof(usb_dev_t));
    uint8 psi = xhci_get_psi(xhcd, port_num);
    uint8 spc_idx = xhcd->port_to_spc[port_num];
    udev->xhcd = xhcd;
    udev->port_speed = xhcd->spc[spc_idx].psi_dict[psi].mapped_speed;
    udev->speed_kbps = xhcd->spc[spc_idx].psi_dict[psi].speed_kbps;
    udev->psiv = xhcd->spc[spc_idx].psi_dict[psi].psiv;

    // 【级联外设】
    udev->parent_hub = parent_hub;
    if (parent_hub != NULL) {
        udev->root_port_num = parent_hub->root_port_num; // 继承亲爹的根端口
        udev->parent_port_num = port_num;;
        udev->hub_depth = parent_hub->hub_depth + 1;
        uint8 shift = parent_hub->hub_depth << 2;
        udev->route_string = parent_hub->route_string | (port_num << shift);
    }else {
        // 【主板直连外设】
        udev->root_port_num = port_num; // 🌟 物理坐标在这里！(1 ~ MaxPorts)
        udev->parent_port_num = 0;      // 🌟 既然没爹，当然是0！完美契合 xHCI 规范！
        udev->route_string = 0;         // 直连无路由
        udev->hub_depth = 0;
    }

    udev->dev.type = &usb_dev_type;
    udev->dev.parent = &xhcd->xdev->dev;
    udev->dev.bus = &usb_bus_type;

    usb_enable_slot_ep0(udev); //启用slot 和 ep0
    usb_get_dev_desc(udev);    //获取设备描述符
    usb_get_cfg_desc(udev);    //获取配置描述符
    usb_get_string_desc(udev); //获取字符串描述符

    return udev;
}

//======================================= 驱动======================================

/**
 * @brief [工业级] 总线匹配引擎：扫描接口下的【所有备用接口】，检查是否与驱动匹配
 * @param  usb_if 目标 USB 接口对象
 * @param  drv    尝试挂载的驱动对象
 * @return usb_id_t* 命中匹配的 ID 规则指针，未命中返回 NULL
 */
static inline usb_id_t *usb_match_id(usb_if_t *usb_if, driver_t *drv) {
    // 🌟 终极防线：拦截所有非法野指针
    if (!usb_if || !usb_if->if_alts || !usb_if->udev) return NULL;
    if (!drv || !drv->id_table) return NULL;

    usb_dev_desc_t *dev_desc = usb_if->udev->dev_desc;

    // 🌟 第一层循环：遍历该接口下所有的【备用接口 (Alternate Settings)】
    for (uint8 alt_idx = 0; alt_idx < usb_if->num_if_alts; alt_idx++) {
        usb_if_desc_t *if_desc = usb_if->if_alts[alt_idx].if_desc;

        // 🌟 第二层循环：遍历驱动程序的 ID 表
        for (usb_id_t *id = drv->id_table; id->match_flags != 0; id++) {

            // 1. 匹配厂商 ID (VID)
            if ((id->match_flags & USB_MATCH_VENDOR) &&
                id->vendor_id != dev_desc->vendor_id) {
                continue;
                }

            // 2. 匹配产品 ID (PID)
            if ((id->match_flags & USB_MATCH_PRODUCT) &&
                id->product_id != dev_desc->product_id) {
                continue;
                }

            // 3. 匹配接口大类 (Class)
            if ((id->match_flags & USB_MATCH_INT_CLASS) &&
                id->if_class != if_desc->interface_class) {
                continue;
                }

            // 4. 匹配接口子类 (Subclass)
            if ((id->match_flags & USB_MATCH_INT_SUBCLASS) &&
                id->if_subclass != if_desc->interface_subclass) {
                continue;
                }

            // 5. 匹配接口协议 (Protocol)
            if ((id->match_flags & USB_MATCH_INT_PROTOCOL) &&
                id->if_protocol != if_desc->interface_protocol) {
                continue;
                }

            // 🌟 核心突破：只要在任意一个备用接口中找到了匹配规则，立刻宣告匹配成功！
            // 驱动层可以通过 (id - drv->id_table) 知道是谁命中的，也可以通过 alt_idx 知道是哪个图纸通过了
            return id;
        }
    }

    // 遍历完所有备用接口下的所有规则，均未命中
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
