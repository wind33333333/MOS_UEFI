#pragma once
#include "moslib.h"

struct bus_type_t;
struct device_t;

typedef struct driver_t{
    char                *name;                              // 驱动名：如 "xhci_pci" "usb_storage" "uas"
    struct bus_type_t   *bus;                               // 驱动属于哪个 bus（必须与设备 bus 一致才可能匹配）
    void                *id_table;                          /* 匹配表：不同总线定义不同格式（PCIe 用 vendor/device/class；USB 用 idVendor/idProduct/InterfaceClass...） */
    int                 (*probe)(struct device_t *dev);     // probe：统一接口
    void                (*remove)(struct device_t *dev);    // remove：统一接口
    list_head_t         bus_node;                           /* 挂到 bus->drivers 链表 */
} driver_t;

void driver_register(driver_t *drv);



