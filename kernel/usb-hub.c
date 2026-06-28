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
static int32 usb_hub_port_get_status(usb_dev_t *udev, uint8 port_num, uint32 *port_status) {
    // 1. 直接拉出宿主 Hub 身上那块预先埋好的常驻 DMA 盾牌
    usb_hub_t *hub = (usb_hub_t *) udev->drv_data;
    uint32 *port_sts = hub->port_status;

    // 2. 干净的纯血控制传输，直接让 xHCI 硬件写入这个常驻缓冲区
    int32 ret = usb_control_msg(udev, port_sts,
                                USB_DIR_IN, USB_REQ_TYPE_CLASS, USB_RECIP_OTHER,
                                USB_REQ_GET_STATUS, 0, port_num, 4);
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
static inline int32
usb_hub_set_port_feature(usb_dev_t *udev, uint8 port_num, usb_port_feature_e feature, uint8 ext_arg) {
    uint16 index = ((uint16) ext_arg << 8) | port_num;
    return usb_control_msg(udev, NULL,
                           USB_DIR_OUT, USB_REQ_TYPE_CLASS, USB_RECIP_OTHER,
                           USB_REQ_SET_FEATURE, feature, index, 0);
}

/**
 * @brief 底层通用的特征擦除原语
 */
static inline int32 usb_hub_clear_port_feature(usb_dev_t *udev, uint8 port_num, usb_port_feature_e feature,
                                               uint8 ext_arg) {
    uint16 index = ((uint16) ext_arg << 8) | port_num;
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
static inline int32 usb_hub_port_power_on(usb_dev_t *udev, uint8 port_num) {
    return usb_hub_set_port_feature(udev, port_num, USB_PORT_FEAT_POWER, 0);
}

/**
 * @brief 给指定物理端口断电
 */
static inline int32 usb_hub_port_power_off(usb_dev_t *udev, uint8 port_num) {
    return usb_hub_clear_port_feature(udev, port_num, USB_PORT_FEAT_POWER, 0);
}

/**
 * @brief 强行将激活的端口下线 (逻辑强拔)
 */
static inline int32 usb_hub_port_disable(usb_dev_t *udev, uint8 port_num) {
    return usb_hub_clear_port_feature(udev, port_num, USB_PORT_FEAT_ENABLE, 0);
}

/**
 * @brief 发射标准热复位信号 (2.0物理拉低线路 / 3.0超高速 Hot Reset)
 * 🌟 这是发现新插入设备后，触发硬件使能的绝对第一步！
 */
static inline int32 usb_hub_port_reset(usb_dev_t *udev, uint8 port_num) {
    return usb_hub_set_port_feature(udev, port_num, USB_PORT_FEAT_RESET, 0);
}


// =========================================================================
// 🐢 4. Layer 2: USB 2.0 纯血独占特征动作
// =========================================================================

/**
 * @brief 强迫 2.0 端口进入挂起 (Suspend) 状态
 */
static inline int32 usb_hub_port_set_suspend20(usb_dev_t *udev, uint8 port_num) {
    return usb_hub_set_port_feature(udev, port_num, USB_PORT_FEAT_SUSPEND, 0);
}

/**
 * @brief 向 2.0 端口发射 Resume 信号将其从挂起中唤醒
 */
static inline int32 usb_hub_port_clear_suspend20(usb_dev_t *udev, uint8 port_num) {
    return usb_hub_clear_port_feature(udev, port_num, USB_PORT_FEAT_SUSPEND, 0);
}

/**
 * @brief 强制 2.0 端口执行特定电气合规性测试 (Compliance Test)
 * @param test_selector 测试类型选择子 (如 J_STATE, K_STATE, SE0_NAK 等)
 */
static inline int32 usb_hub_port_set_test_mode(usb_dev_t *udev, uint8 port_num, uint8 test_selector) {
    return usb_hub_set_port_feature(udev, port_num, USB_PORT_FEAT_TEST, test_selector);
}

/**
 * @brief 控制 2.0 工业级扩展坞上的端口指示灯 (LED) 颜色或闪烁模式
 * @param indicator_selector 灯光状态选择子 (如 1=琥珀色, 2=绿色, 0=关闭)
 */
static inline int32 usb_hub_port_set_indicator(usb_dev_t *udev, uint8 port_num, uint8 indicator_selector) {
    return usb_hub_set_port_feature(udev, port_num, USB_PORT_FEAT_INDICATOR, indicator_selector);
}


// =========================================================================
// 🚀 5. Layer 2: USB 3.0+ 超高速物理链路硬核控制动作
// =========================================================================

/**
 * @brief 🔥 [大锤物理强刷] 发射 Warm Reset 信号！
 * 🌟 当超高速链路不幸跌落至 SS.Inactive 硬件死锁状态时，这是唯一的解药！
 */
static inline int32 usb_hub_port_bh_reset(usb_dev_t *udev, uint8 port_num) {
    return usb_hub_set_port_feature(udev, port_num, USB_PORT_FEAT_BH_PORT_RESET, 0);
}

/**
 * @brief USB 3.0+ SetFeature(PORT_LINK_STATE) 专属原始非移位参数
 * ⚠️ 专属控制层，专门用来当做设置函数的参数，严禁与读取位图混用！
 */
typedef enum : uint8 {
    USB3_SET_LINK_U0 = 0, // [强制唤醒] 命令链路切回 U0 满血工作状态 (数据对流)
    USB3_SET_LINK_U1 = 1, // [自动省电] 强制切入轻度时钟门控待机 (微秒级恢复)
    USB3_SET_LINK_U2 = 2, // [自动省电] 强制切入深度模拟电路待机 (百微秒级恢复)
    USB3_SET_LINK_U3 = 3, // [选择性挂起] 强制进入深度休眠 (Selective Suspend)

    // --- 以下 3 个是上一轮漏掉的硬核物理层控制命令 ---

    USB3_SET_LINK_SS_DISABLED = 4, // [逻辑断开3.0] 强行关闭超高速物理收发器。
    // 🌟 奇效：此命令会逼迫该接口上的双模设备降级去跑 2.0 线路！

    USB3_SET_LINK_RX_DETECT = 5, // [强制重连] 强行解散当前的链路训练状态机，
    // 让端口退回到最原始的发射物理脉冲、重新探测外设阻抗的阶段。

    USB3_SET_LINK_COMPLIANCE = 15 // [合规测试] 强迫端口进入电气特性测试模式（工厂用仪器测眼图专属）
} usb3_set_link_state_e;

/**
 * @brief 强制命令 3.0 物理层跳转到指定的链路状态 (代替传统的 Selector 2)
 * @param target_link_state 目标链路状态 (如 USB3_PORT_LINK_U3 代表命令设备深度休眠)
 */
static inline int32
usb_hub_port_set_link_state(usb_dev_t *udev, uint8 port_num, usb3_set_link_state_e target_link_state) {
    return usb_hub_set_port_feature(udev, port_num, USB_PORT_FEAT_LINK_STATE, target_link_state);
}

/**
 * @brief 设置 3.0 端口在空闲多久后可以自动切换到 U1 低功耗待机状态
 * @param timeout_val 超时系数 (单位: 1微秒，0xFF代表永不限制)
 */
static inline int32 usb_hub_port_set_u1_timeout(usb_dev_t *udev, uint8 port_num, uint8 timeout_val) {
    return usb_hub_set_port_feature(udev, port_num, USB_PORT_FEAT_U1_TIMEOUT, timeout_val);
}

/**
 * @brief 设置 3.0 端口在空闲多久后可以自动切换到 U2 深度低功耗状态
 * @param timeout_val 超时系数 (单位: 256微秒)
 */
static inline int32 usb_hub_port_set_u2_timeout(usb_dev_t *udev, uint8 port_num, uint8 timeout_val) {
    return usb_hub_set_port_feature(udev, port_num, USB_PORT_FEAT_U2_TIMEOUT, timeout_val);
}

/**
 * @brief 配置超高速远程唤醒过滤掩码
 * @param wake_mask 允许唤醒的条件位图 (如开启远程插拔唤醒、过流唤醒等)
 */
static inline int32 usb_hub_port_set_remote_wake_mask(usb_dev_t *udev, uint8 port_num, uint8 wake_mask) {
    return usb_hub_set_port_feature(udev, port_num, USB_PORT_FEAT_REMOTE_WAKE_MASK, wake_mask);
}

/**
 * @brief 强制超高速端口无脑接受主机的硬件电源管理 (PM) 握手协商
 */
static inline int32 usb_hub_port_force_linkpm_accept(usb_dev_t *udev, uint8 port_num) {
    return usb_hub_set_port_feature(udev, port_num, USB_PORT_FEAT_FORCE_LINKPM_ACCEPT, 0);
}


// =========================================================================
// 🧹 6. Layer 2: 2.0 / 3.0 统一高位事件变更擦除 (Ack Interrupt 专属，杜绝中断风暴)
// =========================================================================

/**
 * @brief 确认并签收 [物理插拔] 事件 (解除 Bit 16 报警)
 */
static inline int32 usb_hub_port_clear_connection_change(usb_dev_t *udev, uint8 port_num) {
    return usb_hub_clear_port_feature(udev, port_num, USB_PORT_FEAT_C_CONNECTION, 0);
}

/**
 * @brief 确认并签收 [端口硬件级强行禁用] 事件 (解除 Bit 17 报警)
 */
static inline int32 usb_hub_port_clear_enable_change(usb_dev_t *udev, uint8 port_num) {
    return usb_hub_clear_port_feature(udev, port_num, USB_PORT_FEAT_C_ENABLE, 0);
}

/**
 * @brief 确认并签收 [2.0挂起苏醒完成] 事件 (解除 Bit 18 报警)
 */
static inline int32 usb_hub_port_clear_suspend_change(usb_dev_t *udev, uint8 port_num) {
    return usb_hub_clear_port_feature(udev, port_num, USB_PORT_FEAT_C_SUSPEND, 0);
}

/**
 * @brief 确认并签收 [端口短路过流] 事件 (解除 Bit 19 报警)
 */
static inline int32 usb_hub_port_clear_over_current_change(usb_dev_t *udev, uint8 port_num) {
    return usb_hub_clear_port_feature(udev, port_num, USB_PORT_FEAT_C_OVER_CURRENT, 0);
}

/**
 * @brief 确认并签收 [标准复位完成] 事件 (解除 Bit 20 报警)
 * 🌟 在单任务轮询状态机中，刷完这个原语，即代表端口彻底降服，可以直接下发 SetAddress 分配拓扑地址。
 */
static inline int32 usb_hub_port_clear_reset_change(usb_dev_t *udev, uint8 port_num) {
    return usb_hub_clear_port_feature(udev, port_num, USB_PORT_FEAT_C_RESET, 0);
}


// =========================================================================
// 💥 7. Layer 2: USB 3.0+ 超高速专用高位事件变更擦除 (Ack 3.x Only)
// =========================================================================

/**
 * @brief 确认并签收 [超高速物理链路状态突变] 事件 (解除 Bit 25 报警)
 * 🌟 比如链路成功从 Rx.Detect 晋升为完全体 U0 状态时触发的喜报，必须发此指令确认签收。
 */
static inline int32 usb_hub_port_clear_link_state_change(usb_dev_t *udev, uint8 port_num) {
    return usb_hub_clear_port_feature(udev, port_num, USB_PORT_FEAT_C_PORT_LINK_STATE, 0);
}

/**
 * @brief 确认并签收 [物理层阻抗/配置异常严重错误] 事件 (解除 Bit 26 报警)
 */
static inline int32 usb_hub_port_clear_config_error_change(usb_dev_t *udev, uint8 port_num) {
    return usb_hub_clear_port_feature(udev, port_num, USB_PORT_FEAT_C_PORT_CONFIG_ERROR, 0);
}

/**
 * @brief 确认并签收 [大锤 Warm Reset 复位完成] 事件 (解除 Bit 29 报警)
 * 🌟 当你用大锤把死锁的链路救活后，硬件会抛出此事件，必须立刻通过此函数“确认签收”，否则总线永无宁日。
 */
static inline int32 usb_hub_port_clear_bh_reset_change(usb_dev_t *udev, uint8 port_num) {
    return usb_hub_clear_port_feature(udev, port_num, USB_PORT_FEAT_C_BH_PORT_RESET, 0);
}


void usb_hub_port_eunm(usb_dev_t *parent_hub, uint8 port_num) {
    usb_dev_t *udev = usb_dev_create(parent_hub->xhcd, parent_hub,port_num);
    usb_if_create(udev);
    usb_dev_register(udev);
    usb_if_register(udev);
}


// =========================================================================
// 🚀 核心处理引擎：全异步端口事件分发器
// =========================================================================
/**
 * @brief Hub 端口事件异步处理原语
 * @param udev     Hub 自身的设备描述符
 * @param port_num 发生事件的端口号 (从 1 开始)
 */
void usb_hub_process_port_event(usb_dev_t *udev, uint8 port_num) {
    uint32 init_port_status = 0;

    // 📸 1. 获取引发中断的初始物理快照
    if (usb_hub_port_get_status(udev, port_num, &init_port_status) < 0) {
        color_printk(RED, BLACK, "[Hub Port %d] Failed to read port status!\n", port_num);
        return;
    }

    // 获取该端口在操作系统层面的软件备忘录
    usb_hub_t *hub = udev->drv_data;
    usb_hub_port_t *port = &hub->ports[port_num];

    color_printk(GREEN, BLACK, "[Hub Port %d] Async IRQ! Status: %#x, Current State: %d\n",
                 port_num, init_port_status, port->state);

    // =========================================================================
    // 🧽 阶段一：硬件保洁区 (Acknowledge) - 见 1 擦 1，防止中断风暴
    // =========================================================================
    // 1. 跨代际通用报警
    if (init_port_status & USB_PORT_STAT_C_OVERCURRENT) usb_hub_port_clear_over_current_change(udev, port_num);
    if (init_port_status & USB_PORT_STAT_C_RESET)       usb_hub_port_clear_reset_change(udev, port_num);
    if (init_port_status & USB_PORT_STAT_C_CONNECTION)  usb_hub_port_clear_connection_change(udev, port_num);

    // 2. 代际独占报警
    if (udev->port_speed > USB_SPEED_HIGH) {
        if (init_port_status & USB3_PORT_STAT_C_BH_RESET)   usb_hub_port_clear_bh_reset_change(udev, port_num);
        if (init_port_status & USB3_PORT_STAT_C_LINK_STATE) usb_hub_port_clear_link_state_change(udev, port_num);
        if (init_port_status & USB3_PORT_STAT_C_CONFIG_ERR) usb_hub_port_clear_config_error_change(udev, port_num);
    } else {
        if (init_port_status & USB2_PORT_STAT_C_ENABLE)     usb_hub_port_clear_enable_change(udev, port_num);
        if (init_port_status & USB2_PORT_STAT_C_SUSPEND)    usb_hub_port_clear_suspend_change(udev, port_num);
    }


    // =========================================================================
    // ⚙️ 阶段二：全异步业务调度区 (Action) - 修改状态，发起接力
    // =========================================================================

    // 💥 动作 A：致命过流保护 (最高优先级)
    if (init_port_status & USB_PORT_STAT_C_OVERCURRENT) {
        color_printk(RED, BLACK, "[Hub Port %d] OVERCURRENT! Powering off...\n", port_num);
        usb_hub_port_power_off(udev, port_num);
        port->state = PORT_STATE_DISCONNECTED; // 软件状态复位
        // TODO: 回收该端口上可能存在的 usb_dev_t
        return;
    }

    // 🔌 动作 B：物理层插拔突变 或 扫街抓到的“哑巴”设备
    // 触发条件 1：硬件报告了插拔突变 (C_CONNECTION)
    // 触发条件 2：硬件物理上插着设备 (CONNECTION)，但我们的软件备忘录却认为它还没连接 (DISCONNECTED) -> 这就是扫街兜底抓到的！
    if ((init_port_status & USB_PORT_STAT_C_CONNECTION) ||
        ((init_port_status & USB_PORT_STAT_CONNECTION) && port->state == PORT_STATE_DISCONNECTED)) {

        if (init_port_status & USB_PORT_STAT_CONNECTION) {
            // -------------------------------------------------------------
            // 🟢 新设备插入：开局第一棒，发射复位命令，更新备忘录后立刻撤退！
            // -------------------------------------------------------------
            color_printk(GREEN, BLACK, "[Hub Port %d] Async: New Device. Firing Reset...\n", port_num);

            if (udev->port_speed > USB_SPEED_HIGH) {
                //usb 3.0 设备
                uint32 link_state = init_port_status & USB3_PORT_STAT_LINK_MASK;

                if (link_state == USB3_PORT_LINK_SS_INACTIVE) {
                    usb_hub_port_bh_reset(udev, port_num); // 抡大锤
                    port->state = PORT_STATE_WAITING_WARM_RESET;
                } else if (link_state != USB3_PORT_LINK_U0) {
                    usb_hub_port_reset(udev, port_num);    // 温柔复位
                    port->state = PORT_STATE_WAITING_HOT_RESET;
                } else {
                    // 插入即 U0，直接就绪
                    port->state = PORT_STATE_ENABLED;
                    // TODO: 直接发起 SetAddress 异步控制传输！
                    usb_hub_port_eunm(udev, port_num);

                }
            } else {
                // 2.0 传统复位
                usb_hub_port_reset(udev, port_num);
                port->state = PORT_STATE_WAITING_HOT_RESET;
            }

            return; // 💥 第一棒交接完毕，CPU 解放！

        } else {
            // -------------------------------------------------------------
            // 🔴 设备拔出：清理现场，宣告死亡
            // -------------------------------------------------------------
            color_printk(YELLOW, BLACK, "[Hub Port %d] Async: Device Disconnected!\n", port_num);
            port->state = PORT_STATE_DISCONNECTED;
            // TODO: 调用 Disable Slot 命令，解绑底层驱动
            return;
        }
    }

    // 🎯 动作 C：复位接力棒！(接收 C_RESET 或 C_BH_RESET 硬件回执)
    if ((init_port_status & USB_PORT_STAT_C_RESET) || (init_port_status & USB3_PORT_STAT_C_BH_RESET)) {

        // 翻开备忘录对账：我确实在等复位吗？
        if (port->state == PORT_STATE_WAITING_HOT_RESET ||
            port->state == PORT_STATE_WAITING_WARM_RESET) {

            // 最终检阅：复位完成了，硬件真的 Enable 且处于可用状态了吗？
            boolean is_30 = (udev->port_speed > USB_SPEED_HIGH);
            boolean is_enabled = (init_port_status & USB_PORT_STAT_ENABLE) != 0;
            boolean is_u0 = ((init_port_status & USB3_PORT_STAT_LINK_MASK) == USB3_PORT_LINK_U0);

            if (is_enabled && (!is_30 || is_u0)) {
                // 提取设备真实速度 (此时取出的速度是绝对准确的)
                uint32 raw_speed = is_30 ? ((init_port_status & USB3_PORT_STAT_SPEED_MASK) >> 10)
                                          : (init_port_status & USB2_PORT_STAT_SPEED_MASK);

                color_printk(GREEN, BLACK, "[Hub Port %d] Async: Reset Success! Speed: %#x. Ready for Enum!\n", port_num, raw_speed);

                // 📝 状态切入稳态
                port->state = PORT_STATE_ENABLED;

                // 💥 真正的异步枚举动作在这里发生：
                // TODO: 组装一个 Setup_Packet 为 SET_ADDRESS 的 URB，
                // 挂载下一步回调函数后，下发给 xHCI 命令环！
                usb_hub_port_eunm(udev, port_num);

            } else {
                color_printk(RED, BLACK, "[Hub Port %d] Async: Reset finished but port dead!\n", port_num);
                port->state = PORT_STATE_DISCONNECTED; // 救不回来，放弃治疗
            }
        }
    }

    // 💤 动作 D：链路与电源状态突变处理 (包含主动唤醒与被动死锁)
    if (!(init_port_status & USB_PORT_STAT_C_CONNECTION)) {

        // 🚀 USB 3.0 高级链路状态机
        if (udev->port_speed > USB_SPEED_HIGH && (init_port_status & USB3_PORT_STAT_C_LINK_STATE)) {
            uint32 current_link = init_port_status & USB3_PORT_STAT_LINK_MASK;

            if (port->state == PORT_STATE_WAITING_LINK_CHANGE) {
                // 情景 1：我们主动下发的链路跳转指令完成了
                if (current_link == USB3_PORT_LINK_U3) {
                    port->state = PORT_STATE_SUSPENDED;
                } else {
                    port->state = PORT_STATE_ENABLED;
                }
            } else if (port->state == PORT_STATE_ENABLED && current_link == USB3_PORT_LINK_SS_INACTIVE) {
                // 情景 2：纯被动物理塌方！正在飙数据，突然死锁了
                color_printk(RED, BLACK, "[Hub 3.x Port %d] PASSIVE CRASH! Deploying Emergency Hammer...\n", port_num);
                usb_hub_port_bh_reset(udev, port_num);
                port->state = PORT_STATE_WAITING_WARM_RESET; // 切入大锤等待态
            } else if (port->state == PORT_STATE_SUSPENDED && current_link == USB3_PORT_LINK_U0) {
                // 情景 3：设备被外部唤醒 (Remote Wakeup)
                color_printk(GREEN, BLACK, "[Hub 3.x Port %d] Woke up from U3!\n", port_num);
                port->state = PORT_STATE_ENABLED;
                // TODO: 通知该设备的驱动程序恢复 I/O
            }
        }

        // 🐢 USB 2.0 传统电源状态机
        if (udev->port_speed <= USB_SPEED_HIGH) {
            if (init_port_status & USB2_PORT_STAT_C_SUSPEND) {
                // 无论主动还是被动，C_SUSPEND 都意味着唤醒完成
                if (port->state == PORT_STATE_WAITING_RESUME || port->state == PORT_STATE_SUSPENDED) {
                    color_printk(GREEN, BLACK, "[Hub 2.0 Port %d] Woke up from Suspend!\n", port_num);
                    port->state = PORT_STATE_ENABLED;
                    // TODO: 恢复外设驱动工作
                }
            }
            if (init_port_status & USB2_PORT_STAT_C_ENABLE) {
                // 2.0 被动断连报警 (EMI 干扰或物理层错误)
                if (port->state == PORT_STATE_ENABLED) {
                    color_printk(RED, BLACK, "[Hub 2.0 Port %d] Unexpected Disable! Attempting Rescue...\n", port_num);
                    usb_hub_port_reset(udev, port_num);
                    port->state = PORT_STATE_WAITING_HOT_RESET; // 尝试盲拉一把
                }
            }
        }
    }
}

//hub驱动
int32 usb_hub_probe(usb_if_t *uif, usb_id_t *uid) {
    usb_dev_t *udev = uif->udev;
    usb_hub_t *hub = kzalloc(sizeof(usb_hub_t));
    hub->uif = uif;
    hub->port_status = kzalloc_dma(sizeof(uint32));
    hub->port_bitmap_status = kzalloc_dma(32);
    udev->is_hub = TRUE;
    udev->drv_data = hub;
    usb_if_alt_t *if_alt = NULL;

    if (udev->port_speed > USB_SPEED_HIGH) { //1. 3.0hub 初始化分支
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
        hub->ports = kzalloc((udev->hub_num_ports + 1) * sizeof(usb_hub_port_t));
        for (uint8 i = 1; i <= udev->hub_num_ports; i++) {
            hub->ports[i].port_num = i;
            uint16 removable_bitmap = hub30_desc->device_removable;
            hub->ports[i].is_removable = !((removable_bitmap >> i) & 1);
        }
    } else {//1. 2.0hub 初始化分支
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
        hub->ports = kzalloc((udev->hub_num_ports + 1) * sizeof(usb_hub_port_t));
        for (uint8 port_num = 1; port_num <= udev->hub_num_ports; port_num++) {
            hub->ports[port_num].port_num = port_num;
            uint8 byte_idx = port_num / 8;
            uint8 bit_idx = port_num % 8;
            hub->ports[port_num].is_removable = !((hub20_desc->device_removable[byte_idx] >> bit_idx) & 1);
        }
    }

    //2.设置udev为hub模式
    usb_ctx_slot_cfg(udev);

    //3.启用接口
    usb_ep_t *ep1 = &if_alt->eps[0];
    ep1->ring_max_trbs = 32;
    usb_enable_alt_if(if_alt);

    //4.所有端口上电
    for (uint8 port_num = 1; port_num <= udev->hub_num_ports; port_num++) {
        usb_hub_port_power_on(udev, port_num);
    }

    //5.等待100毫秒等待hub物理状态稳定
    // uint32 times = 0x5000000;
    // while (times) {
    //     times--;
    //     asm_pause();
    // }

    //6.第一次初始化hub后手动扫描每个端口是否有设备防止遗漏
    for (uint8 port_num = 1; port_num <= udev->hub_num_ports; port_num++) {
        usb_hub_process_port_event(udev,port_num);
    }

    //7.配置好中断 URB,提交队列后续有设备插入拔出等异步实现
    hub->int_urb = usb_alloc_urb();
    usb_fill_int_urb(hub->int_urb, udev, ep1, hub->port_bitmap_status, ep1->max_packet_size,ep1->interval);
    usb_submit_urb(hub->int_urb);

    while (hub->int_urb->is_done == FALSE) {
        asm_pause();
    }

    color_printk(RED,BLACK, "usb_hub bitmap:%#x  \n", hub->port_bitmap_status[0]);



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
