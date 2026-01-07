#include "bus.h"
#include "pcie.h"

//pcie总线
bus_type_t pcie_bus_type;

//usb总线
bus_type_t usb_bus_type;

int pcie_bus_match(device_t *dev,driver_t *drv);
void pcie_bus_init(void);

//创建一个pcie总线和usb总线
INIT_TEXT void bus_init(void){
    pcie_bus_type.name = "PCIe Bus Type";
    pcie_bus_type.match = pcie_bus_match;
    list_head_init(&pcie_bus_type.dev_list);
    list_head_init(&pcie_bus_type.drv_list);   //创建pcie总线
    pcie_bus_init();                      //pcie总线初始化

    usb_bus_type.name = "USB Bus Type";
    usb_bus_type.match = NULL;
    list_head_init(&usb_bus_type.dev_list);
    list_head_init(&usb_bus_type.drv_list);   //创建usb总线
}