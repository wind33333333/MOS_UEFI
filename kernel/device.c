#include "device.h"
#include "driver.h"
#include "bus.h"

//向总线注册设备
void device_register(device_t *dev) {
    bus_type_t *bus = dev->bus;
    list_add_head(&bus->dev_list, &dev->bus_node); //把设备挂到总线上
    if (dev->parent) list_add_head(&dev->parent->child_list,&dev->child_node); //如果父设备存在，把设备挂到父设备的子链
    list_head_init(&dev->child_list);//初始化设备子链
    for (list_head_t *next_drv_node = bus->drv_list.next;next_drv_node != &bus->drv_list;next_drv_node = next_drv_node->next){//遍历总线驱动链表是否匹配设备
        driver_t *drv = CONTAINER_OF(next_drv_node,driver_t,bus_node);
        if (bus->match(dev,drv)) {
            dev->drv = drv;     //把驱动挂到设备上
            bus->probe(dev);    //回调总线初始化设备
            drv->probe(dev);    //回调驱动初始化设备
            break;      //驱动匹配成功结束
        }
    }
}