#include "usb-hub.h"
#include "errno.h"
#include "printk.h"
#include "slub.h"

// ==========================================
// 📦 Hub 专属类描述符获取 (Class Descriptor)
// ==========================================

/**
 * 获取普通 hub描述符
 */
static inline int32 usb_hub20_get_desc(usb_dev_t *udev, void *buf) {
    return usb_control_msg(udev, buf,
                           USB_DIR_IN, USB_REQ_TYPE_CLASS, USB_RECIP_DEVICE,
                           USB_REQ_GET_DESCRIPTOR, (USB_DESC_TYPE_HUB20 << 8) | 0, 0, sizeof(usb_hub20_desc_t));
}

// 获取 ss hub描述符
static inline int32 usb_hub30_get_desc(usb_dev_t *udev, void *buf) {
    return usb_control_msg(udev, buf,
                           USB_DIR_IN, USB_REQ_TYPE_CLASS, USB_RECIP_DEVICE,
                           USB_REQ_GET_DESCRIPTOR, (USB_DESC_TYPE_HUB30 << 8) | 0, 0, sizeof(usb_hub30_desc_t));
}


// ==========================================
// 🔍 Hub 端口状态读取 (有数据阶段)
// ==========================================


/**
 * @brief 获取 Hub 端口的 4 字节状态数据 (TheresaOS 零拷贝高性能版)
 */
static int32 usb_hub_port_get_status(usb_dev_t *udev, uint8 port_id, uint32 *port_status) {
    // 1. 直接拉出宿主 Hub 身上那块预先埋好的常驻 DMA 盾牌
    usb_hub_t *hub = (usb_hub_t *)udev->drv_data;
    uint32 *port_sts = hub->port_status;

    // 2. 干净的纯血控制传输，直接让 xHCI 硬件写入这个常驻缓冲区
    int32 ret = usb_control_msg(udev, port_sts,
                                USB_DIR_IN, USB_REQ_TYPE_CLASS, USB_RECIP_OTHER,
                                USB_REQ_GET_STATUS, 0, port_id, 4);
    *port_status = *port_sts;

    // 🌟 零内存释放，零碎片产生，安全退出！
    return ret;
}

// =========================================================================
// ⚓ 2. Layer 1: 通用控制原语底层 (负责收拢 9 参数，支持 wIndex 高位打包)
// =========================================================================

/**
 * @brief 底层通用的特征设置原语
 * @param ext_arg 扩展参数。如果是 3.0 链路控制或 2.0 测试模式，该参数会自动打包进 wIndex 的高 8 位。
 */
static inline int32 usb_hub_set_port_feature(usb_dev_t *udev, uint8 port_id, usb_port_feature_e feature, uint8 ext_arg) {
    uint16 index = ((uint16)ext_arg << 8) | port_id;
    return usb_control_msg(udev, NULL,
                           USB_DIR_OUT, USB_REQ_TYPE_CLASS, USB_RECIP_OTHER,
                           USB_REQ_SET_FEATURE, feature, index, 0);
}

/**
 * @brief 底层通用的特征擦除原语
 */
static inline int32 usb_hub_clear_port_feature(usb_dev_t *udev, uint8 port_id, usb_port_feature_e feature, uint8 ext_arg) {
    uint16 index = ((uint16)ext_arg << 8) | port_id;
    return usb_control_msg(udev, NULL,
                           USB_DIR_OUT, USB_REQ_TYPE_CLASS, USB_RECIP_OTHER,
                           USB_REQ_CLEAR_FEATURE, feature, index, 0);
}


// =========================================================================
// 🔌 3. Layer 2: 面向业务的语义化 Inline 动作外挂 (2.0/3.0 通用基础控制)
// =========================================================================

/**
 * @brief 给指定物理端口闭合继电器通电 (VBUS 5V 上电)
 */
static inline int32 usb_hub_port_up_power(usb_dev_t *udev, uint8 port_id) {
    return usb_hub_set_port_feature(udev, port_id, USB_PORT_FEAT_POWER, 0);
}

/**
 * @brief 给指定物理端口断电
 */
static inline int32 usb_hub_port_down_power(usb_dev_t *udev, uint8 port_id) {
    return usb_hub_clear_port_feature(udev, port_id, USB_PORT_FEAT_POWER, 0);
}

/**
 * @brief 强行将激活的端口下线 (逻辑强拔)
 */
static inline int32 usb_hub_port_disable(usb_dev_t *udev, uint8 port_id) {
    return usb_hub_clear_port_feature(udev, port_id, USB_PORT_FEAT_ENABLE, 0);
}

/**
 * @brief 发射标准热复位信号 (2.0物理拉低线路 / 3.0超高速 Hot Reset)
 * 🌟 这是发现新插入设备后，触发硬件使能的绝对第一步！
 */
static inline int32 usb_hub_port_reset(usb_dev_t *udev, uint8 port_id) {
    return usb_hub_set_port_feature(udev, port_id, USB_PORT_FEAT_RESET, 0);
}


// =========================================================================
// 🐢 4. Layer 2: USB 2.0 纯血独占特征动作
// =========================================================================

/**
 * @brief 强迫 2.0 端口进入挂起 (Suspend) 状态
 */
static inline int32 usb_hub_port_set_suspend20(usb_dev_t *udev, uint8 port_id) {
    return usb_hub_set_port_feature(udev, port_id, USB_PORT_FEAT_SUSPEND, 0);
}

/**
 * @brief 向 2.0 端口发射 Resume 信号将其从挂起中唤醒
 */
static inline int32 usb_hub_port_clear_suspend20(usb_dev_t *udev, uint8 port_id) {
    return usb_hub_clear_port_feature(udev, port_id, USB_PORT_FEAT_SUSPEND, 0);
}

/**
 * @brief 强制 2.0 端口执行特定电气合规性测试 (Compliance Test)
 * @param test_selector 测试类型选择子 (如 J_STATE, K_STATE, SE0_NAK 等)
 */
static inline int32 usb_hub_port_set_test_mode(usb_dev_t *udev, uint8 port_id, uint8 test_selector) {
    return usb_hub_set_port_feature(udev, port_id, USB_PORT_FEAT_TEST, test_selector);
}

/**
 * @brief 控制 2.0 工业级扩展坞上的端口指示灯 (LED) 颜色或闪烁模式
 * @param indicator_selector 灯光状态选择子 (如 1=琥珀色, 2=绿色, 0=关闭)
 */
static inline int32 usb_hub_port_set_indicator(usb_dev_t *udev, uint8 port_id, uint8 indicator_selector) {
    return usb_hub_set_port_feature(udev, port_id, USB_PORT_FEAT_INDICATOR, indicator_selector);
}


// =========================================================================
// 🚀 5. Layer 2: USB 3.0+ 超高速物理链路硬核控制动作
// =========================================================================

/**
 * @brief 🔥 [大锤物理强刷] 发射 Warm Reset 信号！
 * 🌟 当超高速链路不幸跌落至 SS.Inactive 硬件死锁状态时，这是唯一的解药！
 */
static inline int32 usb_hub_port_bh_reset(usb_dev_t *udev, uint8 port_id) {
    return usb_hub_set_port_feature(udev, port_id, USB_PORT_FEAT_BH_PORT_RESET, 0);
}

/**
 * @brief USB 3.0+ SetFeature(PORT_LINK_STATE) 专属原始非移位参数
 * ⚠️ 专属控制层，专门用来当做设置函数的参数，严禁与读取位图混用！
 */
typedef enum : uint8 {
    USB3_SET_LINK_U0            = 0,  // [强制唤醒] 命令链路切回 U0 满血工作状态 (数据对流)
    USB3_SET_LINK_U1            = 1,  // [自动省电] 强制切入轻度时钟门控待机 (微秒级恢复)
    USB3_SET_LINK_U2            = 2,  // [自动省电] 强制切入深度模拟电路待机 (百微秒级恢复)
    USB3_SET_LINK_U3            = 3,  // [选择性挂起] 强制进入深度休眠 (Selective Suspend)

    // --- 以下 3 个是上一轮漏掉的硬核物理层控制命令 ---

    USB3_SET_LINK_SS_DISABLED   = 4,  // [逻辑断开3.0] 强行关闭超高速物理收发器。
                                      // 🌟 奇效：此命令会逼迫该接口上的双模设备降级去跑 2.0 线路！

    USB3_SET_LINK_RX_DETECT     = 5,  // [强制重连] 强行解散当前的链路训练状态机，
                                      // 让端口退回到最原始的发射物理脉冲、重新探测外设阻抗的阶段。

    USB3_SET_LINK_COMPLIANCE    = 15  // [合规测试] 强迫端口进入电气特性测试模式（工厂用仪器测眼图专属）
} usb3_set_link_state_e;

/**
 * @brief 强制命令 3.0 物理层跳转到指定的链路状态 (代替传统的 Selector 2)
 * @param target_link_state 目标链路状态 (如 USB3_PORT_LINK_U3 代表命令设备深度休眠)
 */
static inline int32 usb_hub_port_set_link_state(usb_dev_t *udev, uint8 port_id, usb3_set_link_state_e target_link_state) {
    return usb_hub_set_port_feature(udev, port_id, USB_PORT_FEAT_LINK_STATE, target_link_state);
}

/**
 * @brief 设置 3.0 端口在空闲多久后可以自动切换到 U1 低功耗待机状态
 * @param timeout_val 超时系数 (单位: 1微秒，0xFF代表永不限制)
 */
static inline int32 usb_hub_port_set_u1_timeout(usb_dev_t *udev, uint8 port_id, uint8 timeout_val) {
    return usb_hub_set_port_feature(udev, port_id, USB_PORT_FEAT_U1_TIMEOUT, timeout_val);
}

/**
 * @brief 设置 3.0 端口在空闲多久后可以自动切换到 U2 深度低功耗状态
 * @param timeout_val 超时系数 (单位: 256微秒)
 */
static inline int32 usb_hub_port_set_u2_timeout(usb_dev_t *udev, uint8 port_id, uint8 timeout_val) {
    return usb_hub_set_port_feature(udev, port_id, USB_PORT_FEAT_U2_TIMEOUT, timeout_val);
}

/**
 * @brief 配置超高速远程唤醒过滤掩码
 * @param wake_mask 允许唤醒的条件位图 (如开启远程插拔唤醒、过流唤醒等)
 */
static inline int32 usb_hub_port_set_remote_wake_mask(usb_dev_t *udev, uint8 port_id, uint8 wake_mask) {
    return usb_hub_set_port_feature(udev, port_id, USB_PORT_FEAT_REMOTE_WAKE_MASK, wake_mask);
}

/**
 * @brief 强制超高速端口无脑接受主机的硬件电源管理 (PM) 握手协商
 */
static inline int32 usb_hub_port_force_linkpm_accept(usb_dev_t *udev, uint8 port_id) {
    return usb_hub_set_port_feature(udev, port_id, USB_PORT_FEAT_FORCE_LINKPM_ACCEPT, 0);
}


// =========================================================================
// 🧹 6. Layer 2: 2.0 / 3.0 统一高位事件变更擦除 (Ack Interrupt 专属，杜绝中断风暴)
// =========================================================================

/**
 * @brief 确认并签收 [物理插拔] 事件 (解除 Bit 16 报警)
 */
static inline int32 usb_hub_port_clear_connection_change(usb_dev_t *udev, uint8 port_id) {
    return usb_hub_clear_port_feature(udev, port_id, USB_PORT_FEAT_C_CONNECTION, 0);
}

/**
 * @brief 确认并签收 [端口硬件级强行禁用] 事件 (解除 Bit 17 报警)
 */
static inline int32 usb_hub_port_clear_enable_change(usb_dev_t *udev, uint8 port_id) {
    return usb_hub_clear_port_feature(udev, port_id, USB_PORT_FEAT_C_ENABLE, 0);
}

/**
 * @brief 确认并签收 [2.0挂起苏醒完成] 事件 (解除 Bit 18 报警)
 */
static inline int32 usb_hub_port_clear_suspend_change(usb_dev_t *udev, uint8 port_id) {
    return usb_hub_clear_port_feature(udev, port_id, USB_PORT_FEAT_C_SUSPEND, 0);
}

/**
 * @brief 确认并签收 [端口短路过流] 事件 (解除 Bit 19 报警)
 */
static inline int32 usb_hub_port_clear_over_current_change(usb_dev_t *udev, uint8 port_id) {
    return usb_hub_clear_port_feature(udev, port_id, USB_PORT_FEAT_C_OVER_CURRENT, 0);
}

/**
 * @brief 确认并签收 [标准复位完成] 事件 (解除 Bit 20 报警)
 * 🌟 在单任务轮询状态机中，刷完这个原语，即代表端口彻底降服，可以直接下发 SetAddress 分配拓扑地址。
 */
static inline int32 usb_hub_port_clear_reset_change(usb_dev_t *udev, uint8 port_id) {
    return usb_hub_clear_port_feature(udev, port_id, USB_PORT_FEAT_C_RESET, 0);
}


// =========================================================================
// 💥 7. Layer 2: USB 3.0+ 超高速专用高位事件变更擦除 (Ack 3.x Only)
// =========================================================================

/**
 * @brief 确认并签收 [超高速物理链路状态突变] 事件 (解除 Bit 25 报警)
 * 🌟 比如链路成功从 Rx.Detect 晋升为完全体 U0 状态时触发的喜报，必须发此指令确认签收。
 */
static inline int32 usb_hub_port_clear_link_state_change(usb_dev_t *udev, uint8 port_id) {
    return usb_hub_clear_port_feature(udev, port_id, USB_PORT_FEAT_C_PORT_LINK_STATE, 0);
}

/**
 * @brief 确认并签收 [物理层阻抗/配置异常严重错误] 事件 (解除 Bit 26 报警)
 */
static inline int32 usb_hub_port_clear_config_error_change(usb_dev_t *udev, uint8 port_id) {
    return usb_hub_clear_port_feature(udev, port_id, USB_PORT_FEAT_C_PORT_CONFIG_ERROR, 0);
}

/**
 * @brief 确认并签收 [大锤 Warm Reset 复位完成] 事件 (解除 Bit 29 报警)
 * 🌟 当你用大锤把死锁的链路救活后，硬件会抛出此事件，必须立刻通过此函数“确认签收”，否则总线永无宁日。
 */
static inline int32 usb_hub_port_clear_bh_reset_change(usb_dev_t *udev, uint8 port_id) {
    return usb_hub_clear_port_feature(udev, port_id, USB_PORT_FEAT_C_BH_PORT_RESET, 0);
}


//获取 Device Context 数组中的指定条目
static inline void *usb_get_out_ctx_entry(void *out_ctx, uint32 dci, uint8 ctx_size) {
    return (uint8 *) out_ctx + ctx_size * dci;
}


//hub驱动
int32 usb_hub_probe(usb_if_t *uif, usb_id_t *uid) {
    usb_dev_t *udev = uif->udev;
    usb_hub_t *hub = kzalloc(sizeof(usb_hub_t));
    hub->uif = uif;
    hub->int_urb = usb_alloc_urb();
    hub->port_status = kzalloc_dma(sizeof(uint32));
    hub->port_bitmap_status = kzalloc_dma(32);
    udev->is_hub = TRUE;
    udev->drv_data = hub;

    usb_if_alt_t *if_alt = NULL;

    if (udev->port_speed > USB_SPEED_HIGH) {
        color_printk(GREEN,BLACK, "hub3.0!!! speed:%d psiv:%d port:%d  \n", udev->port_speed, udev->psiv,
                     udev->root_port_num);

        if_alt = usb_find_alt_if(uif, 0x9,USB_MATCH_ANY,USB_MATCH_ANY);
        if (if_alt == NULL) return -ENODEV;

        // ==========================================
        // 🚀 USB 3.0 (SuperSpeed) Hub 处理逻辑
        // 描述符类型：0x2A，长度永远固定为 12 字节！
        // ==========================================
        usb_hub30_desc_t *hub30_desc = kzalloc_dma(sizeof(usb_hub30_desc_t));

        // 获取描述符
        int32 error = usb_hub30_get_desc(udev, hub30_desc);
        if (error < 0) return error;
        hub->hub30_desc = hub30_desc;

        //解析hub描述符
        udev->hub_num_ports = hub30_desc->num_ports;
        udev->hub_mtt = 0;
        udev->hub_ttt = 0;
        hub->is_individual_pwr = (hub30_desc->hub_characteristics & 0x03) == 0x01;
        hub->is_individual_ocp = ((hub30_desc->hub_characteristics >> 3) & 0x03) == 0x01;
        hub->power_delay_ms = hub30_desc->power_on_to_power_good << 1;

        // 分配 hub 端口内存并解析哪些是不可拆卸端口
        hub->ports = kzalloc((udev->hub_num_ports + 1) * sizeof(hub_port_t));
        for (uint8 i = 1; i <= udev->hub_num_ports; i++) {
            hub->ports[i].port_id = i;
            uint16 removable_bitmap = hub30_desc->device_removable;
            hub->ports[i].is_removable = !((removable_bitmap >> i) & 1);
        }


    } else {
        color_printk(GREEN,BLACK, "hub2.0!!! speed:%d psiv:%d port:%d  \n", udev->port_speed, udev->psiv,
                     udev->root_port_num);
        // ==========================================
        // 🐢 USB 2.0/1.1 (High/Full/Low Speed) Hub 处理逻辑
        // ==========================================
        // 1. 🥈 尝试寻找 USB 2.0 高级多事务 Hub (MTT, Protocol = 2)
        if (if_alt = usb_find_alt_if(uif,USB_MATCH_ANY,USB_MATCH_ANY, 2)) {
            udev->hub_mtt = 1;
        } else if (if_alt = usb_find_alt_if(uif,USB_MATCH_ANY,USB_MATCH_ANY, 1)) {
            // 2. 🥉 降级寻找 USB 2.0 单事务 Hub (STT, Protocol = 1)
            udev->hub_mtt = 0;
        } else if (if_alt = usb_find_alt_if(uif,USB_MATCH_ANY,USB_MATCH_ANY, 0)) {
            // 3. 🪨 终极保底：USB 1.1 全速 Hub 或基础兼容模式 (Protocol = 0)
            udev->hub_mtt = 0;
        } else {
            // 终极防御：如果连 Protocol 0 都找不到，说明这是一个损坏的设备或非 Hub 设备
            color_printk(RED, BLACK, "USB: Failed to find any valid Hub protocol!\n");
            return -ENODEV;
        }

        //获取2.0hub描述符
        usb_hub20_desc_t *hub20_desc = kzalloc_dma(sizeof(usb_hub20_desc_t));
        int32 error = usb_hub20_get_desc(udev, hub20_desc);
        if (error < 0) return error;
        hub->hub20_desc = hub20_desc;

        //解析hub描述符
        udev->hub_num_ports = hub20_desc->num_ports;
        udev->hub_ttt = (hub20_desc->hub_characteristics >> 5) & 0x03;
        hub->is_individual_pwr = (hub20_desc->hub_characteristics & 0x03) == 0x01;
        hub->is_individual_ocp = ((hub20_desc->hub_characteristics >> 3) & 0x03) == 0x01;
        hub->power_delay_ms = hub20_desc->power_on_to_power_good << 1;

        // 分配 hub 端口内存并解析哪些是不可拆卸端口
        hub->ports = kzalloc((udev->hub_num_ports + 1) * sizeof(hub_port_t));
        for (uint8 i = 1; i <= udev->hub_num_ports; i++) {
            hub->ports[i].port_id = i;
            uint8 byte_idx = i / 8;
            uint8 bit_idx  = i % 8;
            hub->ports[i].is_removable = !((hub20_desc->device_removable[byte_idx] >> bit_idx) & 1);
        }
    }

    //设置udev为hub模式
    usb_ctx_slot_cfg(udev);

    xhci_slot_ctx_t *slot_ctx = usb_get_out_ctx_entry(udev->out_ctx, 0, udev->xhcd->ctx_size);
    color_printk(RED,BLACK, "is_hub:%d num_port:%d  \n", slot_ctx->is_hub, slot_ctx->num_ports);

    //启用接口
    usb_ep_t *ep1 = &if_alt->eps[0];
    ep1->ring_max_trbs = 32;
    usb_enable_alt_if(if_alt);

    //给所有端口上电
    for (uint8 i = 1; i <= udev->hub_num_ports; i++) {
        usb_hub_port_up_power(udev, i);
    }

    // 🌟 物理规律：必须等待电容充电完毕！(你的 hub->power_delay_ms 派上用场了)
    uint32 times = 0x5000000;
    while (times) {
        times--;
        asm_pause();
    }

// =========================================================================
    // 🔍 2. 开机扫街 (处理端口上遗留设备 - TheresaOS 完全体状态机)
    // =========================================================================
    for (uint8 i = 1; i <= udev->hub_num_ports; i++) {
        uint8 port_num = hub->ports[i].port_id;
        uint32 port_status = 0;

        // 获取当前端口实时物理快照
        if (usb_hub_port_get_status(udev, port_num, &port_status) < 0) {
            color_printk(RED, BLACK, "[Hub] Failed to get status for port %d\n", port_num);
            continue;
        }
        color_printk(GREEN, BLACK, "[Hub Scan] Port %d Raw Status: %#x\n", port_num, port_status);

        // 定点确认签收开机时突发的物理插拔变化标志
        if (port_status & USB_PORT_STAT_C_CONNECTION) {
            usb_hub_port_clear_connection_change(udev, port_num);
        }

        // 🚨 判定物理接口上是否有东西插着
        if (port_status & USB_PORT_STAT_CONNECTION) {

            if (udev->port_speed > USB_SPEED_HIGH) {
                // =============================================================
                // 🚀 双轨分流：USB 3.0+ 超高速扫街容错状态机
                // =============================================================

                // 探测地雷：如果 3.0 链路训练死锁在非激活状态 (SS.Inactive)
                if ((port_status & USB3_PORT_STAT_LINK_MASK) == USB3_PORT_LINK_SS_INACTIVE) {
                    color_printk(RED, BLACK, "[Hub 3.x] Port %d Deadlocked in SS.Inactive! Deploying Big Hammer...\n", port_num);

                    // 下发 28 号 Selector: Warm Reset 强刷物理层
                    usb_hub_port_bh_reset(udev, port_num);

                    // 轮询死等物理大锤复位完成 (wPortChange Bit 21)
                    volatile uint32 timeout = 5000000;
                    while (timeout > 0) {
                        usb_hub_port_get_status(udev, port_num, &port_status);
                        if (port_status & USB3_PORT_STAT_C_BH_RESET) break;
                        timeout--;
                    }

                    if (timeout == 0) {
                        color_printk(RED, BLACK, "[Hub 3.x] Port %d Warm Reset Timeout!\n", port_num);
                        continue;
                    }

                    // 签收大锤复位和链路突变警报
                    usb_hub_port_clear_bh_reset_change(udev, port_num);
                    usb_hub_port_clear_link_state_change(udev, port_num);
                }

                // 再次获取最新状态，确认链路训练结果
                usb_hub_port_get_status(udev, port_num, &port_status);

                // 物理规律：3.0 设备如果正常，开机时早就自发完成了链路训练并自动进入了 ENABLE + U0 状态
                if ((port_status & USB_PORT_STAT_ENABLE) && ((port_status & USB3_PORT_STAT_LINK_MASK) == USB3_PORT_LINK_U0)) {
                    color_printk(GREEN, BLACK, "[Hub 3.x] Port %d Link active in U0! Speed ID: %d\n",
                                 port_num, (port_status & USB3_PORT_STAT_SPEED_MASK) >> 10);

                    // 顺手吃掉由于物理层先前握手残留下来的历史变更报警，防止中断风暴
                    if (port_status & USB_PORT_STAT_C_RESET)        usb_hub_port_clear_reset_change(udev, port_num);
                    if (port_status & USB3_PORT_STAT_C_LINK_STATE)  usb_hub_port_clear_link_state_change(udev, port_num);

                    // 💥 3.0 成功降落：直接分配 Slot 编号，下发 SetAddress 枚举 3.x 设备！
                    // uint8 ss_speed = USB_SPEED_SUPER; // 或根据 MASK 细分 10G/20G
                    // usb_enroll_child_device(udev, port_num, ss_speed);

                } else {
                    // 如果插了 3.0 设备但没能自发进入 U0，尝试手动下发 Hot Reset 强推一把
                    color_printk(YELLOW, BLACK, "[Hub 3.x] Port %d connected but not ready. Forcing Hot Reset...\n", port_num);
                    usb_hub_port_reset(udev, port_num);

                    volatile uint32 timeout = 5000000;
                    while (timeout > 0) {
                        usb_hub_port_get_status(udev, port_num, &port_status);
                        if (port_status & USB_PORT_STAT_C_RESET) break;
                        timeout--;
                    }
                    usb_hub_port_clear_reset_change(udev, port_num);

                    // 再次核对
                    if (port_status & USB_PORT_STAT_ENABLE) {
                        color_printk(GREEN, BLACK, "[Hub 3.x] Port %d Hot Reset Successful.\n", port_num);
                        // usb_enroll_child_device(udev, port_num, USB_SPEED_SUPER);
                    }
                }

            } else {
                // =============================================================
                // 🐢 双轨分流：USB 2.0 传统扫街催熟状态机
                // =============================================================
                // 2.0 硬件很懒，连线成功后绝不会自动 Enable，必须靠软件手动发射 Reset 电平催熟
                color_printk(YELLOW, BLACK, "[Hub 2.0] Port %d connected. Sending Reset Signal...\n", port_num);
                usb_hub_port_reset(udev, port_num);

                // 🌟 物理规律：死等标准复位完成 (wPortChange Bit 20 立起)
                volatile uint32 timeout = 5000000; // 粗略估算契合你的单任务环境
                while (timeout > 0) {
                    usb_hub_port_get_status(udev, port_num, &port_status);
                    if (port_status & USB_PORT_STAT_C_RESET) {
                        break;
                    }
                    timeout--;
                }

                if (timeout == 0) {
                    color_printk(RED, BLACK, "[Hub 2.0] Port %d Reset Timeout!\n", port_num);
                    continue;
                }

                // 🔪 核心修复 2：按图索骥。立刻擦除复位完成标志
                usb_hub_port_clear_reset_change(udev, port_num);

                // 🔪 核心修复 3：顺手擦除随之产生的 2.0 独占 Enable 变化标志
                if (port_status & USB2_PORT_STAT_C_ENABLE) {
                    usb_hub_port_clear_enable_change(udev, port_num);
                }

                // 最终核对：2.0 端口复位后，硬件是否成功将其转入 ENABLE 转发状态？
                if (port_status & USB_PORT_STAT_ENABLE) {
                    // 重新获取干净的快照以提取速率信息
                    usb_hub_port_get_status(udev, port_num, &port_status);

                    // 🌟 核心修复 4：精准提取 2.0 速度代际
                    uint32 raw_speed = port_status & USB2_PORT_STAT_SPEED_MASK;
                    uint8 child_speed = USB_SPEED_FULL; // 保底全速

                    if (raw_speed == USB2_PORT_STAT_LOW_SPEED) {
                        child_speed = USB_SPEED_LOW;
                        color_printk(GREEN, BLACK, "[Hub 2.0] Port %d: Low-Speed (1.5Mbps) Device Ready!\n", port_num);
                    } else if (raw_speed == USB2_PORT_STAT_HIGH_SPEED) {
                        child_speed = USB_SPEED_HIGH;
                        color_printk(GREEN, BLACK, "[Hub 2.0] Port %d: High-Speed (480Mbps) Device Ready!\n", port_num);
                    } else {
                        color_printk(GREEN, BLACK, "[Hub 2.0] Port %d: Full-Speed (12Mbps) Device Ready!\n", port_num);
                    }

                    // 💥 2.0 成功降落：为这个子设备分配 xHCI Slot 地址，获取设备描述符！
                    // usb_enroll_child_device(udev, port_num, child_speed);
                }
            }
        }
    }


    // usb_fill_bulk_urb(hub->int_urb, udev, ep1, hub->status_bitmap, ep1->max_packet_size);
    // usb_submit_urb(hub->int_urb);
    //
    // // 等结果
    // while (hub->int_urb->is_done == FALSE) {
    //     asm_pause();
    // }
    //
    // color_printk(RED,BLACK, "usb_hub2.0 bitmap:%#x  \n", hub->status_bitmap[0]);
    //
    // while (1);
}

void usb_hub_remove(usb_if_t *usb_if) {
}

usb_drv_t *create_usb_hub_driver() {
    usb_drv_t *usb_drv = kzalloc(sizeof(usb_drv_t));
    usb_id_t *id_table = kzalloc(sizeof(usb_id_t) * 2);
    id_table[0].match_flags = USB_MATCH_INT_CLASS;
    id_table[0].if_class = 0x9;
    usb_drv->drv.name = "usb_hub";
    usb_drv->drv.id_table = id_table;
    usb_drv->probe = usb_hub_probe;
    usb_drv->remove = usb_hub_remove;
    return usb_drv;
}
