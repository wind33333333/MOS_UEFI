#pragma once
#include "moslib.h"
#include "device.h"
#include "driver.h"
#include "xhci.h"


struct usb_dev_t;
struct usb_if_t;

//usb驱动id表
typedef struct usb_id_t {
    // interface class 匹配（最常用）
    uint8  if_class;
    uint8  if_subclass;
    uint8  if_protocol;
} usb_id_t;

//usb驱动
typedef struct{
    driver_t drv;
    int  (*probe)(struct usb_if_t *usb_if, usb_id_t *id);
    void (*remove)(struct usb_if_t *usb_if);
} usb_drv_t;

//usb端点
typedef struct usb_ep_t {
    //端点描述符
    uint8       ep_dci;            // 端点 1-31
    uint8       ep_type;           // 端点：控制/批量/中断/等时
    uint16      max_packet;        // wMaxPacketSize 解码后的最大包长（基础值）
    uint8       mult;              // USB 2.0 High-Speed 高带宽事务 (Mult) 处理 0=1 transaction, 1=2 trans, 2=3 trans
    uint8       interval;          // bInterval（中断/等时用；bulk 通常可忽略但保留）

    //超高速端点伴随描述符
    uint8       max_burst;         // USB3 bMaxBurst（0=1 burst；仅 SS/SSP 有意义）
    uint16      max_streams;         // bulk 端点支持的最大 stream 数（由 ss_comp->bmAttributes 解码，0 表示不支持 streams（BOT 一般用不到，UAS 可能需要）
    uint16      bytes_per_interval; // USB3 wBytesPerInterval（中断/等时重要）

    void        *extras_desc;    // 动态数组：紧随端点后的 class-specific/未知描述符块，枚举层不解释语义，交给类驱动（例如 UAS）按需解析
} usb_ep_t;

//usb替用接口
typedef struct usb_if_alt_t {
    struct usb_if_t *usb_if;
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

//端点
typedef struct endpoint_t{
    union {
        xhci_ring_t  transfer_ring;
        xhci_ring_t  *stream_rings;   // per-stream rings数组 (如果启用流)
    };
    uint32 streams_count;        // 2^max_streams_exp+1
}endpoint_t;

//USB设备
typedef struct usb_dev_t{
    uint8                           port_id;
    uint8                           slot_id;
    usb_device_descriptor_t*        dev_desc;           //设备描述符
    usb_config_descriptor_t*        config_desc;        //配置描述符
    usb_string_descriptor_t*        language_desc;      //语言描述符
    usb_string_descriptor_t*        manufacturer_desc;  //制造商描述符
    usb_string_descriptor_t*        product_desc;       //产品型号名描述符
    usb_string_descriptor_t*        serial_number_desc; //序列号描述符
    void*                           dev_ctx;           // 设备上下文
    endpoint_t                      eps[32];           // 端点0-31
    xhci_hcd_t*              xhcd;   // xhci控制器
    device_t                        dev;
    uint8                           interfaces_count;  // 接口数量
    usb_if_t                        *interfaces;       // 接口指针根据接口数量动态分配
    uint8                           *manufacturer;     // 制造商ascii字符
    uint8                           *product;          // 产品型号ascii字符
    uint8                           *serial_number;    // 序列号ascii字符
    struct usb_dev_t                *parent_hub;       // 上游 hub 的 usb_dev（roothub 则为 NULL）
    uint8                           parent_port;       // 插在 parent_hub 的哪个端口（1..N；roothub=0）

} usb_dev_t;

//获取需要input端点的上下文地址
static inline void *xhci_get_input_ctx_addr(xhci_hcd_t *xhcd,xhci_input_ctrl_ctx_t *input_ctx, uint32 ep_dci) {
    uint8 ctx_size = xhcd->ctx_size;
    return (uint8 *)input_ctx + ctx_size * (ep_dci + 1);
}


//获取需要端点的上下文地址
static inline void *xhci_get_ctx_addr(usb_dev_t *udev, uint32 ep_dci) {
    return (uint8*)udev->dev_ctx + udev->xhcd->ctx_size * ep_dci;
}


//获取下一个描述符
static inline void *usb_get_next_desc(usb_descriptor_head *head) {
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

#define MAX_STREAMS 6  //最大支持流数量（2^6=64）

int usb_bus_match(device_t* dev,driver_t* drv);
int usb_bus_probe(device_t* dev);
void usb_bus_remove(device_t* dev);

struct pcie_dev_t;
void usb_dev_scan(struct pcie_dev_t *xhci_dev);
int usb_set_config(usb_dev_t *usb_dev);
int usb_set_interface(usb_if_t *usb_if);
int usb_endpoint_init(usb_if_alt_t *if_alt);
int32 usb_clear_feature_halt(usb_dev_t *usb_dev, uint8 ep_dci);

//注册usb接口
static inline void usb_if_register(usb_if_t* usb_if);

//注册usb设备
static inline void usb_dev_register(usb_dev_t *usb_dev);

//注册usb驱动
void usb_drv_register(usb_drv_t *usb_drv);