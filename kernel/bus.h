#pragma once
#include "moslib.h"

typedef struct device_t;
typedef struct driver_t;

typedef struct {
    const char *name;                                // 总线名字：如 "pci" "usb" "platform"
    int (*match)(device_t *dev,driver_t *drv);       // 匹配函数：决定某个设备(dev)是否能由某个驱动(drv)管理
    int (*probe)(device_t *dev);                     // 总线层 probe：如果提供，则由总线统一完成初始化流程，并在内部调用 drv->probe
    void (*remove)(device_t *dev);                   // 总线层 remove：设备解绑/移除时调用；通常内部会转调 drv->remove
    void (*shutdown)(device_t *dev);                 // 总线层 shutdown：关机/重启前的收尾
    list_head_t dev_list;                            // struct device::bus_node 挂在这里
    list_head_t drv_list;                            // struct device_driver::bus_node 挂在这里
} bus_type_t;

void bus_register(bus_type_t *bus);
void bus_rescan(bus_type_t *bus);