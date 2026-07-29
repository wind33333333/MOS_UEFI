#pragma once

#include "moslib.h"

struct input_dev_t;

/* STREAMING_CHUNK:定义私有属性(Usage)与公有属性(Field)结构体... */
// 数据的【私有属性】 (What it means)
typedef struct {
    uint32 hid_id; // 硬件协议里的完整用途 ID (Page | ID)
    uint16 event_type; // TheresaOS 内部事件类型 (如 EV_KEY, EV_REL)
    uint16 event_code; // TheresaOS 内部具体键码 (如 KEY_A, REL_X)
} hid_usage_t;


typedef struct hid_field_t {
    // === 通道与路由属性 ===
    uint8 report_type; // 0:INPUT, 1:OUTPUT, 2:FEATURE
    uint8 report_id; // 报文 ID

    // === 物理寻址地图 (核心) ===
    uint32 report_count; // 元素个数 (以此为准严格分配内存)
    uint32 bit_offset; // 在 raw buffer 中的绝对起始偏移量
    uint32 bit_size; // 单个元素的 bit 数量

    // === 硬件语义属性 ===
    uint32 application_id;
    uint16 usage_page; // 全局用途页
    uint32 flags; // Data/Const, Array/Var 等标志位
    uint16 usage_min; // Array 模式下的身份下限
    uint16 usage_max; // Array 模式下的身份上限

    // === 量纲与物理范围 ===
    int32 logical_min; // 发送的数值逻辑下限
    int32 logical_max;
    int32 physical_min; // 代表的物理真实下限
    int32 physical_max;
    uint32 unit; // 物理单位
    int32 unit_exponent; // 单位指数

    // === (强制按 report_count 分配，紧接在结构体尾部) ===
    uint32 max_usages;   // 内存中实际展开的 Usage 数量 (例如：256)
    hid_usage_t usages[]; //柔性数组动态计算长度
} hid_field_t;

/*
 * hid_dev: HID 物理设备实例
 * 作为底层 USB 驱动和上层输入子系统之间的桥梁。
 */
typedef struct hid_dev_t {
    // 以下为 TheresaOS 底层 USB 通信所需的上下文
    struct usb_urb_t *int_urb; // 中断传输的 URB 指针
    uint8 *report_buf; // 接收数据的 Raw Buffer
    struct usb_if_t *uif; // 绑定的 USB 接口实例 (usb_if)

    struct input_dev_t *input;

    uint32 field_count;    // 数量统计
    hid_field_t *fields[];  // field柔性指针数组
} hid_dev_t;

struct usb_drv_t *create_usb_hid_driver();

