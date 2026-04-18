#include "usb-hub.h"


int32 get_hub_desc(usb_if_t *uif,usb_hub_desc_e usb_hub_desc,void *data_buf,uint16 length) {
    usb_dev_t *udev = uif->udev;
    usb_setup_packet_t setup_pkg = {0};

    // 组装hub类 Setup 包
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

int32 usb_hub_probe(usb_if_t *uif,usb_id_t *uid) {
    usb_dev_t *udev = uif->udev;

    if (udev->port_speed > USB_HIGH_SPEED) {
        // ==========================================
        // 🚀 USB 3.0 (SuperSpeed) Hub 处理逻辑
        // 描述符类型：0x2A，长度永远固定为 12 字节！
        // ==========================================
        usb_hub3_desc_t hub3_desc;

        // 一步到位，直接吞下 12 字节！
        int32 ret = get_hub_desc(intf, HUB_DESC_TYPE_30, &hub3_desc, 12);
        if (ret < 0) return ret;

        color_printk(GREEN, BLACK, "[Hub 3.0] 一次读取成功！端口数: %d\n", hub3_desc.num_ports);
        // 开始你的 USB 3.0 路由表 (Route String) 和端口操作...
    }else {

    }


}