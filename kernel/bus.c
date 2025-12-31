#include "bus.h"
#include "pcie.h"

//pcie总线
bus_type_t pcie_bus;

//usb总线
bus_type_t usb_bus;

int pcie_bus_match(device_t *dev,driver_t *drv);
int pcie_bus_probe(device_t *dev);
void pcie_bus_remove(device_t *dev);
void pcie_bus_init(void);

//创建一个pcie总线和usb总线
INIT_TEXT void bus_init(void){
    pcie_bus.name = "PCIe Bus";
    pcie_bus.match = pcie_bus_match;
    pcie_bus.probe = pcie_bus_probe;
    pcie_bus.remove = pcie_bus_remove;
    list_head_init(&pcie_bus.dev_list);
    list_head_init(&pcie_bus.drv_list);   //创建pcie总线
    pcie_bus_init();                      //pcie总线初始化

    usb_bus.name = "USB Bus";
    usb_bus.match = NULL;
    usb_bus.probe = NULL;
    usb_bus.remove = NULL;
    list_head_init(&usb_bus.dev_list);
    list_head_init(&usb_bus.drv_list);   //创建usb总线
}