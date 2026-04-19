#include "usb-hub.h"

#include "slub.h"

//获取hub描述符
int32 get_hub_desc(usb_dev_t *udev,usb_hub_desc_e usb_hub_desc,void *data_buf,uint16 length) {
    // 组装hub类 Setup 包
    usb_setup_packet_t setup_pkg = {0};
    setup_pkg.recipient = USB_RECIP_DEVICE;
    setup_pkg.req_type  = USB_REQ_TYPE_CLASS;
    setup_pkg.dtd       = USB_DIR_IN;
    setup_pkg.request   = USB_REQ_GET_DESCRIPTOR;
    setup_pkg.value     = usb_hub_desc<<8;
    setup_pkg.index     = 0;
    setup_pkg.length    = length;

    // 直接通过 EP0 发送控制传输并透传错误码
    return usb_control_msg(udev, &setup_pkg, data_buf);
}

//hub驱动
int32 usb_hub_probe(usb_if_t *uif,usb_id_t *uid) {
    usb_dev_t *udev = uif->udev;

    if (udev->port_speed > USB_HIGH_SPEED) {
        // ==========================================
        // 🚀 USB 3.0 (SuperSpeed) Hub 处理逻辑
        // 描述符类型：0x2A，长度永远固定为 12 字节！
        // ==========================================
        usb_hub3_desc_t *hub3_desc = kzalloc_dma(sizeof(usb_hub3_desc_t));

        // 一步到位，直接吞下 12 字节！
        int32 ret = get_hub_desc(udev, USB_HUB3_DESC, &hub3_desc, 12);
        if (ret < 0) return ret;

    }else {
        // ==========================================
        // 🐢 USB 2.0/1.1 (High/Full/Low Speed) Hub 处理逻辑
        // 描述符类型：0x29，变长地雷，必须踩两步！
        // ==========================================
        usb_hub2_desc_t *hub2_desc = kzalloc_dma(71) ;

        // 第一步：先读 8 字节探路
        int32 ret = get_hub_desc(udev, USB_HUB2_DESC, hub2_desc, 8);
        if (ret < 0) return ret;

        // 第二步：算出真实物理长度，再次读取
        uint8 num_ports = hub2_desc->num_ports;
        uint16 real_len = 7 + ((num_ports / 8) + 1) * 2;

        ret = get_hub_desc(udev, USB_HUB2_DESC, hub2_desc, real_len);
        if (ret < 0) return ret;

    }

    while (1);
}

void usb_hub_remove(usb_if_t *usb_if) {

}

usb_drv_t *create_usb_hub_driver() {
    usb_drv_t *usb_drv = kzalloc(sizeof(usb_drv_t));
    usb_id_t *id_table = kzalloc(sizeof(usb_id_t)*1);
    id_table[0].if_class = 0x9;
    id_table[0].if_subclass = 0x0;
    id_table[0].if_protocol = 0x0;
    usb_drv->drv.name = "usb_hub";
    usb_drv->drv.id_table = id_table;
    usb_drv->probe = usb_hub_probe;
    usb_drv->remove = usb_hub_remove;
    return usb_drv;
}