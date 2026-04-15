#include "xhci.h"
#include "printk.h"
#include "pcie.h"
#include "slub.h"
#include "vmm.h"
#include "driver.h"
#include "vmalloc.h"
#include "usb-core.h"
#include "errno.h"
#include "interrupt.h"


// 计算步进后的索引，自动跨越 Link TRB
static inline uint32 xhci_submit_ring_next_idx(uint32 cur_idx,uint32 size) {
    // 如果走到倒数第一个位置 (Link TRB)，直接绕回 0
    return (++cur_idx == size - 1) ? 0 : cur_idx;
}


uint64 xhci_submit_ring_enq(xhci_submit_ring_t *ring, xhci_trb_t *trb_push) {
    // 1. 【双指针防溢出检查】
    // 如果再走一步就撞上消费者游标了，说明环满了！
    uint32 ring_size = ring->size;
    uint32 next_enq = xhci_submit_ring_next_idx(ring->enq_idx,ring_size);
    if (next_enq == ring->deq_idx) {
        return XHCI_RING_FULL; // 拒绝写入
    }

    // 2. 写入数据
    xhci_trb_t *dest = &ring->ring_base[ring->enq_idx];
    uint64 dest_pa = va_to_pa(dest); // 或使用预先算好的基址

    //设置trb的cyc位
    dest->raw[0] = trb_push->raw[0];
    dest->raw[1] = trb_push->raw[1];
    dest->link.cycle = ring->cycle;

    // 3. 处理 Link TRB 跨越与 Cycle 翻转
    if (ring->enq_idx == (ring_size - 2)) {
        // 注意：现在 enq_idx 停在倒数第二个(悬崖边上)
        xhci_trb_t *link_trb = &ring->ring_base[ring_size - 1];
        link_trb->link.cycle = ring->cycle;
        link_trb->link.chain = trb_push->link.chain;
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
    if (cur_trb->cmd_comp_event.cycle != ring->cycle) {
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
char* xhci_get_comp_code_str(xhci_trb_comp_code_e comp_code) {
    switch (comp_code) {
        // ==========================================
        // 1. 通用与系统级事件
        // ==========================================
        case XHCI_COMP_TIMEOUT:                      return "Hardware Timeout";
        case XHCI_COMP_INVALID:                      return "Invalid TRB/State";
        case XHCI_COMP_SUCCESS:                      return "Success";
        case XHCI_COMP_TRB_ERROR:                    return "TRB Format Error";
        case XHCI_COMP_RESOURCE_ERROR:               return "xHC Resource Exhausted";
        case XHCI_COMP_VF_EVENT_RING_FULL_ERROR:     return "VF Event Ring Full";
        case XHCI_COMP_EVENT_RING_FULL_ERROR:        return "Event Ring Full";
        case XHCI_COMP_EVENT_LOST_ERROR:             return "Event Lost (Ring Overflow)";
        case XHCI_COMP_UNDEFINED_ERROR:              return "Undefined Fatal Hardware Error";

        // ==========================================
        // 2. 命令事件专属
        // ==========================================
        case XHCI_COMP_BANDWIDTH_ERROR:              return "Bandwidth Error";
        case XHCI_COMP_NO_SLOTS_AVAILABLE_ERROR:     return "No Slots Available";
        case XHCI_COMP_INVALID_STREAM_TYPE_ERROR:    return "Invalid Stream Type";
        case XHCI_COMP_SLOT_NOT_ENABLED_ERROR:       return "Slot Not Enabled";
        case XHCI_COMP_ENDPOINT_NOT_ENABLED_ERROR:   return "Endpoint Not Enabled";
        case XHCI_COMP_PARAMETER_ERROR:              return "Context Parameter Error";
        case XHCI_COMP_CONTEXT_STATE_ERROR:          return "Context State Error";
        case XHCI_COMP_COMMAND_RING_STOPPED:         return "Command Ring Stopped";
        case XHCI_COMP_COMMAND_ABORTED:              return "Command Aborted";
        case XHCI_COMP_SECONDARY_BANDWIDTH_ERROR:    return "Secondary Bandwidth Error";

        // ==========================================
        // 3. 传输事件专属
        // ==========================================
        case XHCI_COMP_DATA_BUFFER_ERROR:            return "Data Buffer Error (DMA)";
        case XHCI_COMP_BABBLE_ERROR:                 return "Babble Error (Device going crazy)";
        case XHCI_COMP_USB_TRANSACTION_ERROR:        return "USB Transaction Error (CRC/Timeout)";
        case XHCI_COMP_STALL_ERROR:                  return "STALL (Device Rejected)";
        case XHCI_COMP_SHORT_PACKET:                 return "Short Packet";
        case XHCI_COMP_RING_UNDERRUN:                return "Isoch Ring Underrun";
        case XHCI_COMP_RING_OVERRUN:                 return "Isoch Ring Overrun";
        case XHCI_COMP_BANDWIDTH_OVERRUN_ERROR:      return "Bandwidth Overrun";
        case XHCI_COMP_NO_PING_RESPONSE_ERROR:       return "No Ping Response (USB 3.0 Link)";
        case XHCI_COMP_INCOMPATIBLE_DEVICE_ERROR:    return "Incompatible Device";
        case XHCI_COMP_MISSED_SERVICE_ERROR:         return "Missed Isoch Service";
        case XHCI_COMP_STOPPED:                      return "Transfer Stopped";
        case XHCI_COMP_STOPPED_LENGTH_INVALID:       return "Transfer Stopped (Length Invalid)";
        case XHCI_COMP_STOPPED_SHORT_PACKET:         return "Transfer Stopped (Short Packet)";
        case XHCI_COMP_MAX_EXIT_LATENCY_TOO_LARGE:   return "Max Exit Latency Too Large (U1/U2 Wake)";
        case XHCI_COMP_ISOCH_BUFFER_OVERRUN:         return "Isoch Buffer Overrun";
        case XHCI_COMP_INVALID_STREAM_ID_ERROR:      return "Invalid Stream ID (UAS)";
        case XHCI_COMP_SPLIT_TRANSACTION_ERROR:      return "Split Transaction Error (Hub)";

        default:                                     return "Unknown/Unhandled Error";
    }
}



/**
 * @brief 将 xHCI 硬件专属的 TRB 完成码，翻译成全宇宙通用的 POSIX 错误码
 * @param comp_code  xHCI 硬件抛出的完成码
 * @return int32     返回 0 表示正常，负数表示各种 POSIX 异常
 */
int32 xhci_translate_error(xhci_trb_comp_code_e comp_code) {
    switch (comp_code) {

        // ==========================================================
        // 🟢 【第一梯队：和平时期 (Happy Path)】
        // 处理策略：一路绿灯，继续推进业务状态机
        // ==========================================================
        case XHCI_COMP_SUCCESS:
        case XHCI_COMP_SHORT_PACKET:        // 短包在 BOT/UAS 中是合法的结束标志
            return 0;

        // ==========================================================
        // 🔵 【第二梯队：优雅刹车 (Graceful Stop)】
        // 出现场景：上层主动调用了 Stop Endpoint 或 Abort Command
        // 处理策略：清理残留的面单内存，不需要物理抢救
        // ==========================================================
        case XHCI_COMP_COMMAND_RING_STOPPED:
        case XHCI_COMP_COMMAND_ABORTED:
        case XHCI_COMP_STOPPED:
        case XHCI_COMP_STOPPED_LENGTH_INVALID:
        case XHCI_COMP_STOPPED_SHORT_PACKET:
            return -ESHUTDOWN;              // POSIX: 传输端点已被安全关闭/停机

        // ==========================================================
        // 🟡 【第三梯队：数据与协议死锁 (Data/Protocol Halt)】
        // 出现场景：Data 环或 Cmd 环报错，硬件端点坠入 `Halted` 状态。
        // 处理策略：调用 `xhci_cmd_reset_ep` 抢救主板，上发 TMF/ClearHalt 安抚 U 盘。
        // ==========================================================
        case XHCI_COMP_STALL_ERROR:
            return -EPIPE;                  // POSIX: 管道破裂 (U 盘主动拒绝)

        case XHCI_COMP_USB_TRANSACTION_ERROR:
            return -EPROTO;                 // POSIX: 协议错误 (线缆松动、CRC校验错、超时)

        case XHCI_COMP_BABBLE_ERROR:
        case XHCI_COMP_RING_OVERRUN:
        case XHCI_COMP_ISOCH_BUFFER_OVERRUN:
        case XHCI_COMP_BANDWIDTH_OVERRUN_ERROR:
            return -EOVERFLOW;              // POSIX: 数值溢出 (设备像漏水一样疯狂发数据)

        case XHCI_COMP_SPLIT_TRANSACTION_ERROR:
            return -ECOMM;                  // POSIX: 发送时通信错误 (通常是 USB Hub 级错误)

        case XHCI_COMP_TRB_ERROR:
        case XHCI_COMP_DATA_BUFFER_ERROR:
        case XHCI_COMP_INVALID_STREAM_ID_ERROR:
            return -EILSEQ;                 // POSIX: 非法的字节序/操作 (驱动传给主板的 TRB 地址乱了)

        // ==========================================================
        // 🟠 【第四梯队：资源与配置拒绝 (Config Rejection)】
        // 出现场景：主板发脾气，拒绝执行你的 Configure Endpoint 等配置命令。
        // 处理策略：向上层汇报设备无法接入，释放分配的上下文内存。
        // ==========================================================
        case XHCI_COMP_BANDWIDTH_ERROR:
        case XHCI_COMP_SECONDARY_BANDWIDTH_ERROR:
            return -ENOSPC;                 // POSIX: 设备上没有空间 (总线带宽被其他设备占满了)

        case XHCI_COMP_NO_SLOTS_AVAILABLE_ERROR:
        case XHCI_COMP_RESOURCE_ERROR:
            return -ENOMEM;                 // POSIX: 内存分配不足 (主板内部寄存器/内存池枯竭)

        case XHCI_COMP_INVALID_STREAM_TYPE_ERROR:
        case XHCI_COMP_PARAMETER_ERROR:
            return -EINVAL;                 // POSIX: 无效的参数 (驱动代码写 Bug 了，填错了结构体)

        case XHCI_COMP_SLOT_NOT_ENABLED_ERROR:
        case XHCI_COMP_ENDPOINT_NOT_ENABLED_ERROR:
        case XHCI_COMP_INCOMPATIBLE_DEVICE_ERROR:
        case XHCI_COMP_NO_PING_RESPONSE_ERROR:
            return -ENODEV;                 // POSIX: 没有这样的设备 (端点未激活 或 U 盘休眠叫不醒)

        case XHCI_COMP_CONTEXT_STATE_ERROR:
            return -EPERM;                  // POSIX: 操作不允许 (状态机时序违规，如在 Running 时改指针)

        // ==========================================================
        // 🔴 【第五梯队：灾难级系统崩溃 (Catastrophic Error)】
        // 出现场景：中断系统瘫痪，或主板硬件发生物理级坏死。
        // 处理策略：触发内核级总线大复位 (Host Controller Reset)，甚至 Kernel Panic。
        // ==========================================================
        case XHCI_COMP_TIMEOUT:
            return -ETIMEDOUT;              // POSIX: 连接超时 (咱们驱动层自己定义的超时)

        case XHCI_COMP_EVENT_RING_FULL_ERROR:
        case XHCI_COMP_VF_EVENT_RING_FULL_ERROR:
        case XHCI_COMP_EVENT_LOST_ERROR:
            return -ENOBUFS;                // POSIX: 没有可用的缓冲区 (你的死循环事件泵卡了，主板回执爆仓)

        case XHCI_COMP_UNDEFINED_ERROR:
        case XHCI_COMP_INVALID:
        default:
            return -EIO;                    // POSIX: 物理输入/输出错误 (万劫不复的底线错误)
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
    if (pa_or_err == XHCI_RING_FULL) { // 假设环满返回此宏或 -1
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


//======================================= 命令环命令 ===========================================================

//分配插槽
int32 xhci_cmd_enable_slot(xhci_hcd_t *xhcd, uint8 *out_slot_id) {
    // 1. 组装cmd_trb
    xhci_trb_t cmd_trb = {0};
    cmd_trb.enable_slot.type = XHCI_TRB_TYPE_ENABLE_SLOT;
    cmd_trb.enable_slot.slot_type = 0;

    xhci_command_t command = {0};
    int32 status = xhci_submit_cmd(xhcd,&cmd_trb,&command);

    *out_slot_id = command.slot_id;

    return status;
}

/**
 * @brief 注销并释放 xHCI 设备槽位 (常用于热拔出或枚举失败的灾难恢复)
 * @param xhcd xHCI 控制器上下文
 * @param usb_dev         要销毁的 USB 设备对象
 * @return int8           0 表示成功，-1 表示失败
 */
int32 xhci_cmd_disable_slot(xhci_hcd_t *xhcd, uint8 slot_id) {
    // 1. 组装cmd_trb
    xhci_trb_t cmd_trb = {0};
    cmd_trb.disable_slot.trb_type = XHCI_TRB_TYPE_DISABLE_SLOT;
    cmd_trb.disable_slot.slot_id  = slot_id;

    int32 status = xhci_submit_cmd(xhcd,&cmd_trb,NULL);

    return status;
}


//设置设备地址
int32 xhci_cmd_addr_dev(xhci_hcd_t *xhcd, uint8 slot_id,xhci_input_ctx_t *input_ctx) {
    // 1. 组装cmd_trb
    xhci_trb_t cmd_trb = {0};
    cmd_trb.addr_dev.trb_type = XHCI_TRB_TYPE_ADDRESS_DEVICE;
    cmd_trb.addr_dev.input_ctx_ptr = va_to_pa(input_ctx);
    cmd_trb.addr_dev.slot_id = slot_id;
    cmd_trb.addr_dev.bsr = 0;

    int32 status = xhci_submit_cmd(xhcd,&cmd_trb,NULL);

    return status;

}

/**
 * @brief 底层指令：发送 Configure Endpoint 命令
 * @param xhcd xHCI 控制器实例
 * @param input_ctx_pa    Input Context 的物理地址 (必须64字节对齐)
 * @param slot_id         目标 Slot ID
 * @param dc              Deconfigure 标志 (0=配置端点, 1=一键清除所有业务端点)
 */
int32 xhci_cmd_cfg_ep(xhci_hcd_t *xhcd, xhci_input_ctx_t *input_ctx, uint8 slot_id, uint8 dc) {
    // 1. 组装cmd_trb
    xhci_trb_t cmd_trb = {0};
    cmd_trb.cfg_ep.trb_type = XHCI_TRB_TYPE_CONFIGURE_EP;
    cmd_trb.cfg_ep.input_ctx_ptr = va_to_pa(input_ctx);
    cmd_trb.cfg_ep.slot_id = slot_id;
    cmd_trb.cfg_ep.dc = dc;

    int32 status = xhci_submit_cmd(xhcd,&cmd_trb,NULL);

    return status;

}

/**
 * @brief 底层指令：发送 Evaluate Context 命令
 * @param xhci            xHCI 控制器实例
 * @param input_ctx_pa    Input Context 的物理地址
 * @param slot_id         目标 Slot ID
 */
int32 xhci_cmd_eval_ctx(xhci_hcd_t *xhcd, xhci_input_ctx_t *input_ctx, uint8 slot_id) {
    // 1. 组装cmd_trb
    xhci_trb_t cmd_trb = {0};
    cmd_trb.eval_ctx.trb_type = XHCI_TRB_TYPE_EVALUATE_CTX;
    cmd_trb.eval_ctx.input_ctx_ptr = va_to_pa(input_ctx);
    cmd_trb.eval_ctx.slot_id = slot_id;

    int32 status = xhci_submit_cmd(xhcd,&cmd_trb,NULL);

    return status;

}


//重置端点
int32 xhci_cmd_reset_ep(xhci_hcd_t *xhcd, uint8 slot_id, uint8 ep_dci) {
    // 1. 组装cmd_trb
    xhci_trb_t cmd_trb = {0};
    cmd_trb.rest_ep.type = XHCI_TRB_TYPE_RESET_EP;
    cmd_trb.rest_ep.tsp = 0;
    cmd_trb.rest_ep.ep_dci = ep_dci;
    cmd_trb.rest_ep.slot_id = slot_id;

    int32 status = xhci_submit_cmd(xhcd,&cmd_trb,NULL);

    return status;
}

/**
 * @brief 紧急刹车：停止指定设备的指定端点 (用于超时或STALL抢救)
 * @param xhcd    xHCI 控制器上下文
 * @param slot_id 出事设备的 Slot ID
 * @param ep_dci  出事端点的 DCI (注意: EP0 的 DCI 是 1)
 * @return int32  0 表示成功刹车，负数表示 POSIX 错误码 (如 -EINVAL)
 */
int32 xhci_cmd_stop_ep(xhci_hcd_t *xhcd, uint8 slot_id, uint8 ep_dci) {
    if (xhcd == NULL || slot_id == 0 || ep_dci == 0 || ep_dci > 31) {
        return -EINVAL; // Invalid argument: 非法参数
    }

    // 1. 组装cmd_trb
    xhci_trb_t cmd_trb = {0};
    cmd_trb.stop_ep.trb_type = XHCI_TRB_TYPE_STOP_EP; // 15
    cmd_trb.stop_ep.slot_id  = slot_id;
    cmd_trb.stop_ep.ep_dci   = ep_dci;
    cmd_trb.stop_ep.suspend  = 0; // 坚决不挂起，要求主板彻底停下传输环

    int32 status = xhci_submit_cmd(xhcd,&cmd_trb,NULL);

    return status;
}


/**
 * @brief 发送 Set TR Dequeue Pointer Command，强制移动端点底层的出队指针
 * * @param xhci          xHCI 控制器实例
 * @param slot_id       设备槽位号
 * @param ep_dci        端点上下文索引 (DCI，例如 IN通常是 3，OUT通常是 4)
 * @param transfer_ring 需要被修改指针的 Transfer Ring 结构体指针
 * @return int32        完成状态码 (XHCI_COMP_SUCCESS 表示成功)
 */
int32 xhci_cmd_set_tr_deq_ptr(xhci_hcd_t *xhcd, uint8 slot_id, uint8 ep_dci,xhci_submit_ring_t *transfer_ring) {


    // 【核心算力】：找到 Transfer Ring 中软件当前准备写入的、下一个干净槽位的物理地址
    uint64 next_clean_trb_pa = va_to_pa(&transfer_ring->ring_base[transfer_ring->enq_idx]);

    // 获取软件当前在这个槽位上预期的 Cycle Bit 状态
    uint8 next_cycle_state = transfer_ring->cycle;

    // 硬件强制规范：物理地址的最低位 (Bit 0) 必须包含 Dequeue Cycle State (DCS)
    // 这样硬件指针挪过去之后，才知道下次扫描该期待 0 还是 1
    xhci_trb_t cmd_trb={0};
    cmd_trb.set_tr_deq_ptr.tr_deq_ptr = next_clean_trb_pa | next_cycle_state;
    cmd_trb.set_tr_deq_ptr.type = XHCI_TRB_TYPE_SET_TR_DEQUEUE;
    cmd_trb.set_tr_deq_ptr.ep_dci = ep_dci;
    cmd_trb.set_tr_deq_ptr.slot_id = slot_id;

    int32 status = xhci_submit_cmd(xhcd,&cmd_trb,NULL);

    return status;
}

/**
 * @brief 底层指令：发送 Reset Device 命令
 * @param xhci            xHCI 控制器实例
 * @param slot_id         目标 Slot ID
 * @return xhci_trb_comp_code_e 硬件完成码
 */
int32 xhci_cmd_reset_dev(xhci_hcd_t *xhcd, uint8 slot_id) {
    xhci_trb_t cmd_trb = {0};
    cmd_trb.reset_dev.trb_type = XHCI_TRB_TYPE_RESET_DEVICE;
    cmd_trb.reset_dev.slot_id = slot_id;

    int32 status = xhci_submit_cmd(xhcd,&cmd_trb,NULL);

    return status;

}


//=============================================================================================================



//xhic扩展能力搜索
uint8 xhci_ecap_find(xhci_hcd_t *xhcd, void *ecap_arr, uint8 cap_id) {
    uint32 offset = xhcd->cap_reg->hccparams1 >> 16;
    uint32 *ecap = (uint32 *) xhcd->cap_reg;
    uint8 count = 0;
    while (offset) {
        ecap += offset;
        if ((*ecap & 0xFF) == cap_id) {
            ((uint64*)ecap_arr)[count++] = (uint64)ecap;
        };
        offset = (*ecap >> 8) & 0xFF;
    }
    return count;
}

//停止xhci
int32 xhci_stop(xhci_hcd_t *xhcd) {
    xhcd->op_reg->usbcmd &= ~XHCI_CMD_RS; //停止xhci
    uint32 times = 20000000;
    while (times--) {
        if ((xhcd->op_reg->usbsts & XHCI_STS_HCH) != 0)
            return 0;
    }
    color_printk(RED, BLACK, "xHCI: Stop timeout! Controller refused to halt.\n");
    return -ETIMEDOUT;
}

//复位xhci
int32 xhci_reset(xhci_hcd_t *xhcd) {
    // 规范防线：确保复位前已经停止！
    if ((xhcd->op_reg->usbsts & XHCI_STS_HCH) == 0) {
        color_printk(YELLOW, BLACK, "xHCI: Warning, halting controller before reset...\n");
        xhci_stop(xhcd);
    }

    // 触发复位！主板 xHC 开始脑裂重启
    xhcd->op_reg->usbcmd |= XHCI_CMD_HCRST;

    uint32 times = 20000000;
    while (times--) {
        // 条件 1: 硬件完成复位操作后，会自动将 HCRST 清零
        uint8 reset_done = (xhcd->op_reg->usbcmd & XHCI_CMD_HCRST) == 0;

        // 条件 2: 硬件内部微码加载完毕，准备好接客，会将 CNR (未准备好) 清零
        uint8 is_ready = ((xhcd->op_reg->usbsts & XHCI_STS_CNR) == 0);

        if (reset_done && is_ready) {
            return 0; // 完美复位并就绪！
        }
    }

    color_printk(RED, BLACK, "xHCI: Reset timeout! Controller died during reset.\n");
    return -ETIMEDOUT;
}


/**
 * @brief 启动 xHCI 控制器
 */
int32 xhci_start(xhci_hcd_t *xhcd) {
    if (!xhcd || !xhcd->op_reg) return -EINVAL;

    // 1. 读取当前的 USBCMD 寄存器值
    // 💡 架构师习惯：尽量避免直接对硬件寄存器使用 |= 操作符，
    // 先读到局部变量，修改完再统一写回，防止引发意料之外的总线写事务。
    uint32 cmd = xhcd->op_reg->usbcmd;

    // 2. ★ 核心修正：同时开启运行、中断和系统错误报警！
    cmd |= (XHCI_CMD_RS | XHCI_CMD_INTE | XHCI_CMD_HSEE);

    // 3. 点火！写入硬件
    xhcd->op_reg->usbcmd = cmd;

    // 4. ★ 科学的超时等待 (xHCI 规范指出 HCH 通常在 16ms 内清零)
    // 我们给予 50 毫秒的宽限期，使用内核标准的 mdelay (毫秒级延时)
    uint32 times = 20000000;
    while (times--) {
        if ((xhcd->op_reg->usbsts & XHCI_STS_HCH) == 0)
            return 0;
    }

    // 5. 如果 50ms 后 HCH 依然为 1，说明芯片死机或硬件故障
    color_printk(RED, BLACK, "xHCI: Start timeout! Controller refused to run. USBSTS: %#x\n",
                 xhcd->op_reg->usbsts);
    return -ETIMEDOUT;
}

//启用xhci中断
void xhci_enable_intr(xhci_hcd_t *xhcd,uint16 intr_number) {
    uint32 iman = xhcd->rt_reg->intr_regs[intr_number].iman;
    iman |= XHCI_IMAN_IE;
    xhcd->rt_reg->intr_regs[intr_number].iman = iman;
}

//禁用xhci中断
void xhci_disable_intr(xhci_hcd_t *xhcd,uint16 intr_number) {
    int32 iman = xhcd->rt_reg->intr_regs[intr_number].iman;
    iman &= ~XHCI_IMAN_IE;
    xhcd->rt_reg->intr_regs[intr_number].iman = iman;
}


static inline void xhci_submit_ring_update_deq_idx(xhci_submit_ring_t *ring, uint64 comp_trb_pa) {
    // 根据物理地址，反算出它在环里的数组索引
    uint32 deq_idx = (comp_trb_pa - va_to_pa(ring->ring_base))>>4;

    // 硬件执行完这个了，说明下一个是待处理的，更新消费者游标
    ring->deq_idx = xhci_submit_ring_next_idx(deq_idx,ring->size);
}

// 提取大于等于 val 的下一个 2 的幂次 (内核神技)
// 如果 val = 12KB，算出来就是 16KB
static inline uint64 roundup_pow_of_two(uint64 val) {
    val--;
    val |= val >> 1;
    val |= val >> 2;
    val |= val >> 4;
    val |= val >> 8;
    val |= val >> 16;
    val |= val >> 32;
    return val + 1;
}

/**
 * @brief 通过硬件返回的物理地址，极速逆向找回 Stream Ring
 * 依赖底层伙伴算法 (Buddy Allocator) 提供的自然对齐特性
 */
static inline xhci_submit_ring_t* xhci_get_ring_by_pa(usb_ep_t *ep, uint64 trb_pa) {
    if (ep->enable_streams_exp == 0) {
        return &ep->ring_arr[0];
    }

    // 1. 获取物理字节大小
    uint64 ring_byte_size = ep->ring_arr[1].size << 4;

    // 🌟 防御陷阱：强制向上取整到 2 的幂次，对齐伙伴系统的真实分配粒度！
    uint64 buddy_aligned_size = roundup_pow_of_two(ring_byte_size);

    // 2. 生成绝对安全的物理掩码
    uint64 pa_mask = ~(buddy_aligned_size - 1);

    // 3. ⚡️ 一步提取物理基址 (纳秒级计算),并转换成虚拟地址
    void *target_base_va = pa_to_va(trb_pa & pa_mask);


    // 4. 极速命中匹配
    uint32 streams_count = (1 << ep->enable_streams_exp) + 1;
    for (uint32 s = 1; s < streams_count; s++) {
        // 由于是严格相等的寄存器比较，这里绝不会有分支预测惩罚
        if (ep->ring_arr[s].ring_base == target_base_va) {
            return &ep->ring_arr[s];
        }
    }

    return NULL;
}

// 传输任务处理 (多核完美并发版)
void xhci_handle_transfer_event(xhci_hcd_t *xhcd, xhci_trb_t *evt_trb) {
    uint64 trb_pa     = evt_trb->transfer_event.tr_trb_ptr;
    uint8  evt_ep_dci = evt_trb->transfer_event.ep_dci;
    uint8  slot_id    = evt_trb->transfer_event.slot_id;
    uint32 comp_code  = evt_trb->transfer_event.comp_code;

    usb_dev_t *udev = xhcd->udevs[slot_id];
    if (udev == NULL) return;
    usb_ep_t *ep = udev->eps[evt_ep_dci];
    if (ep == NULL) return;

    // =======================================================
    // 🌟 1. 逆向定位：找出真正发生事件的那个环！
    // =======================================================
    xhci_submit_ring_t *target_ring = xhci_get_ring_by_pa(ep, trb_pa);
    if (target_ring == NULL) {
        color_printk(RED, BLACK, "xHCI: Ghost TRB PA %llx from hardware!\n", trb_pa);
        return;
    }

    // =======================================================
    // 🔒 2. 获取该环的专属锁 (绝不影响其他 Stream 的提交)
    // =======================================================
    uint64 cpu_flags;
    spin_lock_irqsave(&target_ring->ring_lock, &cpu_flags);

    // =======================================================
    // 3. 在目标环的安全链表里进行小范围遍历
    // =======================================================
    list_head_t *curr, *next;
    list_for_each_safe(curr,next,&target_ring->pending_list){
        usb_urb_t *urb = CONTAINER_OF(curr, usb_urb_t, node);

        // 🎯 物理地址对上了！这就是硬件刚刚做完的任务
        if (urb->last_trb_pa == trb_pa) {
            // 结算账单
            xhci_submit_ring_update_deq_idx(target_ring, trb_pa);
            list_del_init(curr);
            urb->status = xhci_translate_error(comp_code);
            urb->actual_length = urb->transfer_len - evt_trb->transfer_event.tr_len;
            urb->is_done = TRUE;
            break; // 找到了就跳出循环
        }
    }

    // 🔓 4. 解锁释放
    spin_unlock_irqrestore(&target_ring->ring_lock, cpu_flags);
}


//命令完成事件trb处理程序
void xhci_handle_cmd_completion(xhci_submit_ring_t *cmd_ring,xhci_trb_t *evt_trb) {
    uint64 trb_pa     = evt_trb->cmd_comp_event.cmd_trb_ptr;
    uint8  slot_id    = evt_trb->cmd_comp_event.slot_id;
    uint32 comp_code  = evt_trb->cmd_comp_event.comp_code;
    uint32 comp_param = evt_trb->cmd_comp_event.cmd_comp_param;

    uint64 cpu_flags;
    spin_lock_irqsave(&cmd_ring->ring_lock, &cpu_flags);
    // 遍历该端点上所有正在飞的面单
    list_head_t *curr, *next;
    list_for_each_safe(curr,next,&cmd_ring->pending_list){
       xhci_command_t *command = CONTAINER_OF(curr, xhci_command_t, node);

       if (command->cmd_trb_pa == trb_pa ) {
           xhci_submit_ring_update_deq_idx(cmd_ring, trb_pa);
           list_del_init(curr);
           command->slot_id = slot_id;
           command->comp_code = comp_code;
           command->comp_param = comp_param;
           command->is_done = TRUE;
           break;
       }
   }
    // 🔓 4. 解锁释放
    spin_unlock_irqrestore(&cmd_ring->ring_lock, cpu_flags);
}

//端口状态处理
void xhci_handle_port_status_change(xhci_hcd_t *xhcd,xhci_trb_t *evt_trb) {

}


/**
 * @brief 解析并分发单一事件 (纯业务逻辑)
 */
void xhci_process_single_event(xhci_hcd_t *xhcd, xhci_trb_t *trb) {
    trb_type_e trb_type = trb->cmd_comp_event.trb_type;
    xhci_trb_comp_code_e comp_code = trb->cmd_comp_event.comp_code;

    switch (trb_type) {
        case XHCI_TRB_TYPE_TRANSFER_EVENT:
            // 【类型 32：传输事件】(U 盘业务收发)
            xhci_handle_transfer_event(xhcd, trb);
            break;
        case XHCI_TRB_TYPE_CMD_COMPLETION:
            // 【类型 33：命令完成事件】(主板建桥图纸回执)
            xhci_handle_cmd_completion(&xhcd->cmd_ring, trb);
            break;
        case XHCI_TRB_TYPE_PORT_STATUS_CHG:
            // 【类型 34：端口状态改变事件】(物理插拔感知)
            xhci_handle_port_status_change(xhcd, trb);
            break;
        case XHCI_TRB_TYPE_HOST_CTRL:
            // 【类型 37：主板级遗言】
            color_printk(RED, BLACK, "\n[FATAL KERNEL PANIC] xHCI Host Controller Event Triggered!\n");
        default:
            break;
    }
}


//xhci中断服务函数
irqreturn_e xhci_isr(cpu_registers_t *regs,void *dev_id) {
    pcie_dev_t *xdev = dev_id;
    xhci_hcd_t *xhcd = xdev->priv_data;

    uint8 evtnt_idx = 0;
    xhci_event_ring_t *evt_ring = &xhcd->event_ring_arr[evtnt_idx];

    // =================================================================
    // 🛡️ 硬件级防御：清除可能残留的 IMAN_IP (防老旧/非标主板中断风暴)
    // =================================================================
    uint32 iman = xhcd->rt_reg->intr_regs[evtnt_idx].iman;
    if (iman & XHCI_IMAN_IP) {
        // RW1C: iman 变量里的 IP 位此时是 1，直接写回，触发硬件清零！
        // 这一步也顺便保持了 IE (Interrupt Enable) 位的原状态不变
        xhcd->rt_reg->intr_regs[evtnt_idx].iman = iman;
    }

    boolean processed_any = FALSE;
    xhci_trb_t current_trb; // 在栈上分配 16 字节，极其高效且安全

    // =================================================================
    // 👑 优雅的迭代循环：一边取，一边分发！
    // =================================================================
    while (xhci_event_ring_deq(evt_ring, &current_trb) == 0) {
        xhci_process_single_event(xhcd, &current_trb);
        processed_any = TRUE;

    }

    // =================================================================
    // 签收中断，归还硬件控制权
    // =================================================================
    if (processed_any) {
        // 由于 xhci_get_next_event 已经帮我们把 dequeue_ptr 移到了最新位置
        // 这里直接计算物理地址，写入 ERDP 即可// 置 EHB 位为 1，清除硬件挂起状态
        uint64 erdp_pa = va_to_pa(&evt_ring->ring_base[evt_ring->deq_idx]) | XHCI_ERDP_EHB;
        xhcd->rt_reg->intr_regs[evtnt_idx].erdp = erdp_pa;
    }
}


//传输环，命令环分配函数
int32 xhci_alloc_submit_ring(xhci_submit_ring_t *ring,uint32 size) {
    ring->ring_base = kzalloc_dma(size * sizeof(xhci_trb_t));
    ring->enq_idx = 0;
    ring->deq_idx = 0;
    ring->size = size;
    ring->cycle = 1;
    ring->ring_lock = 0;
    list_head_init(&ring->pending_list);

    // 🌟 提前埋好最后一节的 Link TRB，把死活不变的数据写死！
    xhci_trb_t *trb = &ring->ring_base[size - 1];
    trb->link.ring_segment_ptr = va_to_pa(ring->ring_base);
    trb->link.toggle_cycle = 1;
    trb->link.trb_type = XHCI_TRB_TYPE_LINK;
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


//xhci设备探测初始化驱动
int32 xhci_probe(pcie_dev_t *xdev, pcie_id_t *id) {
    xdev->dev.drv_data = kzalloc(sizeof(xhci_hcd_t)); //存放xhci相关信息
    xhci_hcd_t *xhcd = xdev->dev.drv_data;
    xhcd->xdev = xdev;
    xdev->priv_data = xhcd;
    xdev->bar[0].vaddr = iomap(xdev->bar[0].paddr, xdev->bar[0].size,PAGE_4K_SIZE,PAGE_ROOT_RW_UC_4K);

    /*初始化xhci寄存器*/
    xhcd->cap_reg = xdev->bar[0].vaddr; //xhci能力寄存器基地址
    xhcd->op_reg = xdev->bar[0].vaddr + xhcd->cap_reg->cap_length; //xhci操作寄存器基地址
    xhcd->rt_reg = xdev->bar[0].vaddr + xhcd->cap_reg->rtsoff; //xhci运行时寄存器基地址
    xhcd->db_reg = xdev->bar[0].vaddr + xhcd->cap_reg->dboff; //xhci门铃寄存器基地址

    /*停止复位xhci*/
    if (xhci_reset(xhcd) == -ETIMEDOUT) {
        while (1);
    }

    xhcd->ctx_size = 32 << ((xhcd->cap_reg->hccparams1 & HCCP1_CSZ) >> 2);     /*设备上下文字节数*/
    xhcd->major_bcd = xhcd->cap_reg->hciversion >> 8; //xhci主版本
    xhcd->minor_bcd = xhcd->cap_reg->hciversion & 0xFF; //xhci次版本
    xhcd->max_ports = xhcd->cap_reg->hcsparams1 >> 24; //xhci最大端口数
    xhcd->max_intrs = xhcd->cap_reg->hcsparams1 >> 8 & 0x7FF; //xhci最大中断数
    xhcd->max_streams_exp = ((xhcd->cap_reg->hccparams1 >> 12) & 0xF)+1; //计算xhci支持的最大流 2^(N+1)

    // =========================================================================
    // 阶段 1：搜寻与分配 (寻找主板上所有的协议支持清单)
    // =========================================================================

    /* 定义一个指针数组，最多容纳 16 个协议能力块 (一般主板也就 2~3 个，如 USB 2.0 和 USB 3.0) */
    xhci_ecap_supported_protocol *ecap_spc_arr[16];

    /* 调用扩展能力雷达，寻找所有 ID 为 2 (Supported Protocol Capability) 的能力块 */
    xhcd->spc_count = xhci_ecap_find(xhcd, ecap_spc_arr, 2);

    /* 为软件端的协议抽象层 (xhci_spc_t) 分配连续的内存数组 */
    xhcd->spc = kzalloc(sizeof(xhci_spc_t) * xhcd->spc_count);

    /* 为“端口号 -> 协议索引”的映射表分配内存 (极其关键的 O(1) 查表数组) */
    xhcd->port_to_spc = kmalloc(xhcd->max_ports);

    /* 将映射表全部初始化为 0xFF，代表“尚未映射/无效端口” */
    asm_mem_set(xhcd->port_to_spc, 0xFF, xhcd->max_ports);

    // =========================================================================
    // 阶段 2：深度解析硬件协议表 (将硬件寄存器状态翻译为内核软件结构)
    // =========================================================================
    for (uint8 i = 0; i < xhcd->spc_count; i++) {
        xhci_spc_t *spc = &xhcd->spc[i];                                // 软件结构指针
        xhci_ecap_supported_protocol *spc_ecap = ecap_spc_arr[i];       // 硬件寄存器指针

        /* 解析 USB 协议版本号 (例如：0x0300 代表 USB 3.0, 0x0200 代表 USB 2.0) */
        spc->major_bcd = spc_ecap->protocol_ver >> 24;                  // 提取主版本号 (Major)
        spc->minor_bcd = spc_ecap->protocol_ver >> 16 & 0xFF;           // 提取次版本号 (Minor)

        /* 巧妙提取名称：直接进行 4 字节 (uint32) 的内存拷贝，通常是 "USB " */
        *(uint32 *) spc->name = *(uint32 *) spc_ecap->name;
        /* 把第 4 个字符 (索引3) 强行置 0，将 "USB " 截断成完美的 C 语言字符串 "USB\0" */
        spc->name[3] = 0;

        /* 解析协议自定义字段 (12 Bits)，通常用于集线器层级深度限制等高级特性 */
        spc->proto_defined = spc_ecap->port_info >> 16 & 0xFFF;

        /* ★ 核心拓扑数据 ★ */
        spc->port_first = spc_ecap->port_info & 0xFF;                   // 属于该协议的起始端口号 (注意：硬件是从 1 开始的！)
        spc->port_count = spc_ecap->port_info >> 8 & 0xFF;              // 属于该协议的连续端口总数

        /* 解析协议插槽类型 (5 Bits)，用于后续设备上下文的初始化 */
        spc->slot_type = spc_ecap->protocol_slot_type & 0x1F;

        /* 解析自定义速率表 (PSI) 的数量 (4 Bits) */
        spc->psi_count = spc_ecap->port_info >> 28 & 0xF;

        // =========================================================================
        // 阶段 3：装载“动态速率翻译字典” (应对 USB 3.1+ 的非标准速率)
        // =========================================================================
        if (spc->psi_count) {
            /* 如果硬件提供了 PSI 表，为它们分配内存 */
            uint32 *psi = kzalloc(sizeof(uint32) * spc->psi_count);
            spc->psi = psi;
            /* 将硬件提供的速率映射表一一拷贝到内核中，供以后查询 */
            for (uint8 j = 0; j < spc->psi_count; j++) {
                psi[j] = spc_ecap->protocol_speed[j];
            }
        }

        // =========================================================================
        // 阶段 4：建立 O(1) 端口映射表 (极其关键的防雷区)
        // =========================================================================
        /* * 硬件大坑：spc->port_first 是从 1 开始计数的 (物理世界的习惯)
         * 软件数组：xhcd->port_to_spc 是从 0 开始计数的 (C 语言的习惯)
         * 所以必须使用 spc->port_first - 1 进行完美降维降级对齐！
         */
        for (uint8 j = spc->port_first - 1; j < spc->port_first - 1 + spc->port_count; j++) {
            /* 将逻辑端口号 j 映射到当前协议的索引 i 上 */
            /* 以后只要知道端口号 j，读取 port_to_spc[j]，瞬间就能知道它是 USB 2.0 还是 3.0 */
            xhcd->port_to_spc[j] = i;
        }
    }


    /*初始化设备上下文*/
    xhcd->max_slots = xhcd->cap_reg->hcsparams1 & 0xff;
    xhcd->dcbaap = kzalloc_dma((xhcd->max_slots+1)<<3);
    //分配设备上下文插槽内存,最大插槽数量(插槽从1开始需要+1)*8字节内存
    xhcd->op_reg->dcbaap = va_to_pa(xhcd->dcbaap); //把设备上下文基地址数组表的物理地址写入寄存器
    xhcd->op_reg->config = xhcd->max_slots; //把最大插槽数量写入寄存器

    //xhci支持多少个slot就分配多少个udev结构
    xhcd->udevs = kzalloc((xhcd->max_slots+1)<<3);

    /*初始化命令环*/
    xhci_alloc_submit_ring(&xhcd->cmd_ring,64); //命令环分配64个槽位
    xhcd->op_reg->crcr = va_to_pa(xhcd->cmd_ring.ring_base) | 1; //命令环物理地址写入crcr寄存器，置位rcs

    /*初始化中断器*/
    //可以根据cpu核心和MaxIntrs取小值设置多事件环。暂时设置1个事件环
    xhcd->enable_event_ring_count = 1;
    xhci_event_ring_t *event_ring_arr = kzalloc(sizeof(xhci_event_ring_t) * xhcd->enable_event_ring_count);
    xhcd->event_ring_arr = event_ring_arr;
    for (uint16 i = 0; i < xhcd->enable_event_ring_count; i++) {
        xhci_alloc_event_ring(&event_ring_arr[i],1024); //每个事件环设置1024个槽位

        xhcd->rt_reg->intr_regs[i].erstsz = 1; //设置1,单事件环段
        xhcd->rt_reg->intr_regs[i].erstba = va_to_pa(event_ring_arr[i].erst_base); //事件环段表物理地址写入寄存器
        xhcd->rt_reg->intr_regs[i].erdp = va_to_pa(event_ring_arr[i].ring_base); //事件环物理地址写入寄存器
    }

    /*初始化暂存器缓冲区*/
    uint32 spb_number = (xhcd->cap_reg->hcsparams2 & 0x1f << 21) >> 16 | xhcd->cap_reg->hcsparams2>> 27;
    if (spb_number) {
        uint64 *spb_array = kzalloc_dma(spb_number << 3); //分配暂存器缓冲区指针数组
        for (uint32 i = 0; i < spb_number; i++) spb_array[i] = va_to_pa(kzalloc(PAGE_4K_SIZE << asm_tzcnt(xhcd->op_reg->pagesize)));
        //分配暂存器缓存区
        xhcd->dcbaap[0] = va_to_pa(spb_array); //暂存器缓存去数组指针写入设备上下写文数组0
    }

    // =========================================================================
    // 👑 必须先铺设“中断管线” (从 CPU 到 PCIe 总线)
    // =========================================================================
    /* 1. 向大管家申请中断向量号 */
    pcie_alloc_irq(xdev, 1);

    /* 2. 注册内核软件 ISR (此时 CPU 准备好接客了) */
    pcie_register_isr(xdev, 0, xhci_isr, xdev->dev.name);

    /* 3. 填入 MSI-X Table 并拉下总线电闸 (此时 PCIe 链路打通) */
    pcie_enable_irq(xdev);

    // =========================================================================
    // 🚀 管线铺好后，最后一步才能“放水” (启动 xHCI 外设)
    // =========================================================================
    /* 4. 打开具体的事件环阀门 (配置 IMAN 寄存器，启用 0 号队列) */
    xhci_enable_intr(xhcd, 0);

    /* 5. 轰鸣点火！启动全局 xHCI 控制器 (此时 USBCMD.RS 和 INTE 置 1) */
    /* 一旦执行完这句代码，随时可能有真实的硬件中断砸进 xhci_isr！*/
    xhci_start(xhcd);

    color_printk(
        GREEN,BLACK,
        "XHCI Version:%x.%x MaxSlots:%d MaxIntrs:%d MaxPorts:%d Dev_Ctx_Size:%d USBcmd:%#x USBsts:%#x    \n",
        xhcd->major_bcd, xhcd->minor_bcd, xhcd->max_slots,
        xhcd->max_intrs, xhcd->max_ports,
        xhcd->ctx_size, xhcd->op_reg->usbcmd,
        xhcd->op_reg->usbsts);

    for (uint8 i = 0; i < xhcd->spc_count; i++) {
        xhci_spc_t *spc = &xhcd->spc[i];
        color_printk(GREEN,BLACK, "spc%d %s%x.%x port_first:%d port_count:%d psi_count:%d    \n", i, spc->name,
                     spc->major_bcd, spc->minor_bcd, spc->port_first, spc->port_count, spc->psi_count);
    }


    extern void usb_dev_scan(xhci_hcd_t *xhcd);
    usb_dev_scan(xhcd);

    color_printk(GREEN,BLACK, "\nUSBcmd:%#x  USBsts:%#x  \n", xhcd->op_reg->usbcmd,
                 xhcd->op_reg->usbsts);
}

void xhci_remove(pcie_dev_t *xhci_dev) {
}

//xhci驱动初始化
pcie_drv_t *xhci_drv_init(void) {
    pcie_drv_t *xhci_drv = kmalloc(sizeof(pcie_drv_t));
    pcie_id_t *id_table = kzalloc(sizeof(pcie_id_t) * 2);
    id_table->class_code = XHCI_CLASS_CODE;
    xhci_drv->drv.name = "XHCI-driver";
    xhci_drv->drv.id_table = id_table;
    xhci_drv->probe = xhci_probe;
    xhci_drv->remove = xhci_remove;
    return xhci_drv;
}
