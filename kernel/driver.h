#pragma once
#include "moslib.h"

struct bus_type_t;
struct device_t;

typedef struct driver_t{
    char *name;                              // 驱动名：如 "xhci_pci" "usb_storage" "uas"
    struct bus_type_t  *bus;                  // 驱动属于哪个 bus（必须与设备 bus 一致才可能匹配）
    int  (*probe)(struct device_t *dev);      // probe：当 match 成功后调用；负责初始化设备、分配 driver_data、开启中断/DMA 等
    void (*remove)(struct device_t *dev);      // remove：设备移除/解绑时调用；负责关闭硬件、释放 driver_data
    list_head_t bus_node;               // 挂到 bus->drv_list
} driver_t;

void driver_register(driver_t *drv);



