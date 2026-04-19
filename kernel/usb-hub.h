#pragma once
#include "moslib.h"
#include "usb-core.h"

/**
 * @brief USB 端口特征选择器 (Port Feature Selectors)
 * 用于 usb_set_port_feature 和 usb_clear_port_feature 等标准请求
 */
typedef enum {
    // ==========================================
    // 🎯 端口基础控制 (常规 Feature)
    // ==========================================
    USB_PORT_FEAT_CONNECTION     = 0,
    USB_PORT_FEAT_ENABLE         = 1,
    USB_PORT_FEAT_SUSPEND        = 2,
    USB_PORT_FEAT_OVER_CURRENT   = 3,
    USB_PORT_FEAT_RESET          = 4,   // 复位端口 (插入新设备时必备)

    USB_PORT_FEAT_POWER          = 8,   // 给端口上电 (极其重要)
    USB_PORT_FEAT_LOWSPEED       = 9,
    USB_PORT_FEAT_HIGHSPEED      = 10,

    // ==========================================
    // ⚠️ 端口状态变化擦除 (Clear Feature 专属)
    // 用于向硬件确认已收到中断，擦除对应的 Change 标志位
    // ==========================================
    USB_PORT_FEAT_C_CONNECTION   = 16,  // 清除“连接状态改变”标志位
    USB_PORT_FEAT_C_ENABLE       = 17,
    USB_PORT_FEAT_C_SUSPEND      = 18,
    USB_PORT_FEAT_C_OVER_CURRENT = 19,
    USB_PORT_FEAT_C_RESET        = 20   // 清除“复位完成”标志位

} usb_port_feature_e;

// ==========================================
// 📡 端口状态掩码 (Port Status Bitmasks)
// 用于解析 usb_get_port_status 返回的 4 字节数据
// ==========================================
// 低 16 位：当前状态 (wPortStatus)
#define USB_PORT_STAT_CONNECTION    0x0001 // 1 = 有设备插着
#define USB_PORT_STAT_ENABLE        0x0002 // 1 = 端口已启用
#define USB_PORT_STAT_SUSPEND       0x0004
#define USB_PORT_STAT_OVERCURRENT   0x0008
#define USB_PORT_STAT_RESET         0x0010 // 1 = 端口正在复位中
#define USB_PORT_STAT_POWER         0x0100 // 1 = 端口已上电
#define USB_PORT_STAT_LOW_SPEED     0x0200
#define USB_PORT_STAT_HIGH_SPEED    0x0400
#define USB_PORT_STAT_SUPER_SPEED   0x0C00 // USB 3.0 专属速度标志

// 高 16 位：变化标志 (wPortChange)
// 如果这些位是 1，代表发生了中断，你处理完后必须用 ClearFeature 把它清 0！
#define USB_PORT_STAT_C_CONNECTION  0x00010000 // 发生过插拔事件！
#define USB_PORT_STAT_C_ENABLE      0x00020000
#define USB_PORT_STAT_C_SUSPEND     0x00040000
#define USB_PORT_STAT_C_OVERCURRENT 0x00080000
#define USB_PORT_STAT_C_RESET       0x00100000 // 复位已完成！


/**
 * @brief Hub 的单个下游端口状态追踪器
 * 这个结构体完全由软件维护，是对物理端口状态的抽象。
 */
typedef struct {
    uint8  port_no;             // 端口号 (1-based，从 1 开始)
    boolean is_removable; // 🌟 新增：这个设备是热插拔的，还是主板焊死的？

    // 缓存最后一次通过 usb_get_port_status 读到的状态
    uint16 current_status;      // 当前状态掩码 (wPortStatus)
    uint16 current_change;      // 变化标志掩码 (wPortChange)

    // 拓扑树指针：如果这个端口上插了东西，指向那个设备的实例
    // 如果端口是空的，这里必须是 NULL
    struct usb_dev *child_dev;

} hub_port_t;



/**
 * @brief USB Hub 驱动私有上下文控制块 (Hub Context)
 * 无论是 2.0 还是 3.0 的 Hub，在软件层面上都统一抽象为这个结构体。
 */
typedef struct {
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
    struct usb_urb *irq_urb;    // 常驻内存的中断请求面单 (轮询端点 0x81)
    uint8          *irq_buffer; // DMA 内存指针，用于接收发生状态变化的端口位图 (Bitmap)
    uint16         irq_buf_len; // 缓冲区的长度：通常为 (num_ports / 8) + 1 字节

    // ==========================================
    // 4. 下游拓扑阵列
    // ==========================================
    // 💡 架构师注：不要在这里写死 hub_port_t ports[255]！
    // 既然知道了 num_ports，在 probe 初始化时动态 kmalloc 数组，能省下大量内核内存。
    hub_port_t *ports;

    // ==========================================
    // 5. 并发与同步锁 (SMP 安全必备)
    // ==========================================
    // 防止极其恶劣的物理情况：用户像疯子一样在 1 号口和 2 号口同时狂插拔 U 盘。
    // 这会导致中断函数重入，必须用自旋锁或互斥锁保护内部状态。
    uint32 lock;

} usb_hub_t;