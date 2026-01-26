#pragma once
#include "moslib.h"

struct bus_type_t;
struct driver_t;

typedef struct device_type_t {
    const char *name;
} device_type_t;

/* 设备对象：所有具体设备（pcie_dev/usb_device/usb_interface）都嵌入它 */
typedef struct device_t{
    char                *name;          /* 设备名：用于日志与调试（可带路径信息） */
    list_head_t         bus_node;       /* 挂到 bus->devices 链表 */
    list_head_t         child_node;     /* 挂到 parent->children 链表 */
    list_head_t         child_list;     /* 子设备链表（用于向下遍历拓扑） */
    device_type_t       *type;          /* 设备类型相关 */
    struct device_t     *parent;        /* 物理拓扑父设备（桥、Hub、控制器等） */
    struct bus_type_t   *bus;           /* 所属总线 */
    struct driver_t     *drv;           /* 绑定的驱动（已 probe 成功） */
    void                *drv_data;      /* 驱动私有数据（probe 里设置） */
} device_t;

void device_register(device_t *dev);