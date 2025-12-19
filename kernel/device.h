#pragma once
#include "moslib.h"

struct bus_type_t;
struct driver_t;

typedef struct device_t{
    char  *name;                   // 设备名
    struct device       *parent;   // 父设备：USB设备的父是Hub/控制器；xHCI 的父通常是 PCI 设备
    struct bus_type_t   *bus;      // 设备属于哪个 bus
    struct driver_t     *drv;      // 当前绑定的 driver；未绑定则为 NULL
    void *private;                 //设备私有数据指针
    list_head_t         bus_node;  // 挂到 bus->dev_list
} device_t;

void device_register(device_t *dev);