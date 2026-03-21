#pragma once
#include "moslib.h"
#include "device.h"
#include "driver.h"
#include "xhci.h"

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
    USB_DESC_TYPE_HUB                   = 0x29, // USB 2.0 集线器描述符
    USB_DESC_TYPE_SS_HUB                = 0x2A  // USB 3.0 超高速集线器描述符

} usb_desc_type_e;

typedef struct {
    uint8 length; // 描述符长度
    usb_desc_type_e desc_type; // 描述符类型
}usb_desc_head;

/*usb设备描述符
描述符长度，固定为 18 字节（0x12）
描述符类型，固定为 0x01（设备描述符）*/
typedef struct {
    usb_desc_head head;
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
    usb_desc_head head;
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
    usb_desc_head head;
    uint16 string[]; // UTF-16LE 编码的字符串内容（变长数组）
} usb_string_desc_t;

/*接口描述符
描述符长度，固定为 9 字节（0x09）
描述符类型，固定为 0x04（接口描述符）*/
typedef struct {
    usb_desc_head head;
    uint8 interface_number; // 接口编号，从 0 开始，标识该接口
    uint8 alternate_setting; // 备用设置编号，同一接口的不同配置（通常为 0）
    uint8 num_endpoints; // 该接口使用的端点数量（不包括端点 0）
    uint8 interface_class; // 接口类代码，定义接口功能（如 0x03 表示 HID，0x08 表示 Mass Storage）
    uint8 interface_subclass; // 接口子类代码，进一步细化接口类（如 HID 的子类）
    uint8 interface_protocol; // 接口协议代码，定义类内协议（如 HID 的 0x01 表示键盘）
    uint8 interface_index; // 接口字符串描述符索引（0 表示无）
} usb_if_desc_t;

// 定义 xHCI 的端点类型宏，方便代码阅读
#define XHCI_EP_TYPE_ISOCH   1
#define XHCI_EP_TYPE_BULK    2
#define XHCI_EP_TYPE_INTR    3

/*端点描述符
描述符长度（固定7字节）
描述符类型：0x05 = 端点描述符*/
typedef struct {
    usb_desc_head head;
    uint8 endpoint_address; // 端点地址：位7方向(0=OUT,主机→设备 1=IN，设备→主机)，位3-0端点号
    uint8 attributes; // 传输类型：0x00=控制，0x01=Isochronous，0x02=Bulk，0x03=Interrupt
    uint16 max_packet_size; // 该端点的最大包长（不同速度有不同限制）
    uint8 interval; // 轮询间隔（仅中断/同步传输有意义）
} usb_ep_desc_t;

/*  超高速端点伴随描述符
 *  uint8  bLength;            // 固定 6
    uint8  bDescriptorType;    // 0x30 表示 SuperSpeed Endpoint Companion Descriptor*/
typedef struct {
    usb_desc_head head;
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
    usb_desc_head head;
    usb_uas_pipe_id_e  pipe_id;              // 端点用途标识 1=command_out 2=status_in 3=bulk_in 4=bulk_out
    uint8  reserved;
} usb_uas_pipe_usage_desc_t;

/*HID 类描述符（可选
描述符长度
描述符类型：0x21 = HID 描述符*/
typedef struct {
    usb_desc_head head;
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
    usb_desc_head head;
    uint8  num_ports;           // ★ 下游端口总数 (极其关键，决定了你要轮询几次)

    // wHubCharacteristics 包含了极其重要的位域：
    // Bit 1:0 - 电源切换模式 (00=所有端口联动上电, 01=各端口独立上电, 11=无电源切换)
    // Bit 4:3 - 过流保护模式 (00=全局过流保护, 01=单端口独立过流保护, 11=无保护)
    uint16 hub_characteristics;

    uint8  power_on_to_power_good;      // 端口上电后，需要等多久电源才能稳定？(单位是 2ms，比如填 50 就是要等 100ms)
    uint8  hub_control_current;    // Hub 芯片自身工作需要的最大电流 (mA)

    // ==========================================
    // ⚠️ 变长警告：下面这两个字段在内存中紧挨着，但长度是不固定的！
    // 它们的字节数 = (bNbrPorts / 8) + 1
    // 比如 4 口 Hub，这里就是 1 个字节；10 口 Hub，这里就是 2 个字节。
    // ==========================================

    // uint8 DeviceRemovable[]; // 位图：指示每个端口上的设备是不是焊死的（比如笔记本内置摄像头）
    // uint8 PortPwrCtrlMask[]; // 历史遗留字段，USB 1.1 的产物，USB 2.0 规定全填 0xFF

} usb_hub2_desc_t;

/**
 * @brief USB 3.0 超高速集线器描述符 (Type: 0x2A)
 * 优势：长度固定为 12 字节，无变长数组陷阱。
 */
typedef struct {
    usb_desc_head head;
    uint8  num_ports;           // 下游端口总数 (由于规范限制，绝不会超过 15)
    uint16 hub_characteristics; // 特性掩码 (与 USB 2.0 类似，但去掉了废弃位)
    uint8  power_on_to_power_good;      // 单位依然是 2ms
    uint8  hub_control_current;    // ★ 注意：USB 3.0 规范里，这里的单位变成了 2mA！

    // ==========================================
    // SuperSpeed 专属的新字段 (用于内核调度器评估总线延迟)
    // ==========================================
    uint8  hub_hdr_hecLat;       // Hub 数据包头解码延迟 (Hub Header Decode Latency)
    uint16 hub_delay;           // Hub 转发数据块的纳秒级平均延迟 (单位: ns)

    // ==========================================
    // 曾经的变长数组，现在变成了固定的 16-bit 整数
    // ==========================================
    uint16 device_removable;     // 16位位图。Bit 1~15 代表对应的端口是否可移除。(Bit 0 保留)

} usb_hub3_desc_t;

typedef enum : uint8 {
    USB_RECIP_DEVICE    = 0,  //设备
    USB_RECIP_INTERFACE = 1,  //接口
    USB_RECIP_ENDPOINT  = 2,  //端点
    USB_RECIP_OTHER     = 3   //其他
} usb_recipient_e;

// USB 请求类型枚举 (对应 bmRequestType 的 Bits 5-6:type)
typedef enum : uint8 {
    USB_REQ_TYPE_STANDARD = 0, // 0 = 标准请求 (Standard Request)场景：设备枚举、获取设备描述符 (Get Descriptor)、分配地址 (Set Address)、清除端点卡死 (Clear Feature) 等。
    USB_REQ_TYPE_CLASS    = 1, // 1 = 类特定请求 (Class Request)比如我们 U 盘 (Mass Storage Class) 专属的 BOT Mass Storage Reset (0xFF) 和 Get Max LUN (0xFE)。
    USB_REQ_TYPE_VENDOR   = 2,  // 2 = 厂商自定义请求 (Vendor Request)
    USB_REQ_TYPE_RESERVED = 3   // 3 = 保留 (Reserved)
} usb_req_type_e;

// USB 数据传输方向枚举 (对应 bmRequestType 的 Bit 7: dtd)
typedef enum :uint8 {
    USB_DIR_OUT = 0, // 0 = OUT (主机到设备 / Host to Device)作用：控制传输的数据阶段，数据是由主机发送给设备的。注意：如果这个控制请求根本【没有】数据阶段（比如只发一个没有参数的命令），按照规范，方向也必须填 OUT(0)。
    USB_DIR_IN  = 1  // 1 = IN (设备到主机 / Device to Host)作用：控制传输的数据阶段，主机期待设备把数据传回来，场景：比如你想读取 U 盘的最大 LUN (Get Max LUN)，或者读取设备的描述符 (Get Descriptor) 时。
} usb_data_dir_e;

// ============================================================================
// USB 具体请求指令枚举 (对应 bRequest)
// 注意：0x00~0x0F 通常为标准请求，0x20以上及 0xFE/0xFF 常用于类特定请求
// ============================================================================
typedef enum : uint8 {
    // ------------------------------------------------------------------------
    // 【USB 标准请求】(当 RequestType == USB_REQ_TYPE_STANDARD 时有效)
    // ------------------------------------------------------------------------
    USB_REQ_GET_STATUS        = 0x00, // 获取状态 (比如看看设备是自供电还是总线供电，端点有没有Halt)
    USB_REQ_CLEAR_FEATURE     = 0x01, // 清除特性 (★我们在撬开卡死端点时用的就是这个！)
    // 0x02 是保留的
    USB_REQ_SET_FEATURE       = 0x03, // 设置特性 (比如让设备进入测试模式，或挂起特定端点)
    // 0x04 是保留的
    USB_REQ_SET_ADDRESS       = 0x05, // 设置地址 (设备刚插入时地址为0，xHCI用这个给它分配一个 1~127 的新地址)
    USB_REQ_GET_DESCRIPTOR    = 0x06, // 获取描述符 (★枚举设备、获取厂商PID/VID、端点配置全靠它)
    USB_REQ_SET_DESCRIPTOR    = 0x07, // 设置描述符 (极少使用，通常用于固件更新)
    USB_REQ_GET_CONFIGURATION = 0x08, // 获取当前配置 (看看设备现在处于哪种工作模式)
    USB_REQ_SET_CONFIGURATION = 0x09, // 设置当前配置 (告诉设备：“你现在作为U盘模式启动吧”)
    USB_REQ_GET_INTERFACE     = 0x0A, // 获取当前接口备用设置
    USB_REQ_SET_INTERFACE     = 0x0B, // 设置接口备用设置 (常见于带麦克风和音响的复合 USB 耳机切换采样率)
    USB_REQ_SYNCH_FRAME       = 0x0C, // 同步帧 (仅用于等时传输，如音频/视频设备同步时间戳)
    // ------------------------------------------------------------------------
    // 【Mass Storage (BOT) 类专属请求】(当 qtype == USB_REQ_TYPE_CLASS 时有效)
    // ------------------------------------------------------------------------
    BOT_REQ_GET_MAX_LUN       = 0xFE, // 获取最大逻辑单元号 (问读卡器：“你身上插了几个SD卡？”)
    BOT_REQ_MASS_STORAGE_RESET= 0xFF  // 批量仅复位 (★核弹按钮：让U盘的协议状态机瞬间清零重启)

} usb_request_e;

// ============================================================================
// USB 标准特性选择器枚举 (对应 Clear Feature / Set Feature 的 wValue 字段)
// ============================================================================
typedef enum : uint16 {
    // ------------------------------------------------------------------------
    // 【发给 Endpoint (端点) 的特性】(当 recipient == ENDPOINT 时)
    // ------------------------------------------------------------------------
    // 作用：清除它，就能解开端点的 STALL 状态，让数据通道重新开放。
    // ★ 在 BOT 错误恢复中，你的 value 必须填这个！
    USB_FEATURE_ENDPOINT_HALT        = 0x00,

    // ------------------------------------------------------------------------
    // 【发给 Device (设备) 的特性】(当 recipient == DEVICE 时)
    // ------------------------------------------------------------------------
    USB_FEATURE_DEVICE_REMOTE_WAKEUP = 0x01, // 远程唤醒 (比如敲击休眠键盘唤醒电脑)
    USB_FEATURE_TEST_MODE            = 0x02, // 测试模式 (主要用于主板硬件出厂测试)

    // USB 3.0 新增设备特性
    USB_FEATURE_U1_ENABLE            = 0x30, // 允许进入 U1 节能状态
    USB_FEATURE_U2_ENABLE            = 0x31  // 允许进入 U2 节能状态
} usb_feature_selector_e;


/*
 * usb 8字节请求包
 */
typedef struct usb_req_pkg_t {
    //bmRequestType
    usb_recipient_e recipient : 5;
    usb_req_type_e  req_type  : 2;
    usb_data_dir_e  dtd       : 1;

    //bRequest
    usb_request_e   request; //请求代码

    //wValue
    uint16          value; //请求值，具体含义由 b_request 定义 例如：GET_DESCRIPTOR 中，w_value 高字节为描述符类型，低字节为索引

    //wIndex
    uint16          index; //索引或偏移，具体含义由 b_request 定义 例如：接口号、端点号或字符串描述符索引

    //wLength
    uint16          length; //数据阶段的传输长度（字节）主机到设备：发送的数据长度 设备到主机：请求的数据长度
}usb_req_pkg_t;

//=============================================================

typedef enum : uint8 {
    USB_TX_CMD_ADDR_DEV,    // 事务：分配地址 (无中生有创世)
    USB_TX_CMD_EVAL_CTX,    // 事务：评估上下文 (微调参数，如 EP0 包长)
    USB_TX_CMD_CFG_EP,      // 事务：配置端点 (常规增删业务端点，DC=0)
    USB_TX_CMD_DECFG_ALL    // 事务：格式化端点 (一键抹除所有业务端点，保留 EP0，DC=1)
} usb_tx_cmd_e;

//usb驱动id表
typedef struct usb_id_t {
    // interface class 匹配（最常用）
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
    //解析后的端点描述符
    uint8       ep_dci;               // 端点 0-31
    uint8       ep_type;           // 端点：控制/批量/中断/等时
    uint16      max_packet_size;        // wMaxPacketSize 解码后的最大包长（基础值）
    uint8       interval;          // bInterval（中断/等时用；bulk 通常可忽略但保留）
    uint8       cerr;

    //解析后的超高速端点伴随描述符
    uint8       max_burst;         // USB3 bMaxBurst（0=1 burst；仅 SS/SSP 有意义）
    uint16      max_streams;         // bulk 端点支持的最大 stream 数（由 ss_comp->bmAttributes 解码，0 表示不支持 streams（BOT 一般用不到，UAS 可能需要）
    uint16      bytes_per_interval; // USB3 wBytesPerInterval（中断/等时重要）
    uint8       mult;              // USB 2.0 High-Speed 高带宽事务 (Mult) 处理 0=1 transaction, 1=2 trans, 2=3 trans
    uint8       lsa;
    uint32      max_esit_payload;
    uint8       hid;
    uint16      average_trb_length;
    uint64      trq_phys_addr;


    //软件结构
    union {
        xhci_ring_t  transfer_ring;
        xhci_ring_t  *streams_ring_array;   // per-stream rings数组 (如果启用流)
    };
    void        *streams_ctx_array;
    uint32      enable_streams_count;        // 2^max_streams_exp+1

    // 动态数组：紧随端点后的 class-specific/未知描述符块，枚举层不解释语义，交给类驱动（例如 UAS）按需解析
    void        *extras_desc;
} usb_ep_t;

//usb替用接口
typedef struct usb_if_alt_t {
    struct usb_if_t *uif;
    usb_if_desc_t *if_desc;  // 指向 cfg_raw 内
    uint8 altsetting;

    uint8 if_class;
    uint8 if_subclass;
    uint8 if_protocol;

    uint8 ep_count;     // 端点数量
    usb_ep_t *eps;      // 可选：解析后的端点数组
} usb_if_alt_t;

//usb接口
typedef struct usb_if_t {
    struct usb_dev_t *udev;
    uint8 if_num;
    uint8 alt_count;
    usb_if_alt_t *alts;
    usb_if_alt_t *cur_alt;   // 或 cur_alt_idx
    device_t dev;
} usb_if_t;



//USB设备
typedef struct usb_dev_t{
    uint8                           slot_id;
    uint8                           port_id;
    uint8                           port_speed;
    uint16                          interrupter_target;
    usb_dev_desc_t                  *dev_desc;             //设备描述符
    usb_cfg_desc_t                  *config_desc;          //配置描述符
    usb_string_desc_t               *language_desc;      //语言描述符
    usb_string_desc_t               *manufacturer_desc;  //制造商描述符
    usb_string_desc_t               *product_desc;       //产品型号名描述符
    usb_string_desc_t               *serial_number_desc; //序列号描述符
    void                            *dev_ctx;           // 设备上下文
    xhci_input_ctx_t                *input_ctx;        // 输入上下文
    uint32                          active_ep_map;      //当前活跃的端点图
    usb_ep_t                        ep0;                // 端点0
    usb_ep_t                        *eps[32];           // 端点0-31 驱动把接口端点挂到usb_dev,方便usb_core层管理
    xhci_hcd_t                      *xhcd;              // xhci控制器
    device_t                        dev;
    uint8                           interfaces_count;  // 接口数量
    usb_if_t                        *interfaces;       // 接口指针根据接口数量动态分配
    uint8                           *manufacturer;     // 制造商ascii字符
    uint8                           *product;          // 产品型号ascii字符
    uint8                           *serial_number;    // 序列号ascii字符
    struct usb_dev_t                *parent_hub;       // 上游 hub 的 usb_dev（roothub 则为 NULL）
    uint8                           parent_port;       // 插在 parent_hub 的哪个端口（1..N；roothub=0）
    uint16                          max_exit_latency;

    uint8                           is_hub;          // 是否为 Hub
    uint8                           hub_num_ports;   // Hub 的端口数
    uint8                           hub_mtt;         // 是否支持多事务翻译器
    uint8                           hub_ttt;
} usb_dev_t;

#define MAX_STREAMS 6  //最多支持流数量（2^6=64）

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

//获取 Input Context 数组中的指定条目
static inline void *xhci_get_input_ctx_entry(xhci_hcd_t *xhcd,xhci_input_ctx_t *input_ctx, uint32 ep_dci) {
    uint8 ctx_size = xhcd->ctx_size;
    return (uint8 *)input_ctx + ctx_size * (ep_dci + 1);
}


//获取 Device Context 数组中的指定条目
static inline void *xhci_get_dev_ctx_entry(usb_dev_t *udev, uint32 ep_dci) {
    return (uint8*)udev->dev_ctx + udev->xhcd->ctx_size * ep_dci;
}


//获取下一个描述符
static inline void *usb_get_next_desc(usb_desc_head *head) {
    return (uint8*)head + head->length;
}

//配置描述符结束地址
static inline void *usb_cfg_end(usb_cfg_desc_t *usb_config_desc)
{
    return (uint8*)usb_config_desc + usb_config_desc->total_length;
}

/* 在 uif->alts[] 中按 altsetting 值查找（不能用 altsetting 当数组下标） */
static inline usb_if_alt_t *usb_find_alt_by_num(usb_if_t *usb_if, uint8 altsetting)
{
    for (uint8 i = 0; i < usb_if->alt_count; i++) {
        if (usb_if->alts[i].altsetting == altsetting)
            return &usb_if->alts[i];
    }
    return NULL;
}

extern struct bus_type_t usb_bus_type;



//注册usb接口
static inline void usb_if_register(usb_dev_t *udev) {
    for (uint32 i = 0; i < udev->interfaces_count; i++) {
        usb_if_t *usb_if = &udev->interfaces[i];
        if (usb_if != NULL) {
            // 触发系统级的 match/probe (比如唤醒 bot.c 或 uas.c 驱动)
            device_register(&usb_if->dev);
        }
    }
}

//注册usb设备
static inline void usb_dev_register(usb_dev_t *usb_dev) {
    device_register(&usb_dev->dev);
}

void usb_drv_register(usb_drv_t *usb_drv);//注册usb驱动
int usb_bus_match(device_t* dev,driver_t* drv);
int usb_bus_probe(device_t* dev);
void usb_bus_remove(device_t* dev);

int32 usb_clear_feature_halt(usb_dev_t *udev, uint8 ep_dci);


