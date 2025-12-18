#include "driver.h"
#include "device.h"
#include "bus.h"


//向总线注册驱动
void driver_register(driver_t *drv) {
    bus_type_t *bus = drv->bus;
    list_add_head(&bus->drv_list,&drv->bus_node);
    list_head_t *next_dev_node = bus->dev_list.next;
    while (next_dev_node != &bus->drv_list) {
        device_t *dev = CONTAINER_OF(next_dev_node,device_t,bus_node);
        if (bus->match(dev,drv) && !dev->drv) {
            dev->drv = drv;
            drv->probe(dev);
        }
        next_dev_node = next_dev_node->next;
    }
}