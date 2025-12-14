#include "device.h"

#include "bus.h"

//向总线注册设备
void device_register(device_t *dev) {
    list_add_head(&dev->bus->dev_list, &dev->bus_node);
}
