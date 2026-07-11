#include "xhci-ctx.h"
#include "xhci-cmd.h"
#include "xhci-hcd.h"
#include "xhci-ring.h"
#include "errno.h"
#include "../../../mm/include/slub.h"
#include "usb-core.h"


//============================================== 上下文操作函数 ===========================================================

//获取 Input Context 数组中的指定条目
static inline void *xhci_get_in_ctx_entry(xhci_input_ctrl_ctx_t *input_ctx, uint32 dci,uint8 ctx_size) {
    return (uint8 *)input_ctx + ctx_size * (dci + 1);
}

// 获取 Output Context (Device Context) 数组中的指定条目
static inline void *xhci_get_out_ctx_entry(void *out_ctx, uint32 dci, uint8 ctx_size) {
    return (uint8 *)out_ctx + ctx_size * dci;
}

/**
 * @brief 开启一个事务：将硬件真实状态同步到input
 */
static void xhci_ctx_in_sync(usb_dev_t *udev) {
    // 1. 物理清零管控中心 (Input Control Context，占 1 个 ctx_size)
    // 彻底消灭上一次下发命令残留的 Add/Drop 幽灵标志位
    xhci_input_ctrl_ctx_t *input_ctrl_ctx = udev->in_ctx;
    input_ctrl_ctx->add_context_flags = 0;
    input_ctrl_ctx->drop_context_flags = 0;

    // 2. 完美的移花接木：将主板维护的 Device Context 拷贝到 Input Context 的数据区
    // 注意偏移量：Input Context 从第 1 个条目开始，才是 Slot 和 EP
    uint8 ctx_size = udev->xhcd->ctx_size;
    void *in_ctx = xhci_get_in_ctx_entry(input_ctrl_ctx,0,ctx_size);

    // 3. 拷贝 32 个 Context (1 个 Slot + 31 个 EP)
    asm_mem_cpy(udev->out_ctx, in_ctx, ctx_size * 32);
}


// ============================================================================
// 📦 上下文状态机管理 (纯宏装配版)
// ============================================================================

/**
 * @brief [纯内存] 全量同步 Slot 上下文
 * @note 无论是创世(Address)、基建(CFG_EP)还是微调(EVAL_CTX)，统一调用此函数。
 * 硬件会根据下发的命令类型，自动提取它关心的字段，忽略不关心的字段。
 * @param udev 目标 USB 设备 (真理之源)
 */
static void xhci_ctx_slot_update(usb_dev_t *udev) {
    xhci_input_ctrl_ctx_t *input_ctrl_ctx = udev->in_ctx;

    // 1. 强制打上 Slot 图纸被涂改的标记 (Bit 0 特权)
    // 既然是全量同步，Slot 必定被修改，直接置位
    input_ctrl_ctx->add_context_flags |= INPUT_CTRL_ADD_SLOT;

    // 2. 拿到 Slot Context 的图纸指针
    xhci_slot_ctx_t *slot = xhci_get_in_ctx_entry(input_ctrl_ctx, 0, udev->xhcd->ctx_size);

    // 🌟 核心算法：算出投影位图并更新 context_entries, 先删后建
    uint32 projected_map = (udev->active_ep_map & ~input_ctrl_ctx->drop_context_flags) | input_ctrl_ctx->add_context_flags;
    // 使用前导零计算最高被激活的端点 DCI
    uint32 entries = 31 - asm_lzcnt32(projected_map);

    // ===================================================================
    // 🛡️ DW0：基础物理、路由属性与端点极值
    // ===================================================================
    uint32 dw0 = SLOT_CTX_DW0_ROUTE(udev->route_string) |
                 SLOT_CTX_DW0_SPEED(udev->psiv) |
                 SLOT_CTX_DW0_CTX_ENTRIES(entries);

    if (udev->hub_mtt) dw0 |= SLOT_CTX_DW0_MTT;
    if (udev->is_hub)  dw0 |= SLOT_CTX_DW0_HUB;

    slot->dw0 = dw0;

    // ===================================================================
    // 🛡️ DW1：集线器全局拓扑属性 (Configure Endpoint 基建阶段核心)
    // ===================================================================
    slot->dw1 = SLOT_CTX_DW1_MAX_EXIT_LAT(udev->max_exit_latency) |
                SLOT_CTX_DW1_ROOT_PORT(udev->root_hub_port_num) |
                SLOT_CTX_DW1_NUM_PORTS(udev->hub_num_ports);

    // ===================================================================
    // 🛡️ DW2：行政路由与电源管理 (Evaluate Context 微调阶段核心)
    // ===================================================================
    slot->dw2 = SLOT_CTX_DW2_TT_HUB_SLOT(udev->tt_hub_slot_id) |
                SLOT_CTX_DW2_TT_PORT_NUM(udev->tt_port_num) |
                SLOT_CTX_DW2_TT_THINK_TIME(udev->hub_ttt) |
                SLOT_CTX_DW2_INTR_TARGET(udev->interrupter_target);

    // DW3 通常由硬件回写 (如设备地址)，或者在初始化时置 0
    slot->dw3 = 0;
}

/**
 * @brief 将 USB 描述符中的 bInterval 换算为 xHCI 硬件要求的指数值
 *
 * @param interval 设备端点描述符中读取的原始值
 * @param speed     设备当前的运行速度
 * @return uint32   符合 xHCI Endpoint Context Interval 字段规范的值
 */
static inline uint32 xhci_calc_interval(uint8 interval, uint8 speed) {
    if (interval == 0) return 0;
    if (speed >= USB_SPEED_HIGH) {
        // 高速/超高速：本身就是指数格式
        return interval - 1;
    } else {
        // 低速/全速：线性毫秒格式，转为以 125us 为底的微帧指数
        return 34 - asm_lzcnt32(interval);
    }
}


/**
 * @brief [纯内存] 增加并配置一个端点上下文
 * @param udev 目标 USB 设备
 * @param ep   端点结构体
 */
static void xhci_ctx_ep_add(usb_dev_t *udev, usb_ep_t *ep) {
    xhci_input_ctrl_ctx_t *input_ctrl_ctx = udev->in_ctx;
    uint8 dci = ep->ep_dci;

    // 1. 在 Input Control Context 中点亮新增该端点的标志
    input_ctrl_ctx->add_context_flags |= INPUT_CTRL_ADD_EP(dci);

    // 2. 获取该端点在内存中的独立 Context 块
    xhci_ep_ctx_t *ep_ctx = xhci_get_in_ctx_entry(input_ctrl_ctx, dci, udev->xhcd->ctx_size);


    // ===================================================================
    // 🛡️ DW0：突发传输、数据流、轮询间隔与高位负载
    // ===================================================================
    uint32 dw0 = EP_CTX_DW0_MULT(ep->mult) |
                 EP_CTX_DW0_MAX_PSTREAMS(ep->enable_streams_exp) |
                 EP_CTX_DW0_INTERVAL(xhci_calc_interval(ep->interval, udev->port_speed)) |
                 EP_CTX_DW0_MAX_ESIT_HI(ep->max_esit_payload >> 16);

    if (ep->lsa) dw0 |= EP_CTX_DW0_LSA;
    ep_ctx->dw0 = dw0;

    // ===================================================================
    // 🛡️ DW1：错误容忍、端点类型与最大包长
    // ===================================================================
    uint32 dw1 = EP_CTX_DW1_CERR(ep->cerr) |
                 EP_CTX_DW1_EP_TYPE(ep->ep_type) |
                 EP_CTX_DW1_MAX_BURST(ep->max_burst) |
                 EP_CTX_DW1_MAX_PACKET(ep->max_packet_size);

    if (ep->hid) dw1 |= EP_CTX_DW1_HID;
    ep_ctx->dw1 = dw1;

    // ===================================================================
    // 🛡️ DW2 & DW3：出队指针与初始 Cycle 状态 (合并在 64 位赋值中)
    // ===================================================================
    ep_ctx->tr_dequeue_ptr = ep->trq_phys_addr;

    // ===================================================================
    // 🛡️ DW4：平均 TRB 长度与低位负载
    // ===================================================================
    ep_ctx->dw4 = EP_CTX_DW4_AVG_TRB_LEN(ep->average_trb_length) |
                  EP_CTX_DW4_MAX_ESIT_LO(ep->max_esit_payload);
}


//删除一个端点上下文
static inline void xhci_ctx_ep_drop(usb_dev_t *udev, usb_ep_t *ep) {
    udev->in_ctx->drop_context_flags |= (1 << ep->ep_dci);
}


static inline void xhci_up_ep_map(usb_dev_t *udev) {
    udev->active_ep_map &= ~udev->in_ctx->drop_context_flags;
    udev->active_ep_map |= udev->in_ctx->add_context_flags;
}

/**
 * @note 向物理总线发送 SET_ADDRESS 包，使设备进入 Addressed 稳态。
 */
static inline int32 xhci_ctx_addr_dev(usb_dev_t *udev) {
    xhci_ctx_in_sync(udev);
    xhci_ctx_ep_add(udev, udev->eps[1]);
    xhci_ctx_slot_update(udev);
    int32 err = xhci_cmd_addr_dev(udev->xhcd, udev->slot_id, udev->in_ctx);
    if (err < 0) return err;
    xhci_up_ep_map (udev);
}

//配置slot context
int32 xhci_ctx_slot_cfg(usb_dev_t *udev) {
    xhci_ctx_in_sync(udev);
    xhci_ctx_slot_update(udev);
    int32 err = xhci_cmd_cfg_ep(udev->xhcd, udev->slot_id, udev->in_ctx, 0);
    if (err < 0) return err;
    xhci_up_ep_map (udev);
}

//微调slot ep0 context
int32 xhci_ctx_slot_ep0_eval(usb_dev_t *udev) {
    xhci_ctx_in_sync(udev);
    xhci_ctx_ep_add(udev,udev->eps[1]);
    xhci_ctx_slot_update(udev);
    int32 err = xhci_cmd_eval_ctx(udev->xhcd, udev->slot_id, udev->in_ctx);
    if (err < 0) return err;
    xhci_up_ep_map (udev);
}


//批量增删改业务端点
int32 xhci_ctx_eps_cfg(usb_if_alt_t *drop_uif_alt,usb_if_alt_t *add_uif_alt) {

    usb_dev_t *udev = NULL;
    uint8 drop_num_ep = 0;
    uint8 add_num_ep = 0;

    // 1. 提取设备指针并进行防御性检查
    if (drop_uif_alt) {
        udev = drop_uif_alt->uif->udev;
        drop_num_ep = drop_uif_alt->if_desc->num_endpoints;
    }

    if (add_uif_alt) {
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
    xhci_ctx_in_sync(udev);

    // 5. Drop 旧端点
    for (uint8 i = 0; i < drop_num_ep; i++) {
        xhci_ctx_ep_drop(udev,&drop_uif_alt->eps[i]);
    }

    // 6. Add 新端点
    for (uint8 i = 0; i < add_num_ep; i++) {
        xhci_ctx_ep_add(udev,&add_uif_alt->eps[i]);
    }

    // 7. 更新 Slot Context（重新计算 context_entries）
    xhci_ctx_slot_update(udev);

    int32 err = xhci_cmd_cfg_ep(udev->xhcd, udev->slot_id, udev->in_ctx, 0);
    if (err < 0) return err;
    xhci_up_ep_map (udev);
}

//批量清空业务端点
int32 xhci_ctx_deconfigure_all(usb_dev_t *udev ) {
    int32 err =xhci_cmd_cfg_ep(udev->xhcd, udev->slot_id, udev->in_ctx, 1);
    if (err < 0) return err;
    udev->active_ep_map = (1 << 0) | (1 << 1); // 仅留 Slot 和 EP0
}


/**
 * @brief 阶段 1：分配设备上下文，配置 Slot 和 EP0，并赋予物理地址 (支持 BSR 两阶段探测)
 * @param udev USB 设备对象
 * @return int32 0 表示成功，-1 表示失败
 */
int32 xhci_enable_slot_ep0(usb_dev_t *udev) {
    xhci_hcd_t *xhcd = udev->xhcd;

    // 1. 启用插槽 (Enable Slot)
    int32 err = xhci_cmd_enable_slot(xhcd, udev->root_hub_port_num, &udev->slot_id);
    if (err < 0) return err;

    // 2. 分配设备上下文 (Output Context)
    uint8 ctx_size = xhcd->ctx_size;
    udev->out_ctx = kzalloc_dma(XHCI_DEVICE_CONTEXT_COUNT * ctx_size);
    xhcd->dcbaap[udev->slot_id] = va_to_pa(udev->out_ctx);
    xhcd->udevs[udev->slot_id] = udev;

    // 3. 分配输入上下文 (Input Context)
    udev->in_ctx = kzalloc_dma(XHCI_INPUT_CONTEXT_COUNT * ctx_size);

    // 4. 挂载到 O(1) 路由表
    usb_ep_t *uep0 = kzalloc(sizeof(usb_ep_t));
    udev->eps[1] = uep0;

    // --- 计算初始 Max Packet Size (MPS0) ---
    // 1. USB 3.0 (SuperSpeed) 必定是 512。
    // 2. USB 2.0 (High Speed) 协议规定必定是 64。
    // 3. USB 1.1 (Full/Low Speed) 可能是 8/16/32/64，为了绝对安全，先盲猜最小包长 8
    uint32 mps = (udev->port_speed >= USB_SPEED_SUPER_5G) ? 512 :
                 (udev->port_speed == USB_SPEED_HIGH)  ? 64  : 8;

    // 5. 填充端点 0 数据结构
    uep0->ep_dci = 1;
    uep0->cerr = 3;
    uep0->ep_type = 4; // Control Endpoint
    uep0->max_packet_size = mps;
    uep0->average_trb_length = mps;
    uep0->max_streams_exp = 0;
    uep0->enable_streams_exp = 0;
    uep0->ring_max_trbs = 32;
    xhci_alloc_ep_ring(uep0);

    //发送 SET_ADDRESS！
    xhci_ctx_addr_dev(udev);
    return 0;
}



//===================================================================================================================