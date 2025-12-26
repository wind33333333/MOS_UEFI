#include "device.h"
#include "driver.h"
#include "bus.h"

//向总线注册设备
void device_register(device_t *dev) {
    bus_type_t *bus = dev->bus;
    list_add_head(&bus->dev_list, &dev->bus_node);
    for (list_head_t *next_drv_node = bus->drv_list.next;next_drv_node != &bus->drv_list;next_drv_node = next_drv_node->next){
        driver_t *drv = CONTAINER_OF(next_drv_node,driver_t,bus_node);
        if (bus->match(dev,drv)) {
            bus->probe(dev);
            break;
        }
    }
}