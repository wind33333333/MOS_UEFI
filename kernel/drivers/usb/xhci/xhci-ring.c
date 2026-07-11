#include "xhci-ring.h"
#include "xhci-hcd.h"
#include "errno.h"
#include "../../../mm/include/vmm.h"
#include "../../../mm/include/slub.h"
#include "usb-core.h"
#include "printk.h"

uint64 xhci_submit_ring_enq(xhci_submit_ring_t *ring, xhci_trb_t *trb_push) {
    // 1. 【双指针防溢出检查】
    // 如果再走一步就撞上消费者游标了，说明环满了！
    uint32 ring_size = ring->size;
    uint32 next_enq = xhci_submit_ring_next_idx(ring->enq_idx,ring_size);
    if (next_enq == ring->deq_idx) {
        return ENOMEM; // 拒绝写入
    }

    // 2. 写入数据
    xhci_trb_t *dest = &ring->ring_base[ring->enq_idx];
    uint64 dest_pa = va_to_pa(dest); // 或使用预先算好的基址

    //设置trb的cyc位
    dest->parameter = trb_push->parameter;
    dest->status = trb_push->status;
    dest->control = trb_push->control | ring->cycle;

    // 3. 处理 Link TRB 跨越与 Cycle 翻转
    if (ring->enq_idx == (ring_size - 2)) {
        // 注意：现在 enq_idx 停在倒数第二个(悬崖边上)
        xhci_trb_t *link_trb = &ring->ring_base[ring_size - 1];
        // 重新组装 Link TRB 的控制字：
        // 必须包含：TRB类型(LINK) + 硬件切换标志(TOGGLE_CYCLE) + 链条标志(CHAIN)
        link_trb->control = TRB_SET_TYPE(TRB_TYPE_LINK) | TRB_TOGGLE_CYCLE | (trb_push->control & TRB_CHAIN) | ring->cycle;

        // 翻转操作系统的软件 Cycle 状态，迎接下一轮的覆写
        ring->cycle ^= 1;
    }

    // 4. 更新生产者游标
    ring->enq_idx = next_enq;

    return dest_pa;
}

/**
 * @brief 尝试从事件环中取出一个新事件
 * @param intr 当前队列的中断上下文结构体
 * @param out_evt 用于输出的 TRB 指针 (拷贝到外层，防止 DMA 竞争)
 * @return true 成功取到新事件，false 环已空
 */
int32 xhci_event_ring_deq(xhci_event_ring_t *ring, xhci_trb_t *out_evt) {
    // 1. 定位到当前指针指向的 TRB
    xhci_trb_t *cur_trb = &ring->ring_base[ring->deq_idx];

    // 2. 检查 Cycle 位：如果硬件还没把事件写进来，直接返回 0
    if (TRB_GET_CYCLE(cur_trb->control) != ring->cycle) {
        return -EAGAIN;
    }

    // 3. 提取事件数据！
    *out_evt = *cur_trb;

    // 4. 指针向前走一步 (完全接管环形队列的维护)
    ring->deq_idx++;
    if (ring->deq_idx >= ring->ring_size) {
        ring->deq_idx = 0;
        ring->cycle ^= 1; // 环绕时翻转 Cycle 预期
    }

    return 0;
}

/**
 * @brief 将 xHCI 硬件完成码转换为人类可读的字符串 (全量 37 种完成码全覆盖)
 */
char* xhci_get_comp_code_str(uint8 comp_code) {
    switch (comp_code) {
        // ==========================================
        // 1. 通用与系统级事件
        // ==========================================
        case COMP_SUCCESS:                      return "Success";
        case COMP_TRB_ERROR:                    return "TRB Format Error";
        case COMP_RESOURCE_ERROR:               return "xHC Resource Exhausted";
        case COMP_VF_EVENT_RING_FULL_ERROR:     return "VF Event Ring Full";
        case COMP_EVENT_RING_FULL_ERROR:        return "Event Ring Full";
        case COMP_EVENT_LOST_ERROR:             return "Event Lost (Ring Overflow)";
        case COMP_UNDEFINED_ERROR:              return "Undefined Fatal Hardware Error";

        // ==========================================
        // 2. 命令事件专属
        // ==========================================
        case COMP_BANDWIDTH_ERROR:              return "Bandwidth Error";
        case COMP_NO_SLOTS_AVAILABLE_ERROR:     return "No Slots Available";
        case COMP_INVALID_STREAM_TYPE_ERROR:    return "Invalid Stream Type";
        case COMP_SLOT_NOT_ENABLED_ERROR:       return "Slot Not Enabled";
        case COMP_ENDPOINT_NOT_ENABLED_ERROR:   return "Endpoint Not Enabled";
        case COMP_PARAMETER_ERROR:              return "Context Parameter Error";
        case COMP_CONTEXT_STATE_ERROR:          return "Context State Error";
        case COMP_COMMAND_RING_STOPPED:         return "Command Ring Stopped";
        case COMP_COMMAND_ABORTED:              return "Command Aborted";
        case COMP_SECONDARY_BANDWIDTH_ERROR:    return "Secondary Bandwidth Error";

        // ==========================================
        // 3. 传输事件专属
        // ==========================================
        case COMP_DATA_BUFFER_ERROR:            return "Data Buffer Error (DMA)";
        case COMP_BABBLE_ERROR:                 return "Babble Error (Device going crazy)";
        case COMP_USB_TRANSACTION_ERROR:        return "USB Transaction Error (CRC/Timeout)";
        case COMP_STALL_ERROR:                  return "STALL (Device Rejected)";
        case COMP_SHORT_PACKET:                 return "Short Packet";
        case COMP_RING_UNDERRUN:                return "Isoch Ring Underrun";
        case COMP_RING_OVERRUN:                 return "Isoch Ring Overrun";
        case COMP_BANDWIDTH_OVERRUN_ERROR:      return "Bandwidth Overrun";
        case COMP_NO_PING_RESPONSE_ERROR:       return "No Ping Response (USB 3.0 Link)";
        case COMP_INCOMPATIBLE_DEVICE_ERROR:    return "Incompatible Device";
        case COMP_MISSED_SERVICE_ERROR:         return "Missed Isoch Service";
        case COMP_STOPPED:                      return "Transfer Stopped";
        case COMP_STOPPED_LENGTH_INVALID:       return "Transfer Stopped (Length Invalid)";
        case COMP_STOPPED_SHORT_PACKET:         return "Transfer Stopped (Short Packet)";
        case COMP_MAX_EXIT_LATENCY_TOO_LARGE:   return "Max Exit Latency Too Large (U1/U2 Wake)";
        case COMP_ISOCH_BUFFER_OVERRUN:         return "Isoch Buffer Overrun";
        case COMP_INVALID_STREAM_ID_ERROR:      return "Invalid Stream ID (UAS)";
        case COMP_SPLIT_TRANSACTION_ERROR:      return "Split Transaction Error (Hub)";

        default:                                     return "Unknown/Unhandled Error";
    }
}






//发送xhci命令
int32 xhci_submit_cmd(xhci_hcd_t *xhcd,xhci_trb_t *cmd_trb,xhci_command_t *out_command) {
    // 1. 在外面慢吞吞地申请内存 (完全不占锁)
    xhci_command_t *command = kzalloc(sizeof(xhci_command_t));
    if (command == NULL) {
        return -ENOMEM;
    }

    // 2. 尝试入队，并严格检查结果
    uint64 cpu_flags;
    spin_lock_irqsave(&xhcd->cmd_ring.ring_lock,&cpu_flags);
    uint64 pa_or_err = xhci_submit_ring_enq(&xhcd->cmd_ring, cmd_trb);
    if (pa_or_err == ENOMEM) { // 假设环满返回此宏或 -1
        spin_unlock_irqrestore(&xhcd->cmd_ring.ring_lock, cpu_flags);
        kfree(command);     // 入队失败，销毁面单
        return -EBUSY;      // 返回系统繁忙
    }

    // 3. 组装面单并挂链
    command->cmd_trb_pa = pa_or_err;
    command->status = -EINPROGRESS;
    command->is_done = FALSE;
    list_add_tail(&xhcd->cmd_ring.pending_list, &command->node);
    spin_unlock_irqrestore(&xhcd->cmd_ring.ring_lock, cpu_flags);

    // 4. 敲击门铃，通知硬件
    xhci_ring_doorbell(xhcd, 0, 0);

    // 5. 🌀 核心魔法：原地轮询死等 (Busy-Wait)
    while (command->is_done == FALSE) {
        asm_pause();
        __asm__ __volatile__ ("" ::: "memory");
    }

    // 6. 返回结果
    if (out_command != NULL) {
        *out_command = *command;
    }

    // 7. 释放面单
    kfree(command);

    return out_command->status;
}


/**
 * @brief 分配并初始化 xHCI 提交环 (用于 Command Ring 和 Transfer Ring)
 * @param ring 指向要初始化的环形缓冲区控制块
 * @param size 环的总大小 (必须包含最后的 Link TRB，通常为 16~256 之间)
 * @return int32 0 表示成功，负数表示内存不足 (如 -ENOMEM)
 */
int32 xhci_alloc_submit_ring(xhci_submit_ring_t *ring, uint32 size) {
    // 1. 分配 DMA 连续内存
    // 规范要求：Ring 的首地址必须是 64 字节(Cache Line)或 16 字节对齐。
    // kzalloc_dma 不仅要分配对齐的物理内存，还会将其全部清零。
    ring->ring_base = (xhci_trb_t *)kzalloc_dma(size * sizeof(xhci_trb_t));
    if (ring->ring_base == NULL) {
        return -ENOMEM;
    }

    // 2. 初始化软件状态机与游标
    ring->enq_idx = 0;       // 生产者(CPU)入队游标
    ring->deq_idx = 0;       // 消费者(硬件)出队游标
    ring->size    = size;    // 环的总长度 (包含 Link TRB)
    ring->cycle   = 1;       // ★ xHCI 规范：新初始化的环，硬件期待的 Cycle 起始值必须为 1
    ring->ring_lock = 0;     // 自旋锁初始化 (确保 SMP 并发安全)
    list_head_init(&ring->pending_list); // 挂载待处理 URB 请求的链表

    // =========================================================================
    // 🌟 3. 预置 Link TRB (闭环的最后一块拼图)
    // 提前在环的最后一个槽位把“车尾指向车头”的物理指针焊死。
    // =========================================================================
    xhci_trb_t *link_trb = &ring->ring_base[size - 1];

    // 🎯 极爽的 64 位赋值：直接将 Ring 首地址的物理地址填入 DW0 & DW1
    link_trb->parameter = va_to_pa(ring->ring_base);

    // DW2 (Status) 保持为 0，对于 Link TRB 此处通常无用
    link_trb->status = 0;

    // 🎯 宏拼装 DW3 (Control)：
    // 指明类型为 LINK_TRB，并要求硬件跨过这里时切换内部 Toggle 期待值。
    link_trb->control = TRB_SET_TYPE(TRB_TYPE_LINK) | TRB_TOGGLE_CYCLE | TRB_CYCLE;

    return 0; // 完美收官
}

//传输环，命令环释放函数
int32 xhci_free_submit_ring(xhci_submit_ring_t *ring) {
    if (ring->ring_base != NULL) {
        kfree(ring->ring_base);
        ring->ring_base = NULL;
    }
    ring->enq_idx = 0;
    ring->deq_idx = 0;
    ring->size = 0;
    ring->cycle = 0;
}

//事件环分配函数
int32 xhci_alloc_event_ring(xhci_event_ring_t *ring,uint32 ring_size) {
    ring->ring_base = kzalloc_dma(ring_size * sizeof(xhci_trb_t));
    ring->deq_idx = 0;
    ring->ring_size = ring_size;
    ring->cycle = 1;

    ring->erst_base = kzalloc_dma(sizeof(xhci_erst_t)); //分配事件环段表内存，单段只分配一个
    ring->erst_base->ring_seg_base = va_to_pa(ring->ring_base);
    ring->erst_base->ring_seg_size = ring_size;
    ring->erst_base->reserved = 0;
}


//事件环释放函数
int32 xhci_free_event_ring(xhci_event_ring_t *ring) {

}

/**
 * @brief [内联执行器] 处理 EP0 控制传输 (三阶段)
 * @note 依赖最新版 16 字节宏装配架构与纯净版 usb_setup_packet_t。
 * Cycle 位和 wmb() 屏障已由 xhci_submit_ring_enq 底层引擎接管！
 */
static inline uint64 xhci_submit_control_transfer(usb_urb_t *urb, xhci_submit_ring_t *ring, uint8 wants_ioc) {
    uint64 last_trb_pa = 0;
    xhci_trb_t trb = {0}; // 每次循环前复用并清零

    // =======================================================
    // 🌟 核心修复：适配刚刚重构的无位域版 usb_setup_packet_t
    // =======================================================
    uint16 length  = urb->setup_packet->length;

    // 通过位掩码安全提取数据方向 (Bit 7: 1=IN, 0=OUT)
    uint8  req_dir = urb->setup_packet->request_type & USB_REQ_DIR_IN;

    // =======================================================
    // [阶段 1: Setup TRB] - 告诉设备要做什么
    // =======================================================

    // 🚀 终极炫技：IDT=1 时，直接把 8 字节的结构体当成一个 64 位整数塞进 parameter！
    trb.parameter = *(uint64 *)urb->setup_packet;

    trb.status = TRB_SET_TR_LEN(8); // 规范强求：Setup TRB 长度必须为 8

    uint32 setup_ctrl = TRB_SET_TYPE(TRB_TYPE_SETUP_STAGE) | TRB_IDT;

    // 配置 TRT (Transfer Type) 告知硬件后续是否有数据
    if (length == 0) {
        setup_ctrl |= TRB_SET_TRT_NO_DATA;
    } else if (req_dir == USB_REQ_DIR_IN) {
        setup_ctrl |= TRB_SET_TRT_IN_DATA;
    } else {
        setup_ctrl |= TRB_SET_TRT_OUT_DATA;
    }

    trb.control = setup_ctrl;
    xhci_submit_ring_enq(ring, &trb);

    // =======================================================
    // [阶段 2: Data TRB] - 搬运实际数据 (可选)
    // =======================================================
    if (length != 0 && urb->transfer_buf != NULL) {
        trb.parameter = va_to_pa(urb->transfer_buf);
        trb.status    = TRB_SET_TR_LEN(length);

        uint32 data_ctrl = TRB_SET_TYPE(TRB_TYPE_DATA_STAGE);
        if (req_dir == USB_REQ_DIR_IN) {
            data_ctrl |= TRB_SET_DIR_IN;
        } else {
            data_ctrl |= TRB_SET_DIR_OUT;
        }

        trb.control = data_ctrl;
        xhci_submit_ring_enq(ring, &trb);
    }

    // =======================================================
    // [阶段 3: Status TRB] - 最终握手确认
    // =======================================================
    trb.parameter = 0;
    trb.status    = 0;

    uint32 status_ctrl = TRB_SET_TYPE(TRB_TYPE_STATUS_STAGE);

    // 🛡️ 握手方向与数据方向永远相反。没数据或发数据时，要收状态(IN)；收数据时，要发状态(OUT)。
    if (length == 0 || req_dir == USB_REQ_DIR_OUT) {
        status_ctrl |= TRB_SET_DIR_IN;
    } else {
        status_ctrl |= TRB_SET_DIR_OUT;
    }

    // 根据上层请求，决定是否在最后一环触发硬件中断 (IOC)
    if (wants_ioc) {
        status_ctrl |= TRB_IOC;
    }

    trb.control = status_ctrl;
    last_trb_pa = xhci_submit_ring_enq(ring, &trb);

    return last_trb_pa;
}


/**
 * @brief [内联执行器] 处理 Bulk/Interrupt 普通传输 (大块切片与 ZLP)
 * @note 依赖最新版 16 字节宏装配架构。
 * 内存屏障 wmb()、Cycle 状态位、以及 Link TRB 闭环均由底层 xhci_submit_ring_enq 引擎自动维护！
 */
static inline uint64 xhci_submit_normal_transfer(usb_urb_t *urb, xhci_submit_ring_t *ring, uint8 wants_ioc) {
    uint64 last_trb_pa = 0;
    xhci_trb_t trb; // 临时装配容器

    uint32 left_len   = urb->transfer_len;
    uint64 current_pa = va_to_pa(urb->transfer_buf);

    // 1. 获取端点最大包长，防呆兜底 512 字节
    uint16 max_packet = urb->ep ? urb->ep->max_packet_size : 512;
    if (max_packet == 0) max_packet = 512;

    // 2. 判定是否需要追加 ZLP (零长度数据包：数据量刚好是最大包长的整数倍，且带短包结束标志)
    uint8 needs_zlp = (urb->transfer_flags & URB_ZERO_PACKET) &&
                      (urb->transfer_len > 0) &&
                      ((urb->transfer_len % max_packet) == 0);

    // 3. 核心切片循环：解决物理内存跨越 64KB 边界导致 DMA 越界车祸的问题
    while (left_len > 0) {
        // 计算当前物理地址距离下一个 64KB 对齐边界还有多大空间 (0x10000 = 64KB)
        uint32 space_to_boundary = 0x10000 - (uint32)(current_pa & 0xFFFF);

        uint8  has_more_data = (left_len > space_to_boundary);
        uint32 chunk_len     = has_more_data ? space_to_boundary : left_len;

        // 🌟 状态计算逻辑完美内聚为局部变量，防止脏状态在循环内交叉污染
        // 核心修复：如果数据没发完，或者虽然数据发完了但必须跟一个 ZLP，Chain 都必须为 1，告诉硬件它们属于同一个 TD 传输块
        uint32 chain = (has_more_data || needs_zlp) ? TRB_CHAIN : 0;

        // 核心修复：全村唯一的 IOC 只能在绝对的最后一块 TRB 上点亮 (防双重中断风暴)
        uint32 ioc   = (!has_more_data && !needs_zlp && wants_ioc) ? TRB_IOC : 0;

        // 🚀 使用重构后的参数字、状态字、控制字进行纯粹的位或装配
        trb.parameter = current_pa;
        trb.status    = TRB_SET_TR_LEN(chunk_len) | TRB_SET_INTR_TARGET(0);
        trb.control   = TRB_SET_TYPE(TRB_TYPE_NORMAL) | chain | ioc;

        // 推入硬件命令环
        last_trb_pa = xhci_submit_ring_enq(ring, &trb);

        current_pa += chunk_len;
        left_len   -= chunk_len;
    }

    // =======================================================
    // 🎁 4. 极客彩蛋追加：精准下发 ZLP 空车厢
    // =======================================================
    if (needs_zlp) {
        // 指针停在哪无所谓，长度强制为 0
        trb.parameter = current_pa;
        trb.status    = TRB_SET_TR_LEN(0) | TRB_SET_INTR_TARGET(0);

        // 绝对的最后一环，拉断链条 (chain = 0)，并赋予这节空车厢唤醒 CPU 的权利 (wants_ioc)
        uint32 zlp_ctrl = TRB_SET_TYPE(TRB_TYPE_NORMAL);
        if (wants_ioc) {
            zlp_ctrl |= TRB_IOC;
        }
        trb.control = zlp_ctrl;

        last_trb_pa = xhci_submit_ring_enq(ring, &trb);
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
int32 xhci_submit_urb(usb_urb_t *urb) {
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

// 给端点分配环 (终极统一抽象版)
int32 xhci_alloc_ep_ring(usb_ep_t *ep) {
    uint64 tr_dequeue_ptr;

    int32 err;

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
        if (streams_ctx_array == NULL) return -ENOMEM;

        // 记录上下文，方便后续释放内存
        ep->streams_ctx_array = streams_ctx_array;

        // 2. ★ 核心重构：给软件管理的统一环数组 (分配 N+1 个)
        // 索引 0 闲置防越界，索引 1~N 对应真实的 Stream ID
        ep->ring_arr = kzalloc(num_streams * sizeof(xhci_submit_ring_t));
        if (ep->ring_arr == NULL) return -ENOMEM;

        // 更新逻辑状态
        ep->lsa = 1; // 线性流数组标志
        ep->hid = 1; // 主机初始化禁用标志

        // 初始化每一个流环
        for (uint32 s = 1; s < num_streams; s++) {
            // 对数组中的每一个环进行物理分配
            err = xhci_alloc_submit_ring(&ep->ring_arr[s],ep->ring_max_trbs);
            if (err < 0) return err;

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
        if (ep->ring_arr == NULL) return -ENOMEM;

        // 2. 分配并初始化这唯一的环 (它就是 rings[0])
        err = xhci_alloc_submit_ring(&ep->ring_arr[0],ep->ring_max_trbs);
        if (err < 0) return err;

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
int32 xhci_free_ep_ring(usb_ep_t *ep) {
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






