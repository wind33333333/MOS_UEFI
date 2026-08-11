#pragma once

#include "moslib.h"

#pragma pack(push, 1)

//========================== USB描述符 =========================

/**
 * @brief USB 描述符类型 (naos 完美多态版)
 * 警告：0x20 及以上的宏存在值域重叠，解析时必须结合 Interface Class 上下文！
 */
typedef enum : uint8 {
    // ==========================================
    // 第一阵营：标准描述符 (Standard) - 绝对领域，无多态歧义
    // 适用范围：所有 USB 设备的通用枚举阶段
    // ==========================================
    USB_DESC_TYPE_DEVICE                = 0x01, // 设备描述符
    USB_DESC_TYPE_CONFIG                = 0x02, // 配置描述符
    USB_DESC_TYPE_STRING                = 0x03, // 字符串描述符
    USB_DESC_TYPE_INTERFACE             = 0x04, // 接口描述符
    USB_DESC_TYPE_ENDPOINT              = 0x05, // 端点描述符
    USB_DESC_TYPE_DEVICE_QUALIFIER      = 0x06, // 设备限定描述符 (USB 2.0 专属)
    USB_DESC_TYPE_OTHER_SPEED_CONFIG    = 0x07, // 其他速度配置描述符
    USB_DESC_TYPE_INTERFACE_POWER       = 0x08, // 接口电源描述符
    USB_DESC_TYPE_OTG                   = 0x09, // OTG 描述符
    USB_DESC_TYPE_DEBUG                 = 0x0A, // 调试描述符
    USB_DESC_TYPE_INTERFACE_ASSOCIATION = 0x0B, // 接口关联描述符 (IAD)
    USB_DESC_TYPE_BOS                   = 0x0F, // 二进制对象存储描述符 (USB 3.0+)
    USB_DESC_TYPE_DEVICE_CAPABILITY     = 0x10, // 设备能力描述符
    USB_DESC_TYPE_SS_ENDPOINT_COMPANION = 0x30, // 超高速端点伴随描述符 (USB 3.0 极度关键)
    USB_DESC_TYPE_SSP_ISOCH_COMPANION   = 0x31, // 超高速+ 等时端点伴随描述符

    // ==========================================
    // 第二阵营：类特定描述符 (Class-Specific) - 多态重叠区
    // 解析条件：必须在遇到 0x04 (接口描述符) 之后，根据 bInterfaceClass 决定含义！
    // ==========================================

    // --- [多态分支 A] 音频 (UAC) / 视频 (UVC) / 通信 (CDC) 等类 ---
    // 前提: bInterfaceClass == 0x01(Audio) 或 0x0E(Video) 或 0x02(CDC)
    USB_DESC_TYPE_CS_DEVICE             = 0x21,
    USB_DESC_TYPE_CS_CONFIG             = 0x22,
    USB_DESC_TYPE_CS_STRING             = 0x23,
    USB_DESC_TYPE_CS_INTERFACE          = 0x24, // 音视频处理单元、格式声明等
    USB_DESC_TYPE_CS_ENDPOINT           = 0x25,

    // --- [多态分支 B] 人机交互设备 (HID，如鼠标/键盘) ---
    // 前提: bInterfaceClass == 0x03 (HID)
    USB_DESC_TYPE_HID                   = 0x21, // ★ 与 CS_DEVICE 撞车！描述 HID 版本和报文数
    USB_DESC_TYPE_HID_REPORT            = 0x22, // ★ 与 CS_CONFIG 撞车！极其复杂的报文定义树
    USB_DESC_TYPE_HID_PHYSICAL          = 0x23, // ★ 与 CS_STRING 撞车！物理设备部件描述

    // --- [多态分支 C] 大容量存储 (Mass Storage - UAS 协议) ---
    // 前提: bInterfaceClass == 0x08 且 bInterfaceProtocol == 0x62 (UAS)
    USB_DESC_TYPE_UAS_PIPE_USAGE        = 0x24, // ★ 与 CS_INTERFACE 撞车！(你抓出来的 U 盘就是它)

    // ==========================================
    // 第三阵营：集线器专用 (Hub Specific) - 事实上的独立类
    // 前提: bDeviceClass == 0x09 (Hub)
    // ==========================================
    USB_DESC_TYPE_HUB20                = 0x29, // USB 2.0 集线器描述符
    USB_DESC_TYPE_HUB30                = 0x2A  // USB 3.0 超高速集线器描述符

} usb_desc_type_e;

typedef struct {
    uint8 length; // 描述符长度
    usb_desc_type_e desc_type; // 描述符类型
}usb_desc_head_t;

/*usb设备描述符
描述符长度，固定为 18 字节（0x12）
描述符类型，固定为 0x01（设备描述符）*/
typedef struct {
    usb_desc_head_t head;
    uint16 usb_version;         // USB 协议版本，BCD 编码（如 0x0200 表示 USB 2.0，0x0300 表示 USB 3.0）
    uint8 device_class;         // 设备类代码，定义设备类别（如 0x00 表示类在接口描述符定义，0x03 表示 HID）
    uint8 device_subclass;      // 设备子类代码，进一步细化设备类（如 HID 的子类）
    uint8 device_protocol;      // 设备协议代码，定义类内协议（如 HID 的 0x01 表示键盘）
    uint8 max_packet_size0;     // 端点 0 的最大数据包大小（字节），USB 2.0 为 8/16/32/64，USB 3.x 为 9（表示 2^9=512 字节）
    uint16 vendor_id;           // 供应商 ID（VID），由 USB-IF 分配，标识制造商
    uint16 product_id;          // 产品 ID（PID），由厂商分配，标识具体产品
    uint16 device_version;      // 设备发布版本，BCD 编码（如 0x0100 表示版本 1.00）
    uint8 manufacturer_index;   // 制造商字符串描述符索引（0 表示无）
    uint8 product_index;        // 产品字符串描述符索引（0 表示无）
    uint8 serial_number_index;  // 序列号字符串描述符索引（0 表示无，建议提供唯一序列号）
    uint8 num_configurations;   // 支持的配置描述符数量（通常为 1）
} usb_dev_desc_t;

/*usb配置描述符
描述符长度，固定为 9 字节（0x09）
描述符类型，固定为 0x02（配置描述符）*/
typedef struct {
    usb_desc_head_t head;
    uint16 total_length; // 配置描述符总长度（包括所有子描述符，如接口、端点等），单位为字节
    uint8 num_interfaces; // 该配置支持的接口数量
    uint8 configuration_value; // 配置值，用于 SET_CONFIGURATION 请求（通常从 1 开始）
    uint8 configuration_index; // 配置字符串描述符索引（0 表示无）
    uint8 attributes; /*配置属性
                                位7：固定为 1（保留）
                                位6：1=自供电，0=总线供电
                                位5：1=支持远程唤醒，0=不支持
                                位4-0：保留，置 0*/
    uint8 max_power; // 最大功耗，单位为 2mA（USB 2.0）或 8mA（USB 3.x）例如：50 表示 USB 2.0 的 100mA 或 USB 3.x 的 400mA
} usb_cfg_desc_t;

/*USB 字符串描述符
描述符长度（含头部和字符串）
描述符类型 = 0x03*/
typedef struct {
    usb_desc_head_t head;
    uint16 string[]; // UTF-16LE 编码的字符串内容（变长数组）
} usb_string_desc_t;

/*接口描述符
描述符长度，固定为 9 字节（0x09）
描述符类型，固定为 0x04（接口描述符）*/
typedef struct {
    usb_desc_head_t head;
    uint8 interface_number; // 接口编号，从 0 开始，标识该接口
    uint8 alternate_setting; // 备用设置编号，同一接口的不同配置（通常为 0）
    uint8 num_endpoints; // 该接口使用的端点数量（不包括端点 0）
    uint8 interface_class; // 接口类代码，定义接口功能（如 0x03 表示 HID，0x08 表示 Mass Storage）
    uint8 interface_subclass; // 接口子类代码，进一步细化接口类（如 HID 的子类）
    uint8 interface_protocol; // 接口协议代码，定义类内协议（如 HID 的 0x01 表示键盘）
    uint8 interface_index; // 接口字符串描述符索引（0 表示无）
} usb_if_desc_t;

// 定义 xHCI 的端点类型宏，方便代码阅读
#define USB_EP_TYPE_CONTROL 0  // 00: 控制传输 (Control) - 仅 EP0 使用，发命令/枚举
#define USB_EP_TYPE_ISOCH   1  // 01: 同步传输 (Isochronous) - 摄像头/声卡，保证实时性，不保证到达
#define USB_EP_TYPE_BULK    2  // 10: 批量传输 (Bulk) - U盘/硬盘，保证到达，不保证实时性
#define USB_EP_TYPE_INTR    3  // 11: 中断传输 (Interrupt) - 键盘/鼠标，周期性轮询

/*端点描述符
描述符长度（固定7字节）
描述符类型：0x05 = 端点描述符*/
typedef struct {
    usb_desc_head_t head;
    uint8 endpoint_address; // 端点地址：位7方向(0=OUT,主机→设备 1=IN，设备→主机)，位3-0端点号
    uint8 attributes; // 传输类型：0x00=控制，0x01=Isochronous，0x02=Bulk，0x03=Interrupt
    uint16 max_packet_size; // 该端点的最大包长（不同速度有不同限制）
    uint8 interval; // 轮询间隔（仅中断/同步传输有意义）
} usb_ep_desc_t;

/*  超高速端点伴随描述符
 *  uint8  bLength;            // 固定 6
    uint8  bDescriptorType;    // 0x30 表示 SuperSpeed Endpoint Companion Descriptor*/
typedef struct {
    usb_desc_head_t head;
    uint8 max_burst; // 每次突发包数（0-15），实际表示突发数+1
    uint8 attributes; // 位 4:0 Streams 支持数 (Bulk)，或多事务机会 (Isoch)
    uint16 bytes_per_interval; // 对于 Isoch/Interrupt，最大字节数
} usb_ss_comp_desc_t;

/**
 * @brief UAS (USB Attached SCSI) 管道用途标识符 (Pipe ID)
 */
typedef enum : uint8 {
    // ==========================================
    // 1. 控制平面 (Control Plane) - 负责下发指令和接收执行结果
    // ==========================================
    USB_UAS_PIPE_COMMAND_OUT = 1,  // [指令下发] 主机 -> U盘。用于发送 SCSI 命令块 (CDB)
    USB_UAS_PIPE_STATUS_IN   = 2,  // [状态回执] U盘 -> 主机。用于接收命令执行成功与否的 Sense Data

    // ==========================================
    // 2. 数据平面 (Data Plane) - 负责极限速度的底层扇区搬运
    // ==========================================
    USB_UAS_PIPE_BULK_IN     = 3,  // [数据读取] U盘 -> 主机。你的 "Data-In Pipe" (你刚才抓包抓到的就是它！)
    USB_UAS_PIPE_BULK_OUT    = 4   // [数据写入] 主机 -> U盘。你的 "Data-Out Pipe"
} usb_uas_pipe_id_e;

/* USA管道描述符
 * uint8  bLength;            // 固定 4
 * uint8  bDescriptorType;    // 0x24
 */
typedef struct {
    usb_desc_head_t head;
    usb_uas_pipe_id_e  pipe_id;              // 端点用途标识 1=command_out 2=status_in 3=bulk_in 4=bulk_out
    uint8  reserved;
} usb_uas_pipe_usage_desc_t;

/*HID 类描述符（可选
描述符长度
描述符类型：0x21 = HID 描述符*/
typedef struct {
    usb_desc_head_t head;
    uint16 bcd_hid;              // HID 协议版本号 (BCD码，如 0x0111 代表 1.11)
    uint8  country_code;        // 国家/地区代码 (0x00 表示硬件不区分国家)
    uint8  num_descriptors;     // 下级附属描述符的数量 (通常至少为 1)

    // 下面是附属描述符的信息（绝大多数情况只有 1 个，即报告描述符）
    uint8  report_descriptor_type;   // 附属描述符类型 (固定为 0x22，代表报告描述符)
    uint16 report_descriptor_length; // ★ 附属描述符的总长度 (字节数)
} usb_hid_desc_t;

/**
 * @brief USB 2.0 集线器描述符 (Type: 0x29)
 * 警告：这是一个变长结构体！最后两个数组的长度取决于 bNbrPorts。
 */
typedef struct {
    usb_desc_head_t head;
    uint8  num_ports;               // ★ 下游端口总数 (极其关键，决定了你要轮询几次)

    // wHubCharacteristics 核心特性位图：
    // Bit 1:0 - 电源切换模式 (00=全局联动上电, 01=各端口独立上电, 1X=无电源切换/始终通电)
    //           -> 💡 架构师注：无论报告哪种模式，向每个端口无脑发送上电指令都是最安全、最兼容的做法！
    // Bit 2   - 复合设备标识 (0=独立Hub, 1=复合设备，Hub内部硬连了其他设备)
    // Bit 4:3 - 过流保护模式 (00=全局过流保护, 01=单端口独立过流保护, 1X=无保护)
    // Bit 6:5 - TT 思考时间  (00=8, 01=16, 10=24, 11=32 FS bit times) -> 仅用于 USB 2.0 降速翻译
    // Bit 7   - 端口指示灯控制 (0=不支持, 1=支持)
    uint16 hub_characteristics;

    uint8  power_on_to_power_good;  // 端口上电后，需要等多久电源才能稳定？(单位是 2ms，比如填 50 就是要等 100ms)
    uint8  hub_control_current;     // Hub 芯片自身工作需要的最大电流 (单位: 1mA)

    // ==========================================
    // ⚠️ 变长警告：该字段在内存中长度不固定！
    // 字节数 = (num_ports / 8) + 1
    // 比如 4 口 Hub，这里就是 1 个字节；10 口 Hub，这里就是 2 个字节。
    // ==========================================
    uint8 device_removable[32];       // 位图：指示每个端口上的设备是不是焊死的（比如笔记本内置摄像头）

    uint8 port_pwr_ctrl_mask[32];  // 💡 USB 2.0 规范已废弃全填 0xFF，此处物理超度！
} usb_hub20_desc_t;

/**
 * @brief USB 3.0 超高速集线器描述符 (Type: 0x2A)
 * 优势：长度固定为 12 字节，无变长数组陷阱。
 */
typedef struct {
    usb_desc_head_t head;
    uint8  num_ports;           // 下游端口总数 (由于规范限制，绝不会超过 15)

    // wHubCharacteristics 核心特性位图 (去除了 USB 2.0 的 TT 字段)：
    // Bit 1:0 - 电源切换模式 (00=全局联动上电, 01=独立上电, 1X=无电源切换)
    // Bit 2   - 复合设备标识 (0=独立Hub, 1=复合设备)
    // Bit 4:3 - 过流保护模式 (00=全局保护, 01=独立保护, 1X=无保护)
    // Bit 6:5 - 被废弃，严格填 0
    // Bit 7   - 端口指示灯控制 (0=不支持, 1=支持)
    uint16 hub_characteristics;

    uint8  power_on_to_power_good;  // 单位依然是 2ms
    uint8  hub_control_current;     // 🌟 修正3：在 Hub 描述符里，3.0 的单位依然是 1mA！

    // ==========================================
    // SuperSpeed 专属的新字段 (用于内核调度器评估总线延迟)
    // ==========================================
    uint8  hub_hdr_hecLat;          // Hub 数据包头解码延迟 (Hub Header Decode Latency)
    uint16 hub_delay;               // Hub 转发数据块的纳秒级平均延迟 (单位: ns)

    // ==========================================
    // 曾经的变长数组，现在变成了固定的 16-bit 整数
    // ==========================================
     uint16 device_removable;        // 16位位图。Bit 1~15 代表对应的端口是否可移除。(Bit 0 保留)

} usb_hub30_desc_t;

// ============================================================================
// 📦 USB 标准 Setup 控制请求包 (严格 8 字节对齐)
// 规范出处: USB 2.0 Spec, Section 9.3 (USB Device Requests)
// ============================================================================
typedef struct usb_setup_packet_t {
    uint8  request_type; // 请求类型、数据方向和接收者
    uint8  request;      // 请求代码 (如 GET_DESCRIPTOR, SET_ADDRESS)
    uint16 value;        // 请求值 (具体含义由 bRequest 决定)
    uint16 index;        // 索引或偏移 (如 接口号、端点号)
    uint16 length;       // 数据阶段的传输长度（字节）
} usb_setup_packet_t;

#pragma pack(pop)

// ============================================================================
// 🛠️ bmRequestType 宏装配器 (Bitmask Macros)
// ============================================================================
// 1. 数据传输方向 (Data Transfer Direction) - Bit 7
#define USB_REQ_DIR_OUT         (0 << 7) // 主机 -> 设备 (Host to Device)
#define USB_REQ_DIR_IN          (1 << 7) // 设备 -> 主机 (Device to Host)

// 2. 请求类型 (Type) - Bits 6:5
#define USB_REQ_TYPE_STANDARD   (0 << 5) // 标准请求 (所有的基础枚举都用这个)
#define USB_REQ_TYPE_CLASS      (1 << 5) // 类特定请求 (如 HID 报告, 大容量存储 BOT Reset)
#define USB_REQ_TYPE_VENDOR     (2 << 5) // 厂商自定义请求
#define USB_REQ_TYPE_RESERVED   (3 << 5) // 保留

// 3. 接收者 (Recipient) - Bits 4:0
#define USB_REQ_REC_DEVICE      0x00     // 接收者：设备全局
#define USB_REQ_REC_INTERFACE   0x01     // 接收者：特定接口 (Interface)
#define USB_REQ_REC_ENDPOINT    0x02     // 接收者：特定端点 (Endpoint)
#define USB_REQ_REC_OTHER       0x03     // 接收者：其他 (如 Hub 的特定端口)

// 🌟 终极装配宏：一键生成 bmRequestType
#define USB_BM_REQ_TYPE(dir, type, rec) ((dir) | (type) | (rec))

// ---------------------------------------------------------
// 📖 附赠：标准 bRequest 代码字典 (USB 2.0 Spec Table 9-4)这些是所有 USB 设备都必须无条件支持的基础命令 (统称 Chapter 9 请求)
// ---------------------------------------------------------
// 🔍 状态与控制类
#define USB_REQ_GET_STATUS        0x00  // 获取状态。用于查询设备是否自供电/支持远程唤醒，或者查询某个端点是否处于 STALL（卡死）状态。
#define USB_REQ_CLEAR_FEATURE     0x01  // 清除特性。最常用的保命命令：当 U 盘发生严重错误端点被 Halt 时，主机发这个命令去清除端点的 STALL 状态。
#define USB_REQ_SET_FEATURE       0x03  // 设置特性。比如让设备进入挂起/测试模式，或者设置某个特定端点的特性。

// 🎫 身份与地址类
#define USB_REQ_SET_ADDRESS       0x05  // 设置地址。枚举第一步：主控给刚插入的设备分配一个 1~127 的唯一地址。（注意：在 xHCI 中，这通常由底层的 Address Device TRB 硬件代劳，较少由软件发控制传输打包）。

// 📂 描述符获取类 (核心枚举命令)
#define USB_REQ_GET_DESCRIPTOR    0x06  // 获取描述符。最高频命令！系统查户口专用，用来读取设备是啥、有几个接口、要多大电流、叫什么名字。
#define USB_REQ_SET_DESCRIPTOR    0x07  // 设置描述符。非常罕见，允许主机修改设备的描述符（绝大多数设备是只读的，不支持此命令）。

// ⚙️ 配置与接口激活类
#define USB_REQ_GET_CONFIGURATION 0x08  // 获取配置。问设备：“你现在用的是几号配置？”
#define USB_REQ_SET_CONFIGURATION 0x09  // 设置配置。枚举的关键一步：读完所有描述符后，主机发送此命令激活某个配置（通常是 Config 1），设备这才真正通电开始工作。
#define USB_REQ_GET_INTERFACE     0x0A  // 获取接口。查询当前接口正在使用的是哪个备用设置 (Alternate Setting)。
#define USB_REQ_SET_INTERFACE     0x0B  // 设置接口。常用于带宽需求可变的设备（如 USB 摄像头、麦克风），用来在不同的等时流 (Isochronous) 带宽方案间实时切换。



// ---------------------------------------------------------
// 📑 描述符类型字典 (填入 wValue 的高字节)当你发送 USB_REQ_GET_DESCRIPTOR 时，告诉设备你想拿哪份“档案”
// ---------------------------------------------------------
#define USB_DESC_TYPE_DEVICE      0x01  // 设备描述符。每个设备只有 1 个。包含最重要的 VID (厂商ID), PID (产品ID), 以及 bMaxPacketSize0 (端点0最大包长)。
#define USB_DESC_TYPE_CONFIG      0x02  // 配置描述符。包含总功耗信息。获取它时，设备通常会连带把下面的 Interface 和 Endpoint 描述符像糖葫芦一样一并返回！
#define USB_DESC_TYPE_STRING      0x03  // 字符串描述符。人类可读的文本，比如制造厂商叫 "Kingston"，产品名叫 "DataTraveler 3.0"。
#define USB_DESC_TYPE_INTERFACE   0x04  // 接口描述符。定义该接口是鼠标 (HID)、U盘 (Mass Storage) 还是网卡。
#define USB_DESC_TYPE_ENDPOINT    0x05  // 端点描述符。定义该管道是 IN 还是 OUT，是 Bulk、Interrupt 还是 Isochronous，以及最大传输带宽。
#define USB_DESC_TYPE_BOS         0x0F  // BOS描述符 (Binary Object Store)。USB 3.0+ 专属档案。用来查询设备是否支持 SuperSpeed (5Gbps+) 特性及高级链路电源管理 (LPM)。
#define USB_DESC_TYPE_REPORT      0x22  // hid设备报告描述符


// ============================================================================
// 🔌 1. USB Hub 类专属请求 (Hub Class Requests)
// 规范出处：USB 2.0 Spec Chapter 11 / USB 3.2 Spec Chapter 10
// 触发条件：Setup 包的 bmRequestType (Type = Class, Recipient = Device/Other)
// ============================================================================

// 【USB 2.0 Hub 专属：事务转换器 (TT) 控制】
// 背景：当高速 (480Mbps) Hub 下面挂载了全速/低速设备 (如老式键盘) 时，Hub 内部的 TT 负责做速率转换缓存。
#define HUB_REQ_CLEAR_TT_BUFFER     0x08 // 清除 TT 缓冲区。当发往全/低速设备的 Split 事务发生严重错误卡死时，主机发此命令清空 Hub 内部积压的数据。
#define HUB_REQ_RESET_TT            0x09 // 复位 TT。当 TT 内部状态机彻底崩溃时，强制重启该 Hub 端口的事务转换器硬件逻辑。
#define HUB_REQ_GET_TT_STATE        0x0A // 获取 TT 状态。主要用于底层内核 Debug。
#define HUB_REQ_STOP_TT             0x0B // 停止 TT。暂停 TT 工作，以便主机读取内部状态用于故障排查。

// 【USB 3.0 (SuperSpeed) Hub 专属】
#define HUB_REQ_SET_HUB_DEPTH       0x0C // 🌟 设置 Hub 深度。强制时序：必须在 Set Configuration 之后、读 Hub 描述符之前发送！告诉 Hub 它在拓扑树的第几层，否则 Hub 无法正确解析 20-bit 的路由字符串。
#define HUB_REQ_GET_PORT_ERR_COUNT  0x0D // 获取端口错误计数。读取 SuperSpeed 物理层链路错误次数，评估线材质量或信号完整性。

// ★ 架构师备忘录：
// Hub 真正高频使用的控制端口上电、复位的指令，其实是复用了标准的 USB_REQ_SET_FEATURE / USB_REQ_CLEAR_FEATURE。
// 只是需要在 wValue 中传入特定的 Hub Feature Selector，比如 PORT_POWER (8) 或 PORT_RESET (4)。


// ============================================================================
// 💾 2. USB 大容量存储 (Mass Storage BOT) 专属请求
// 规范出处：USB Mass Storage Class Bulk-Only Transport (BOT) Spec
// 触发条件：Setup 包的 bmRequestType (Type = Class, Recipient = Interface)
// ============================================================================
#define BOT_REQ_GET_MAX_LUN         0xFE // 获取最大逻辑单元号 (Logical Unit Number)。问读卡器：“你有几个插槽？” 设备返回最大编号（例如返回 3，代表有 0~3 共 4 个槽位）。单体 U盘通常返回 0。

#define BOT_REQ_MASS_STORAGE_RESET  0xFF // 批量仅复位 (BOT Reset)。
// ★ 灾难恢复核弹按钮：在协议转换或异常测试中，如果 U 盘内部的 SCSI 命令解析器卡死 (Bulk 端点连续 STALL)，
// 发送此命令可让其命令解析器软重启，且不会导致物理 USB 链路断开（无需重新走地址分配和枚举）。


// ============================================================================
// 🎛️ 3. USB 标准特性选择器 (Feature Selectors)
// 对应 Clear Feature / Set Feature 请求的 wValue 字段
// ============================================================================

// ------------------------------------------------------------------------
// 【发给 Endpoint (端点) 的特性】(当 recipient == ENDPOINT 时)
// ------------------------------------------------------------------------
#define USB_FEATURE_ENDPOINT_HALT   0x00
// 作用：清除它 (Clear Feature)，就能解开端点的 STALL 状态，让数据通道重新开放。
// ★ 连招技巧：在 BOT 错误恢复中，发送完 BOT_REQ_MASS_STORAGE_RESET 后，必须紧接着向对应的 Bulk IN 和 Bulk OUT 端点发送 Clear Feature(ENDPOINT_HALT)，两者配合才能彻底救活卡死的 U 盘！

// ------------------------------------------------------------------------
// 【发给 Device (设备) 的特性】(当 recipient == DEVICE 时)
// ------------------------------------------------------------------------
#define USB_FEATURE_DEVICE_REMOTE_WAKEUP 0x01 // 远程唤醒 (比如敲击休眠键盘唤醒整台电脑)
#define USB_FEATURE_TEST_MODE            0x02 // 测试模式 (主要用于主板硬件层面的出厂眼图测试)

// 【USB 3.0 新增设备级节能特性】
#define USB_FEATURE_U1_ENABLE            0x30 // 允许设备进入 U1 节能状态 (微秒级唤醒)
#define USB_FEATURE_U2_ENABLE            0x31 // 允许设备进入 U2 节能状态 (毫秒级唤醒)