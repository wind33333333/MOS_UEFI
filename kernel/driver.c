#include "driver.h"
#include "device.h"
#include "bus.h"


//向总线注册驱动
void driver_register(driver_t *drv) {
    bus_type_t *bus = drv->bus;
    list_add_head(&bus->drv_list,&drv->bus_node);
    for (list_head_t *next_dev_node = bus->dev_list.next;next_dev_node != &bus->dev_list;next_dev_node = next_dev_node->next){
        device_t *dev = CONTAINER_OF(next_dev_node,device_t,bus_node);
        if (dev->drv) continue;  //设备驱动存，跳过设备。
        if (bus->match(dev,drv)) {
            dev->drv = drv;
            bus->probe(dev);
        }
    }
}