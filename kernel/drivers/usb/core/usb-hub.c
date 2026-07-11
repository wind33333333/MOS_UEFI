#include "usb-hub.h"
#include "xhci-ring.h"
#include "xhci-hcd.h"
#include "xhci-ctx.h"
#include "errno.h"
#include "printk.h"
#include "slub.h"


// ==========================================
// 📦 Hub 专属类描述符获取 (Class Descriptor)
// ==========================================
// ============================================================================
// 🔌 USB Hub 类控制传输 API (依托 7 参数大一统枢纽)
// 注意：Hub 请求的 bmRequestType 必须是 USB_REQ_TYPE_CLASS！
// ============================================================================

/**
 * @brief 获取 USB 2.0 (全速/高速) Hub 描述符
 * @note 描述符类型为 0x29 (USB_DESC_TYPE_HUB20)
 */
static inline int32 usb_hub20_get_desc(usb_dev_t *udev, void *buf) {
    // 🌟 一键生成 bmRequestType: 10100000b (0xA0)
    // 方向: IN, 类型: CLASS (类特定), 接收者: DEVICE
    uint8 req_type = USB_BM_REQ_TYPE(USB_REQ_DIR_IN, USB_REQ_TYPE_CLASS, USB_REQ_REC_DEVICE);

    return usb_control_msg(udev, buf,
                           req_type,
                           USB_REQ_GET_DESCRIPTOR,
                           (USB_DESC_TYPE_HUB20 << 8) | 0, // wValue: 高字节 0x29，低字节 0
                           0,                              // wIndex: 0
                           sizeof(usb_hub20_desc_t));      // wLength: 2.0 Hub 描述符大小
}

/**
 * @brief 获取 USB 3.0+ (超高速) Hub 描述符
 * @note 描述符类型为 0x2A (USB_DESC_TYPE_HUB30)
 */
static inline int32 usb_hub30_get_desc(usb_dev_t *udev, void *buf) {
    uint8 req_type = USB_BM_REQ_TYPE(USB_REQ_DIR_IN, USB_REQ_TYPE_CLASS, USB_REQ_REC_DEVICE);

    return usb_control_msg(udev, buf,
                           req_type,
                           USB_REQ_GET_DESCRIPTOR,
                           (USB_DESC_TYPE_HUB30 << 8) | 0, // wValue: 高字节 0x2A，低字节 0
                           0,                              // wIndex: 0
                           sizeof(usb_hub30_desc_t));      // wLength: 3.0 Hub 描述符大小
}

/**
 * @brief 设置 Hub 深度 (Set Hub Depth) - 超高速 Hub 专属强制指令！
 * @note 必须在 Set Configuration 之后、获取 Hub 描述符之前发送。
 */
static inline int32 usb_hub30_set_depth(usb_dev_t *udev) {
    // 🌟 一键生成 bmRequestType: 00100000b (0x20)
    // 方向: OUT, 类型: CLASS (类特定), 接收者: DEVICE
    uint8 req_type = USB_BM_REQ_TYPE(USB_REQ_DIR_OUT, USB_REQ_TYPE_CLASS, USB_REQ_REC_DEVICE);

    return usb_control_msg(udev, NULL,
                           req_type,
                           HUB_REQ_SET_HUB_DEPTH,
                           udev->hub_depth, // wValue: Hub 所在的拓扑深度 (Root Hub 下直连为 0)
                           0,               // wIndex: 0
                           0);              // wLength: 纯命令，无数据阶段
}


// ============================================================================
// 📡 1. Hub 端口状态获取 (TheresaOS 零拷贝高性能版)
// ============================================================================

/**
 * @brief 获取 Hub 端口的 4 字节状态数据
 * @param udev        Hub 的设备对象
 * @param port_num    目标物理端口号 (1-based)
 * @param port_status 用于向上传递状态的指针
 */
static int32 usb_hub_port_get_status(usb_dev_t *udev, uint8 port_num, uint32 *port_status) {
    // 1. 直接拉出宿主 Hub 身上那块预先埋好的常驻 DMA 盾牌
    usb_hub_t *hub = (usb_hub_t *)udev->drv_data;
    uint32 *port_sts = hub->port_status;

    // 🌟 一键生成 bmRequestType: 10100011b (0xA3)
    // 方向: IN, 类型: CLASS, 接收者: OTHER (代表目标是端口)
    uint8 req_type = USB_BM_REQ_TYPE(USB_REQ_DIR_IN, USB_REQ_TYPE_CLASS, USB_REQ_REC_OTHER);

    // 2. 干净的纯血控制传输，直接让 xHCI 硬件把数据 DMA 到这个常驻缓冲区
    int32 ret = usb_control_msg(udev, port_sts,
                                req_type,
                                USB_REQ_GET_STATUS,
                                0,        // wValue: 0
                                port_num, // wIndex: 端口号
                                4);       // wLength: 状态固定为 4 字节

    if (ret == 0) {
        *port_status = *port_sts;
    }

    // 🌟 零内存释放，零碎片产生，安全退出！
    return ret;
}


// ============================================================================
// ⚓ 2. Layer 1: 通用控制原语底层 (支持 wIndex 高位打包)
// ============================================================================

/**
 * @brief 底层通用的特征设置原语 (Set Port Feature)
 * @param feature 端口特征选择器 (如 USB_HUB_PORT_FEAT_POWER)
 * @param ext_arg 扩展参数。如果是 3.0 链路控制或 2.0 测试模式，会自动打包进 wIndex 的高 8 位。
 */
static inline int32 usb_hub_port_set_feature(usb_dev_t *udev, uint8 port_num, uint16 feature, uint8 ext_arg) {
    // 🌟 一键生成 bmRequestType: 00100011b (0x23)
    // 方向: OUT, 类型: CLASS, 接收者: OTHER
    uint8 req_type = USB_BM_REQ_TYPE(USB_REQ_DIR_OUT, USB_REQ_TYPE_CLASS, USB_REQ_REC_OTHER);

    // 将 ext_arg 拼接到 wIndex 的高字节，port_num 放在低字节
    uint16 index = ((uint16)ext_arg << 8) | port_num;

    return usb_control_msg(udev, NULL,
                           req_type,
                           USB_REQ_SET_FEATURE,
                           feature, // wValue: 特征宏 (如复位、上电)
                           index,   // wIndex: 高位扩展 + 低位端口号
                           0);      // wLength: 0 (纯命令)
}

/**
 * @brief 底层通用的特征擦除原语 (Clear Port Feature)
 * @param feature 要擦除的特征或事件报警 (如 USB_HUB_PORT_FEAT_C_CONNECTION)
 */
static inline int32 usb_hub_port_clear_feature(usb_dev_t *udev, uint8 port_num, uint16 feature, uint8 ext_arg) {
    uint8 req_type = USB_BM_REQ_TYPE(USB_REQ_DIR_OUT, USB_REQ_TYPE_CLASS, USB_REQ_REC_OTHER);

    uint16 index = ((uint16)ext_arg << 8) | port_num;

    return usb_control_msg(udev, NULL,
                           req_type,
                           USB_REQ_CLEAR_FEATURE,
                           feature, // wValue: 要清除的特征宏
                           index,   // wIndex: 高位扩展 + 低位端口号
                           0);      // wLength: 0 (纯命令)
}

// =========================================================================
// 🔌 3. Layer 2: 面向业务的语义化 Inline 动作外挂 (2.0/3.0 通用基础控制)
// =========================================================================

/**
 * @brief 给指定物理端口闭合继电器通电 (VBUS 5V 上电)
 */
static inline int32 usb_hub_port_power_on(usb_dev_t *udev, uint8 port_num) {
    return usb_hub_port_set_feature(udev, port_num, USB_HUB_PORT_FEAT_POWER, 0);
}

/**
 * @brief 给指定物理端口断电
 */
static inline int32 usb_hub_port_power_off(usb_dev_t *udev, uint8 port_num) {
    return usb_hub_port_clear_feature(udev, port_num, USB_HUB_PORT_FEAT_POWER, 0);
}

/**
 * @brief 强行将激活的端口下线 (逻辑强拔)
 */
static inline int32 usb_hub_port_disable(usb_dev_t *udev, uint8 port_num) {
    return usb_hub_port_clear_feature(udev, port_num, USB_HUB_PORT_FEAT_ENABLE, 0);
}

/**
 * @brief 发射标准热复位信号 (2.0物理拉低线路 / 3.0超高速 Hot Reset)
 * 🌟 这是发现新插入设备后，触发硬件使能的绝对第一步！
 */
static inline int32 usb_hub_port_reset_hot(usb_dev_t *udev, uint8 port_num) {
    return usb_hub_port_set_feature(udev, port_num, USB_HUB_PORT_FEAT_RESET, 0);
}


// =========================================================================
// 🐢 4. Layer 2: USB 2.0 纯血独占特征动作
// =========================================================================

/**
 * @brief 强迫 2.0 端口进入挂起 (Suspend) 状态
 */
static inline int32 usb_hub_port_set_suspend20(usb_dev_t *udev, uint8 port_num) {
    return usb_hub_port_set_feature(udev, port_num, USB_HUB_PORT_FEAT_SUSPEND, 0);
}

/**
 * @brief 向 2.0 端口发射 Resume 信号将其从挂起中唤醒
 */
static inline int32 usb_hub_port_clear_suspend20(usb_dev_t *udev, uint8 port_num) {
    return usb_hub_port_clear_feature(udev, port_num, USB_HUB_PORT_FEAT_SUSPEND, 0);
}

/**
 * @brief 强制 2.0 端口执行特定电气合规性测试 (Compliance Test)
 * @param test_selector 测试类型选择子 (如 J_STATE, K_STATE, SE0_NAK 等)
 */
static inline int32 usb_hub_port_set_test_mode(usb_dev_t *udev, uint8 port_num, uint8 test_selector) {
    return usb_hub_port_set_feature(udev, port_num, USB_HUB_PORT_FEAT_TEST, test_selector);
}

/**
 * @brief 控制 2.0 工业级扩展坞上的端口指示灯 (LED) 颜色或闪烁模式
 * @param indicator_selector 灯光状态选择子 (如 1=琥珀色, 2=绿色, 0=关闭)
 */
static inline int32 usb_hub_port_set_indicator(usb_dev_t *udev, uint8 port_num, uint8 indicator_selector) {
    return usb_hub_port_set_feature(udev, port_num, USB_HUB_PORT_FEAT_INDICATOR, indicator_selector);
}


// =========================================================================
// 🚀 5. Layer 2: USB 3.0+ 超高速物理链路硬核控制动作
// =========================================================================

/**
 * @brief 🔥 [大锤物理强刷] 发射 Warm Reset 信号！
 * 🌟 当超高速链路不幸跌落至 SS.Inactive 硬件死锁状态时，这是唯一的解药！
 */
static inline int32 usb_hub_port_reset_warm(usb_dev_t *udev, uint8 port_num) {
    return usb_hub_port_set_feature(udev, port_num, USB_HUB_PORT_FEAT_BH_PORT_RESET, 0);
}

/**
 * @brief 强制命令 3.0 物理层跳转到指定的链路状态 (代替传统的 Selector 2 Suspend)
 * @param udev              目标 Hub 设备
 * @param port_num          目标物理端口号
 * @param target_link_state 目标链路状态 (如 USB3_SET_LINK_U3 代表命令设备深度休眠)
 */
static inline int32 usb_hub_port_set_link_state(usb_dev_t *udev, uint8 port_num, uint8 target_link_state) {
    return usb_hub_port_set_feature(udev, port_num, USB_HUB_PORT_FEAT_LINK_STATE, target_link_state);
}

/**
 * @brief 设置 3.0 端口在空闲多久后可以自动切换到 U1 低功耗待机状态
 * @param timeout_val 超时系数 (单位: 1微秒，0xFF代表永不限制)
 */
static inline int32 usb_hub_port_set_u1_timeout(usb_dev_t *udev, uint8 port_num, uint8 timeout_val) {
    return usb_hub_port_set_feature(udev, port_num, USB_HUB_PORT_FEAT_U1_TIMEOUT, timeout_val);
}

/**
 * @brief 设置 3.0 端口在空闲多久后可以自动切换到 U2 深度低功耗状态
 * @param timeout_val 超时系数 (单位: 256微秒)
 */
static inline int32 usb_hub_port_set_u2_timeout(usb_dev_t *udev, uint8 port_num, uint8 timeout_val) {
    return usb_hub_port_set_feature(udev, port_num, USB_HUB_PORT_FEAT_U2_TIMEOUT, timeout_val);
}

/**
 * @brief 配置超高速远程唤醒过滤掩码
 * @param wake_mask 允许唤醒的条件位图 (如开启远程插拔唤醒、过流唤醒等)
 */
static inline int32 usb_hub_port_set_remote_wake_mask(usb_dev_t *udev, uint8 port_num, uint8 wake_mask) {
    return usb_hub_port_set_feature(udev, port_num, USB_HUB_PORT_FEAT_REMOTE_WAKE_MASK, wake_mask);
}

/**
 * @brief 强制超高速端口无脑接受主机的硬件电源管理 (PM) 握手协商
 */
static inline int32 usb_hub_port_force_linkpm_accept(usb_dev_t *udev, uint8 port_num) {
    return usb_hub_port_set_feature(udev, port_num, USB_HUB_PORT_FEAT_FORCE_LINKPM_ACCEPT, 0);
}


// =========================================================================
// 🧹 6. Layer 2: 2.0 / 3.0 统一高位事件变更擦除 (Ack Interrupt 专属，杜绝中断风暴)
// =========================================================================

/**
 * @brief 确认并签收 [物理插拔] 事件 (解除 Bit 16 报警)
 */
static inline int32 usb_hub_port_clear_connection_change(usb_dev_t *udev, uint8 port_num) {
    return usb_hub_port_clear_feature(udev, port_num, USB_HUB_PORT_FEAT_C_CONNECTION, 0);
}

/**
 * @brief 确认并签收 [端口硬件级强行禁用] 事件 (解除 Bit 17 报警)
 */
static inline int32 usb_hub_port_clear_enable_change(usb_dev_t *udev, uint8 port_num) {
    return usb_hub_port_clear_feature(udev, port_num, USB_HUB_PORT_FEAT_C_ENABLE, 0);
}

/**
 * @brief 确认并签收 [2.0挂起苏醒完成] 事件 (解除 Bit 18 报警)
 */
static inline int32 usb_hub_port_clear_suspend_change(usb_dev_t *udev, uint8 port_num) {
    return usb_hub_port_clear_feature(udev, port_num, USB_HUB_PORT_FEAT_C_SUSPEND, 0);
}

/**
 * @brief 确认并签收 [端口短路过流] 事件 (解除 Bit 19 报警)
 */
static inline int32 usb_hub_port_clear_over_current_change(usb_dev_t *udev, uint8 port_num) {
    return usb_hub_port_clear_feature(udev, port_num, USB_HUB_PORT_FEAT_C_OVER_CURRENT, 0);
}

/**
 * @brief 确认并签收 [标准复位完成] 事件 (解除 Bit 20 报警)
 * 🌟 在单任务轮询状态机中，刷完这个原语，即代表端口彻底降服，可以直接下发 SetAddress 分配拓扑地址。
 */
static inline int32 usb_hub_port_clear_reset_change(usb_dev_t *udev, uint8 port_num) {
    return usb_hub_port_clear_feature(udev, port_num, USB_HUB_PORT_FEAT_C_RESET, 0);
}


// =========================================================================
// 💥 7. Layer 2: USB 3.0+ 超高速专用高位事件变更擦除 (Ack 3.x Only)
// =========================================================================

/**
 * @brief 确认并签收 [超高速物理链路状态突变] 事件 (解除 Bit 25 报警)
 * 🌟 比如链路成功从 Rx.Detect 晋升为完全体 U0 状态时触发的喜报，必须发此指令确认签收。
 */
static inline int32 usb_hub_port_clear_link_state_change(usb_dev_t *udev, uint8 port_num) {
    return usb_hub_port_clear_feature(udev, port_num, USB_HUB_PORT_FEAT_C_PORT_LINK_STATE, 0);
}

/**
 * @brief 确认并签收 [物理层阻抗/配置异常严重错误] 事件 (解除 Bit 26 报警)
 */
static inline int32 usb_hub_port_clear_config_error_change(usb_dev_t *udev, uint8 port_num) {
    return usb_hub_port_clear_feature(udev, port_num, USB_HUB_PORT_FEAT_C_PORT_CONFIG_ERROR, 0);
}

/**
 * @brief 确认并签收 [大锤 Warm Reset 复位完成] 事件 (解除 Bit 29 报警)
 * 🌟 当你用大锤把死锁的链路救活后，硬件会抛出此事件，必须立刻通过此函数“确认签收”，否则总线永无宁日。
 */
static inline int32 usb_hub_port_clear_bh_reset_change(usb_dev_t *udev, uint8 port_num) {
    return usb_hub_port_clear_feature(udev, port_num, USB_HUB_PORT_FEAT_C_BH_PORT_RESET, 0);
}

static inline void usb_hub_port_dev_create(usb_dev_t *parent_hub, uint8 port_num,uint32 portsc) {
    usb_dev_t *udev = kzalloc(sizeof(usb_dev_t));
    udev->xhcd = parent_hub->xhcd;
    xhci_hcd_t *xhcd = udev->xhcd;

    udev->parent_hub = parent_hub;
    udev->root_hub_port_num = parent_hub->root_hub_port_num; // 继承亲爹的根端口
    udev->hub_depth = parent_hub->hub_depth + 1;
    uint8 shift = parent_hub->hub_depth << 2;
    udev->route_string = parent_hub->route_string | (port_num << shift);
    udev->tt_hub_slot_id = 0;
    udev->tt_port_num = 0;

    // 向外接 Hub 发送控制包获取端口状态
    if (parent_hub->port_speed > USB_SPEED_HIGH) {
        // 1. USB 3.0+ Hub：根据 USB-IF 规范，3.0 Hub 节点下只跑 3.0 设备！
        switch (portsc & USB3_HUB_PORT_STAT_SPEED_MASK) {
            case USB3_HUB_PORT_STAT_SPEED_5G:
                udev->port_speed = USB_SPEED_SUPER_5G;
                break;
            case USB3_HUB_PORT_STAT_SPEED_10G:
                udev->port_speed = USB_SPEED_SUPER_10G;
                break;
            case USB3_HUB_PORT_STAT_SPEED_20G:
                udev->port_speed = USB_SPEED_SUPER_20G;
            default:
                break;
        }
    } else {
        // 2. USB 2.0 Hub：必须解析 wPortStatus 状态包的位 9 和位 10
        // Bit 9: Low Speed, Bit 10: High Speed. 都不亮就是 Full Speed.
        switch (portsc & USB2_HUB_PORT_STAT_SPEED_MASK) {
            case USB2_HUB_PORT_STAT_FULL_SPEED:
                udev->port_speed = USB_SPEED_FULL;
                break;
            case USB2_HUB_PORT_STAT_LOW_SPEED:
                udev->port_speed = USB_SPEED_LOW;
                break;
            case USB2_HUB_PORT_STAT_HIGH_SPEED:
                udev->port_speed = USB_SPEED_HIGH;
                break;
            default:
                break;
        }

        // 只有全速/低速才去追踪 TT
        if (udev->port_speed <= USB_SPEED_FULL) {
            if (parent_hub->port_speed == USB_SPEED_HIGH) {
                udev->tt_hub_slot_id = parent_hub->slot_id;
                udev->tt_port_num = port_num;
            }else {
                udev->tt_hub_slot_id = parent_hub->tt_hub_slot_id;
                udev->tt_port_num = parent_hub->tt_port_num;
            }
        }

    }

    // 🌟 【终极严谨修正：反向查字典确认硬件 PSIV】🌟
    // 拿着刚刚推断出的 udev->port_speed，去主控的字典里找它真正对应的 psiv
    uint8 root_port = udev->root_hub_port_num;
    uint8 spc_idx = xhcd->port_to_spc[root_port];
    xhci_psi_t *psi_dict = xhcd->spc[spc_idx].psi_dict;
    // PSI 取值范围通常是 0~15 (4个 bit)
    for (int i = 0; i < 16; i++) {
        // 如果字典中记录的映射速度，刚好等同于我们当前设备的速度
        if (psi_dict[i].mapped_speed == udev->port_speed) {
            udev->speed_kbps = psi_dict[i].speed_kbps; //物理带宽
            udev->psiv = psi_dict[i].psiv; //硬件真实的 PSIV
            break; // 找到了就立刻退出
        }
    }

    usb_dev_init(udev);

}

// =========================================================================
// 🚀 核心处理引擎：纯粹的单端口事件分发器 (剥离了 URB 提交逻辑)
// =========================================================================
/**
 * @brief Hub 端口事件异步处理原语
 * @note 此函数现在是一个纯粹的业务处理函数，只负责端口的状态流转。
 * URB 的重新提交已经移交给了外层调用者 (如守护进程) 统一批处理！
 * @param udev     Hub 自身的设备描述符
 * @param port_num 发生事件的端口号 (从 1 开始)
 */
void usb_hub_process_port_event(usb_dev_t *udev, uint8 port_num) {
    uint32 portsc = 0;

    // 📸 1. 获取引发中断的初始物理快照
    if (usb_hub_port_get_status(udev, port_num, &portsc) < 0) {
        color_printk(RED, BLACK, "[Hub Port %d] Failed to read port status!\n", port_num);
        return;
    }

    usb_hub_t *hub = udev->drv_data;
    usb_hub_port_t *port = &hub->ports[port_num];

    //color_printk(GREEN, BLACK, "[Hub Port %d] Async IRQ! Status: %#x, Current State: %d hub_speed:%d  \n",port_num, portsc, port->state,udev->port_speed);

    // =========================================================================
    // 🧽 阶段一：硬件保洁区 (Acknowledge) - 见 1 擦 1，防止中断风暴
    // =========================================================================
    if (portsc & USB_HUB_PORT_STAT_C_OVERCURRENT) usb_hub_port_clear_over_current_change(udev, port_num);
    if (portsc & USB_HUB_PORT_STAT_C_RESET) usb_hub_port_clear_reset_change(udev, port_num);
    if (portsc & USB_HUB_PORT_STAT_C_CONNECTION) usb_hub_port_clear_connection_change(udev, port_num);

    if (udev->port_speed > USB_SPEED_HIGH) {
        if (portsc & USB3_HUB_PORT_STAT_C_BH_RESET) usb_hub_port_clear_bh_reset_change(udev, port_num);
        if (portsc & USB3_HUB_PORT_STAT_C_LINK_STATE) usb_hub_port_clear_link_state_change(udev, port_num);
        if (portsc & USB3_HUB_PORT_STAT_C_CONFIG_ERR) usb_hub_port_clear_config_error_change(udev, port_num);
    } else {
        if (portsc & USB2_HUB_PORT_STAT_C_ENABLE) usb_hub_port_clear_enable_change(udev, port_num);
        if (portsc & USB2_HUB_PORT_STAT_C_SUSPEND) usb_hub_port_clear_suspend_change(udev, port_num);
    }


    // =========================================================================
    // ⚙️ 阶段二：全异步业务调度区 (Action) - 修改状态，发起接力
    // =========================================================================

    // 💥 动作 A：致命过流保护 (最高优先级)
    if (portsc & USB_HUB_PORT_STAT_C_OVERCURRENT) {
        color_printk(RED, BLACK, "[Hub Port %d] OVERCURRENT! Powering off...\n", port_num);
        usb_hub_port_power_off(udev, port_num);
        port->state = PORT_STATE_DISCONNECTED; // 软件状态复位

        if (port->child_dev) {
            // TODO: 回收旧设备的 usb_dev_t
            port->child_dev = NULL;
        }
        return; // 🛡️ 安全退出，URB 提交已由外层统一接管
    }

    // 🔌 动作 B：物理层插拔突变 或 扫街兜底
    if ((portsc & USB_HUB_PORT_STAT_C_CONNECTION) ||
        ((portsc & USB_HUB_PORT_STAT_CONNECTION) && port->state == PORT_STATE_DISCONNECTED)) {
        if (portsc & USB_HUB_PORT_STAT_CONNECTION) {
            //color_printk(GREEN, BLACK, "[Hub Port %d] Async: New Device. Firing Reset...\n", port_num);

            // 🛡️ 防御：如果因为抖动导致状态机没清理干净，强行清理旧设备防内存泄漏！
            if (port->child_dev != NULL) {
                color_printk(YELLOW, BLACK, "[Hub Port %d] Bouncing detected! Purging old ghost device.\n", port_num);
                // TODO: destroy_usb_device(port->child_dev);
                port->child_dev = NULL;
            }

            if ((udev->port_speed > USB_SPEED_HIGH) &&
                ((portsc & USB3_HUB_PORT_STAT_LINK_MASK) == USB3_HUB_PORT_LINK_SS_INACTIVE)) {
                // USB 3.0 设备且链路死锁，抡大锤：暖复位 (BH Reset)
                usb_hub_port_reset_warm(udev, port_num);
                port->state = PORT_STATE_WAITING_WARM_RESET;
            } else {
                // 💥 无论 2.0 还是正常的 3.0 (含 U0)，统统一律发送常规热复位！
                usb_hub_port_reset_hot(udev, port_num);
                port->state = PORT_STATE_WAITING_HOT_RESET;
            }
            return; // 🛡️ 安全退出，发射第一棒命令完毕
        } else {
            color_printk(YELLOW, BLACK, "[Hub Port %d] Async: Device Disconnected!\n", port_num);
            port->state = PORT_STATE_DISCONNECTED;

            if (port->child_dev) {
                // TODO: 调用 Disable Slot，释放底层数据结构内存
                port->child_dev = NULL;
            }
            return; // 🛡️ 安全退出，宣告死亡完毕
        }
    }

    // 🎯 动作 C：复位接力棒！
    if ((portsc & USB_HUB_PORT_STAT_C_RESET) || (portsc & USB3_HUB_PORT_STAT_C_BH_RESET)) {
        if (port->state == PORT_STATE_WAITING_HOT_RESET ||
            port->state == PORT_STATE_WAITING_WARM_RESET) {
            boolean is_30 = (udev->port_speed > USB_SPEED_HIGH);
            boolean is_u0 = ((portsc & USB3_HUB_PORT_STAT_LINK_MASK) == USB3_HUB_PORT_LINK_U0);

            // 🛡️ 修复 USB 3.0 的 Enable 陷阱：USB 3.0 的端口 Enable 位永远是 0，必须靠 U0 判定！
            boolean is_enabled = is_30 ? is_u0 : ((portsc & USB_HUB_PORT_STAT_ENABLE) != 0);
            if (is_enabled) {
                port->state = PORT_STATE_ENABLED;
                // 💥 真正的异步枚举动作在这里发生：
                // TODO: 组装 SET_ADDRESS URB 下发给 xHCI
                usb_hub_port_dev_create(udev, port_num,portsc);
            } else {
                color_printk(RED, BLACK, "[Hub Port %d] Async: Reset finished but port dead!\n", port_num);
                port->state = PORT_STATE_DISCONNECTED; // 救不回来，放弃治疗
            }
            return; // 🛡️ 安全退出：复位流程已走完
        }
    }

    // 💤 动作 D：链路与电源状态突变处理 (移除冗余的外层 IF，展平逻辑)

    // 🚀 USB 3.0 高级链路状态机
    if (udev->port_speed > USB_SPEED_HIGH && (portsc & USB3_HUB_PORT_STAT_C_LINK_STATE)) {
        uint32 current_link = portsc & USB3_HUB_PORT_STAT_LINK_MASK;

        if (port->state == PORT_STATE_WAITING_LINK_CHANGE) {
            if (current_link == USB3_HUB_PORT_LINK_U3) {
                port->state = PORT_STATE_SUSPENDED;
            } else {
                port->state = PORT_STATE_ENABLED;
            }
        } else if (port->state == PORT_STATE_ENABLED && current_link == USB3_HUB_PORT_LINK_SS_INACTIVE) {
            color_printk(RED, BLACK, "[Hub 3.x Port %d] PASSIVE CRASH! Deploying Emergency Hammer...\n", port_num);
            usb_hub_port_reset_warm(udev, port_num);
            port->state = PORT_STATE_WAITING_WARM_RESET;
        } else if (port->state == PORT_STATE_SUSPENDED && current_link == USB3_HUB_PORT_LINK_U0) {
            color_printk(GREEN, BLACK, "[Hub 3.x Port %d] Woke up from U3!\n", port_num);
            port->state = PORT_STATE_ENABLED;
        }
    }

    // 🐢 USB 2.0 传统电源状态机
    if (udev->port_speed <= USB_SPEED_HIGH) {
        if (portsc & USB2_HUB_PORT_STAT_C_SUSPEND) {
            if (port->state == PORT_STATE_WAITING_RESUME || port->state == PORT_STATE_SUSPENDED) {
                color_printk(GREEN, BLACK, "[Hub 2.0 Port %d] Woke up from Suspend!\n", port_num);
                port->state = PORT_STATE_ENABLED;
            }
        }
        if (portsc & USB2_HUB_PORT_STAT_C_ENABLE) {
            if (port->state == PORT_STATE_ENABLED) {
                color_printk(RED, BLACK, "[Hub 2.0 Port %d] Unexpected Disable! Attempting Rescue...\n", port_num);
                usb_hub_port_reset_hot(udev, port_num);
                port->state = PORT_STATE_WAITING_HOT_RESET;
            }
        }
    }
}

//hub驱动
int32 usb_hub_probe(usb_if_t *uif, usb_id_t *uid) {
    usb_dev_t *udev = uif->udev;

    // USB 规范硬性限制：外部 Hub 最多级联 5 层。udev->hub_depth = 0 代表第一级外接 Hub ... 为 4 代表第五级外接 Hub。
    if (udev->hub_depth >= 5) {
        color_printk(RED, BLACK, "USB Topology Error: Hub cascade depth (%d) exceeds 5 levels limit!\n", udev->hub_depth + 1);
        return -E2BIG; // 返回“参数过大”或适当的错误码，让系统放弃匹配此驱动
    }

    usb_hub_t *hub = kzalloc(sizeof(usb_hub_t));
    hub->uif = uif;
    hub->port_status = kzalloc_dma(sizeof(uint32));
    hub->port_bitmap_status = kzalloc_dma(32);
    udev->is_hub = TRUE;
    udev->drv_data = hub;
    usb_if_alt_t *if_alt = NULL;

    if (udev->port_speed > USB_SPEED_HIGH) {
        usb_hub30_set_depth(udev);  //usb3.0hub需要设置hub深度

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
        hub->ports = kzalloc((udev->hub_num_ports + 1) * sizeof(usb_hub_port_t));
        for (uint8 i = 1; i <= udev->hub_num_ports; i++) {
            hub->ports[i].port_num = i;
            uint16 removable_bitmap = hub30_desc->device_removable;
            hub->ports[i].is_removable = !((removable_bitmap >> i) & 1);
        }
    } else {
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
        hub->ports = kzalloc((udev->hub_num_ports + 1) * sizeof(usb_hub_port_t));
        for (uint8 port_num = 1; port_num <= udev->hub_num_ports; port_num++) {
            hub->ports[port_num].port_num = port_num;
            uint8 byte_idx = port_num / 8;
            uint8 bit_idx = port_num % 8;
            hub->ports[port_num].is_removable = !((hub20_desc->device_removable[byte_idx] >> bit_idx) & 1);
        }
    }

    //2.设置udev为hub模式
    xhci_ctx_slot_cfg(udev);

    //3.启用接口
    usb_ep_t *ep1 = &if_alt->eps[0];
    ep1->ring_max_trbs = 32;
    usb_enable_alt_if(if_alt);

    //4.所有端口上电
    for (uint8 port_num = 1; port_num <= udev->hub_num_ports; port_num++) {
        usb_hub_port_power_on(udev, port_num);
    }

    //5.配置好中断 URB,提交队列后续有设备插入拔出等异步实现
    hub->int_urb = usb_alloc_urb();
    usb_fill_int_urb(hub->int_urb, udev, ep1, hub->port_bitmap_status, ep1->max_packet_size, ep1->interval);
    xhci_submit_urb(hub->int_urb);
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
