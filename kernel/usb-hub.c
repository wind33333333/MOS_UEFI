#include "usb-hub.h"

#include "slub.h"

//获取hub描述符
static int32 get_hub_desc(usb_dev_t *udev,usb_hub_desc_e usb_hub_desc,void *data_buf,uint16 length) {
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


// ==========================================
// 🛠️ 内部核心函数 (不对外暴露，专干脏活累活)
// ==========================================
/**
 * @brief Hub 端口特征底层控制核心函数
 * @param req_cmd 决定是 SET_FEATURE (0x03) 还是 CLEAR_FEATURE (0x01)
 */
static int32 usb_port_feature_ctrl(usb_dev_t *udev, uint8 port_no, usb_port_feature_e feature, usb_request_e req_cmd) {
    usb_setup_packet_t setup_pkg = {0};

    // 组装通用的 Setup 包
    setup_pkg.recipient = USB_RECIP_OTHER;   // 接收者：Port
    setup_pkg.req_type  = USB_REQ_TYPE_CLASS;// 类请求
    setup_pkg.dtd       = USB_DIR_OUT;       // 方向：主机到设备

    setup_pkg.request   = req_cmd;           // 🌟 动态决定是 Set 还是 Clear
    setup_pkg.value     = feature;           // 填入特征选择器
    setup_pkg.index     = port_no;           // 端口号
    setup_pkg.length    = 0;                 // 无数据阶段

    return usb_control_msg(udev, &setup_pkg, NULL);
}


/**
 * @brief 设置 Hub 端口的特性 (例如上电、复位)
 */
static inline int32 usb_set_port_feature(usb_dev_t *udev, uint8 port_no, usb_port_feature_e feature) {
    return usb_port_feature_ctrl(udev, port_no, feature, USB_REQ_SET_FEATURE);
}

/**
 * @brief 清除 Hub 端口的某个状态变化标志 (确认中断)
 */
static inline int32 usb_clear_port_feature(usb_dev_t *udev, uint8 port_no, usb_port_feature_e feature) {
    return usb_port_feature_ctrl(udev, port_no, feature, USB_REQ_CLEAR_FEATURE);
}


/**
 * @brief 获取 Hub 端口的 4 字节状态数据
 * @param udev        Hub 设备对象
 * @param port_no     端口号 (从 1 开始)
 * @param port_status 传出参数：用于接收 4 字节的端口状态
 * @return int32      状态码 (0 成功，<0 失败)
 */
int32 usb_get_port_status(usb_dev_t *udev, uint8 port_no, uint32 *port_status) {
    usb_setup_packet_t setup_pkg = {0};

    // 组装 Setup 包
    setup_pkg.recipient = USB_RECIP_OTHER;   // 🌟 接收者依然是 Other (Port)
    setup_pkg.req_type  = USB_REQ_TYPE_CLASS;// 类请求
    setup_pkg.dtd       = USB_DIR_IN;        // 方向：设备到主机 (我们要读数据)

    setup_pkg.request   = USB_REQ_GET_STATUS; // 0x00
    setup_pkg.value     = 0;                  // 规范要求全填 0
    setup_pkg.index     = port_no;            // 指向具体端口
    setup_pkg.length    = 4;                  // 🌟 必然返回 4 个字节！

    // 抛给底层，数据会写进 port_status 变量里
    uint32 *port_sts = kzalloc_dma(sizeof(uint32));
    usb_control_msg(udev, &setup_pkg, port_sts);
    *port_status = *port_sts;
    kfree(port_sts);
    return 0;
}



//hub驱动
int32 usb_hub_probe(usb_if_t *uif,usb_id_t *uid) {
    usb_dev_t *udev = uif->udev;
    usb_hub_t *hub = kzalloc(sizeof(usb_hub_t)) ;
    hub->uif = uif;

    if (udev->port_speed > USB_HIGH_SPEED) {
        // ==========================================
        // 🚀 USB 3.0 (SuperSpeed) Hub 处理逻辑
        // 描述符类型：0x2A，长度永远固定为 12 字节！
        // ==========================================
        usb3_hub_desc_t *hub3_desc = kzalloc_dma(sizeof(usb3_hub_desc_t));

        // 一步到位，直接吞下 12 字节！
        int32 ret = get_hub_desc(udev, USB_HUB3_DESC, &hub3_desc, 12);
        if (ret < 0) return ret;

    }else {
        // ==========================================
        // 🐢 USB 2.0/1.1 (High/Full/Low Speed) Hub 处理逻辑
        // 描述符类型：0x29，变长地雷，必须踩两步！
        // ==========================================
        usb2_hub_desc_t *hub2_desc = kzalloc_dma(71) ;

        // 第一步：先读 8 字节探路
        int32 ret = get_hub_desc(udev, USB_HUB2_DESC, hub2_desc, 8);
        if (ret < 0) return ret;

        // 第二步：算出真实物理长度，再次读取
        uint8 num_ports = hub2_desc->num_ports;
        uint16 real_len = 7 + ((num_ports / 8) + 1) * 2;

        ret = get_hub_desc(udev, USB_HUB2_DESC, hub2_desc, real_len);
        if (ret < 0) return ret;

        hub->is_usb3 = FALSE;
        hub->num_ports = hub2_desc->num_ports;
        hub->power_delay_ms = hub2_desc->power_on_to_power_good<<1;
        hub->is_individual_pwr = (hub2_desc->hub_characteristics & 0x03) == 0x01;
        hub->is_individual_ocp = ((hub2_desc->hub_characteristics >> 3) & 0x03) == 0x01;
        hub->tt_think_time = ((hub2_desc->hub_characteristics>>5 & 3)+1)*8;

        hub->ports = kzalloc(hub->num_ports*sizeof(hub_port_t));

        for (uint8 i = 0; i < hub->num_ports; i++) {
            hub->ports[i].port_no = i + 1;

            // 解析位图 (以 USB 2.0 为例，注意位图是从 Bit 1 开始算的)
            // Bit 1 对应 端口 1，以此类推
            uint8 byte_idx = (i + 1) / 8;
            uint8 bit_idx  = (i + 1) % 8;

            // 如果该位是 0，代表 Removable；是 1 代表 Non-Removable (硬接线)
            hub->ports[i].is_removable = (hub2_desc->device_removable[byte_idx] >> bit_idx) & 1;

        }

        //hub端口上电
        for (uint8 i = 0; i < hub->num_ports; i++) {
            usb_set_port_feature(udev, hub->ports[i].port_no, USB_PORT_FEAT_POWER );
        }

        //读取所有端口状态
        for (uint8 i = 0; i < hub->num_ports; i++) {
            usb_get_port_status(udev, hub->ports[i].port_no, &hub->ports[i].current_status);

            if (hub->ports[i].current_status & USB_PORT_STAT_CONNECTION) {
                usb_set_port_feature(udev, hub->ports[i].port_no, USB_PORT_FEAT_RESET);
            }
        }


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