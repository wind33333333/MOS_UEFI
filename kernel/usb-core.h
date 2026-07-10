#pragma once
#include "moslib.h"
#include "device.h"
#include "driver.h"
#include "xhci.h"

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
    uint16 hid; // HID 版本号
    uint8 country_code; // 国家代码（0=无）
    uint8 num_descriptors; // 后面跟随的子描述符数量
    // 后面通常跟 HID 报告描述符（类型0x22）等
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



//=============================================================


// 定义匹配标志位 (位掩码)
#define USB_MATCH_ANY (-1)
#define USB_MATCH_VENDOR       0x0001  // 要求匹配 VID
#define USB_MATCH_PRODUCT      0x0002  // 要求匹配 PID
#define USB_MATCH_INT_CLASS    0x0080  // 要求匹配接口大类
#define USB_MATCH_INT_SUBCLASS 0x0100  // 要求匹配接口子类
#define USB_MATCH_INT_PROTOCOL 0x0200  // 要求匹配接口协议

typedef struct usb_id_t {
    uint16 match_flags; // 🌟 灵魂字段：告诉 match 函数怎么做比较

    // 设备级匹配
    uint16 vendor_id;
    uint16 product_id;

    // 接口级匹配
    uint8  if_class;
    uint8  if_subclass;
    uint8  if_protocol;
} usb_id_t;


//usb驱动
typedef struct usb_drv_t{
    driver_t drv;
    int  (*probe)(struct usb_if_t *usb_if, usb_id_t *id);
    void (*remove)(struct usb_if_t *usb_if);
} usb_drv_t;

//usb端点
typedef struct usb_ep_t {
    // 1. 通用 USB 逻辑特性 (完全独立于硬件，直接来源于描述符)
    uint8       ep_type;           // 端点：控制/批量/中断/等时
    uint16      max_packet_size;   //wMaxPacketSize：解码后的单次最大包长
    uint8       interval;          //bInterval：轮询间隔（中断/等时用；bulk 通常可忽略但保留）

    // USB 3.0 超高速伴随特性 (SuperSpeed Companion)
    uint8       max_burst;          // USB3 bMaxBurst（0=1 burst；仅 SS/SSP 有意义）
    uint16      max_streams_exp;    // bulk 端点支持的最大 stream 数（由 ss_comp->bmAttributes 解码，0 表示不支持 streams（BOT 一般用不到，UAS 可能需要）
    uint16      bytes_per_interval; // wBytesPerInterval：周期性端点每 ESIT 传输字节数
    uint8       mult;               // USB 2.0 High-Speed 高带宽事务 (Mult) 处理 0=1 transaction, 1=2 trans, 2=3 trans

    // 动态数组：紧随端点后的 class-specific/未知描述符块，枚举层不解释语义，交给类驱动（例如 UAS）按需解析
    void        *extras_desc;
    uint16      extras_len;

    // 2. 仅为 xHCI 定制的强绑定硬件特性 (Endpoint Context 推导值与 DMA 资源)
    uint8       ep_dci;            // xHCI 专属的设备上下文索引 (Device Context Index, 1~31)
    uint8       cerr;              // xHCI 错误重试计数 (通常设为 3)
    uint8       lsa;               // xHCI 线性流数组标志 (Linear Stream Array)
    uint8       hid;               // xHCI 主机发起禁用标志 (Host Initiate Disable)
    uint32      max_esit_payload;  // xHCI 周期端点有效载荷 (基于通用参数推导计算)
    uint16      average_trb_length;// xHCI 专用的 DMA 预取启发值 (Average TRB Length)
    uint64      trq_phys_addr;     // xHCI 硬件出队指针 (TR Dequeue Pointer) 物理地址

    // ★ 统一传输环数组：
    // 情况 A (非流模式): 分配大小为 1 的数组。rings[0] 就是普通的 transfer_ring。
    // 情况 B (流模式)  : 分配大小为 num_streams + 1 的数组。rings[1...N] 是流环。
    uint8              enable_streams_exp;// xHCI 实际向主板申请并启用的流指数
    uint32             ring_max_trbs; // 上层驱动期望的环大小 (0 表示使用 Core 默认值)
    xhci_submit_ring_t *ring_arr;            // xHCI 传输环数组 (普通模式大小为1，流模式大小为 N+1)
    void               *streams_ctx_array;   // xHCI 流上下文数组的 DMA 内存基地址

} usb_ep_t;

//usb备用接口
typedef struct usb_if_alt_t {
    struct usb_if_t *uif;
    usb_if_desc_t *if_desc;  // 指向 cfg_raw 内
    void          *extras_desc; //接口似有描述符
    uint16        extras_len;
    usb_ep_t      *eps;      // 可选：解析后的端点数组
} usb_if_alt_t;

//usb接口
typedef struct usb_if_t {
    struct usb_dev_t *udev;
    uint8 num_if_alts;          // 备用接口数量
    usb_if_alt_t *if_alts;      // 备用接口数组
    usb_if_alt_t *activity_if_alt;   // 当前激活的备用接口
    device_t dev;
    void    *drv_data;
} usb_if_t;


//USB设备
typedef struct usb_dev_t{
    // 1. 通用总线拓扑与设备模型
    device_t                        dev;               // 继承系统基础设备对象
    struct usb_dev_t                *parent_hub;       // 亲爹指针 (直连主板则为 NULL)
    uint8                           root_hub_port_num;     // 🌟 新增：认祖归宗，主板上的物理根端口号
    uint8                           tt_hub_slot_id;
    uint8                           tt_port_num;   // 替代原来的 port_num：插在亲爹的第几个口上？
    uint8                           psiv;             // xHCI 专属的底层 DMA 挡位 (用于填 Slot Context)
    usb_port_speed_e                port_speed;       // 🌟 1. 保留全局标准枚举 (供状态机流转和描述符解析使用)
    uint32                          speed_kbps;       // 🌟 2. 新增：扁平化的绝对物理带宽 (供高级驱动精确计算资源)
    uint32                          route_string;
    uint8                           hub_depth;      // 🌟 新增：记录当前设备处于第几层 (0=直连主板, 1=第一层Hub...)

    // Hub 专属特性 (非 Hub 时忽略)
    boolean                         is_hub;        //1=hub 0=普通设备
    uint8                           hub_num_ports; //hub端口数量
    uint8                           hub_mtt; //Multiple Transaction Translators - 多事务翻译器
    uint8                           hub_ttt;
    uint16                          max_exit_latency;

    // 2. 纯 USB 协议概念 (描述符与配置)
    usb_dev_desc_t                  *dev_desc;             //设备描述符
    usb_cfg_desc_t                  *config_desc;          //配置描述符
    usb_string_desc_t               *language_desc;        //语言描述符
    usb_string_desc_t               *manufacturer_desc;    //制造商描述符
    usb_string_desc_t               *product_desc;         //产品型号名描述符
    usb_string_desc_t               *serial_number_desc;   //序列号描述符

    uint8                           *manufacturer;     // 制造商ascii字符
    uint8                           *product;          // 产品型号ascii字符
    uint8                           *serial_number;    // 序列号ascii字符

    // 3. 逻辑端点与接口路由 (暴露给业务层驱动的资源)
    usb_if_t                        *ifs;              // 接口指针根据接口数量动态分配
    usb_ep_t                        *eps[32];          // eps[0]仅占位，eps[1]-eps[31]=端点1-31

    // 4. 仅为xhci定制强绑定
    uint8                           slot_id;
    uint16                          interrupter_target;
    void                            *out_ctx;    // 硬件状态
    xhci_input_ctrl_ctx_t           *in_ctx;     // 软件状态
    uint32                          active_ep_map;       //当前活跃的端点图
    xhci_hcd_t                      *xhcd;              // xhci控制器
    void                            *drv_data;
} usb_dev_t;

/* ========================================================================
 * URB 传输控制标志位 (Transfer Flags) - 完美复刻 Linux 内核语义
 * ======================================================================== */

// 1. 数据完整性控制
#define URB_SHORT_NOT_OK        0x0001  // ★ 严格模式：发生短包 (Short Packet) 时直接视为错误。仅适用于 IN 传输。

// 2. 内存与 DMA 管理
#define URB_NO_TRANSFER_DMA_MAP 0x0004  // 驱动已自行完成物理内存映射，底层请直接使用 urb->transfer_dma，不要再调 IOMMU/映射函数。
#define URB_NO_SETUP_DMA_MAP    0x0008  // 驱动已自行完成 Setup 包的物理映射，底层请直接使用 urb->setup_dma。
#define URB_FREE_BUFFER         0x0100  // 内核托管：当这个 URB 被销毁时，请帮我自动 kfree 掉 transfer_buffer。

// 3. 协议边缘情况处理
#define URB_ZERO_PACKET         0x0040  // ★ Bulk OUT 极客专属：如果传输长度刚好是 MaxPacketSize 的整数倍，强制在末尾追加一个 0 字节包 (ZLP)，防止设备死等。

// 4. 硬件中断调度控制 (替换原先的 ioc)
#define URB_NO_INTERRUPT        0x0080  // ★ 幽灵模式：静默传输，传输完成时【不触发】硬件中断 (即清零最后一个 TRB 的 IOC 位)。通常用于大批量 URB 连续提交的场景，只在最后一个 URB 开启中断。

// 5. 传输方向强制覆盖 (可选，通常优先以端点描述符方向为准)
#define URB_DIR_IN              0x0200  // 强制标明方向为：设备 -> 主机
#define URB_DIR_OUT             0x0000  // 强制标明方向为：主机 -> 设备
#define URB_DIR_MASK            0x0200  // 用于提取方向的掩码

/**
 * @brief USB 请求块 (USB Request Block) - 纯逻辑版
 */
typedef struct usb_urb_t {
    // === 1. 路由寻址区 ===
    usb_dev_t   *udev;        // 目标设备上下文
    usb_ep_t    *ep;          // 目标端点
    uint16      stream_id;    // UAS 协议专用的 Stream ID

    // === 2. 业务载荷区 ===
    usb_setup_packet_t *setup_packet;// EP0 控制包指针
    void        *transfer_buf;  // 数据缓冲区虚拟地址
    uint32      transfer_len;   // 期望传输总长度
    uint32      interval;

    // 👑 核心换血：用标志位取代具体的硬件配置
    uint32      transfer_flags; // 传输控制组合掩码 (如 URB_SHORT_NOT_OK | URB_NO_INTERRUPT)

    // === 3. 状态与回调区 ===
    uint32      actual_length;  // [新增] 实际成功传输的字节数 (硬件回填)
    uint64      last_trb_pa;    // 最后一个 TRB 的物理地址 (仅做底层同步过渡用)
    int         status;         // URB 状态码

    // void (*complete)(struct usb_urb *urb); // 未来做全异步驱动时，这里放回调函数

    list_head_t node;         // 挂载到端点 pending_urbs 的链表节点

    // 🌟 单任务环境的终极同步神器
    volatile boolean is_done;
} usb_urb_t;


//端点转Dci
static inline uint8 epaddr_to_epdci(uint8 ep) {
    asm volatile(
        "rolb $1,%0"
        :"+q"(ep)
        :
        :"cc");
    return ep;
}

//Dci转端点
static inline uint8 epdci_to_epaddr(uint8 dci) {
    asm volatile(
        "rorb $1,%0"
        :"+q"(dci)
        :
        :"cc");
    return dci;

}


//获取下一个描述符
static inline void *usb_get_next_desc(usb_desc_head_t *head) {
    return (uint8*)head + head->length;
}


extern struct bus_type_t usb_bus_type;

void usb_dev_init(usb_dev_t *udev);

//注册usb设备
static inline void usb_dev_register(usb_dev_t *usb_dev) {
    device_register(&usb_dev->dev);
}

int32 usb_if_create(usb_dev_t *udev);

void usb_if_register(usb_dev_t *udev);


void usb_drv_register(usb_drv_t *usb_drv);//注册usb驱动
int usb_bus_match(device_t* dev,driver_t* drv);
int usb_bus_probe(device_t* dev);
void usb_bus_remove(device_t* dev);

int32 usb_submit_urb(usb_urb_t *urb);
usb_urb_t *usb_alloc_urb(void);
void usb_free_urb(usb_urb_t *urb);
void usb_fill_control_urb(usb_urb_t *urb,usb_dev_t *udev,usb_ep_t *ep,usb_setup_packet_t *setup_packet,void *transfer_buf,uint32 transfer_len);
void usb_fill_bulk_urb(usb_urb_t *urb,usb_dev_t *udev,usb_ep_t *ep,void *transfer_buf,uint32 transfer_len);
void usb_fill_int_urb(usb_urb_t *urb,usb_dev_t *udev,usb_ep_t *ep,void *transfer_buf,uint32 transfer_len,uint32 interval);

usb_if_alt_t* usb_find_alt_if(usb_if_t *uif, int16 class, int16 subclass, int16 protocol);
int32 usb_enable_alt_if(usb_if_alt_t *new_alt);
int32 usb_cfg_alt_streams(usb_if_alt_t *alt, uint8 want_streams_exp);

int32 usb_control_msg(usb_dev_t *udev, void *data_buf,
                      uint8 request_type,uint8 request, uint16 value, uint16 index, uint16 length);


// ============================================================================
// 🚦 端点状态控制与接口配置 API (极简封装版)
// ============================================================================
/**
 * @brief 端点解锁 (清除端点 Halt / 清除 Stall 状态)
 * @param udev   目标设备
 * @param ep_dci xHCI 端点上下文索引 (DCI)
 * @note 用于抢救出现 STALL 错误的端点。无数据阶段，length = 0。
 */
static inline int32 usb_clear_ep_halt(usb_dev_t *udev, uint8 ep_dci) {
    // 🌟 发往 Endpoint 的标准控制请求
    uint8 req_type = USB_BM_REQ_TYPE(USB_REQ_DIR_OUT, USB_REQ_TYPE_STANDARD, USB_REQ_REC_ENDPOINT);

    return usb_control_msg(udev, NULL,
                           req_type,
                           USB_REQ_CLEAR_FEATURE,
                           USB_FEATURE_ENDPOINT_HALT, // wValue: 清除 Halt 特性
                           epdci_to_epaddr(ep_dci),   // wIndex: 目标端点物理地址
                           0);                        // wLength: 0
}

/**
 * @brief 端点上锁 (强制端点进入 Halt/Stall 状态)
 * @param udev   目标设备
 * @param ep_dci xHCI 端点上下文索引 (DCI)
 * @note 通常用于模拟错误或调试。无数据阶段，length = 0。
 */
static inline int32 usb_set_ep_halt(usb_dev_t *udev, uint8 ep_dci) {
    uint8 req_type = USB_BM_REQ_TYPE(USB_REQ_DIR_OUT, USB_REQ_TYPE_STANDARD, USB_REQ_REC_ENDPOINT);

    return usb_control_msg(udev, NULL,
                           req_type,
                           USB_REQ_SET_FEATURE,
                           USB_FEATURE_ENDPOINT_HALT, // wValue: 设置 Halt 特性
                           epdci_to_epaddr(ep_dci),   // wIndex: 目标端点物理地址
                           0);                        // wLength: 0
}

/**
 * @brief 激活配置 (Set Configuration)
 * @note 这是一个纯命令传输，没有后续的数据包，因此 buffer 为 NULL，length 为 0。
 */
static inline int32 usb_set_cfg(usb_dev_t *udev) {
    // 🌟 发往整个 Device 的标准控制请求
    uint8 req_type = USB_BM_REQ_TYPE(USB_REQ_DIR_OUT, USB_REQ_TYPE_STANDARD, USB_REQ_REC_DEVICE);

    return usb_control_msg(udev, NULL,
                           req_type,
                           USB_REQ_SET_CONFIGURATION,
                           udev->config_desc->configuration_value, // wValue: 要激活的配置号
                           0,                                      // wIndex: 0
                           0);                                     // wLength: 0
}

/**
 * @brief 激活接口 (Set Interface)
 * @note 用于在复合设备 (如带有多个备用设置的摄像头或声卡) 中切换接口的 Alternate Setting。
 */
static inline int32 usb_set_if(usb_dev_t *udev, uint8 if_num, uint8 alt_num) {
    // 🌟 发往指定 Interface 的标准控制请求
    uint8 req_type = USB_BM_REQ_TYPE(USB_REQ_DIR_OUT, USB_REQ_TYPE_STANDARD, USB_REQ_REC_INTERFACE);

    return usb_control_msg(udev, NULL,
                           req_type,
                           USB_REQ_SET_INTERFACE,
                           alt_num, // wValue: 备用设置号 (Alternate Setting)
                           if_num,  // wIndex: 接口号 (Interface Number)
                           0);      // wLength: 0
}

int32 usb_ctx_slot_cfg(usb_dev_t *udev);
int32 usb_ctx_slot_ep0_eval(usb_dev_t *udev);
int32 usb_ctx_eps_cfg(usb_if_alt_t *drop_uif_alt,usb_if_alt_t *add_uif_alt);
int32 usb_ctx_deconfigure_all(usb_dev_t *udev );