#include "xhci.h"
#include "printk.h"
#include "pcie.h"
#include "slub.h"
#include "vmm.h"
#include "driver.h"
#include "vmalloc.h"
#include "usb-core.h"

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

xhci_trb_comp_code_e xhci_wait_for_event(
    xhci_hcd_t *xhcd,
    uint16 intr_number,
    trb_type_e expected_type,
    uint64 expected_pa_or_port,
    uint8 slot_id,
    uint8 ep_dci,
    uint32 timeout_ms,
    xhci_trb_t *out_trb)
{
    xhci_ring_t *evt_ring = &xhcd->intr[intr_number].event_rings;

    // 建议在实机压测时，将 timeout_ms 放大为真实的轮询次数 (比如 timeout_ms * 10000)
    while (timeout_ms--) {
        // 1. 获取软件当前正在“盯”着的那个 TRB 槽位
        xhci_trb_t event_trb = evt_ring->ring_base[evt_ring->index];
        if (event_trb.cmd_comp_event.cycle != evt_ring->cycle) {
            asm_pause();
            continue;
        }

        // 事件trb向前移动一个，判断事件环是否满了
        evt_ring->index++;
        if (evt_ring->index >= TRB_COUNT) {
            evt_ring->index = 0;
            evt_ring->cycle ^= 1;
        }
        xhcd->rt_reg->intr_regs[intr_number].erdp = va_to_pa(&evt_ring->ring_base[evt_ring->index]) | XHCI_ERDP_EHB;

        trb_type_e trb_type = event_trb.cmd_comp_event.trb_type;
        xhci_trb_comp_code_e comp_code = event_trb.cmd_comp_event.comp_code;
        uint64 trb_pa;
        boolean is_matched = FALSE;

        // ==========================================================
        // ★ 阶段 2：三大护法事件的精准拦截与规范解码 (Switch 极速跳转表)
        // ==========================================================
        switch (trb_type) {

            case XHCI_TRB_TYPE_TRANSFER_EVENT: {
                // 【类型 32：传输事件】(U 盘业务收发)
                uint8 evt_slot_id = event_trb.transfer_event.slot_id;
                uint8 evt_ep_dci  = event_trb.transfer_event.ep_dci;
                trb_pa = event_trb.transfer_event.tr_trb_ptr;

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
                trb_pa = event_trb.cmd_comp_event.cmd_trb_ptr;
                if (expected_type == XHCI_TRB_TYPE_CMD_COMPLETION && trb_pa == expected_pa_or_port) {
                    is_matched = TRUE;

                }
                break;
            }

            case XHCI_TRB_TYPE_PORT_STATUS_CHG: {
                // 【类型 34：端口状态改变事件】(物理插拔感知)
                uint8 evt_port_id = event_trb.prot_status_change_event.port_id;

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
                // 此时整个 USB 总线已经瘫痪
                // asm_cli();   // 关闭 CPU 中断
                // asm_hlt();   // 强制死机
                break;
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
                *out_trb = event_trb;
            }
            return comp_code;
        }

        // 如果 is_matched == FALSE，由于 index 已推进，会自动接续下一轮循环
    }

    color_printk(RED, BLACK, "xHCI: Event Ring Timeout! Expected Type: %d, Target: %#lx\n", expected_type, expected_pa_or_port);
    return XHCI_COMP_TIMEOUT;
}

/**
 * @brief 处理 xHCI 通用的完成码 (命令环和传输环共享的系统级状态)
 * @param comp_code  要检查的完成码
 * @param trb_pa     出事的 TRB 物理地址 (用于日志定位)
 * @return uint8_t   如果命中了通用码返回 1 (true)，否则返回 0 (false)
 */
uint8 xhci_handle_common_error(xhci_trb_comp_code_e comp_code, uint64 trb_pa) {
    // 2. 拦截并处理所有的“通用级/系统级”硬件崩溃
    switch (comp_code) {
        case XHCI_COMP_TIMEOUT:
            color_printk(RED, BLACK, "[xHCI General Error] TIMEOUT (-1) at PA %#lx: Hardware hang or event lost.\n",
                         trb_pa);
            return 1;

        case XHCI_COMP_TRB_ERROR:
            color_printk(
                RED, BLACK, "[xHCI General Error] TRB Error (5) at PA %#lx: Invalid TRB format (Chain/Type wrong).\n",
                trb_pa);
            return 1;

        case XHCI_COMP_RESOURCE_ERROR:
            color_printk(
                RED, BLACK,
                "[xHCI General Error] Resource Error (7): Controller internal memory/resources exhausted.\n");
            return 1;

        case XHCI_COMP_VF_EVENT_RING_FULL_ERROR:
            color_printk(RED, BLACK, "[xHCI General Error] VF Event Ring Full (16): SR-IOV ring overflow.\n");
            return 1;

        case XHCI_COMP_EVENT_RING_FULL_ERROR:
            color_printk(RED, BLACK, "[xHCI General Error] Event Ring Full (21): OS interrupt handling too slow!\n");
            return 1;

        case XHCI_COMP_EVENT_LOST_ERROR:
            color_printk(
                RED, BLACK, "[xHCI General Error] Event Lost (32): Event ring overflowed, hardware dropped events!\n");
            return 1;

        case XHCI_COMP_UNDEFINED_ERROR:
            color_printk(RED, BLACK, "[xHCI General Error] Undefined Error (33): Fatal hardware crash!\n");
            return 1;

        default:
            // 走到这里，说明这不是通用错误！它是命令环或传输环的专属错误。
            // 返回 0，把皮球踢回给调用者的特定 switch-case 里去处理。
            return 0;
    }
}

//======================================= 命令环命令 ===========================================================

/**
 * @brief xHCI 统一的命令环 (Command Ring) 发射与严厉诊断函数 (带数据回传功能)
 * @param xhcd          xHCI 控制器上下文指针
 * @param cmd_trb       已经组装好具体字段的 Command TRB 指针
 * @param timeout_us    超时时间 (通常 2000000us)
 * @param out_event_trb [输出参数] 用于接收主板返回的完整事件 TRB。如果不关心返回数据，可传入 NULL。
 * @return xhci_comp_code_t 返回强类型的硬件完成码
 */
xhci_trb_comp_code_e xhci_execute_command_sync(xhci_hcd_t *xhcd, xhci_trb_t *cmd_trb,
                                               uint32 timeout_us, xhci_trb_t *out_event_trb) {
    //命令如队列
    uint64 cmd_pa = xhci_ring_enqueue(&xhcd->cmd_ring, cmd_trb);

    //命令环响铃
    xhci_ring_doorbell(xhcd, 0, 0);

    // ★ 关键修改：把 out_event_trb 指针继续传递给底层的 wait 函数
    // 底层的 wait 函数在轮询/中断匹配到事件时，需要把那个 Event TRB 的 16 字节内容 copy 到这个指针里
    xhci_trb_comp_code_e comp_code = xhci_wait_for_event(xhcd,0,XHCI_TRB_TYPE_CMD_COMPLETION ,cmd_pa,0,0, timeout_us, out_event_trb);

    // ==========================================================
    // 第一关：极速放行 (Fast Path) - 99% 的情况走这里！
    // ==========================================================
    if (comp_code == XHCI_COMP_SUCCESS) {
        return XHCI_COMP_SUCCESS; // 直接凯旋！外面的调用者收到 1，皆大欢喜。
    }

    // ==========================================================
    // 第二关：急诊室 (通用大病拦截)
    // ==========================================================
    if (xhci_handle_common_error(comp_code, cmd_pa) == 1) {
        // 如果这里返回了 1，说明急诊室已经抢救/打印过红字日志了。
        // 我们直接把这个致命错误码 (比如 -1) 向上抛给业务层！
        return comp_code;
    }

    // ==========================================================
    // 第三关：专科门诊 (【命令环专属】的 10 种逻辑故障全覆盖)
    // ==========================================================
    color_printk(RED, BLACK, "\nxHCI: [Command Error] Command rejected at PA %#lx!\n", cmd_pa);

    switch (comp_code) {
        // ------------------------------------------------------
        // 1. 资源与配额分配类 (通常发生在 Enable Slot / Configure Endpoint)
        // ------------------------------------------------------
        case XHCI_COMP_NO_SLOTS_AVAILABLE_ERROR: // 9
            color_printk(YELLOW, BLACK,
                         "[Diag] No Slots Available (9):\n"
                         "  -> Check: Did you exceed the 'MaxSlotsEn' limit configured in CONFIG register?\n");
            break;

        case XHCI_COMP_BANDWIDTH_ERROR: // 8
            color_printk(YELLOW, BLACK,
                         "[Diag] Bandwidth Error (8):\n"
                         "  -> Check: USB bus bandwidth is fully reserved. Cannot configure this endpoint.\n");
            break;

        case XHCI_COMP_SECONDARY_BANDWIDTH_ERROR: // 35
            color_printk(YELLOW, BLACK,
                         "[Diag] Secondary Bandwidth Error (35):\n"
                         "  -> Check: Failed to allocate secondary bandwidth for the endpoint.\n");
            break;

        // ------------------------------------------------------
        // 2. 目标未就绪类 (对没有激活的对象发号施令)
        // ------------------------------------------------------
        case XHCI_COMP_SLOT_NOT_ENABLED_ERROR: // 11
            color_printk(YELLOW, BLACK,
                         "[Diag] Slot Not Enabled (11):\n"
                         "  -> Check: You issued a command to a Slot ID that hasn't been enabled yet.\n");
            break;

        case XHCI_COMP_ENDPOINT_NOT_ENABLED_ERROR: // 12
            color_printk(YELLOW, BLACK,
                         "[Diag] Endpoint Not Enabled (12):\n"
                         "  -> Check: You are trying to manage an Endpoint (DCI) that is not configured.\n");
            break;

        // ------------------------------------------------------
        // 3. 数据结构与参数填错类 (最常见的代码 Bug！)
        // ------------------------------------------------------
        case XHCI_COMP_PARAMETER_ERROR: // 17
            color_printk(YELLOW, BLACK,
                         "[Diag] Parameter Error (17):\n"
                         "  -> Check 1: Is your Input Context physically 64-byte aligned?\n"
                         "  -> Check 2: Are the Drop/Add Context flags conflicting?\n"
                         "  -> Check 3: Did you set reserved bits to 1?\n");
            break;

        case XHCI_COMP_INVALID_STREAM_TYPE_ERROR: // 10
            color_printk(YELLOW, BLACK,
                         "[Diag] Invalid Stream Type Error (10):\n"
                         "  -> Check: Primary Stream Array (PSA) configuration mismatch for Bulk endpoint.\n");
            break;

        // ------------------------------------------------------
        // 4. 状态机时序错乱类 (试图打破物理规律)
        // ------------------------------------------------------
        case XHCI_COMP_CONTEXT_STATE_ERROR: // 19
            color_printk(YELLOW, BLACK,
                         "[Diag] Context State Error (19):\n"
                         "  -> Check: Are you trying to Reset an endpoint that is NOT in the 'Halted' state?\n"
                         "  -> Check: Are you modifying a Slot that is in the 'Default' state?\n");
            break;

        // ------------------------------------------------------
        // 5. 正常的控制流回执 (不算是错误，但需要拦截)
        // ------------------------------------------------------
        case XHCI_COMP_COMMAND_RING_STOPPED: // 24
            color_printk(GREEN, BLACK, "[Diag] Command Ring Stopped (24): Normal completion.\n");
            break;

        case XHCI_COMP_COMMAND_ABORTED: // 25
            color_printk(GREEN, BLACK, "[Diag] Command Aborted (25): Normal completion.\n");
            break;

        // ------------------------------------------------------
        // 兜底防御
        // ------------------------------------------------------
        default:
            color_printk(YELLOW, BLACK, "[Diag] Unhandled Command Specific Error Code: %d\n", comp_code);
            break;
    }

    return comp_code;
}

//分配插槽
int32 xhci_cmd_enable_slot(xhci_hcd_t *xhcd, uint8 *out_slot_id) {
    xhci_trb_t evt_trb;
    xhci_trb_t cmd_trb = {0};
    cmd_trb.enable_slot.type = XHCI_TRB_TYPE_ENABLE_SLOT;
    cmd_trb.enable_slot.slot_type = 0;

    // 1. 发送命令到命令环 (Command Ring)
    xhci_trb_comp_code_e comp_code = xhci_execute_command_sync(xhcd, &cmd_trb, 30000000, &evt_trb);

    if (comp_code != XHCI_COMP_SUCCESS) {
        color_printk(RED, BLACK, "xHCI: Failed to enable slot! Error: %d\n", comp_code);
        return -1;
    }
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
    xhci_trb_comp_code_e comp_code = xhci_execute_command_sync(xhcd, &cmd_trb, 30000000,NULL);

    if (comp_code != XHCI_COMP_SUCCESS) {
        color_printk(RED, BLACK, "xHCI: Failed to disable Slot ID %d! Hardware code: %d\n", slot_id, comp_code);
        // 注意：即使硬件注销失败，某些情况下我们依然需要强行清理软件层内存，防止泄漏。
        // 但通常硬件注销失败意味着控制器状态机已经出大问题了。
        return -1;
    }
    return 0;
}


//设置设备地址
int32 xhci_cmd_addr_dev(xhci_hcd_t *xhcd, uint8 slot_id,xhci_input_ctx_t *input_ctx) {
    //配置和执行addr_dev命令
    xhci_trb_t cmd_trb = {0};
    cmd_trb.addr_dev.trb_type = XHCI_TRB_TYPE_ADDRESS_DEVICE;
    cmd_trb.addr_dev.input_ctx_ptr = va_to_pa(input_ctx);
    cmd_trb.addr_dev.slot_id = slot_id;
    cmd_trb.addr_dev.bsr = 0;

    xhci_trb_comp_code_e comp_code = xhci_execute_command_sync(xhcd, &cmd_trb, 30000000,NULL);

    if (comp_code != XHCI_COMP_SUCCESS) {
        color_printk(RED, BLACK, "xHCI: Failed to address device! Error: %d\n", comp_code);
        return -1;
    }
    return 0;
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
    xhci_trb_comp_code_e comp_code = xhci_execute_command_sync(xhcd, &cmd_trb, 30000000, NULL);
    if (comp_code != XHCI_COMP_SUCCESS) {
        color_printk(RED, BLACK, "xHCI: Failed to configure_endpoint! Error: %d\n", comp_code);
        return -1;
    }
    return 0;
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
    xhci_trb_comp_code_e comp_code = xhci_execute_command_sync(xhcd, &cmd_trb, 30000000, NULL);
    if (comp_code != XHCI_COMP_SUCCESS) {
        color_printk(RED, BLACK, "xHCI: Failed to evaluate_context! Error: %d\n", comp_code);
        return -1;
    }
    return 0;
}


//重置端点
uint32 xhci_cmd_reset_ep(xhci_hcd_t *xhcd, uint8 slot_id, uint8 ep_dci) {
    xhci_trb_t cmd_trb = {0};
    cmd_trb.rest_ep.type = XHCI_TRB_TYPE_RESET_EP;
    cmd_trb.rest_ep.tsp = 0;
    cmd_trb.rest_ep.ep_dci = ep_dci;
    cmd_trb.rest_ep.slot_id = slot_id;

    xhci_trb_comp_code_e comp_code = xhci_execute_command_sync(xhcd, &cmd_trb, 30000000,NULL);
    if (comp_code != XHCI_COMP_SUCCESS) {
        color_printk(RED, BLACK, "xHCI: Failed to reset endpiont! Error: %d\n", comp_code);
        return -1;
    }
    return 0;
}

/**
 * @brief 紧急刹车：停止指定设备的指定端点 (用于超时抢救)
 * @param xhcd xHCI 控制器上下文
 * @param slot_id         出事设备的 Slot ID
 * @param ep_id           出事端点的 EP ID (注意: EP0 的 ep_id 是 1)
 * @return int8           0 表示成功刹车，-1 表示灾难升级
 */
int32 xhci_cmd_stop_ep(xhci_hcd_t *xhcd, uint8 slot_id, uint8 ep_id) {
    if (slot_id == 0 || ep_id == 0 || ep_id > 31) {
        return -1; // 参数非法
    }

    xhci_trb_t cmd_trb = {0},evt_trb;

    // 1. 组装“拔管”命令
    cmd_trb.stop_ep.trb_type = XHCI_TRB_TYPE_STOP_EP; // 15
    cmd_trb.stop_ep.slot_id  = slot_id;
    cmd_trb.stop_ep.ep_dci    = ep_id;
    cmd_trb.stop_ep.suspend  = 0; // 坚决不挂起，直接要求主板废弃当前内部缓存的坏状态！

    // 2. 发射命令并同步等待
    // 注意：刹车命令通常非常快，主板会在微秒级响应。
    xhci_trb_comp_code_e comp_code = xhci_execute_command_sync(xhcd, &cmd_trb, 30000000, &evt_trb);

    if (comp_code != XHCI_COMP_SUCCESS) {
        // 如果连 Stop Endpoint 都失败了 (比如返回了 CONTEXT_STATE_ERROR 19)
        // 意味着该端点可能已经处于 Halted(死机) 或者 Disabled(未启用) 状态。
        color_printk(RED, BLACK, "xHCI: Failed to Stop EP %d on Slot %d! Hardware code: %d\n", ep_id, slot_id, comp_code);
        return -1;
    }

    // ==========================================================
    // 3. ★ 架构师核心机密：提取刹车痕迹！
    // ==========================================================
    // 当主板成功停下端点时，它返回的 Event TRB 中，
    // cmd_trb_ptr 字段记录的，正是主板【刹车时，硬件指针停留的那个物理地址】！
    // 这个地址对于你接下来的抢救（挪动指针跨过坏点）有着极其关键的决定性作用！

    uint64 hardware_stopped_pa = evt_trb.cmd_comp_event.cmd_trb_ptr;

    color_printk(YELLOW, BLACK, "xHCI: Emergency Stopped EP %d on Slot %d! HW halted at PA: %#lx\n",
                 ep_id, slot_id, hardware_stopped_pa);

    return 0;
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

    xhci_trb_comp_code_e comp_code = xhci_execute_command_sync(xhcd, &cmd_trb, 30000000,NULL);
    if (comp_code != XHCI_COMP_SUCCESS) {
        color_printk(RED, BLACK, "xHCI: Failed to set_tr_dequeue_pointer! Error: %d\n", comp_code);
        return -1;
    }
    return 0;
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
    xhci_trb_comp_code_e comp_code = xhci_execute_command_sync(xhcd, &cmd_trb, 30000000, NULL);
    if (comp_code != XHCI_COMP_SUCCESS) {
        color_printk(RED, BLACK, "xHCI: Failed to reset_device! Error: %d\n", comp_code);
        return -1;
    }
    return 0;
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
    return -1;
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
    return -1;
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
    return -1;
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

    xhcd->ctx_size = 32 << ((xhcd->cap_reg->hccparams1 & HCCP1_CSZ) >> 2);     /*设备上下文字节数*/
    xhcd->major_bcd = xhcd->cap_reg->hciversion >> 8; //xhci主版本
    xhcd->minor_bcd = xhcd->cap_reg->hciversion & 0xFF; //xhci次版本
    xhcd->max_ports = xhcd->cap_reg->hcsparams1 >> 24; //xhci最大端口数
    xhcd->max_intrs = xhcd->cap_reg->hcsparams1 >> 8 & 0x7FF; //xhci最大中断数

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

    /*停止复位xhci*/
    xhci_reset(xhcd);

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
