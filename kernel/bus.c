#include "bus.h"

//pcie总线
bus_type_t pcie_bus;

//usb总线
bus_type_t usb_bus;



//创建一个pcie总线和usb总线
void bus_init(void) {
    pcie_bus.name = "PCIe Bus";
    pcie_bus.match = NULL;
    pcie_bus.probe = NULL;
    pcie_bus.remove = NULL;
    pcie_bus.shutdown = NULL;
    list_head_init(&pcie_bus.dev_list);
    list_head_init(&usb_bus.drv_list);

    usb_bus.name = "USB Bus";
    usb_bus.match = NULL;
    usb_bus.probe = NULL;
    usb_bus.remove = NULL;
    usb_bus.shutdown = NULL;
    list_head_init(&usb_bus.dev_list);
    list_head_init(&usb_bus.drv_list);
}