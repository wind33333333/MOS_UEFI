#include "errno.h"
#include "vmm.h"
#include "printk.h"
#include "xhci-hcd.h"
#include "usb-dev.h"
// =========================================================================
// 🚀 xHCI 控制器全局命令发射器
// =========================================================================

/**
 * @brief 发送 Enable Slot 命令，向 xHCI 硬件申请设备插槽资源
 * @param xhcd        xHCI 全局上下文
 * @param port_num    触发插入事件的物理端口号 (1-based)
 * @param out_slot_id 用于接收硬件返回的 Slot ID
 * @return int32      0 表示成功，负数表示失败
 */
int32 xhci_cmd_enable_slot(xhci_hcd_t *xhcd, uint8 port_num, uint8 *out_slot_id) {
    // 🌟 1. 内核级防御：参数校验
    if (!xhcd || !out_slot_id || port_num == 0 || port_num > xhcd->max_ports) {
        return -EINVAL;
    }

    // 🌟 2. O(1) 极速查表：找到该端口所属的协议控制块
    uint8 spc_idx = xhcd->port_to_spc[port_num];
    if (spc_idx == 0xFF) {
        return -ENODEV; // 严重异常：硬件报告了一个未在任何 SPC 中声明的幽灵端口
    }

    uint8 slot_type = xhcd->spc[spc_idx].slot_type;

    // 3. 组装 cmd_trb (严格清零)
    xhci_trb_t cmd_trb = {0};

    // 🌟 4. 纯粹的宏组装：没有冗余的嵌套，清清爽爽
    // Enable Slot 独占：DW3 [20:16] 存放 Slot Type
    cmd_trb.control = TRB_SET_TYPE(TRB_TYPE_ENABLE_SLOT) | (((uint32)slot_type & 0x1F) << 16);

    // 5. 提交命令并等待完成
    xhci_command_t command = {0};
    int32 status = xhci_submit_cmd(xhcd, &cmd_trb, &command);

    *out_slot_id = command.slot_id;
    return status;
}

/**
 * @brief 注销并释放 xHCI 设备槽位 (常用于热拔出或枚举失败的灾难恢复)
 */
int32 xhci_cmd_disable_slot(xhci_hcd_t *xhcd, uint8 slot_id) {
    xhci_trb_t cmd_trb = {0};

    // 🎯 只需要指定类型和目标 Slot ID，参数与状态全 0
    cmd_trb.control = TRB_SET_TYPE(TRB_TYPE_DISABLE_SLOT) | TRB_SET_SLOT_ID(slot_id);

    return xhci_submit_cmd(xhcd, &cmd_trb, NULL);
}

/**
 * @brief 设置设备地址 (Address Device Command)
 */
int32 xhci_cmd_addr_dev(xhci_hcd_t *xhcd, uint8 slot_id, xhci_input_ctrl_ctx_t *in_ctx) {
    xhci_trb_t cmd_trb = {0};

    // 🎯 直接将 64 位物理地址赋予 parameter，抛弃高低 32 位的痛苦切分
    cmd_trb.parameter = va_to_pa(in_ctx);

    // 🛡️ BSR(Block Set Address Request) 默认不设置 (即0)，让硬件去发包
    cmd_trb.control = TRB_SET_TYPE(TRB_TYPE_ADDRESS_DEVICE) | TRB_SET_SLOT_ID(slot_id);

    return xhci_submit_cmd(xhcd, &cmd_trb, NULL);
}

/**
 * @brief 底层指令：发送 Configure Endpoint 命令
 */
int32 xhci_cmd_cfg_ep(xhci_hcd_t *xhcd, uint8 slot_id, xhci_input_ctrl_ctx_t *in_ctx, uint8 dc) {
    xhci_trb_t cmd_trb = {0};

    cmd_trb.parameter = va_to_pa(in_ctx);

    // 组装基础控制字
    uint32 ctrl = TRB_SET_TYPE(TRB_TYPE_CONFIGURE_EP) | TRB_SET_SLOT_ID(slot_id);

    // 🛡️ 架构师武器：一键清空标志 (Deconfigure)
    if (dc) {
        ctrl |= TRB_SET_DC;
    }
    cmd_trb.control = ctrl;

    return xhci_submit_cmd(xhcd, &cmd_trb, NULL);
}

/**
 * @brief 底层指令：发送 Evaluate Context 命令 (常用于更新 EP0 的最大包长)
 */
int32 xhci_cmd_eval_ctx(xhci_hcd_t *xhcd, uint8 slot_id, xhci_input_ctrl_ctx_t *in_ctx) {
    xhci_trb_t cmd_trb = {0};

    cmd_trb.parameter = va_to_pa(in_ctx);
    cmd_trb.control   = TRB_SET_TYPE(TRB_TYPE_EVALUATE_CTX) | TRB_SET_SLOT_ID(slot_id);

    return xhci_submit_cmd(xhcd, &cmd_trb, NULL);
}

/**
 * @brief 复位端点 (解 STALL 第一步：清除硬件 Halt 状态)
 */
int32 xhci_cmd_reset_ep(xhci_hcd_t *xhcd, uint8 slot_id, uint8 ep_dci) {
    xhci_trb_t cmd_trb = {0};

    // TSP (Target State Preserved) 常规下填 0
    cmd_trb.control = TRB_SET_TYPE(TRB_TYPE_RESET_EP) |
                      TRB_SET_SLOT_ID(slot_id) |
                      TRB_SET_EP_ID(ep_dci);

    return xhci_submit_cmd(xhcd, &cmd_trb, NULL);
}

/**
 * @brief 紧急刹车：停止指定设备的指定端点 (用于超时或STALL抢救)
 */
int32 xhci_cmd_stop_ep(xhci_hcd_t *xhcd, uint8 slot_id, uint8 ep_dci) {
    if (xhcd == NULL || slot_id == 0 || ep_dci == 0 || ep_dci > 31) {
        return -EINVAL;
    }

    xhci_trb_t cmd_trb = {0};

    // Suspend 默认不填(0)，要求主板彻底停下并抛弃内部缓存
    cmd_trb.control = TRB_SET_TYPE(TRB_TYPE_STOP_EP) |
                      TRB_SET_SLOT_ID(slot_id) |
                      TRB_SET_EP_ID(ep_dci);

    return xhci_submit_cmd(xhcd, &cmd_trb, NULL);
}

/**
 * @brief 发送 Set TR Dequeue Pointer Command，强制移动端点底层的出队指针 (解 STALL 核心)
 */
int32 xhci_cmd_set_tr_deq_ptr(xhci_hcd_t *xhcd, uint8 slot_id, uint8 ep_dci, xhci_submit_ring_t *transfer_ring) {
    // 【核心算力】：找到 Transfer Ring 中软件当前准备写入的、下一个干净槽位的物理地址
    uint64 next_clean_trb_pa = va_to_pa(&transfer_ring->ring_base[transfer_ring->enq_idx]);
    uint8 next_cycle_state = transfer_ring->cycle;

    xhci_trb_t cmd_trb = {0};

    // 🌟 极其致命的一步：指针的物理地址最低位，必须烙印上预判的 DCS (Dequeue Cycle State)
    cmd_trb.parameter = next_clean_trb_pa | TRB_SET_DEQ_CYCLE_STATE(next_cycle_state);

    // 如果是 UAS 大容量存储支持 Stream，需在此处配置 Stream ID，BOT 中填 0 即可
    // cmd_trb.status = TRB_SET_STREAM_ID(0);

    cmd_trb.control = TRB_SET_TYPE(TRB_TYPE_SET_TR_DEQUEUE) |
                      TRB_SET_SLOT_ID(slot_id) |
                      TRB_SET_EP_ID(ep_dci);

    return xhci_submit_cmd(xhcd, &cmd_trb, NULL);
}

/**
 * @brief 底层指令：发送 Reset Device 命令 (灾难恢复终极手段)
 */
int32 xhci_cmd_reset_dev(xhci_hcd_t *xhcd, uint8 slot_id) {
    xhci_trb_t cmd_trb = {0};

    cmd_trb.control = TRB_SET_TYPE(TRB_TYPE_RESET_DEVICE) | TRB_SET_SLOT_ID(slot_id);

    return xhci_submit_cmd(xhcd, &cmd_trb, NULL);
}

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
