#include "driver.h"
#include "bus.h"



void driver_register(driver_t *drv) {
    list_add_head(&drv->bus->drv_list,&drv->bus_node);
}