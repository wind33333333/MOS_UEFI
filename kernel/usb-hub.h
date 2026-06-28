#pragma once
#include "moslib.h"
#include "usb-core.h"

/**
 * @brief USB 端口特征选择器 (Port Feature Selectors)
 * 用于 SetPortFeature 和 ClearPortFeature 等标准控制请求 (wValue 字段)。
 */
typedef enum : uint16 {
    // =========================================================================
    // 🎯 1. 2.0 / 3.0 通用基础控制与状态 (0 ~ 4, 8)
    // =========================================================================

    // [只读] 物理连接状态。插拔由用户决定，主机无法 Set/Clear。
    USB_PORT_FEAT_CONNECTION            = 0,

    // [可 Clear] 端口启用状态。
    // - SetFeature: 无效！必须发送 RESET 指令让硬件级链路训练自动启用。
    // - ClearFeature: 强行物理切断数据通道（相当于软件拔狗）。
    USB_PORT_FEAT_ENABLE                = 1,

    // [2.0 可Set/Clear] 传统挂起模式。
    // - 注意：3.0 废弃了此单独位，3.0 统一改走下面的 LINK_STATE (Selector 5)。
    USB_PORT_FEAT_SUSPEND               = 2,

    // [只读] 实时过流状态。由主板物理电路决定。
    USB_PORT_FEAT_OVER_CURRENT          = 3,

    // [核心控制 - 可 Set] 端口复位 (2.0拉低物理线/3.0发射超高速 Hot Reset)。
    // - 硬件复位完成后会自动清除此状态，并触发 C_RESET 变更。
    USB_PORT_FEAT_RESET                 = 4,

    // [核心控制 - 可 Set / 可 Clear] 端口电源开关。
    // - SetFeature: 闭合继电器放电上电 (VBUS 5V 正常)。初始化 Hub 必备！
    USB_PORT_FEAT_POWER                 = 8,

    // =========================================================================
    // 🐢 2. USB 2.0 纯血独占特征 (9, 10, 21, 22)
    // =========================================================================
    USB_PORT_FEAT_LOWSPEED              = 9,  // [只读] 低速指示 (废弃，交由状态位图提取)
    USB_PORT_FEAT_HIGHSPEED             = 10, // [只读] 高速指示 (废弃)
    USB_PORT_FEAT_TEST                  = 21, // [2.0独占 - 可Set] 强制端口进入电气合规性测试模式
    USB_PORT_FEAT_INDICATOR             = 22, // [2.0独占 - 可Set] 控制扩展坞上的物理端口LED指示灯

    // =========================================================================
    // 🚀 3. USB 3.0+ 超高速硬核链路控制 (5, 23, 24, 27, 28, 30)
    // =========================================================================

    // [3.0独占 - 可Set/Clear] 强行修改超高速物理链路状态。
    // - 比如写入 U0(唤醒), U3(挂起休眠)。取代了传统的 Selector 2。
    USB_PORT_FEAT_LINK_STATE            = 5,

    // [3.0独占 - 可Set] 设置超高速链路进入低功耗待机状态的超时阈值
    USB_PORT_FEAT_U1_TIMEOUT            = 23,
    USB_PORT_FEAT_U2_TIMEOUT            = 24,

    // [3.0独占 - 可Set/Clear] 配置端口对超高速远程唤醒事件的过滤掩码
    USB_PORT_FEAT_REMOTE_WAKE_MASK      = 27,

    // [3.0独占 - 可Set] 🔥 Bigger Hammer (Warm Reset) 物理大锤硬复位！
    // - 当端口死锁在 SS.Inactive 时，这是唯一能把它从硬件地狱里砸醒的指令！
    USB_PORT_FEAT_BH_PORT_RESET         = 28,

    // [3.0独占 - 可Set] 强迫低功耗链路必须接受主机的 PM 协商请求
    USB_PORT_FEAT_FORCE_LINKPM_ACCEPT   = 30,

    // =========================================================================
    // 🔴 4. 2.0 / 3.0 统一高位事件变更擦除 (16 ~ 20) -> ClearFeature 专属
    // =========================================================================
    USB_PORT_FEAT_C_CONNECTION          = 16, // 擦除“发生物理插拔”报警
    USB_PORT_FEAT_C_ENABLE              = 17, // 擦除“因物理层严重错误被强制关闭”报警
    USB_PORT_FEAT_C_SUSPEND             = 18, // 擦除“挂起苏醒完成”报警
    USB_PORT_FEAT_C_OVER_CURRENT        = 19, // 擦除“短路过流”报警
    USB_PORT_FEAT_C_RESET               = 20, // 擦除“标准复位完成”报警

    // =========================================================================
    // 💥 5. USB 3.0+ 超高速高位事件变更擦除 (25, 26, 29) -> ClearFeature 专属
    // =========================================================================

    // [3.0独占] 确认并清除“物理链路状态突变 (例如从 Rx.Detect 变成 U0)”报警
    USB_PORT_FEAT_C_PORT_LINK_STATE     = 25,

    // [3.0独占] 确认并清除“超高速物理层严重串扰/配置错误”报警
    USB_PORT_FEAT_C_PORT_CONFIG_ERROR   = 26,

    // [3.0独占] 确认并清除“物理大锤(Warm Reset)强刷复位完成”报警！
    // - 配合上一节读取到的 0x00300203 状态，你必须发这个指令确认，否则中断环立刻炸裂！
    USB_PORT_FEAT_C_BH_PORT_RESET       = 29

} usb_port_feature_e;


// =========================================================================
// 🟢 [通用] 低 16 位：wPortStatus (实时物理状态)
// =========================================================================
#define USB_PORT_STAT_CONNECTION      0x00000001 // [连接状态] 1 = 物理接口上有设备插入
#define USB_PORT_STAT_ENABLE          0x00000002 // [启用状态] 1 = 端口已就绪，数据流通道已打开
#define USB_PORT_STAT_OVERCURRENT     0x00000008 // [过流状态] 1 = 端口发生短路或电流超载！
#define USB_PORT_STAT_RESET           0x00000010 // [复位状态] 1 = 端口正在执行复位流 (2.0物理拉低/3.0热复位)

// =========================================================================
// 🔴 [通用] 高 16 位：wPortChange (事件变更报警) -> 统一清除指令
// =========================================================================
#define USB_PORT_STAT_C_CONNECTION    0x00010000 // [插拔事件] 发生了物理插入或拔出 (Clear Selector: 16)
#define USB_PORT_STAT_C_OVERCURRENT   0x00080000 // [过流突变] 发生短路，或短路已排除 (Clear Selector: 19)
#define USB_PORT_STAT_C_RESET         0x00100000 // [复位完成] 端口标准复位流程成功结束 (Clear Selector: 20)

// =========================================================================
// 🟢 [2.0独占] 低 16 位：wPortStatus (实时物理状态)
// =========================================================================
#define USB2_PORT_STAT_SUSPEND        0x00000004 // [挂起状态] 1 = 端口处于传统选择性挂起模式
#define USB2_PORT_STAT_POWER          0x00000100 // [供电状态] ★ 2.0物理供电标志位于 Bit 8

// 🌟 USB 2.0 专属速度判定：提取 Bit 10:9
#define USB2_PORT_STAT_SPEED_MASK     0x00000600
#define USB2_PORT_STAT_FULL_SPEED     0x00000000 // Bit 9=0, Bit 10=0 (全速 12Mbps)
#define USB2_PORT_STAT_LOW_SPEED      0x00000200 // Bit 9=1, Bit 10=0 (低速 1.5Mbps)
#define USB2_PORT_STAT_HIGH_SPEED     0x00000400 // Bit 9=0, Bit 10=1 (高速 480Mbps)

// 🔍 2.0 边缘工业控制位
#define USB2_PORT_STAT_TEST           0x00000800 // Bit 11: 端口处于测试模式 (Compliance Test)
#define USB2_PORT_STAT_INDICATOR      0x00001000 // Bit 12: 端口指示灯控制已使能

// =========================================================================
// 🔴 [2.0独占] 高 16 位：wPortChange (事件变更报警)
// =========================================================================
#define USB2_PORT_STAT_C_ENABLE       0x00020000 // [断连事件] 端口因物理层严重错误被硬件强制 Disable (Clear Selector: 17)
#define USB2_PORT_STAT_C_SUSPEND      0x00040000 // [唤醒事件] 设备从传统挂起睡眠中苏醒完成 (Clear Selector: 18)

// =========================================================================
// 🟢 [3.0独占] 低 16 位：wPortStatus (实时物理状态)
// =========================================================================
#define USB3_PORT_STAT_POWER          0x00000200 // [供电状态] ★ 3.0供电标志向高位漂移至 Bit 9

// 🌟 USB 3.0 灵魂核心：高级链路状态机枚举 (占据 Bit 8:5)
#define USB3_PORT_STAT_LINK_MASK      0x000001E0
#define USB3_PORT_LINK_U0             (0 << 5)   // 0x0000: 完美就绪 (Active)，正在高速狂飙数据
#define USB3_PORT_LINK_U1             (1 << 5)   // 0x0020: 低功耗待机状态 1 (快恢复)
#define USB3_PORT_LINK_U2             (2 << 5)   // 0x0040: 低功耗待机状态 2 (慢恢复)
#define USB3_PORT_LINK_U3             (3 << 5)   // 0x0060: Device Suspend (3.0 强行挂起休眠状态)
#define USB3_PORT_LINK_SS_DISABLED    (4 << 5)   // 0x0080: 超高速物理收发器管道被强行关闭
#define USB3_PORT_LINK_RX_DETECT      (5 << 5)   // 0x00A0: 正在发射物理脉冲探测对端接收阻抗 (空闲无插入)
#define USB3_PORT_LINK_SS_INACTIVE    (6 << 5)   // 0x00C0: 💥 链路训练死锁/彻底失效！(必须发射大锤复位拯救)
#define USB3_PORT_LINK_POLLING        (7 << 5)   // 0x00E0: 正在进行高速眼图与电平握手训练
#define USB3_PORT_LINK_RECOVERY       (8 << 5)   // 0x0100: 链路由于流控错误正在进行硬件自动修复

// 🌟 USB 3.x 世代多轨速率判定 (占据 Bit 12:10)
#define USB3_PORT_STAT_SPEED_MASK     0x00001C00
#define USB3_PORT_STAT_SPEED_5G       (0 << 10)  // USB 3.2 Gen1 (原 USB 3.0 - 5 Gbps)
#define USB3_PORT_STAT_SPEED_10G      (1 << 10)  // USB 3.2 Gen2 (原 USB 3.1 - 10 Gbps)
#define USB3_PORT_STAT_SPEED_20G      (2 << 10)  // USB 3.2 Gen2x2 (双通道极速 - 20 Gbps)

// =========================================================================
// 🔴 [3.0独占] 高 16 位：wPortChange (事件变更报警)
// =========================================================================
#define USB3_PORT_STAT_C_BH_RESET     0x00200000 // [大锤复位完成] Warm Reset 粗暴物理强刷成功结束 (Clear Selector: 28)
#define USB3_PORT_STAT_C_LINK_STATE   0x00400000 // [链路突变] 链路状态机发生跳转 (例如从 Rx.Detect 变为 U0) (Clear Selector: 25)
#define USB3_PORT_STAT_C_CONFIG_ERR   0x00800000 // [物理报错] 严重的超高速物理层级阻抗配置/串扰错误 (Clear Selector: 26)



// =========================================================================
// 📝 前置定义：端口软件状态备忘录 (时间胶囊)
// 需要存在于你的 hub_port_t 或类似的端口结构体中
// =========================================================================
typedef enum : uint8 {
    // --- 基础稳态 ---
    PORT_STATE_DISCONNECTED = 0,   // 空闲/已物理拔出
    PORT_STATE_ENABLED,            // 正常工作中 (U0 / 处于活动状态)
    PORT_STATE_SUSPENDED,          // 深度休眠中 (U3 / Suspend)

    // --- 异步过渡态 (代替死等，用于接住硬件中断回执) ---
    PORT_STATE_WAITING_HOT_RESET,  // 等待标准复位完成 (等 C_RESET)
    PORT_STATE_WAITING_WARM_RESET, // 等待大锤强刷完成 (等 C_BH_RESET)
    PORT_STATE_WAITING_RESUME,     // 等待物理唤醒完成 (等 C_SUSPEND)
    PORT_STATE_WAITING_LINK_CHANGE // 等待链路跃迁完成 (等 C_LINK_STATE)
} port_state_e;



/**
 * @brief Hub 的单个下游端口状态追踪器
 * 这个结构体完全由软件维护，是对物理端口状态的抽象。
 */
typedef struct {
    uint8  port_num;       // 端口号 (1-based，从 1 开始)
    boolean is_removable; // 🌟 新增：这个设备是热插拔的，还是主板焊死的？

    port_state_e state;

    // 拓扑树指针：如果这个端口上插了东西，指向那个设备的实例
    // 如果端口是空的，这里必须是 NULL
    usb_dev_t *child_dev;

} usb_hub_port_t;



typedef struct usb_hub_t {
    // 1. 向上挂载
    usb_if_t           *uif;

    // 2. 硬件性格字典 (仅保留 Hub 专有的高级控制标志)
    uint32             power_delay_ms;  //上电等待时间
    boolean            is_individual_pwr;  //是否独立供电控制
    boolean            is_individual_ocp;  //是否独立过流保护

    // 3. 中断雷达 (运行时内存)
    usb_urb_t          *irq_urb;
    uint8              *irq_buffer;
    uint16             irq_buf_len;

    // 4. 下游拓扑阵列 (运行时动态分配)
    usb_hub_port_t         *ports;

    // 5. 双总线伴侣
    struct usb_hub_t   *companion_hub;

    union {
        usb_hub30_desc_t *hub30_desc;
        usb_hub20_desc_t *hub20_desc;
    };

    // 6. 并发控制
    uint32             lock;
    uint32             *port_status;
    uint8              *port_bitmap_status;
    usb_urb_t          *int_urb;

} usb_hub_t;

void usb_hub_process_port_event(usb_dev_t *udev, uint8 port_num);


