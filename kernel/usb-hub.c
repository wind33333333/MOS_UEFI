#include "usb-hub.h"

#include "slub.h"

// ==========================================
// 供 Hub 驱动调用的类级请求 (获取 Hub 描述符)
// ⚠️ 注意：Hub 描述符是 Class 请求，且发给 Device
// ==========================================
static inline int32 usb_hub_get_desc(usb_dev_t *udev, usb_desc_type_e desc_type, void *buf, uint16 len) {
    return usb_get_desc(udev, buf,USB_REQ_TYPE_CLASS, USB_RECIP_DEVICE, desc_type, 0, 0, len);
}


// ==========================================
// 🔌 Hub 端口控制核心动作 (Action)
// ==========================================

/**
 * @brief 给 Hub 端口上电
 */
static inline int32 usb_hub_set_port_power(usb_dev_t *udev, uint8 port_no) {
    return usb_ctrl_out(udev, USB_REQ_TYPE_CLASS, USB_RECIP_OTHER,
                        USB_REQ_SET_FEATURE, USB_PORT_FEAT_POWER, port_no);
}

/**
 * @brief 给 Hub 端口断电
 */
static inline int32 usb_hub_clear_port_power(usb_dev_t *udev, uint8 port_no) {
    return usb_ctrl_out(udev, USB_REQ_TYPE_CLASS, USB_RECIP_OTHER,
                        USB_REQ_CLEAR_FEATURE, USB_PORT_FEAT_POWER, port_no);
}

/**
 * @brief 触发 Hub 端口硬复位 (将导致设备进入 Default 状态)
 */
static inline int32 usb_hub_set_port_reset(usb_dev_t *udev, uint8 port_no) {
    return usb_ctrl_out(udev, USB_REQ_TYPE_CLASS, USB_RECIP_OTHER,
                        USB_REQ_SET_FEATURE, USB_PORT_FEAT_RESET, port_no);
}

// ==========================================
// 🧹 Hub 端口状态变化标志擦除 (Ack Interrupt)
// ⚠️ 极其关键：必须全部使用 USB_REQ_CLEAR_FEATURE！
// ==========================================

/**
 * @brief 擦除 [复位完成] 变化标志
 */
static inline int32 usb_hub_clear_port_reset_change(usb_dev_t *udev, uint8 port_no) {
    return usb_ctrl_out(udev, USB_REQ_TYPE_CLASS, USB_RECIP_OTHER,
                        USB_REQ_CLEAR_FEATURE, USB_PORT_FEAT_C_RESET, port_no);
}

/**
 * @brief 擦除 [物理插拔] 变化标志 (防止热插拔中断风暴)
 */
static inline int32 usb_hub_clear_port_connection_change(usb_dev_t *udev, uint8 port_no) {
    return usb_ctrl_out(udev, USB_REQ_TYPE_CLASS, USB_RECIP_OTHER,
                        USB_REQ_CLEAR_FEATURE, USB_PORT_FEAT_C_CONNECTION, port_no);
}

/**
 * @brief 擦除 [端口启用状态改变] 变化标志
 */
static inline int32 usb_hub_clear_port_enable_change(usb_dev_t *udev, uint8 port_no) {
    return usb_ctrl_out(udev, USB_REQ_TYPE_CLASS, USB_RECIP_OTHER,
                        USB_REQ_CLEAR_FEATURE, USB_PORT_FEAT_C_ENABLE, port_no);
}

/**
 * @brief 擦除 [端口休眠/唤醒] 变化标志
 */
static inline int32 usb_hub_clear_port_suspend_change(usb_dev_t *udev, uint8 port_no) {
    return usb_ctrl_out(udev, USB_REQ_TYPE_CLASS, USB_RECIP_OTHER,
                        USB_REQ_CLEAR_FEATURE, USB_PORT_FEAT_C_SUSPEND, port_no);
}

/**
 * @brief 擦除 [过流报警] 变化标志
 */
static inline int32 usb_hub_clear_port_over_current_change(usb_dev_t *udev, uint8 port_no) {
    return usb_ctrl_out(udev, USB_REQ_TYPE_CLASS, USB_RECIP_OTHER,
                        USB_REQ_CLEAR_FEATURE, USB_PORT_FEAT_C_OVER_CURRENT, port_no);
}
/**
 * @brief 获取 Hub 端口的 4 字节状态数据
 * @param udev        Hub 设备对象
 * @param port_no     端口号 (从 1 开始)
 * @param port_status 传出参数：用于接收 4 字节的端口状态
 * @return int32      状态码 (0 成功，<0 失败)
 */
int32 usb_hub_get_port_status(usb_dev_t *udev, uint8 port_no, uint32 *port_status) {
    // 抛给底层，数据会写进 port_status 变量里
    uint32 *port_sts = kzalloc_dma(sizeof(uint32));
    usb_ctrl_in(udev,port_sts, USB_REQ_TYPE_CLASS,USB_RECIP_OTHER,USB_REQ_GET_STATUS,0,port_no,4 );
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
        usb_ss_hub_desc_t *ss_hub_desc = kzalloc_dma(sizeof(usb_ss_hub_desc_t));

        // 一步到位，直接吞下 12 字节！
        int32 ret = usb_hub_get_desc(udev, USB_DESC_TYPE_SS_HUB, ss_hub_desc,12);
        if (ret < 0) return ret;

    }else {
        // ==========================================
        // 🐢 USB 2.0/1.1 (High/Full/Low Speed) Hub 处理逻辑
        // 描述符类型：0x29，变长地雷，必须踩两步！
        // ==========================================
        usb_hub_desc_t *hub_desc = kzalloc_dma(71) ;

        // 第一步：先读 8 字节探路
        int32 ret = usb_hub_get_desc(udev, USB_DESC_TYPE_HUB , hub_desc, 8);
        if (ret < 0) return ret;

        // 第二步：算出真实物理长度，再次读取
        uint8 num_ports = hub_desc->num_ports;
        uint16 real_len = 7 + ((num_ports / 8) + 1) * 2;

        ret = usb_hub_get_desc(udev, USB_DESC_TYPE_HUB , hub_desc, real_len);
        if (ret < 0) return ret;

        hub->is_usb3 = FALSE;
        hub->num_ports = hub_desc->num_ports;
        hub->power_delay_ms = hub_desc->power_on_to_power_good<<1;
        hub->is_individual_pwr = (hub_desc->hub_characteristics & 0x03) == 0x01;
        hub->is_individual_ocp = ((hub_desc->hub_characteristics >> 3) & 0x03) == 0x01;
        hub->tt_think_time = ((hub_desc->hub_characteristics>>5 & 3)+1)*8;

        hub->ports = kzalloc(hub->num_ports*sizeof(hub_port_t));

        for (uint8 i = 0; i < hub->num_ports; i++) {
            hub->ports[i].port_no = i + 1;

            // 解析位图 (以 USB 2.0 为例，注意位图是从 Bit 1 开始算的)
            // Bit 1 对应 端口 1，以此类推
            uint8 byte_idx = (i + 1) / 8;
            uint8 bit_idx  = (i + 1) % 8;

            // 如果该位是 0，代表 Removable；是 1 代表 Non-Removable (硬接线)
            hub->ports[i].is_removable = !(hub_desc->device_removable[byte_idx] >> bit_idx) & 1;

        }

        // ==========================================
        // 1. 暴力上电
        // ==========================================
        for (uint8 i = 0; i < hub->num_ports; i++) {
            usb_hub_set_port_power(udev, hub->ports[i].port_no);
        }

        // 🌟 物理规律：必须等待电容充电完毕！(你的 hub->power_delay_ms 派上用场了)
        //mdelay(hub->power_delay_ms);

        // ==========================================
        // 2. 开机扫街 (处理遗留设备)
        // ==========================================
        for (uint8 i = 0; i < hub->num_ports; i++) {
            uint8 port_no = hub->ports[i].port_no;
            uint32 status = 0;

            usb_hub_get_port_status(udev, port_no, &status);

            // 🔪 擦除开机时产生的插拔变化标志
            if (status & USB_PORT_STAT_C_CONNECTION) {
                usb_hub_clear_port_connection_change(udev, port_no);
            }

            // 如果口子上真的插了设备，开始复位流！
            if (status & USB_PORT_STAT_CONNECTION) {
                //color_printk(GREEN, BLACK, "[Hub] 端口 %d 发现遗留设备，发射复位信号...\n", port_no);

                // 触发硬复位
                usb_hub_set_port_reset(udev, port_no);

                // 🌟 物理规律：必须死等复位完成！
                uint32 timeout = 100; // 最多等 100ms
                while (timeout > 0) {
                    //mdelay(10);
                    usb_hub_get_port_status(udev, port_no, &status);

                    // 检查 C_RESET 标志位是否被硬件置 1
                    if (status & USB_PORT_STAT_C_RESET) {
                        break;
                    }
                    timeout -= 10;
                }

                if (timeout == 0) {
                    //color_printk(RED, BLACK, "[Hub] 端口 %d 复位超时！\n", port_no);
                    continue;
                }

                // 🔪 擦除复位完成标志
                usb_hub_clear_port_reset_change(udev, port_no);

                // 🔪 顺手擦除随之产生的 Enable 变化标志
                if (status & USB_PORT_STAT_C_ENABLE) {
                    usb_hub_clear_port_enable_change(udev, port_no);
                }

                // 最终确认：是否成功 Enable？
                if (status & USB_PORT_STAT_ENABLE) {
                    //color_printk(GREEN, BLACK, "[Hub] 端口 %d 复位成功！(Status: 0x%08x)\n", port_no, status);

                    //  提取速度 (Full/Low/High Speed)
                    //  为这个新设备分配地址 (SetAddress)，获取它的设备描述符！
                }

                usb_hub_get_port_status(udev, port_no, &status);
                usb_hub_get_port_status(udev, port_no, &status);
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