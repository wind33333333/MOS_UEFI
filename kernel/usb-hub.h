#pragma once
#include "moslib.h"
#include "usb-core.h"

/**
 * @brief USB 端口特征选择器 (Port Feature Selectors)
 * 用于 usb_set_port_feature 和 usb_clear_port_feature 等标准控制请求。
 * ⚠️ 架构师提示：不是所有的 Feature 都能被 Set 或 Clear，注意看注释里的权限说明！
 */
typedef enum {
    // ==========================================
    // 🎯 端口基础控制与状态 (常规 Feature)
    // ==========================================

    // [只读] 物理连接状态。插拔由用户决定，主机无法 Set 或 Clear 此特征。
    USB_PORT_FEAT_CONNECTION     = 0,

    // [可 Clear] 端口启用状态。
    // - SetFeature: 无效！必须通过发送 RESET 指令来让硬件自动启用。
    // - ClearFeature: 强行禁用该端口（相当于逻辑上拔掉设备）。
    USB_PORT_FEAT_ENABLE         = 1,

    // [可 Set / 可 Clear] 端口挂起（省电休眠）。
    // - SetFeature: 强制端口进入低功耗 Suspend 模式。
    // - ClearFeature: 向端口发送唤醒信号 (Resume)。
    USB_PORT_FEAT_SUSPEND        = 2,

    // [只读] 实时过流状态。由物理电路决定，主机无法主动 Set 或 Clear。
    USB_PORT_FEAT_OVER_CURRENT   = 3,

    // [核心控制 - 可 Set] 端口复位。
    // - SetFeature: 🌟 对端口发射复位电平！这是发现新设备插入后，枚举流程的绝对第一步！
    // - 硬件复位完成后，会自动清除此状态，并触发 C_RESET 中断。
    USB_PORT_FEAT_RESET          = 4,

    // [核心控制 - 可 Set / 可 Clear] 端口电源。
    // - SetFeature: 🌟 闭合物理继电器，给该端口供电（VBUS 上电）。初始化 Hub 必备！
    // - ClearFeature: 断开供电。
    USB_PORT_FEAT_POWER          = 8,

    // [只读] 设备速度指示位。硬件自动探测，主机无法更改。
    USB_PORT_FEAT_LOWSPEED       = 9,
    USB_PORT_FEAT_HIGHSPEED      = 10,

    // ==========================================
    // ⚠️ 端口状态变化擦除 (Clear Feature 专属)
    // 你的驱动收到 0x81 中断后，必须向硬件发送以下指令以“确认签收”，
    // 否则 Hub 会被卡死，持续疯狂向主机发送中断风暴！
    // ==========================================

    // [擦除确认] 确认已收到“发生物理插拔”的中断
    USB_PORT_FEAT_C_CONNECTION   = 16,

    // [擦除确认] 确认已收到“端口因严重错误被强制禁用”的中断
    USB_PORT_FEAT_C_ENABLE       = 17,

    // [擦除确认] 确认已收到“设备唤醒完成”的中断
    USB_PORT_FEAT_C_SUSPEND      = 18,

    // [擦除确认] 确认已收到“发生短路或短路解除”的中断
    USB_PORT_FEAT_C_OVER_CURRENT = 19,

    // [擦除确认] 确认已收到“复位彻底完成”的中断！
    // 清除这个标志后，你就可以放心地去给设备分配地址 (SetAddress) 了。
    USB_PORT_FEAT_C_RESET        = 20

} usb_port_feature_e;

// ==========================================
// 📡 USB 端口状态与变化掩码 (32 位直读版)
// 配合 usb_get_port_status 返回的 4 字节数据直接使用。
// 用法：if (status & USB_PORT_STAT_C_CONNECTION) { ... }
// ==========================================

// ---------------------------------------------------------
// 🟢 低 16 位：wPortStatus (当前物理状态的实时快照)
// ---------------------------------------------------------
#define USB_PORT_STAT_CONNECTION    0x00000001 // [连接状态] 1 = 物理接口上有设备插入
#define USB_PORT_STAT_ENABLE        0x00000002 // [启用状态] 1 = 端口已就绪，可传输数据
#define USB_PORT_STAT_SUSPEND       0x00000004 // [挂起状态] 1 = 端口处于选择性挂起模式
#define USB_PORT_STAT_OVERCURRENT   0x00000008 // [过流状态] 1 = 端口发生短路或电流超载！
#define USB_PORT_STAT_RESET         0x00000010 // [复位状态] 1 = 端口正在被持续拉低进行硬复位

#define USB_PORT_STAT_POWER         0x00000100 // [供电状态] 1 = 端口已通电

// 🌟 设备速度标识 (占据 Bit 9, 10, 11)
// 0x00000000 = Full-Speed (全速, 12Mbps)
#define USB_PORT_STAT_LOW_SPEED     0x00000200 // Low-Speed  (低速, 1.5Mbps)
#define USB_PORT_STAT_HIGH_SPEED    0x00000400 // High-Speed (高速, 480Mbps)
#define USB_PORT_STAT_SUPER_SPEED   0x00000C00 // SuperSpeed (USB 3.0, 5Gbps)


// ---------------------------------------------------------
// 🔴 高 16 位：wPortChange (中断唤醒的罪魁祸首)
// 💡 注意：如果你检测到了这里的 1，必须发送对应的 ClearFeature 指令！
// 例如：USB_PORT_STAT_C_CONNECTION 对应发 ClearPortFeature(16)
// ---------------------------------------------------------
#define USB_PORT_STAT_C_CONNECTION  0x00010000 // [插拔事件] 发生了物理插入或拔出
#define USB_PORT_STAT_C_ENABLE      0x00020000 // [断连事件] 端口因错误被硬件强制 Disable
#define USB_PORT_STAT_C_SUSPEND     0x00040000 // [唤醒事件] 设备睡眠苏醒完成
#define USB_PORT_STAT_C_OVERCURRENT 0x00080000 // [过流突变] 发生短路，或短路已排除
#define USB_PORT_STAT_C_RESET       0x00100000 // [复位完成] PORT_RESET 指令执行完毕


/**
 * @brief Hub 的单个下游端口状态追踪器
 * 这个结构体完全由软件维护，是对物理端口状态的抽象。
 */
typedef struct {
    uint8  port_no;             // 端口号 (1-based，从 1 开始)
    boolean is_removable; // 🌟 新增：这个设备是热插拔的，还是主板焊死的？

    // 缓存最后一次通过 usb_get_port_status 读到的状态
    uint32 current_status;

    // 拓扑树指针：如果这个端口上插了东西，指向那个设备的实例
    // 如果端口是空的，这里必须是 NULL
    usb_dev_t *child_dev;

} hub_port_t;



/**
 * @brief USB Hub 驱动私有上下文控制块 (Hub Context)
 * 无论是 2.0 还是 3.0 的 Hub，在软件层面上都统一抽象为这个结构体。
 */
typedef struct usb_hub_t{
    // ==========================================
    // 1. 向上挂载 (驱动树绑定)
    // ==========================================
    usb_if_t *uif; // 绑定的接口对象 (Hub 驱动的主场)

    // ==========================================
    // 2. 硬件性格字典 (从描述符中解析出的精简特征)
    // 提前解析好，避免每次操作都去读原生描述符
    // ==========================================
    boolean   is_usb3;          // 标记位：1 = SuperSpeed(3.0), 0 = High/Full/Low Speed(2.0/1.1)
    uint8   num_ports;          // 下游物理端口总数 (极其重要，决定了数组大小和循环次数)
    uint32  power_delay_ms;     // 端口上电后的等待时间 (描述符中的 bPwrOn2PwrGood * 2)

    boolean is_individual_pwr;  // 是否支持独立上电？
    boolean is_individual_ocp;  // 是否支持独立过流保护？
    uint8   tt_think_time;      // TT 思考时间 (仅 USB 2.0 有效，给底层 xHCI 调度器用)

    // ==========================================
    // 3. 中断雷达 (热插拔监听引擎)
    // ==========================================
    usb_urb_t      *irq_urb;    // 常驻内存的中断请求面单 (轮询端点 0x81)
    uint8          *irq_buffer; // DMA 内存指针，用于接收发生状态变化的端口位图 (Bitmap)
    uint16         irq_buf_len; // 缓冲区的长度：通常为 (num_ports / 8) + 1 字节

    // ==========================================
    // 4. 下游拓扑阵列
    // ==========================================
    // 💡 架构师注：不要在这里写死 hub_port_t ports[255]！
    // 既然知道了 num_ports，在 probe 初始化时动态 kmalloc 数组，能省下大量内核内存。
    hub_port_t *ports;

    // ==========================================
    // 6. 双总线架构伴侣 (Peer Hub)
    // ==========================================
    // 如果这是一个 USB 3.0 物理 Hub 的 2.0 灵魂，这个指针指向它的 3.0 灵魂。
    // 反之亦然。如果只是普通的 2.0 Hub，这里为 NULL。
    struct usb_hub_t *companion_hub;

    // ==========================================
    // 5. 并发与同步锁 (SMP 安全必备)
    // ==========================================
    // 防止极其恶劣的物理情况：用户像疯子一样在 1 号口和 2 号口同时狂插拔 U 盘。
    // 这会导致中断函数重入，必须用自旋锁或互斥锁保护内部状态。
    uint32 lock;

} usb_hub_t;