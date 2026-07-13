#pragma once

#include "moslib.h"
#include "driver.h"




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

struct device_t;

int usb_bus_match(struct device_t *dev, driver_t *drv);
int usb_bus_probe(struct device_t *dev);
void usb_bus_remove(struct device_t *dev);

struct usb_dev_t;

void usb_drv_register(usb_drv_t *usb_drv);
void usb_dev_register(struct usb_dev_t *usb_dev);
void usb_if_register(struct usb_dev_t *udev) ;