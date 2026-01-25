#pragma once
#include "moslib.h"
#include "device.h"
#include "driver.h"
#include "xhci.h"

#pragma pack(push,1)

typedef struct {
    uint8 length; // 描述符长度
    uint8 descriptor_type; // 描述符类型
}usb_descriptor_head;

//region usb描述符
/*usb设备描述符
描述符长度，固定为 18 字节（0x12）
描述符类型，固定为 0x01（设备描述符）*/
typedef struct {
    usb_descriptor_head head;
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
} usb_device_descriptor_t;
#define USB_DEVICE_DESCRIPTOR 0x1

/*usb配置描述符
描述符长度，固定为 9 字节（0x09）
描述符类型，固定为 0x02（配置描述符）*/
typedef struct {
    usb_descriptor_head head;
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
} usb_config_descriptor_t;
#define USB_CONFIG_DESCRIPTOR 0x2


/*USB 字符串描述符
描述符长度（含头部和字符串）
描述符类型 = 0x03*/
typedef struct {
    usb_descriptor_head head;
    uint16 string[]; // UTF-16LE 编码的字符串内容（变长数组）
} usb_string_descriptor_t;
#define USB_STRING_DESCRIPTOR 0x3

/*接口描述符
描述符长度，固定为 9 字节（0x09）
描述符类型，固定为 0x04（接口描述符）*/
typedef struct {
    usb_descriptor_head head;
    uint8 interface_number; // 接口编号，从 0 开始，标识该接口
    uint8 alternate_setting; // 备用设置编号，同一接口的不同配置（通常为 0）
    uint8 num_endpoints; // 该接口使用的端点数量（不包括端点 0）
    uint8 interface_class; // 接口类代码，定义接口功能（如 0x03 表示 HID，0x08 表示 Mass Storage）
    uint8 interface_subclass; // 接口子类代码，进一步细化接口类（如 HID 的子类）
    uint8 interface_protocol; // 接口协议代码，定义类内协议（如 HID 的 0x01 表示键盘）
    uint8 interface_index; // 接口字符串描述符索引（0 表示无）
} usb_interface_descriptor_t;
#define USB_INTERFACE_DESCRIPTOR 0x4

/*端点描述符
描述符长度（固定7字节）
描述符类型：0x05 = 端点描述符*/
typedef struct {
    usb_descriptor_head head;
    uint8 endpoint_address; // 端点地址：位7方向(0=OUT,主机→设备 1=IN，设备→主机)，位3-0端点号
    uint8 attributes; // 传输类型：0x00=控制，0x01=Isochronous，0x02=Bulk，0x03=Interrupt
#define USB_EP_CONTROL   0x0   //ep0端点
#define USB_EP_ISOCH     0x1   //等时传输（实时音视频流，带带宽保证，不保证重传）
#define USB_EP_BULK      0x2   //批量传输（大容量数据，可靠，有重传机制，如 U 盘数据块）
#define USB_EP_INTERRUPT 0x3   //中断传输（小包，低延迟，周期性轮询，如键盘鼠标 HID 报告)
    uint16 max_packet_size; // 该端点的最大包长（不同速度有不同限制）
    uint8 interval; // 轮询间隔（仅中断/同步传输有意义）
} usb_endpoint_descriptor_t;
#define USB_ENDPOINT_DESCRIPTOR 0x5

/*  超高速端点伴随描述符
 *  uint8  bLength;            // 固定 6
    uint8  bDescriptorType;    // 0x30 表示 SuperSpeed Endpoint Companion Descriptor*/
typedef struct {
    usb_descriptor_head head;
    uint8 max_burst; // 每次突发包数（0-15），实际表示突发数+1
    uint8 attributes; // 位 4:0 Streams 支持数 (Bulk)，或多事务机会 (Isoch)
    uint16 bytes_per_interval; // 对于 Isoch/Interrupt，最大字节数
} usb_superspeed_companion_descriptor_t;
#define USB_SUPERSPEED_COMPANION_DESCRIPTOR 0x30

/* USA 管道情况描述符
 * uint8  bLength;            // 固定 4
 * uint8  bDescriptorType;    // 0x24
 */
typedef struct {
    usb_descriptor_head head;
    uint8  pipe_id;              // 端点用途标识 1=command_out 2=status_in 3=bulk_in 4=bulk_out
#define USB_PIPE_COMMAND_OUT    1
#define USB_PIPE_STATUS_IN      2
#define USB_PIPE_BULK_IN        3
#define USB_PIPE_BULK_OUT       4
    uint8  reserved;
} usb_usa_pipe_usage_descriptor_t;
#define USB_USA_PIPE_USAGE_DESCTIPTOR 0x24

/*HID 类描述符（可选
描述符长度
描述符类型：0x21 = HID 描述符*/
typedef struct {
    usb_descriptor_head head;
    uint16 hid; // HID 版本号
    uint8 country_code; // 国家代码（0=无）
    uint8 num_descriptors; // 后面跟随的子描述符数量
    // 后面通常跟 HID 报告描述符（类型0x22）等
} usb_hid_descriptor_t;
#define USB_HID_DESCRIPTOR 0x21

/*HUB 类描述符（可选）
描述符长度
描述符类型：0x29 = HUB 描述符*/
typedef struct {
    usb_descriptor_head head;
    uint8 num_ports; // hub 下行端口数量
    uint16 hub_characteristics; // hub 特性位（供电方式、过流保护等）
    uint8 power_on_to_power_good; // 端口上电到电源稳定的时间（单位2ms）
    uint8 hub_control_current; // hub 控制器所需电流
    // 之后还会跟一个可变长度的 DeviceRemovable 和 PortPwrCtrlMask
} usb_hub_descriptor_t;

/* ---------------- USB Hub 相关描述符 ---------------- */
#define USB_DESC_TYPE_HUB           0x29  /* Hub 描述符 Hub Descriptor */

//endregion

#pragma pack(pop)

struct usb_dev_t;
struct usb_if_t;

//usb驱动id表
typedef struct {
    // interface class 匹配（最常用）
    uint8  if_class;
    uint8  if_subclass;
    uint8  if_protocol;
} usb_id_t;

//usb接口驱动
typedef struct{
    driver_t drv;
    int  (*probe)(struct usb_if_t *ifc, usb_id_t *id);
    void (*remove)(struct usb_if_t *ifc);
} usb_if_drv_t;


//usb端点
typedef struct {
    usb_endpoint_descriptor_t *ep_desc;
}usb_ep_t;

//usb替用接口
typedef struct usb_if_alt_t {
    usb_interface_descriptor_t *if_desc;  // 指向 cfg_raw 内
    uint8 altsetting;

    uint8 if_class;
    uint8 if_subclass;
    uint8 if_protocol;

    uint8 ep_count;     // 端点数量
    usb_ep_t *eps;      // 可选：解析后的端点数组
} usb_if_alt_t;

//usb接口
typedef struct usb_if_t {
    struct usb_dev_t *usb_dev;
    uint8 if_num;
    uint8 alt_count;
    usb_if_alt_t *alts;
    usb_if_alt_t *cur_alt;   // 或 cur_alt_idx
    device_t dev;
} usb_if_t;

//USB设备
typedef struct usb_dev_t {
    uint8                           port_id;
    uint8                           slot_id;
    usb_device_descriptor_t*        usb_dev_desc;       //usb设备描述符
    usb_config_descriptor_t*        usb_config_desc;    //usb配置描述符
    xhci_device_context_t*          dev_context;       // 设备上下文
    xhci_ring_t                     ep0;               // 控制端点
    xhci_ring_t                     eps[32];           // 端点0-31
    xhci_controller_t*              xhci_controller;   // xhci控制器
    device_t                        dev;
    struct usb_dev_t                *parent_hub;       // 上游 hub 的 usb_dev（roothub 则为 NULL）
    uint8_t                         parent_port;       // 插在 parent_hub 的哪个端口（1..N；roothub=0）
    uint8                           interfaces_count;  // 接口数量
    usb_if_t                        *interfaces;       // 接口指针根据接口数量动态分配
} usb_dev_t;

///////////////////////////

typedef struct usb_port {
    uint8_t port_id;              // 1..N
    uint8_t connected:1;
    uint8_t enabled:1;
    uint8_t resetting:1;

    usb_dev_t *child;             // 端口当前挂的子设备（NULL 表示空）
} usb_port_t;

typedef enum {
    HUB_KIND_ROOT,                // roothub（端口变化来自 HCD/xHCI）
    HUB_KIND_EXTERNAL,            // 外部 hub（端口变化来自 hub interrupt endpoint）
} hub_kind_t;

typedef struct usb_hub_t {
    hub_kind_t kind;

    /* 归属：哪个 hub interface 管理这份 hub 状态 */
    usb_if_t *intf;        // 注意：归属到 interface（Linux 风格）
    usb_dev_t *hdev;              // hub 对应的物理设备（便于访问拓扑/HCD）

    /* 端口管理 */
    uint8_t port_count;
    usb_port_t *ports;

    /* 事件与并发（精简版） */
    uint32_t pending_bitmap;      // 哪些端口有变化待处理（可选但很有用）
} usb_hub_t;


//获取下一个描述符
static inline void *get_next_desc(usb_descriptor_head *head) {
    return (uint8*)head + head->length;
}

//配置描述符结束地址
static inline void *usb_cfg_end(usb_config_descriptor_t *usb_config_desc)
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

struct pcie_dev_t;
void usb_dev_scan(struct pcie_dev_t *xhci_dev);