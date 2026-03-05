#include "xhci.h"
#include "printk.h"
#include "pcie.h"
#include "slub.h"
#include "vmm.h"
#include "driver.h"
#include "vmalloc.h"

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
 * 等待特定 TRB 完成
 * @param xhci: 控制器实例
 * @param wait_trb_pa: 我们刚才提交的那个 TRB 的物理地址 (用于比对)
 * @param timeout_ms: 超时时间
 */
xhci_trb_comp_code_e xhci_wait_for_event(xhci_hcd_t *xhcd, uint64 wait_trb_pa, uint64 timeout_ms,
                                         xhci_trb_t *out_event_trb) {
    xhci_ring_t *evt_ring = &xhcd->event_ring;
    xhci_trb_t local_trb;
    while (timeout_ms--) {
        //先从事件环取出当前事件trb,如果当前事件trb的cycle位相同则表示事件环有事件
        local_trb = evt_ring->ring_base[evt_ring->index];
        if (local_trb.cmd_comp_event.cycle != evt_ring->cycle) {
            continue;
        }
        //事件trb向前移动一个 ，判断事件环是否满了
        evt_ring->index++;
        if (evt_ring->index >= TRB_COUNT) {
            evt_ring->index = 0;
            evt_ring->cycle ^= 1;
        }
        xhcd->rt_reg->intr_regs[0].erdp = va_to_pa(&evt_ring->ring_base[evt_ring->index]) | XHCI_ERDP_EHB;

        //如果是命令事件或传输事件则饭或trb和完成码给调用者
        if ((local_trb.cmd_comp_event.trb_type == XHCI_TRB_TYPE_TRANSFER_EVENT ||
             local_trb.cmd_comp_event.trb_type == XHCI_TRB_TYPE_CMD_COMPLETION) &&
            local_trb.cmd_comp_event.cmd_trb_ptr == wait_trb_pa) {
            if (out_event_trb != NULL) {
                *out_event_trb = local_trb;
            }
            return local_trb.cmd_comp_event.comp_code;
        }

        // 其他类型的旁路事件处理与日志分发
        switch (local_trb.cmd_comp_event.trb_type) {
            case XHCI_TRB_TYPE_PORT_STATUS_CHG:
                color_printk(YELLOW, BLACK, "[xHCI Event] Port Status Change! Device plugged/unplugged.\n");
                break;
            case XHCI_TRB_TYPE_BANDWIDTH_REQ:
                color_printk(BLUE, BLACK, "[xHCI Event] Bandwidth Request (Ignored).\n");
                break;
            case XHCI_TRB_TYPE_DOORBELL:
                color_printk(BLUE, BLACK, "[xHCI Event] Virtualization Doorbell (Ignored).\n");
                break;
            case XHCI_TRB_TYPE_HOST_CTRL:
                color_printk(RED, BLACK, "[xHCI Event] Host Controller internal state change!\n");
                break;
            case XHCI_TRB_TYPE_DEVICE_NOTIFY:
                color_printk(BLUE, BLACK, "[xHCI Event] Device Notification received (Ignored).\n");
                break;
            case XHCI_TRB_TYPE_MFINDEX_WRAP:
                // 这个中断每 2 秒发生一次，不用打印，否则会刷屏
                break;
            default:
                color_printk(RED, BLACK, "[xHCI Event] Unknown TRB Type: %d\n", local_trb.cmd_comp_event.trb_type);
                break;
        }
    }
    return XHCI_COMP_TIMEOUT;
}

//初始化环
static inline int32 xhci_ring_init(xhci_ring_t *ring, uint32 align_size) {
    ring->ring_base = kzalloc(align_up(TRB_COUNT * sizeof(xhci_trb_t), align_size));
    ring->index = 0;
    ring->cycle = 1;
}

//响铃
static inline void xhci_ring_doorbell(xhci_hcd_t *xhcd, uint8 db_number, uint32 value) {
    xhcd->db_reg[db_number] = value;
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
            color_printk(RED, BLACK, "[xHCI General Error] TIMEOUT (-1) at PA %#llx: Hardware hang or event lost.\n",
                         trb_pa);
            return 1;

        case XHCI_COMP_TRB_ERROR:
            color_printk(
                RED, BLACK, "[xHCI General Error] TRB Error (5) at PA %#llx: Invalid TRB format (Chain/Type wrong).\n",
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
    xhci_trb_comp_code_e comp_code = xhci_wait_for_event(xhcd, cmd_pa, timeout_us, out_event_trb);

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
    color_printk(RED, BLACK, "\nxHCI: [Command Error] Command rejected at PA %#llx!\n", cmd_pa);

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
int32 xhci_enable_slot(xhci_hcd_t *xhcd, uint8 *out_slot_id) {
    xhci_trb_t evt_trb;
    xhci_trb_t cmd_trb = {0};
    cmd_trb.enable_slot.type = XHCI_TRB_TYPE_ENABLE_SLOT;
    cmd_trb.enable_slot.slot_type = 0;

    // 1. 发送命令到命令环 (Command Ring)
    xhci_trb_comp_code_e comp_code = xhci_execute_command_sync(xhcd, &cmd_trb, 5000000, &evt_trb);

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
int32 xhci_disable_slot(xhci_hcd_t *xhcd, uint8 slot_id) {
    if (xhcd == NULL || slot_id == 0) {
        return -1; // 非法参数或设备本就没分配槽位
    }

    xhci_trb_t cmd_trb = {0};

    // 1. 组装注销命令
    cmd_trb.disable_slot.trb_type = XHCI_TRB_TYPE_DISABLE_SLOT;
    cmd_trb.disable_slot.slot_id  = slot_id;

    // 2. 发射命令并同步等待
    xhci_trb_comp_code_e comp_code = xhci_execute_command_sync(xhcd, &cmd_trb, 2000000,NULL);

    if (comp_code != XHCI_COMP_SUCCESS) {
        color_printk(RED, BLACK, "xHCI: Failed to disable Slot ID %d! Hardware code: %d\n", slot_id, comp_code);
        // 注意：即使硬件注销失败，某些情况下我们依然需要强行清理软件层内存，防止泄漏。
        // 但通常硬件注销失败意味着控制器状态机已经出大问题了。
        return -1;
    }
    return 0;
}


//设置设备地址
int32 xhci_cmd_addr_dev(xhci_hcd_t *xhcd, uint8 slot_id,xhci_input_ctrl_ctx_t *input_ctx) {
    //配置和执行addr_dev命令
    xhci_trb_t cmd_trb = {0};
    cmd_trb.addr_dev.trb_type = XHCI_TRB_TYPE_ADDRESS_DEVICE;
    cmd_trb.addr_dev.input_ctx_ptr = va_to_pa(input_ctx);
    cmd_trb.addr_dev.slot_id = slot_id;
    cmd_trb.addr_dev.bsr = 0;

    xhci_trb_comp_code_e comp_code = xhci_execute_command_sync(xhcd, &cmd_trb, 2000000,NULL);

    if (comp_code != XHCI_COMP_SUCCESS) {
        color_printk(RED, BLACK, "xHCI: Failed to address device! Error: %d\n", comp_code);
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

    xhci_trb_comp_code_e comp_code = xhci_execute_command_sync(xhcd, &cmd_trb, 2000000,NULL);
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
    cmd_trb.stop_ep.ep_id    = ep_id;
    cmd_trb.stop_ep.suspend  = 0; // 坚决不挂起，直接要求主板废弃当前内部缓存的坏状态！

    // 2. 发射命令并同步等待
    // 注意：刹车命令通常非常快，主板会在微秒级响应。
    xhci_trb_comp_code_e comp_code = xhci_execute_command_sync(xhcd, &cmd_trb, 2000000, &evt_trb);

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

    color_printk(YELLOW, BLACK, "xHCI: Emergency Stopped EP %d on Slot %d! HW halted at PA: %#llx\n",
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

    xhci_trb_comp_code_e comp_code = xhci_execute_command_sync(xhcd, &cmd_trb, 2000000,NULL);
    if (comp_code != XHCI_COMP_SUCCESS) {
        color_printk(RED, BLACK, "xHCI: Failed to set_tr_dequeue_pointer! Error: %d\n", comp_code);
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
int32 xhci_cmd_cfg_ep(xhci_hcd_t *xhcd, xhci_input_ctrl_ctx_t *input_ctx, uint8 slot_id, uint8 dc) {
    xhci_trb_t cmd_trb = {0};
    cmd_trb.cfg_ep.trb_type = XHCI_TRB_TYPE_CONFIGURE_EP;
    cmd_trb.cfg_ep.input_ctx_ptr = va_to_pa(input_ctx);
    cmd_trb.cfg_ep.slot_id = slot_id;
    cmd_trb.cfg_ep.dc = dc;

    // 敲响命令环门铃，等待主板评估带宽并分配资源
    xhci_trb_comp_code_e comp_code = xhci_execute_command_sync(xhcd, &cmd_trb, 2000000, NULL);
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
int32 xhci_cmd_eval_ctx(xhci_hcd_t *xhcd, xhci_input_ctrl_ctx_t *input_ctx, uint8 slot_id) {
    xhci_trb_t cmd_trb = {0};
    cmd_trb.eval_ctx.trb_type = XHCI_TRB_TYPE_EVALUATE_CTX;
    cmd_trb.eval_ctx.input_ctx_ptr = va_to_pa(input_ctx);
    cmd_trb.eval_ctx.slot_id = slot_id;

    // 敲响命令环门铃，主板将读取并更新上下文
    xhci_trb_comp_code_e comp_code = xhci_execute_command_sync(xhcd, &cmd_trb, 2000000, NULL);
    if (comp_code != XHCI_COMP_SUCCESS) {
        color_printk(RED, BLACK, "xHCI: Failed to evaluate_context! Error: %d\n", comp_code);
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
    xhci_trb_comp_code_e comp_code = xhci_execute_command_sync(xhcd, &cmd_trb, 2000000, NULL);
    if (comp_code != XHCI_COMP_SUCCESS) {
        color_printk(RED, BLACK, "xHCI: Failed to reset_device! Error: %d\n", comp_code);
        return -1;
    }
    return 0;
}

//=============================================================================================================


//======================================= 传输环命令 ===========================================================


//=============================================================================================================


//xhic扩展能力搜索
uint8 xhci_ecap_find(xhci_hcd_t *xhcd, void **ecap_arr, uint8 cap_id) {
    uint32 offset = xhcd->cap_reg->hccparams1 >> 16;
    uint32 *ecap = (uint32 *) xhcd->cap_reg;
    uint8 count = 0;
    while (offset) {
        ecap = (void *) ecap + (offset << 2);
        if ((*ecap & 0xFF) == cap_id) {
            ecap_arr[count++] = ecap;
        };
        offset = (*ecap >> 8) & 0xFF;
    }
    return count;
}

//xhci设备探测初始化驱动
int xhci_probe(pcie_dev_t *xhci_dev, pcie_id_t *id) {
    xhci_dev->dev.drv_data = kzalloc(sizeof(xhci_hcd_t)); //存放xhci相关信息
    xhci_hcd_t *xhcd = xhci_dev->dev.drv_data;
    xhci_dev->bar[0].vaddr = iomap(xhci_dev->bar[0].paddr, xhci_dev->bar[0].size,PAGE_4K_SIZE,PAGE_ROOT_RW_UC_4K);

    /*初始化xhci寄存器*/
    xhcd->cap_reg = xhci_dev->bar[0].vaddr; //xhci能力寄存器基地址
    xhcd->op_reg = xhci_dev->bar[0].vaddr + xhcd->cap_reg->cap_length; //xhci操作寄存器基地址
    xhcd->rt_reg = xhci_dev->bar[0].vaddr + xhcd->cap_reg->rtsoff; //xhci运行时寄存器基地址
    xhcd->db_reg = xhci_dev->bar[0].vaddr + xhcd->cap_reg->dboff; //xhci门铃寄存器基地址

    /*停止复位xhci*/
    xhcd->op_reg->usbcmd &= ~XHCI_CMD_RS; //停止xhci
    while (!(xhcd->op_reg->usbsts & XHCI_STS_HCH)) asm_pause();
    xhcd->op_reg->usbcmd |= XHCI_CMD_HCRST; //复位xhci
    while (xhcd->op_reg->usbcmd & XHCI_CMD_HCRST) asm_pause();
    while (xhcd->op_reg->usbsts & XHCI_STS_CNR) asm_pause();

    /*计算xhci内存对齐边界*/
    xhcd->align_size = PAGE_4K_SIZE << asm_tzcnt(xhcd->op_reg->pagesize);

    /*设备上下文字节数*/
    xhcd->ctx_size = 32 << ((xhcd->cap_reg->hccparams1 & HCCP1_CSZ) >> 2);

    /*初始化设备上下文*/
    xhcd->max_slots = xhcd->cap_reg->hcsparams1 & 0xff;
    xhcd->dcbaap = kzalloc(align_up((xhcd->max_slots + 1) << 3, xhcd->align_size));
    //分配设备上下文插槽内存,最大插槽数量(插槽从1开始需要+1)*8字节内存
    xhcd->op_reg->dcbaap = va_to_pa(xhcd->dcbaap); //把设备上下文基地址数组表的物理地址写入寄存器
    xhcd->op_reg->config = xhcd->max_slots; //把最大插槽数量写入寄存器

    /*初始化命令环*/
    xhci_ring_init(&xhcd->cmd_ring, xhcd->align_size);
    xhcd->op_reg->crcr = va_to_pa(xhcd->cmd_ring.ring_base) | 1; //命令环物理地址写入crcr寄存器，置位rcs

    /*初始化事件环*/
    xhci_ring_init(&xhcd->event_ring, xhcd->align_size);
    xhci_erst_t *erstba = kmalloc(align_up(sizeof(xhci_erst_t), xhcd->align_size)); //分配单事件环段表内存
    erstba->ring_seg_base = va_to_pa(xhcd->event_ring.ring_base); //段表中写入事件环物理地址
    erstba->ring_seg_size = TRB_COUNT; //事件环最大trb个数
    erstba->reserved = 0;
    xhcd->rt_reg->intr_regs[0].erstsz = 1; //设置单事件环段
    xhcd->rt_reg->intr_regs[0].erstba = va_to_pa(erstba); //事件环段表物理地址写入寄存器
    xhcd->rt_reg->intr_regs[0].erdp = va_to_pa(xhcd->event_ring.ring_base); //事件环物理地址写入寄存器

    /*初始化暂存器缓冲区*/
    uint32 spb_number = (xhcd->cap_reg->hcsparams2 & 0x1f << 21) >> 16 | xhcd->cap_reg->
                        hcsparams2
                        >> 27;
    if (spb_number) {
        uint64 *spb_array = kzalloc(align_up(spb_number << 3, xhcd->align_size)); //分配暂存器缓冲区指针数组
        for (uint32 i = 0; i < spb_number; i++) spb_array[i] = va_to_pa(kzalloc(xhcd->align_size));
        //分配暂存器缓存区
        xhcd->dcbaap[0] = va_to_pa(spb_array); //暂存器缓存去数组指针写入设备上下写文数组0
    }

    xhcd->major_bcd = xhcd->cap_reg->hciversion >> 8; //xhci主版本
    xhcd->minor_bcd = xhcd->cap_reg->hciversion & 0xFF; //xhci次版本
    xhcd->max_ports = xhcd->cap_reg->hcsparams1 >> 24; //xhci最大端口数
    xhcd->max_intrs = xhcd->cap_reg->hcsparams1 >> 8 & 0x7FF; //xhci最大中断数

    /*获取协议支持能力*/
    xhci_ecap_supported_protocol *ecap_spc_arr[16];
    xhcd->spc_count = xhci_ecap_find(xhcd, ecap_spc_arr, 2);
    xhcd->spc = kzalloc(sizeof(xhci_spc_t) * xhcd->spc_count);
    xhcd->port_to_spc = kmalloc(xhcd->max_ports);
    asm_mem_set(xhcd->port_to_spc, 0xFF, xhcd->max_ports);
    for (uint8 i = 0; i < xhcd->spc_count; i++) {
        xhci_spc_t *spc = &xhcd->spc[i];
        xhci_ecap_supported_protocol *spc_ecap = ecap_spc_arr[i];
        spc->major_bcd = spc_ecap->protocol_ver >> 24;
        spc->minor_bcd = spc_ecap->protocol_ver >> 16 & 0xFF;
        *(uint32 *) spc->name = *(uint32 *) spc_ecap->name;
        spc->name[3] = 0;
        spc->proto_defined = spc_ecap->port_info >> 16 & 0x1FF;
        spc->port_first = spc_ecap->port_info & 0xFF;
        spc->port_count = spc_ecap->port_info >> 8 & 0xFF;
        spc->slot_type = spc_ecap->protocol_slot_type & 0xF;
        spc->psi_count = spc_ecap->port_info >> 28 & 0xF;
        if (spc->psi_count) {
            uint32 *psi = kzalloc(sizeof(uint32) * spc->psi_count);
            spc->psi = psi;
            for (uint8 j = 0; j < spc->psi_count; j++) {
                psi[j] = spc_ecap->protocol_speed[j];
            }
        }
        for (uint8 j = spc->port_first - 1; j < spc->port_first - 1 + spc->port_count; j++) {
            xhcd->port_to_spc[j] = i;
        }
    }


    color_printk(
        GREEN,BLACK,
        "XHCI Version:%x.%x MaxSlots:%d MaxIntrs:%d MaxPorts:%d Dev_Ctx_Size:%d AlignSize:%d USBcmd:%#x USBsts:%#x    \n",
        xhcd->major_bcd, xhcd->minor_bcd, xhcd->max_slots,
        xhcd->max_intrs, xhcd->max_ports,
        xhcd->ctx_size, xhcd->align_size, xhcd->op_reg->usbcmd,
        xhcd->op_reg->usbsts);

    for (uint8 i = 0; i < xhcd->spc_count; i++) {
        xhci_spc_t *spc = &xhcd->spc[i];
        color_printk(GREEN,BLACK, "spc%d %s%x.%x port_first:%d port_count:%d psi_count:%d    \n", i, spc->name,
                     spc->major_bcd, spc->minor_bcd, spc->port_first, spc->port_count, spc->psi_count);
    }

    /*启动xhci*/
    xhcd->op_reg->usbcmd |= XHCI_CMD_RS;

    timing();

    usb_dev_scan(xhci_dev);

    color_printk(GREEN,BLACK, "\nUSBcmd:%#x  USBsts:%#x", xhcd->op_reg->usbcmd,
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
