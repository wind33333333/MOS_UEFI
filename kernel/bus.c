#include "bus.h"
#include "pcie.h"
#include "usb-core.h"
#include "drivers/usb/include/usb-hub.h"
#include "usb-bus.h"
#include "drivers/usb/include/usb-dev.h"
#include "xhci-service.h"
#include "xhci-ring.h"

//pcie总线
bus_type_t pcie_bus_type;

//usb总线
bus_type_t usb_bus_type;

//scsi总线
bus_type_t scsi_bus_type;

extern usb_drv_t *create_usb_storage_driver();

extern usb_drv_t *create_usb_hub_driver();


//创建一个pcie总线和usb总线
INIT_TEXT void bus_init(void){

    scsi_bus_type.name = "SCSI Bus Type";
    scsi_bus_type.match = NULL;
    scsi_bus_type.probe = NULL;
    scsi_bus_type.remove = NULL;
    list_head_init(&scsi_bus_type.dev_list);
    list_head_init(&scsi_bus_type.drv_list);

    usb_bus_type.name = "USB Bus Type";
    usb_bus_type.match = usb_bus_match;
    usb_bus_type.probe = usb_bus_probe;
    usb_bus_type.remove = usb_bus_remove;
    list_head_init(&usb_bus_type.dev_list);
    list_head_init(&usb_bus_type.drv_list);   //创建usb总线

    void usb_event_queue_init(void);

    pcie_bus_type.name = "PCIe Bus Type";
    pcie_bus_type.match = pcie_bus_match;
    pcie_bus_type.probe = pcie_bus_probe;
    pcie_bus_type.remove = pcie_bus_remove;
    list_head_init(&pcie_bus_type.dev_list);
    list_head_init(&pcie_bus_type.drv_list);   //创建pcie总线
    pcie_bus_init();                           //pcie总线初始化

    usb_drv_t *usb_storage_drv = create_usb_storage_driver();
    usb_drv_register(usb_storage_drv);

    usb_drv_t *usb_hub_driver = create_usb_hub_driver();
    usb_drv_register(usb_hub_driver);



    // =========================================================================
    // 3. 系统级调度中心 (守护程序示范)
    // =========================================================================

    /**
     * @brief USB 核心底半部守护进程 (放在操作系统的 main_loop 中不断轮询)
     * * 这种架构的魔力在于：你在选区代码中的 `xhci_process_port_event` 以及
     * 外接 Hub 的 `usb_hub_process_port_event`，都被移到了这里慢条斯理地执行！
     */
        usb_port_event_t evt;
        // 一直掏出来处理
        while (1) {
            asm_pause();
            if (usb_event_queue_pop(&evt) == FALSE) continue;
            switch (evt.type) {
                case USB_EVENT_XHCI_ROOT_PORT: {
                    xhci_hcd_t *xhcd = (xhci_hcd_t *)evt.parent_dev;
                    // 👉 这里调用你写好的原生端口超级状态机
                    xhci_process_port_event(xhcd, evt.port_num);
                    break;
                }
                case USB_EVENT_HUB_WORK: {
                    usb_dev_t *hub_dev = (usb_dev_t *)evt.parent_dev;
                    usb_hub_t *hub = hub_dev->drv_data;

                    // 1. 在安全的底半部上下文中，慢慢遍历 working_bitmap
                    for (uint8 port_num = 1; port_num <= hub_dev->hub_num_ports; port_num++) {
                        uint8 byte_idx = port_num / 8;
                        uint8 bit_idx = port_num % 8;

                        if (hub->port_bitmap_status[byte_idx] & (1 << bit_idx)) {
                            // 👉 去执行清除标志、复位等阻塞型控制传输
                            usb_hub_process_port_event(hub_dev, port_num);
                        }
                    }

                    // 2. 💥 所有报警端口都已经处理完毕，硬件标志已经被彻底抹除！
                    // 此时由底半部负责统一将轮询 URB“复活”！
                    if (hub->int_urb->is_done == TRUE) {
                        asm_mem_set(hub->port_bitmap_status,0,32);
                        hub->int_urb->is_done = FALSE;
                        xhci_submit_urb(hub->int_urb);
                    }
                    break;
                }
                default:
                    break;
            }
        }


}