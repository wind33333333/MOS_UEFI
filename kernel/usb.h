#pragma once
#include "xhci.h"

typedef struct {
    UINT8 b_request_type;
    UINT8 b_request;
    UINT16 w_value;
    UINT16 w_index;
    UINT16 w_length;
} __attribute__((packed)) usb_setup_packet_t;

typedef struct {
    UINT8  bLength;            // 描述符长度（固定 0x12 = 18 字节）
    UINT8  bDescriptorType;    // 描述符类型（固定 0x01，表示 Device Descriptor）
    UINT16 bcdUSB;             // USB 规范版本（BCD 格式，例如 0x0200 表示 USB 2.0，0x0300 表示 USB 3.0）
    UINT8  bDeviceClass;       // 设备类代码（0x00: 类在接口级定义；0x01: Audio；0x02: CDC；0x03: HID 等）
    UINT8  bDeviceSubClass;    // 设备子类代码（依赖于 bDeviceClass，例如 HID 的子类 0x01 表示 Boot Interface）
    UINT8  bDeviceProtocol;    // 设备协议代码（例如 HID Boot 时 0x01 表示 Keyboard）
    UINT8  bMaxPacketSize0;    // 控制端点 0 (EP0) 的最大包大小（Low Speed: 8；Full Speed: 8/16/32/64；High Speed: 64；SuperSpeed: 512）
    UINT16 idVendor;           // 厂商 ID (VID)，USB-IF 分配的唯一 ID（例如 Intel: 0x8086）
    UINT16 idProduct;          // 产品 ID (PID)，厂商定义，用于驱动匹配
    UINT16 bcdDevice;          // 设备版本（BCD 格式，例如 0x0100 表示 1.00）
    UINT8  iManufacturer;      // 制造商字符串索引（0 表示无字符串）
    UINT8  iProduct;           // 产品字符串索引
    UINT8  iSerialNumber;      // 序列号字符串索引
    UINT8  bNumConfigurations; // 支持的配置数量（通常 1 或更多）
} __attribute__((packed)) usb_device_descriptor_t;

typedef struct {
    UINT8 bLength;
    UINT8 bDescriptorType;
    UINT16 wTotalLength;
    UINT8 bNumInterfaces;
    UINT8 bConfigurationValue;
    UINT8 iConfiguration;
    UINT8 bmAttributes;
    UINT8 bMaxPower;
} __attribute__((packed)) usb_config_descriptor_t;

typedef struct {
    UINT8 bLength;
    UINT8 bDescriptorType;
    UINT8 bInterfaceNumber;
    UINT8 bAlternateSetting;
    UINT8 bNumEndpoints;
    UINT8 bInterfaceClass;
    UINT8 bInterfaceSubClass;
    UINT8 bInterfaceProtocol;
    UINT8 iInterface;
} __attribute__((packed)) usb_interface_descriptor_t;

//USB设备
typedef struct {
    UINT32                          port_id;
    UINT32                          slot_id;
    xhci_ring_t                     ep0_trans_ring;               //端点0传输环虚拟地址 63-1位:为地址 0位:C
    usb_device_descriptor_t         *dev_desc;
    usb_config_descriptor_t         *config_desc;
    usb_interface_descriptor_t      *interface_desc;
    list_head_t list;
}usb_dev_t;

#pragma pack(pop)

void usb_init(xhci_regs_t *xhci_regs);