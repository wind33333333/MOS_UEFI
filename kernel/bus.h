#pragma once
#include "moslib.h"

struct device_t;
struct driver_t;

typedef struct bus_type_t{
    char *name;                                                    // 总线名字：如 "pci" "usb" "platform"
    int (*match)(struct device_t *dev,struct driver_t *drv);       // 匹配函数：决定某个设备(dev)是否能由某个驱动(drv)管理
    int (*probe)(struct device_t *dev);                            // 总线层 probe：如果提供，则由总线统一完成初始化流程，并在内部调用 drv->probe
    void (*remove)(struct device_t *dev);                          // 总线层 remove：设备解绑/移除时调用；通常内部会转调 drv->remove
    void (*shutdown)(struct device_t *dev);                        // 总线层 shutdown：关机/重启前的收尾
    list_head_t dev_list;                                          // struct device::bus_node 挂在这里
    list_head_t drv_list;                                          // struct device_driver::bus_node 挂在这里
} bus_type_t;

void bus_init(void);