#pragma once
#include "moslib.h"

typedef struct device_t;

typedef struct {
    const char *name;                  // 驱动名：如 "xhci_pci" "usb_storage" "uas"
    bus_type_t  *bus;                  // 驱动属于哪个 bus（必须与设备 bus 一致才可能匹配）
    int  (*probe)(device_t *dev);      // probe：当 match 成功后调用；负责初始化设备、分配 driver_data、开启中断/DMA 等
    void (*remove)(device_t *dev);      // remove：设备移除/解绑时调用；负责关闭硬件、释放 driver_data
    void (*shutdown)(device_t *dev);    // shutdown：关机/重启前调用（可选）
    list_head_t bus_node;               // 挂到 bus->drv_list
} driver_t;

void driver_register(driver_t *drv);
int device_bind(device_t *dev, driver_t *drv);



