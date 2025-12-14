#include "driver.h"
#include "bus.h"


//向总线注册驱动
void driver_register(driver_t *drv) {
    list_add_head(&drv->bus->drv_list,&drv->bus_node);
}