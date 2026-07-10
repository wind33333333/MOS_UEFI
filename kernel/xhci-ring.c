#include "xhci.h"
#include "xhci-ring.h"
#include "errno.h"
#include "vmm.h"
#include "slub.h"

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







