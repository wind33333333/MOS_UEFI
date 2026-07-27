#include "driver.h"
#include "device.h"
#include "bus.h"


//向总线注册驱动
void driver_register(driver_t *drv) {
    bus_type_t *bus = drv->bus;
    list_add_head(&bus->drv_list,&drv->bus_node); //把驱动添加到总线驱动链表
    for (list_head_t *next_dev_node = bus->dev_list.next; next_dev_node != &bus->dev_list; next_dev_node = next_dev_node->next){ //遍历总线设备链表是否有设备匹配驱动
        device_t *dev = CONTAINER_OF(next_dev_node,device_t,bus_node);
        if (!dev->drv && bus->match(dev,drv)) { //设备驱动存或不匹配，跳过设备。
            dev->drv = drv;     //把驱动挂到设备上
            bus->probe(dev);    //回调总线初始化设备
            drv->probe(dev);    //回调驱动初始化设备
        }
    }
}