#pragma once
#include "moslib.h"
#include "usb-core.h"

// ============================================================================
// 🎛️ USB 端口特征选择器 (Port Feature Selectors - 纯动作指令版)
// 用于 SetPortFeature 和 ClearPortFeature 发送控制命令 (wValue 字段)。
// 💡 已剔除所有只读/无效的占位符，这里的每一个宏都能真实改变硬件状态！
// ============================================================================

// =========================================================================
// 🎯 1. 通用核心动作指令 (2.0 / 3.0 皆可)
// =========================================================================
#define USB_HUB_PORT_FEAT_ENABLE                1   // [仅 Clear] 强行物理切断数据通道（软件拔出）
#define USB_HUB_PORT_FEAT_SUSPEND               2   // [可 Set/Clear] 传统挂起/唤醒模式 (主要用于 2.0)
#define USB_HUB_PORT_FEAT_RESET                 4   // [可 Set] 发起端口复位 (2.0拉低线 / 3.0热复位)
#define USB_HUB_PORT_FEAT_POWER                 8   // [可 Set/Clear] 控制端口 5V 继电器供电开关

// =========================================================================
// 🐢 2. USB 2.0 独占动作指令
// =========================================================================
#define USB_HUB_PORT_FEAT_TEST                  21  // [可 Set] 强制进入电气合规性测试模式
#define USB_HUB_PORT_FEAT_INDICATOR             22  // [可 Set] 控制物理端口 LED 指示灯

// =========================================================================
// 🚀 3. USB 3.0+ 超高速独占动作指令
// =========================================================================
#define USB_HUB_PORT_FEAT_LINK_STATE            5   // [可 Set/Clear] 强行修改超高速链路状态 (如写入 U0/U3)
#define USB_HUB_PORT_FEAT_U1_TIMEOUT            23  // [可 Set] 设置 U1 低功耗待机超时阈值
#define USB_HUB_PORT_FEAT_U2_TIMEOUT            24  // [可 Set] 设置 U2 低功耗待机超时阈值
#define USB_HUB_PORT_FEAT_REMOTE_WAKE_MASK      27  // [可 Set/Clear] 配置远程唤醒过滤掩码
#define USB_HUB_PORT_FEAT_BH_PORT_RESET         28  // [可 Set] 🔥 发射物理大锤硬复位 (Warm Reset)
#define USB_HUB_PORT_FEAT_FORCE_LINKPM_ACCEPT   30  // [可 Set] 强迫低功耗链路接受 PM 协商

// =========================================================================
// 🔴 4. 事件报警擦除指令 (仅用于 ClearFeature，核心排雷动作)
// 当底层发生插拔或错误，主板会产生中断。你必须发这些指令去把红灯按灭！
// =========================================================================
#define USB_HUB_PORT_FEAT_C_CONNECTION          16  // 擦除“发生物理插拔”报警
#define USB_HUB_PORT_FEAT_C_ENABLE              17  // 擦除“因物理层严重错误被强制关闭”报警
#define USB_HUB_PORT_FEAT_C_SUSPEND             18  // 擦除“挂起苏醒完成”报警
#define USB_HUB_PORT_FEAT_C_OVER_CURRENT        19  // 擦除“短路过流”报警
#define USB_HUB_PORT_FEAT_C_RESET               20  // 擦除“标准复位完成”报警
#define USB_HUB_PORT_FEAT_C_PORT_LINK_STATE     25  // [3.0独占] 擦除“链路状态突变”报警
#define USB_HUB_PORT_FEAT_C_PORT_CONFIG_ERROR   26  // [3.0独占] 擦除“物理层配置错误”报警
#define USB_HUB_PORT_FEAT_C_BH_PORT_RESET       29  // [3.0独占] 擦除“大锤复位完成”报警


// ============================================================================
// 📊 USB 端口状态解析掩码 (Port Status & Change Masks)
// 用于解析 GetPortStatus 返回的 4 字节“体检报告”。
// 💡 这里必须保留所有标志位（包括只读位），因为内核需要靠它们感知物理世界。
// ============================================================================

// =========================================================================
// 🟢 [通用] 低 16 位：wPortStatus (实时物理状态)
// =========================================================================
#define USB_HUB_PORT_STAT_CONNECTION      0x00000001 // [连接状态] 1 = 物理接口上有设备插入
#define USB_HUB_PORT_STAT_ENABLE          0x00000002 // [启用状态] 1 = 端口已就绪，数据流通道已打开
#define USB_HUB_PORT_STAT_OVERCURRENT     0x00000008 // [过流状态] 1 = 端口发生短路或电流超载！
#define USB_HUB_PORT_STAT_RESET           0x00000010 // [复位状态] 1 = 端口正在执行复位流 

// =========================================================================
// 🔴 [通用] 高 16 位：wPortChange (事件变更报警标志)
// =========================================================================
#define USB_HUB_PORT_STAT_C_CONNECTION    0x00010000 // [插拔事件] 发生了物理插入或拔出 
#define USB_HUB_PORT_STAT_C_OVERCURRENT   0x00080000 // [过流突变] 发生短路，或短路已排除 
#define USB_HUB_PORT_STAT_C_RESET         0x00100000 // [复位完成] 端口标准复位流程成功结束 

// =========================================================================
// 🟢 [2.0独占] 低 16 位：wPortStatus (实时物理状态)
// =========================================================================
#define USB2_HUB_PORT_STAT_SUSPEND        0x00000004 // [挂起状态] 1 = 端口处于传统选择性挂起模式
#define USB2_HUB_PORT_STAT_POWER          0x00000100 // [供电状态] ★ 2.0物理供电标志位于 Bit 8

// 🌟 USB 2.0 专属速度判定：提取 Bit 10:9
#define USB2_HUB_PORT_STAT_SPEED_MASK     0x00000600
#define USB2_HUB_PORT_STAT_FULL_SPEED     0x00000000 // Bit 9=0, Bit 10=0 (全速 12Mbps)
#define USB2_HUB_PORT_STAT_LOW_SPEED      0x00000200 // Bit 9=1, Bit 10=0 (低速 1.5Mbps)
#define USB2_HUB_PORT_STAT_HIGH_SPEED     0x00000400 // Bit 9=0, Bit 10=1 (高速 480Mbps)

#define USB2_HUB_PORT_STAT_TEST           0x00000800 // Bit 11: 端口处于测试模式 (Compliance Test)
#define USB2_HUB_PORT_STAT_INDICATOR      0x00001000 // Bit 12: 端口指示灯控制已使能

// =========================================================================
// 🔴 [2.0独占] 高 16 位：wPortChange (事件变更报警标志)
// =========================================================================
#define USB2_HUB_PORT_STAT_C_ENABLE       0x00020000 // [断连事件] 端口因物理层严重错误被硬件强制 Disable 
#define USB2_HUB_PORT_STAT_C_SUSPEND      0x00040000 // [唤醒事件] 设备从传统挂起睡眠中苏醒完成 

// =========================================================================
// 🟢 [3.0独占] 低 16 位：wPortStatus (实时物理状态)
// =========================================================================
#define USB3_HUB_PORT_STAT_POWER          0x00000200 // [供电状态] ★ 3.0供电标志向高位漂移至 Bit 9

// 🌟 USB 3.0 灵魂核心：高级链路状态机枚举 (占据 Bit 8:5)
#define USB3_HUB_PORT_STAT_LINK_MASK      0x000001E0
#define USB3_HUB_PORT_LINK_U0             (0 << 5)   // 完美就绪 (Active)
#define USB3_HUB_PORT_LINK_U1             (1 << 5)   // 低功耗待机状态 1
#define USB3_HUB_PORT_LINK_U2             (2 << 5)   // 低功耗待机状态 2
#define USB3_HUB_PORT_LINK_U3             (3 << 5)   // Device Suspend (3.0 休眠)
#define USB3_HUB_PORT_LINK_SS_DISABLED    (4 << 5)   // 收发器管道被强行关闭
#define USB3_HUB_PORT_LINK_RX_DETECT      (5 << 5)   // 正在发射脉冲探测阻抗 (空闲)
#define USB3_HUB_PORT_LINK_SS_INACTIVE    (6 << 5)   // 💥 链路训练死锁！(需发大锤复位)
#define USB3_HUB_PORT_LINK_POLLING        (7 << 5)   // 正在进行高速眼图训练
#define USB3_HUB_PORT_LINK_RECOVERY       (8 << 5)   // 正在进行硬件自动修复

// 🌟 USB 3.x 世代多轨速率判定 (占据 Bit 12:10)
#define USB3_HUB_PORT_STAT_SPEED_MASK     0x00001C00
#define USB3_HUB_PORT_STAT_SPEED_5G       (0 << 10)  // USB 3.2 Gen1 (5 Gbps)
#define USB3_HUB_PORT_STAT_SPEED_10G      (1 << 10)  // USB 3.2 Gen2 (10 Gbps)
#define USB3_HUB_PORT_STAT_SPEED_20G      (2 << 10)  // USB 3.2 Gen2x2 (20 Gbps)

// =========================================================================
// 🔴 [3.0独占] 高 16 位：wPortChange (事件变更报警标志)
// =========================================================================
#define USB3_HUB_PORT_STAT_C_BH_RESET     0x00200000 // [大锤复位完成] Warm Reset 强刷成功
#define USB3_HUB_PORT_STAT_C_LINK_STATE   0x00400000 // [链路突变] 链路状态机发生跳转
#define USB3_HUB_PORT_STAT_C_CONFIG_ERR   0x00800000 // [物理报错] 严重串扰/配置错误


// ============================================================================
// 🚀 USB 3.0+ SetFeature(PORT_LINK_STATE) 专属动作指令
// ⚠️ 专属控制层，专门用来当做设置函数的参数 (填入 wIndex 高字节)，严禁与读取位图混用！
// ============================================================================

#define USB3_SET_LINK_U0            0  // [强制唤醒] 命令链路切回 U0 满血工作状态 (数据对流)
#define USB3_SET_LINK_U1            1  // [自动省电] 强制切入轻度时钟门控待机 (微秒级恢复)
#define USB3_SET_LINK_U2            2  // [自动省电] 强制切入深度模拟电路待机 (百微秒级恢复)
#define USB3_SET_LINK_U3            3  // [选择性挂起] 强制进入深度休眠 (Selective Suspend)

// --- 硬核物理层控制命令 ---
#define USB3_SET_LINK_SS_DISABLED   4  // [逻辑断开3.0] 强行关闭超高速物理收发器。
                                       // 🌟 奇效：此命令会逼迫该接口上的双模设备降级去跑 2.0 线路！
#define USB3_SET_LINK_RX_DETECT     5  // [强制重连] 强行解散当前的链路训练状态机，
                                       // 让端口退回到最原始的发射物理脉冲、重新探测外设阻抗的阶段。
#define USB3_SET_LINK_COMPLIANCE    15 // [合规测试] 强迫端口进入电气特性测试模式（出厂测眼图专属）



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
typedef struct usb_hub_port_t{
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


