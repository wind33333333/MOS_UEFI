#include "xhci.h"
#include "printk.h"
#include "pcie.h"
#include "slub.h"
#include "vmm.h"
#include "driver.h"
#include "vmalloc.h"
#include "usb-core.h"
#include "errno.h"


//命令环/传输环入队列
uint64 xhci_ring_enqueue(xhci_ring_t *ring, xhci_trb_t *trb_push) {
    xhci_trb_t *trb_ptr;
    if (ring->index >= TRB_COUNT - 1) {
        uint8 chain = ring->ring_base[TRB_COUNT - 2].link.chain; //获取队列前一个trb的chain位

        trb_ptr = &ring->ring_base[TRB_COUNT - 1];
        trb_ptr->raw[0] = 0;
        trb_ptr->raw[1] = 0; //清空环中trb防止残留数据污染

        trb_ptr->link.ring_segment_ptr = va_to_pa(ring->ring_base);
        trb_ptr->link.toggle_cycle = 1;
        trb_ptr->link.trb_type = XHCI_TRB_TYPE_LINK;
        trb_ptr->link.cycle = ring->cycle;
        trb_ptr->link.chain = chain;

        ring->index = 0;
        ring->cycle ^= 1;
    }

    trb_push->link.cycle = ring->cycle; //设置需要如队列trb的cycle位

    trb_ptr = &ring->ring_base[ring->index];
    trb_ptr->raw[0] = trb_push->raw[0];
    trb_ptr->raw[1] = trb_push->raw[1];
    ring->index++;
    return va_to_pa(trb_ptr);
}

/**
 * @brief 从事件环中抓取下一个事件 (纯粹的底层收割机)
 * @param xhcd       控制器的上下文
 * @param intr_num   中断器索引 (目前单核轮询填 0 即可)
 * @param out_event  [输出参数] 用于接收 16 字节事件 TRB 现场的指针
 * @return int32     1 表示抓到了新事件，0 表示没有新事件
 */
int32 xhci_event_ring_dequeue(xhci_hcd_t *xhcd, uint8 intr_num, xhci_trb_t *out_event) {
    xhci_ring_t *evt_ring = &xhcd->intr[intr_num].event_rings;
    xhci_trb_t *trb = &evt_ring->ring_base[evt_ring->index];

    // 1. 检查 Cycle 位：如果硬件还没把事件写进来，直接返回 0
    if (trb->cmd_comp_event.cycle != evt_ring->cycle) {
        return -EAGAIN;
    }

    // 2. 完美拷贝出这 16 字节的案发现场
    *out_event = *trb;

    // 3. 推进软件出队指针
    evt_ring->index++;
    if (evt_ring->index >= TRB_COUNT) {
        evt_ring->index = 0;
        evt_ring->cycle ^= 1; // 环绕时翻转 Cycle 预期
    }

    // 4. ★ 核心防御：敲击 ERDP 寄存器！
    // 告诉主板：“这个事件我吃掉了，你可以把它的空间回收给后续的新事件了！”
    // 加上 XHCI_ERDP_EHB (Event Handler Busy) 标志，清除主板的未决中断状态
    xhcd->rt_reg->intr_regs[intr_num].erdp =
        va_to_pa(&evt_ring->ring_base[evt_ring->index]) | XHCI_ERDP_EHB;

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



#include <errno.h> // 确保包含了 POSIX 错误码头文件

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


/**
 * @brief xHCI 统一命令环发射器 (POSIX 返回值规范)
 * @return int 返回 0 表示成功，返回负数表示 POSIX 错误码
 */
int32 xhci_execute_command_sync(xhci_hcd_t *xhcd, xhci_trb_t *cmd_trb, uint32 timeout_us, xhci_trb_t *out_event_trb) {
    // 1. 命令入队并敲门铃
    uint64 cmd_pa = xhci_ring_enqueue(&xhcd->cmd_ring, cmd_trb);
    xhci_ring_doorbell(xhcd, 0, 0);

    while (timeout_us--) {
        xhci_trb_t event;

        // 尝试抓取事件
        int32 posix_err = xhci_event_ring_dequeue(xhcd, 0, &event);

        // 分支 1：没抓到 (最常见的情况，环是空的)
        if (posix_err == -EAGAIN) {
            asm_pause();
            continue; // 歇一下，下一轮循环再试
        }

        // 分支 2：抓取时发生致命错误 (未来扩展用，比如内存指针越界/主板掉线)
        if (posix_err < 0) {
            return posix_err; // 直接把底层的致命错误抛给上层
        }

        // 分支 3：fetch_err == 0 (完美抓到了事件！)
        if (event.cmd_comp_event.trb_type == XHCI_TRB_TYPE_CMD_COMPLETION &&
            event.cmd_comp_event.cmd_trb_ptr == cmd_pa) {

            if (out_event_trb) *out_event_trb = event; // 保存回执现场

            return xhci_translate_error(event.cmd_comp_event.comp_code); // 返回 POSIX 错误
            }
    }
    return -ETIMEDOUT;

}

/**
 * @brief 同步等待特定事件 (纯正 POSIX 语义版)
 * @return int32 0 表示完美命中且成功，负数表示底层报错 (如 -EPIPE, -ETIMEDOUT, -EIO)
 */
int32 xhci_wait_for_event(
    xhci_hcd_t *xhcd,
    uint16 intr_number,
    trb_type_e expected_type,
    uint64 expected_pa_or_port,
    uint8 slot_id,
    uint8 ep_dci,
    uint32 timeout_ms,
    xhci_trb_t *out_trb)
{

    // 建议在实机压测时，将 timeout_ms 放大为真实的轮询次数 (比如 timeout_ms * 10000)
    while (timeout_ms--) {
        xhci_trb_t event;

        // 尝试抓取事件
        int32 posix_err = xhci_event_ring_dequeue(xhcd, intr_number, &event);

        // 分支 1：没抓到 (最常见的情况，环是空的)
        if (posix_err == -EAGAIN) {
            asm_pause();
            continue; // 歇一下，下一轮循环再试
        }

        // 防御性拦截：如果底层抓取器本身爆出致命错误，直接向上透传
        if (posix_err < 0) {
            return posix_err;
        }

        trb_type_e trb_type = event.cmd_comp_event.trb_type;
        xhci_trb_comp_code_e comp_code = event.cmd_comp_event.comp_code;
        boolean is_matched = FALSE;
        uint64 trb_pa;

        // ==========================================================
        // ★ 阶段 2：三大护法事件的精准拦截与规范解码 (Switch 极速跳转表)
        // ==========================================================
        switch (trb_type) {

            case XHCI_TRB_TYPE_TRANSFER_EVENT: {
                // 【类型 32：传输事件】(U 盘业务收发)
                uint8 evt_slot_id = event.transfer_event.slot_id;
                uint8 evt_ep_dci  = event.transfer_event.ep_dci;
                trb_pa = event.transfer_event.tr_trb_ptr;

                if (expected_type == XHCI_TRB_TYPE_TRANSFER_EVENT) {
                    if (trb_pa == expected_pa_or_port) {
                        // 完美命中：物理地址完全一致 (比如等到了 Status TRB)
                        is_matched = TRUE;
                    } else if (evt_slot_id == slot_id && evt_ep_dci == ep_dci && comp_code != XHCI_COMP_SUCCESS) {
                        // 神级容错：IOC=0 时硬件由于 STALL 等错误提前暴毙，抛出了错误事件。
                        is_matched = TRUE;
                    }
                }
                break;
            }

            case XHCI_TRB_TYPE_CMD_COMPLETION: {
                // 【类型 33：命令完成事件】(主板建桥图纸回执)
                trb_pa = event.cmd_comp_event.cmd_trb_ptr;
                if (expected_type == XHCI_TRB_TYPE_CMD_COMPLETION && trb_pa == expected_pa_or_port) {
                    is_matched = TRUE;
                }
                break;
            }

            case XHCI_TRB_TYPE_PORT_STATUS_CHG: {
                // 【类型 34：端口状态改变事件】(物理插拔感知)
                uint8 evt_port_id = event.prot_status_change_event.port_id;

                if (expected_type == XHCI_TRB_TYPE_PORT_STATUS_CHG) {
                    if (evt_port_id == (uint8)expected_pa_or_port || expected_pa_or_port == 0) {
                        // 命中！(如果 expected_pa_or_port 为 0，表示监听任何端口的插拔事件)
                        is_matched = TRUE;
                    }
                }
                break;
            }

            case XHCI_TRB_TYPE_HOST_CTRL: {
                // 【类型 37：主板级遗言】
                color_printk(RED, BLACK, "\n[FATAL KERNEL PANIC] xHCI Host Controller Event Triggered!\n");

                if (comp_code == 4) {
                    color_printk(RED, BLACK, "Reason: PCIe/DMA Fatal System Error! Hardware is dead.\n");
                } else if (comp_code == 21) {
                    color_printk(RED, BLACK, "Reason: Event Ring Full! OS is too slow to process interrupts.\n");
                } else {
                    color_printk(RED, BLACK, "Reason Code: %d\n", comp_code);
                }

                // ★ POSIX 修正：主板已经物理死亡或环满爆，绝不能再等，直接抛出 I/O 错误！
                return -EIO;
            }

            default: {
                // 收到我们目前不关心的其他事件 (如微帧翻转、门铃等)
                // 默默吃掉，不做处理，等待下一轮 while 循环
                break;
            }
        }

        // ==========================================================
        // 阶段 4：命中出口
        // ==========================================================
        if (is_matched) {
            if (out_trb != NULL) {
                // 留下完整的现场供外层查阅法医鉴定
                *out_trb = event;
            }
            // ★ POSIX 核心重构：将硬件枚举翻译为标准 POSIX 错误码抛出
            return xhci_translate_error(comp_code);
        }

        // 如果 is_matched == FALSE，由于 index 已由底层的 dequeue 推进，会自动接续下一轮循环
    }

    color_printk(RED, BLACK, "xHCI: Event Ring Timeout! Expected Type: %d, Target: %#lx\n", expected_type, expected_pa_or_port);

    // ★ POSIX 修正：不再返回硬件枚举宏，而是标准的 -ETIMEDOUT (110)
    return -ETIMEDOUT;
}



//======================================= 命令环命令 ===========================================================

//分配插槽
int32 xhci_cmd_enable_slot(xhci_hcd_t *xhcd, uint8 *out_slot_id) {
    xhci_trb_t evt_trb;
    xhci_trb_t cmd_trb = {0};
    cmd_trb.enable_slot.type = XHCI_TRB_TYPE_ENABLE_SLOT;
    cmd_trb.enable_slot.slot_type = 0;

    // 1. 发送命令并同步等待
    int32 posix_err = xhci_execute_command_sync(xhcd, &cmd_trb, 30000000, &evt_trb);

    // 2. 异常情况：尽早拦截并向外抛出 (Early Return)
    if (posix_err < 0) {
        return posix_err;
    }

    // 3. 正常情况：执行成功后的赋值
    *out_slot_id = evt_trb.cmd_comp_event.slot_id;

    return 0;
}

/**
 * @brief 注销并释放 xHCI 设备槽位 (常用于热拔出或枚举失败的灾难恢复)
 * @param xhcd xHCI 控制器上下文
 * @param usb_dev         要销毁的 USB 设备对象
 * @return int8           0 表示成功，-1 表示失败
 */
int32 xhci_cmd_disable_slot(xhci_hcd_t *xhcd, uint8 slot_id) {
    if (xhcd == NULL || slot_id == 0) {
        return -1; // 非法参数或设备本就没分配槽位
    }

    xhci_trb_t cmd_trb = {0};

    // 1. 组装注销命令
    cmd_trb.disable_slot.trb_type = XHCI_TRB_TYPE_DISABLE_SLOT;
    cmd_trb.disable_slot.slot_id  = slot_id;

    // 2. 发射命令并同步等待
    return xhci_execute_command_sync(xhcd, &cmd_trb, 30000000,NULL);
}


//设置设备地址
int32 xhci_cmd_addr_dev(xhci_hcd_t *xhcd, uint8 slot_id,xhci_input_ctx_t *input_ctx) {
    //配置和执行addr_dev命令
    xhci_trb_t cmd_trb = {0};
    cmd_trb.addr_dev.trb_type = XHCI_TRB_TYPE_ADDRESS_DEVICE;
    cmd_trb.addr_dev.input_ctx_ptr = va_to_pa(input_ctx);
    cmd_trb.addr_dev.slot_id = slot_id;
    cmd_trb.addr_dev.bsr = 0;

    return  xhci_execute_command_sync(xhcd, &cmd_trb, 30000000,NULL);

}

/**
 * @brief 底层指令：发送 Configure Endpoint 命令
 * @param xhcd xHCI 控制器实例
 * @param input_ctx_pa    Input Context 的物理地址 (必须64字节对齐)
 * @param slot_id         目标 Slot ID
 * @param dc              Deconfigure 标志 (0=配置端点, 1=一键清除所有业务端点)
 */
int32 xhci_cmd_cfg_ep(xhci_hcd_t *xhcd, xhci_input_ctx_t *input_ctx, uint8 slot_id, uint8 dc) {
    xhci_trb_t cmd_trb = {0};
    cmd_trb.cfg_ep.trb_type = XHCI_TRB_TYPE_CONFIGURE_EP;
    cmd_trb.cfg_ep.input_ctx_ptr = va_to_pa(input_ctx);
    cmd_trb.cfg_ep.slot_id = slot_id;
    cmd_trb.cfg_ep.dc = dc;

    // 敲响命令环门铃，等待主板评估带宽并分配资源
    return xhci_execute_command_sync(xhcd, &cmd_trb, 30000000, NULL);

}

/**
 * @brief 底层指令：发送 Evaluate Context 命令
 * @param xhci            xHCI 控制器实例
 * @param input_ctx_pa    Input Context 的物理地址
 * @param slot_id         目标 Slot ID
 */
int32 xhci_cmd_eval_ctx(xhci_hcd_t *xhcd, xhci_input_ctx_t *input_ctx, uint8 slot_id) {
    xhci_trb_t cmd_trb = {0};
    cmd_trb.eval_ctx.trb_type = XHCI_TRB_TYPE_EVALUATE_CTX;
    cmd_trb.eval_ctx.input_ctx_ptr = va_to_pa(input_ctx);
    cmd_trb.eval_ctx.slot_id = slot_id;

    // 敲响命令环门铃，主板将读取并更新上下文
    return xhci_execute_command_sync(xhcd, &cmd_trb, 30000000, NULL);

}


//重置端点
uint32 xhci_cmd_reset_ep(xhci_hcd_t *xhcd, uint8 slot_id, uint8 ep_dci) {
    xhci_trb_t cmd_trb = {0};
    cmd_trb.rest_ep.type = XHCI_TRB_TYPE_RESET_EP;
    cmd_trb.rest_ep.tsp = 0;
    cmd_trb.rest_ep.ep_dci = ep_dci;
    cmd_trb.rest_ep.slot_id = slot_id;

    return xhci_execute_command_sync(xhcd, &cmd_trb, 30000000,NULL);
}

/**
 * @brief 紧急刹车：停止指定设备的指定端点 (用于超时或STALL抢救)
 * @param xhcd    xHCI 控制器上下文
 * @param slot_id 出事设备的 Slot ID
 * @param ep_dci  出事端点的 DCI (注意: EP0 的 DCI 是 1)
 * @return int32  0 表示成功刹车，负数表示 POSIX 错误码 (如 -EINVAL)
 */
int32 xhci_cmd_stop_ep(xhci_hcd_t *xhcd, uint8 slot_id, uint8 ep_dci) {
    // 1. POSIX 标准参数防呆拦截
    if (xhcd == NULL || slot_id == 0 || ep_dci == 0 || ep_dci > 31) {
        return -EINVAL; // Invalid argument: 非法参数
    }

    xhci_trb_t cmd_trb = {0};

    // 2. 组装“拔管”命令
    cmd_trb.stop_ep.trb_type = XHCI_TRB_TYPE_STOP_EP; // 15
    cmd_trb.stop_ep.slot_id  = slot_id;
    cmd_trb.stop_ep.ep_dci   = ep_dci;
    cmd_trb.stop_ep.suspend  = 0; // 坚决不挂起，要求主板彻底停下传输环

    // 3. 发射命令并同步等待，直接获取 POSIX 错误码
    return xhci_execute_command_sync(xhcd, &cmd_trb, 30000000, NULL);
}


/**
 * @brief 发送 Set TR Dequeue Pointer Command，强制移动端点底层的出队指针
 * * @param xhci          xHCI 控制器实例
 * @param slot_id       设备槽位号
 * @param ep_dci        端点上下文索引 (DCI，例如 IN通常是 3，OUT通常是 4)
 * @param transfer_ring 需要被修改指针的 Transfer Ring 结构体指针
 * @return int32        完成状态码 (XHCI_COMP_SUCCESS 表示成功)
 */
int32 xhci_cmd_set_tr_deq_ptr(xhci_hcd_t *xhcd, uint8 slot_id, uint8 ep_dci,
                                  xhci_ring_t *transfer_ring) {
    xhci_trb_t cmd_trb={0};

    // 【核心算力】：找到 Transfer Ring 中软件当前准备写入的、下一个干净槽位的物理地址
    uint64 next_clean_trb_pa = va_to_pa(&transfer_ring->ring_base[transfer_ring->index]);

    // 获取软件当前在这个槽位上预期的 Cycle Bit 状态
    uint8 next_cycle_state = transfer_ring->cycle;

    // 硬件强制规范：物理地址的最低位 (Bit 0) 必须包含 Dequeue Cycle State (DCS)
    // 这样硬件指针挪过去之后，才知道下次扫描该期待 0 还是 1
    cmd_trb.set_tr_deq_ptr.tr_deq_ptr = next_clean_trb_pa | next_cycle_state;
    cmd_trb.set_tr_deq_ptr.type = XHCI_TRB_TYPE_SET_TR_DEQUEUE;
    cmd_trb.set_tr_deq_ptr.ep_dci = ep_dci;
    cmd_trb.set_tr_deq_ptr.slot_id = slot_id;

    return  xhci_execute_command_sync(xhcd, &cmd_trb, 30000000,NULL);
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

    // 敲响门铃，主板将强行清空该 Slot 的所有业务端点状态
    // 敲响命令环门铃，主板将读取并更新上下文
    return xhci_execute_command_sync(xhcd, &cmd_trb, 30000000, NULL);

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

//启动xhci
int32 xhci_start(xhci_hcd_t *xhcd) {
    xhcd->op_reg->usbcmd |= XHCI_CMD_RS;
    uint32 times = 20000000;
    while (times--) {
        if ((xhcd->op_reg->usbsts & XHCI_STS_HCH) == 0)
            return 0;
    }
    color_printk(RED, BLACK, "xHCI: Start timeout! Controller refused to run.\n");
    return -ETIMEDOUT;
}

//启用xhci中断
void xhci_enable_intr(xhci_hcd_t *xhcd,uint16 intr_number) {
    xhcd->rt_reg->intr_regs[intr_number].iman |= 1<1;
}

//禁用xhci中断
void xhci_disable_intr(xhci_hcd_t *xhcd,uint16 intr_number) {
    xhcd->rt_reg->intr_regs[intr_number].iman &= ~(1<1);
}



//xhci设备探测初始化驱动
int xhci_probe(pcie_dev_t *xdev, pcie_id_t *id) {
    xdev->dev.drv_data = kzalloc(sizeof(xhci_hcd_t)); //存放xhci相关信息
    xhci_hcd_t *xhcd = xdev->dev.drv_data;
    xhcd->xdev = xdev;
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

    /*初始化命令环*/
    xhci_alloc_ring(&xhcd->cmd_ring);
    xhcd->op_reg->crcr = va_to_pa(xhcd->cmd_ring.ring_base) | 1; //命令环物理地址写入crcr寄存器，置位rcs

    /*初始化中断器*/
    //可以根据cpu核心和MaxIntrs取小值设置多事件环。暂时设置1个事件环
    xhcd->enable_intr_count = 1;
    xhci_intr *intr = kzalloc(sizeof(xhci_intr) * xhcd->enable_intr_count);
    xhcd->intr = intr;
    for (uint16 i = 0; i < xhcd->enable_intr_count; i++) {
        xhci_alloc_ring(&intr[i].event_rings);
        uint64 evt_pa = va_to_pa(intr[i].event_rings.ring_base);

        xhci_erst_t *erstba = kzalloc_dma(sizeof(xhci_erst_t)); //分配事件环段表内存，单段只分配一个
        intr[i].erstba = erstba;
        erstba->ring_seg_base = evt_pa; //段表中写入事件环物理地址
        erstba->ring_seg_size = TRB_COUNT; //事件环最大trb个数
        erstba->reserved = 0;

        xhci_disable_intr(xhcd,i);  //关闭中断
        xhcd->rt_reg->intr_regs[i].erstsz = 1; //设置1,单事件环段
        xhcd->rt_reg->intr_regs[i].erstba = va_to_pa(erstba); //事件环段表物理地址写入寄存器
        xhcd->rt_reg->intr_regs[i].erdp = evt_pa; //事件环物理地址写入寄存器
    }

    /*初始化暂存器缓冲区*/
    uint32 spb_number = (xhcd->cap_reg->hcsparams2 & 0x1f << 21) >> 16 | xhcd->cap_reg->hcsparams2>> 27;
    if (spb_number) {
        uint64 *spb_array = kzalloc_dma(spb_number << 3); //分配暂存器缓冲区指针数组
        for (uint32 i = 0; i < spb_number; i++) spb_array[i] = va_to_pa(kzalloc(PAGE_4K_SIZE << asm_tzcnt(xhcd->op_reg->pagesize)));
        //分配暂存器缓存区
        xhcd->dcbaap[0] = va_to_pa(spb_array); //暂存器缓存去数组指针写入设备上下写文数组0
    }

    /*启动xhci*/
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
