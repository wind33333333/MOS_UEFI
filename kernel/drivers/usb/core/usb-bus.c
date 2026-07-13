#include "../include/usb-bus.h"
#include "../include/usb-dev.h"

device_type_t usb_dev_type = {"usb-dev"};
device_type_t usb_if_type = {"usb-if"};

extern struct bus_type_t usb_bus_type;

/**
 * @brief [工业级] 总线匹配引擎：扫描接口下的【所有备用接口】，检查是否与驱动匹配
 * @param  usb_if 目标 USB 接口对象
 * @param  drv    尝试挂载的驱动对象
 * @return usb_id_t* 命中匹配的 ID 规则指针，未命中返回 NULL
 */
static inline usb_id_t *usb_match_id(usb_if_t *usb_if, driver_t *drv) {
    // 🌟 终极防线：拦截所有非法野指针
    if (!usb_if || !usb_if->if_alts || !usb_if->udev) return NULL;
    if (!drv || !drv->id_table) return NULL;

    usb_dev_desc_t *dev_desc = usb_if->udev->dev_desc;

    // 🌟 第一层循环：遍历该接口下所有的【备用接口 (Alternate Settings)】
    for (uint8 alt_idx = 0; alt_idx < usb_if->num_if_alts; alt_idx++) {
        usb_if_desc_t *if_desc = usb_if->if_alts[alt_idx].if_desc;

        // 🌟 第二层循环：遍历驱动程序的 ID 表
        for (usb_id_t *id = drv->id_table; id->match_flags != 0; id++) {

            // 1. 匹配厂商 ID (VID)
            if ((id->match_flags & USB_MATCH_VENDOR) &&
                id->vendor_id != dev_desc->vendor_id) {
                continue;
                }

            // 2. 匹配产品 ID (PID)
            if ((id->match_flags & USB_MATCH_PRODUCT) &&
                id->product_id != dev_desc->product_id) {
                continue;
                }

            // 3. 匹配接口大类 (Class)
            if ((id->match_flags & USB_MATCH_INT_CLASS) &&
                id->if_class != if_desc->interface_class) {
                continue;
                }

            // 4. 匹配接口子类 (Subclass)
            if ((id->match_flags & USB_MATCH_INT_SUBCLASS) &&
                id->if_subclass != if_desc->interface_subclass) {
                continue;
                }

            // 5. 匹配接口协议 (Protocol)
            if ((id->match_flags & USB_MATCH_INT_PROTOCOL) &&
                id->if_protocol != if_desc->interface_protocol) {
                continue;
                }

            // 🌟 核心突破：只要在任意一个备用接口中找到了匹配规则，立刻宣告匹配成功！
            // 驱动层可以通过 (id - drv->id_table) 知道是谁命中的，也可以通过 alt_idx 知道是哪个图纸通过了
            return id;
        }
    }

    // 遍历完所有备用接口下的所有规则，均未命中
    return NULL;
}

//usb总线层设备驱动匹配
int usb_bus_match(device_t *dev, driver_t *drv) {
    if (dev->type != &usb_if_type) return FALSE;
    usb_if_t *usb_if = CONTAINER_OF(dev, usb_if_t, dev);
    usb_id_t *id = usb_match_id(usb_if, drv);
    return id ? 1 : 0;
}

//usb总线层探测初始化回调
int usb_bus_probe(device_t *dev) {
}

//usb总线层卸载在回调
void usb_bus_remove(device_t *dev) {
}

//usb驱动层探测初始化回调
int usb_drv_probe(device_t *dev) {
    usb_if_t *usb_if = CONTAINER_OF(dev, usb_if_t, dev);
    usb_drv_t *usb_if_drv = CONTAINER_OF(dev->drv, usb_drv_t, drv);
    usb_id_t *id = usb_match_id(usb_if,dev->drv);
    usb_if_drv->probe(usb_if, id);
    return 0;
}

//usb驱动层卸载回调
void usb_drv_remove(device_t *dev) {
}


//注册usb驱动
void usb_drv_register(usb_drv_t *usb_drv) {
    usb_drv->drv.bus = &usb_bus_type;
    usb_drv->drv.probe = usb_drv_probe;
    usb_drv->drv.remove = usb_drv_remove;
    driver_register(&usb_drv->drv);
}

//注册usb设备
void usb_dev_register(usb_dev_t *usb_dev) {
    device_register(&usb_dev->dev);
}

//注册usb接口
void usb_if_register(usb_dev_t *udev) {
    uint8 num_ifs = udev->config_desc->num_interfaces;
    for (uint32 i = 0; i < num_ifs; i++) {
        usb_if_t *usb_if = &udev->ifs[i];
        if (usb_if != NULL) {
            // 触发系统级的 match/probe (比如唤醒 bot.c 或 uas.c 驱动)
            device_register(&usb_if->dev);
        }
    }
}
