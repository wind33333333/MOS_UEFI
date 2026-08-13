#include "xhci-hcd.h"
#include "errno.h"
#include "vmm.h"
#include "slub.h"
#include "../core/usb-dev.h"


int32 xhci_submit_ring_enq(xhci_submit_ring_t *ring, xhci_trb_t *trb_push) {
    // 1. 🌟 O(1) 终极极简防溢出检查
    if (ring->free_trbs == 0) {
        return ENOMEM;
    }

    uint32 enq_idx = ring->enq_idx;
    uint32 ring_mask = ring->size - 1;

    // 2. 处理 Link TRB 跨越与 Cycle 翻转
    if (enq_idx == ring_mask) {
        // 必须包含：TRB类型(LINK) + 硬件切换标志(TOGGLE_CYCLE) + 链条标志(CHAIN)
        xhci_trb_t *link_trb = &ring->ring_base[ring_mask];
        link_trb->parameter = va_to_pa(ring->ring_base);
        link_trb->status = 0;
        link_trb->control =
                TRB_SET_TYPE(TRB_TYPE_LINK) | TRB_TOGGLE_CYCLE | (trb_push->control & TRB_CHAIN) | ring->cycle;
        ring->cycle ^= 1; // 翻转操作系统的软件 Cycle 状态，迎接下一轮的覆写
        enq_idx = 0;
    }


    // 3. 写入数据
    xhci_trb_t *trb = &ring->ring_base[enq_idx];
    trb->parameter = trb_push->parameter;
    trb->status = trb_push->status;
    trb->control = trb_push->control | ring->cycle;

    // 4. 更新状态
    ring->enq_idx = enq_idx + 1;
    ring->free_trbs--; // 🌟 消耗了一个可用配额

    return enq_idx;
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
char *xhci_get_comp_code_str(uint8 comp_code) {
    switch (comp_code) {
        // ==========================================
        // 1. 通用与系统级事件
        // ==========================================
        case COMP_SUCCESS: return "Success";
        case COMP_TRB_ERROR: return "TRB Format Error";
        case COMP_RESOURCE_ERROR: return "xHC Resource Exhausted";
        case COMP_VF_EVENT_RING_FULL_ERROR: return "VF Event Ring Full";
        case COMP_EVENT_RING_FULL_ERROR: return "Event Ring Full";
        case COMP_EVENT_LOST_ERROR: return "Event Lost (Ring Overflow)";
        case COMP_UNDEFINED_ERROR: return "Undefined Fatal Hardware Error";

        // ==========================================
        // 2. 命令事件专属
        // ==========================================
        case COMP_BANDWIDTH_ERROR: return "Bandwidth Error";
        case COMP_NO_SLOTS_AVAILABLE_ERROR: return "No Slots Available";
        case COMP_INVALID_STREAM_TYPE_ERROR: return "Invalid Stream Type";
        case COMP_SLOT_NOT_ENABLED_ERROR: return "Slot Not Enabled";
        case COMP_ENDPOINT_NOT_ENABLED_ERROR: return "Endpoint Not Enabled";
        case COMP_PARAMETER_ERROR: return "Context Parameter Error";
        case COMP_CONTEXT_STATE_ERROR: return "Context State Error";
        case COMP_COMMAND_RING_STOPPED: return "Command Ring Stopped";
        case COMP_COMMAND_ABORTED: return "Command Aborted";
        case COMP_SECONDARY_BANDWIDTH_ERROR: return "Secondary Bandwidth Error";

        // ==========================================
        // 3. 传输事件专属
        // ==========================================
        case COMP_DATA_BUFFER_ERROR: return "Data Buffer Error (DMA)";
        case COMP_BABBLE_ERROR: return "Babble Error (Device going crazy)";
        case COMP_USB_TRANSACTION_ERROR: return "USB Transaction Error (CRC/Timeout)";
        case COMP_STALL_ERROR: return "STALL (Device Rejected)";
        case COMP_SHORT_PACKET: return "Short Packet";
        case COMP_RING_UNDERRUN: return "Isoch Ring Underrun";
        case COMP_RING_OVERRUN: return "Isoch Ring Overrun";
        case COMP_BANDWIDTH_OVERRUN_ERROR: return "Bandwidth Overrun";
        case COMP_NO_PING_RESPONSE_ERROR: return "No Ping Response (USB 3.0 Link)";
        case COMP_INCOMPATIBLE_DEVICE_ERROR: return "Incompatible Device";
        case COMP_MISSED_SERVICE_ERROR: return "Missed Isoch Service";
        case COMP_STOPPED: return "Transfer Stopped";
        case COMP_STOPPED_LENGTH_INVALID: return "Transfer Stopped (Length Invalid)";
        case COMP_STOPPED_SHORT_PACKET: return "Transfer Stopped (Short Packet)";
        case COMP_MAX_EXIT_LATENCY_TOO_LARGE: return "Max Exit Latency Too Large (U1/U2 Wake)";
        case COMP_ISOCH_BUFFER_OVERRUN: return "Isoch Buffer Overrun";
        case COMP_INVALID_STREAM_ID_ERROR: return "Invalid Stream ID (UAS)";
        case COMP_SPLIT_TRANSACTION_ERROR: return "Split Transaction Error (Hub)";

        default: return "Unknown/Unhandled Error";
    }
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
    ring->ring_base = (xhci_trb_t *) kzalloc_dma(size * sizeof(xhci_trb_t));
    if (ring->ring_base == NULL) {
        return -ENOMEM;
    }

    // 2. 初始化软件状态机与游标
    ring->enq_idx = 0; // 生产者(CPU)入队游标
    ring->deq_idx = 0; // 消费者(硬件)出队游标
    ring->free_trbs = size - 1; //空闲trb数量
    ring->size = size; // 环的总长度 (包含 Link TRB)
    ring->cycle = 1; // ★ xHCI 规范：新初始化的环，硬件期待的 Cycle 起始值必须为 1

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
    ring->free_trbs = 0;
    ring->cycle = 0;
}

//事件环分配函数
int32 xhci_alloc_event_ring(xhci_event_ring_t *ring, uint32 ring_size) {
    ring->ring_base = kzalloc_dma(ring_size * sizeof(xhci_trb_t));
    ring->deq_idx = 0;
    ring->ring_size = ring_size;
    ring->cycle = 1;

    ring->erst_base = kzalloc_dma(sizeof(xhci_erst_t)); //分配事件环段表内存，单段只分配一个
    ring->erst_base->ring_seg_base = va_to_pa(ring->ring_base);
    ring->erst_base->ring_seg_size = ring_size;
    ring->erst_base->reserved = 0;
}



//命令环发送xhci命令
int32 xhci_submit_cmd(xhci_hcd_t *xhcd, xhci_trb_t *cmd_trb, xhci_cmd_io_tracker_t *out_tracker) {
    int32 idx;
    // 1. 尝试入队，并严格检查结果
    while (1) {
        idx = xhci_submit_ring_enq(&xhcd->cmd_ring, cmd_trb);
        if (idx != ENOMEM) {
            // 假设环满返回此宏或 -1
            break;
        }
    }

    // 2. 注册该卡位的任务接收人信息
    xhci_cmd_io_tracker_t *tracker = &xhcd->cmd_io_tracker[idx];
    tracker->async_waker = 0;
    tracker->out_slot_id = 0;
    tracker->out_hw_status = 0;
    tracker->is_completed = FALSE;

    // 3. 敲击门铃，通知硬件
    xhci_ring_doorbell(xhcd, 0, 0);

    // 4. 🌀 核心魔法：原地轮询死等 (Busy-Wait)
    while (tracker->is_completed == FALSE) {
        asm_pause();
        __asm__ __volatile__ ("" ::: "memory");
    }

    // 6. 返回结果
    if (out_tracker != NULL) {
        *out_tracker = *tracker;
    }

    return out_tracker->out_hw_status;
}


/**
 * @brief [控制端点 EP0 专属] 控制传输提交器 (满足 ep->op(ep, req) 标准签名)
 */
int32 xhci_submit_control(usb_dev_t *udev, const xhci_ctrl_req_t *req) {
    xhci_trb_t trb = {0};
    int32 idx;
    xhci_transfer_io_tracker_t *tracker;
    usb_ep_t *ep0 = udev->eps[1];
    xhci_submit_ring_t *ring = ep0->rings;

    // 1. 提取本次传输要求的数据长度与请求方向
    uint16 length = req->setup_packet.length;
    uint8 req_dir = req->setup_packet.request_type & USB_REQ_DIR_IN;

    // =========================================================================
    // [阶段 1: Setup TRB] - IDT=1 立即数嵌入 8 字节；开启 CHAIN=1 连接后继
    // =========================================================================
    trb.parameter = req->setup_packet_raw;
    trb.status = TRB_SET_TR_LEN(8);

    uint32 setup_ctrl = TRB_SET_TYPE(TRB_TYPE_SETUP_STAGE) | TRB_IDT | TRB_CHAIN;
    if (length == 0) {
        setup_ctrl |= TRB_SET_TRT_NO_DATA;
    } else if (req_dir == USB_REQ_DIR_IN) {
        setup_ctrl |= TRB_SET_TRT_IN_DATA;
    } else {
        setup_ctrl |= TRB_SET_TRT_OUT_DATA;
    }

    trb.control = setup_ctrl;
    idx = xhci_submit_ring_enq(ring, &trb);

    // 🌟 全链路覆盖登记：Setup 格写入真正的 task_id (防止极端短包中断查不到主凭证)
    tracker = &ep0->tracker[idx];
    tracker->async_waker = req->waker;
    tracker->task_id = req->task_id;
    tracker->actual_bytes = 0;
    tracker->hw_status = 0;
    tracker->is_completed = FALSE;
    tracker->cb = NULL;

    // =========================================================================
    // [阶段 2: Data TRB] (可选) - 必须开启 CHAIN=1；绑定 Data Buffer 物理地址
    // =========================================================================
    if (length != 0 && req->buf != NULL) {
        trb.parameter = va_to_pa(req->buf);
        trb.status = TRB_SET_TR_LEN(length);

        uint32 data_ctrl = TRB_SET_TYPE(TRB_TYPE_DATA_STAGE) | TRB_CHAIN;
        if (req_dir == USB_REQ_DIR_IN) {
            data_ctrl |= TRB_SET_DIR_IN;
        } else {
            data_ctrl |= TRB_SET_DIR_OUT;
        }

        trb.control = data_ctrl;
        idx = xhci_submit_ring_enq(ring, &trb);

        // 🌟 全链路覆盖登记：Data 格写入 task_id
        tracker = &ep0->tracker[idx];
        tracker->async_waker = req->waker;
        tracker->task_id = req->task_id;
        tracker->actual_bytes = 0;
        tracker->hw_status = 0;
        tracker->is_completed = FALSE;
        tracker->cb = NULL;
    }

    // =========================================================================
    // [阶段 3: Status TRB] - 链条收尾 (CHAIN=0)，并在要求时点亮唯一中断 (IOC=1)
    // =========================================================================
    trb.parameter = 0;
    trb.status = TRB_SET_INTR_TARGET(0);

    uint32 status_ctrl = TRB_SET_TYPE(TRB_TYPE_STATUS_STAGE);
    // 🛡️ 握手方向规则：没数据或发数据时，要收设备状态 (IN)；收数据时，发主板确认 (OUT)
    if (length == 0 || req_dir == USB_REQ_DIR_OUT) {
        status_ctrl |= TRB_SET_DIR_IN;
    } else {
        status_ctrl |= TRB_SET_DIR_OUT;
    }

    if ((req->flags & TX_IOC) != 0) {
        status_ctrl |= TRB_IOC;
    }

    trb.control = status_ctrl;
    idx = xhci_submit_ring_enq(ring, &trb);

    // 🌟 核心主位登记：正常完成时硬件传回本索引物理地址，精准查出 task_id
    tracker = &ep0->tracker[idx];
    tracker->async_waker = req->waker;
    tracker->task_id = req->task_id;
    tracker->actual_bytes = 0;
    tracker->hw_status = 0;
    tracker->is_completed = FALSE;
    tracker->cb = NULL;

    // 计算真实的敲门铃目标值 (DCI
    uint8 slot_id = ep0->udev->slot_id;
    xhci_hcd_t *xhcd = ep0->udev->xhcd;
    xhci_ring_doorbell(xhcd, slot_id, ep0->ep_dci);

    // 4. 🌀 核心魔法：原地轮询死等 (Busy-Wait)
    while (tracker->is_completed == FALSE) {
        asm_pause();
        __asm__ __volatile__ ("" ::: "memory");
    }

    return tracker->hw_status;
}


/**
 * @brief [纯函数 helper] 单次迭代计算 TRB 长度与控制字，推进物理内存
 * @return 组装好的 TRB，调用方直接送到特定的 ring 里
 */
static inline xhci_trb_t xhci_build_data_trb(uint64 *current_pa,
                                             uint32 *left_len,
                                             uint16 max_packet,
                                             uint8 wants_ioc,
                                             uint8 needs_zlp) {
    xhci_trb_t trb = {0};
    uint32 space_to_boundary = 0x10000 - (uint32) (*current_pa & 0xFFFF);
    uint8 has_more_data = (*left_len > space_to_boundary);
    uint32 chunk_len = has_more_data ? space_to_boundary : *left_len;
    uint8 is_last_trb = (!has_more_data) && (!needs_zlp);

    trb.parameter = *current_pa;
    trb.status = TRB_SET_TR_LEN(chunk_len) | TRB_SET_INTR_TARGET(0);

    uint32 control = TRB_SET_TYPE(TRB_TYPE_NORMAL);
    if (!is_last_trb) control |= TRB_CHAIN;
    else if (wants_ioc) control |= TRB_IOC;
    trb.control = control;

    *current_pa += chunk_len;
    *left_len -= chunk_len;
    return trb;
}


/**
 * @brief [普通端点专属] EP1~31 Bulk / Interrupt 提交器 (保留 ZLP 以兼容旧生态)
 */
int32 xhci_submit_normal(usb_ep_t *ep, const xhci_data_req_t *req) {
    xhci_submit_ring_t *ring = ep->rings;
    xhci_transfer_io_tracker_t *tracker;
    int32 idx;

    uint32 left_len = req->length;
    uint64 current_pa = va_to_pa(req->buf);
    uint8 wants_ioc = (req->flags & TX_IOC) != 0;

    // 🌟 严格把控 ZLP 触发条件：
    // A. 本身就是 0 字节的纯握手包
    // B. 上层打上了 TX_ZERO_PACKET 标志，并且长度恰好是 max_packet_size 的整数倍
    uint8 needs_zlp = (req->length == 0) ||
                      ((req->flags & TX_ZERO_PACKET) && ((req->length % ep->max_packet_size) == 0));

    // 纯循环：切片 -> 后置判断 CHAIN/IOC -> 压环
    while (left_len > 0) {
        xhci_trb_t trb = xhci_build_data_trb(&current_pa, &left_len, ep->max_packet_size, 0, needs_zlp);

        // 核心算法：只有当数据发完，且不需要发 ZLP 时，本格才是真正的末尾！
        uint8 is_last_trb = (left_len == 0) && !needs_zlp;

        if (is_last_trb) {
            if (wants_ioc) trb.control |= TRB_IOC; // 终点：点亮中断
        } else {
            trb.control |= TRB_CHAIN; // 沿途/或者后面还有 ZLP：用 CHAIN 缝合！
        }

        idx = xhci_submit_ring_enq(ring, &trb);

        tracker = &ep->tracker[idx];
        tracker->async_waker = req->waker;
        tracker->task_id = req->task_id;
        tracker->is_completed = FALSE;
        tracker->cb = req->cb;
    }

    // 处理 ZLP (它必然是 CHAIN=0 的最后一格)
    if (needs_zlp) {
        xhci_trb_t trb = {
            .parameter = current_pa, .status = 0,
            .control = TRB_SET_TYPE(TRB_TYPE_NORMAL) | (wants_ioc ? TRB_IOC : 0)
        };
        idx = xhci_submit_ring_enq(ring, &trb);

        tracker = &ep->tracker[idx];
        tracker->async_waker = req->waker;
        tracker->task_id = req->task_id;
        tracker->is_completed = FALSE;
        tracker->cb = req->cb;
    }

    xhci_ring_doorbell(ep->udev->xhcd, ep->udev->slot_id, ep->ep_dci);

    // 核心魔法：原地轮询死等 (仅在明确要求阻塞时)
    if (req->flags & TX_BLOCKED) {
        while (tracker->is_completed == FALSE) {
            asm_pause();
            __asm__ __volatile__ ("" ::: "memory");
        }
    }

    return 0;
}

/**
 * @brief [NovaUSB 终极流端点专属] 纯净批量流提交器 (彻底剔除 ZLP 与 ED TRB)
 */
int32 xhci_submit_stream(usb_ep_t *ep, const xhci_data_req_t *req) {
    const uint16 stream_id = req->stream_id;
    xhci_submit_ring_t *ring = &ep->rings[stream_id];

    // 1. 登记影子表 (O(1) 锚点)
    xhci_transfer_io_tracker_t *tracker = &ep->tracker[stream_id];
    tracker->async_waker = req->waker;
    tracker->task_id = req->task_id;
    tracker->is_completed = FALSE;
    tracker->cb = req->cb;

    uint32 left_len = req->length;
    uint64 current_pa = va_to_pa(req->buf);
    uint8 wants_ioc = (req->flags & TX_IOC) != 0;

    // 🌟 架构级防御：流端点不允许 0 长度数据传输！(UAS 协议没有此概念)
    // 如果上层驱动发癫传了 0，直接拦截，保护总线不被发错的 TRB 污染。
    if (req->length == 0) {
        return 0;
    }

    uint16 last_trb_idx = 0;

    // 2. 纯净灌入 Data TRB 循环 (毫无 ZLP 的拖累)
    while (left_len > 0) {
        // needs_zlp 永远传 0
        xhci_trb_t trb = xhci_build_data_trb(&current_pa, &left_len, ep->max_packet_size, 0, 0);

        // 🌟 极致断定：因为绝对没有 ZLP，只要 left_len 归零，本格就 100% 是最后一格！
        uint8 is_last_trb = (left_len == 0);

        if (is_last_trb) {
            if (wants_ioc) trb.control |= TRB_IOC; // 终结态：挂中断
        } else {
            trb.control |= TRB_CHAIN; // 连续态：用 CHAIN 无缝粘合！
        }

        last_trb_idx = xhci_submit_ring_enq(ring, &trb);
    }

    // 3. 🛡️ 架构闭环：记录活跃 TRB 索引防幽灵中断
    // (注意：这里现在指向的是最后一块有真实数据的 TRB！)
    //tracker->active_trb_idx = last_trb_idx;

    // 4. 响铃唤醒 DMA
    uint32 db_target = ep->ep_dci | ((uint32) stream_id << 16);
    xhci_ring_doorbell(ep->udev->xhcd, ep->udev->slot_id, db_target);

    return 0;
}
