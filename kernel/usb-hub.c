#include "usb-hub.h"
#include "errno.h"
#include "printk.h"
#include "slub.h"

// ==========================================
// 📦 Hub 专属类描述符获取 (Class Descriptor)
// ==========================================

/**
 * 获取普通 hub描述符
 */
static inline int32 usb_hub20_get_desc(usb_dev_t *udev, void *buf, uint16 len) {
    return usb_control_msg(udev, buf,
                           USB_DIR_IN, USB_REQ_TYPE_CLASS, USB_RECIP_DEVICE,
                           USB_REQ_GET_DESCRIPTOR, (USB_DESC_TYPE_HUB20  << 8) | 0, 0, len);
}

// 获取 ss hub描述符
static inline int32 usb_hub30_get_desc(usb_dev_t *udev, void *buf, uint16 len) {
    return usb_control_msg(udev, buf,
                           USB_DIR_IN, USB_REQ_TYPE_CLASS, USB_RECIP_DEVICE,
                           USB_REQ_GET_DESCRIPTOR, (USB_DESC_TYPE_HUB30 << 8) | 0, 0, len);
}

// ==========================================
// 🔌 Hub 端口控制核心动作 (Action - 无数据阶段)
// ==========================================

/**
 * @brief 给 Hub 端口上电
 */
static inline int32 usb_hub_set_port_power(usb_dev_t *udev, uint8 port_id) {
    return usb_control_msg(udev, NULL,
                           USB_DIR_OUT, USB_REQ_TYPE_CLASS, USB_RECIP_OTHER,
                           USB_REQ_SET_FEATURE, USB_PORT_FEAT_POWER, port_id, 0);
}

/**
 * @brief 给 Hub 端口断电
 */
static inline int32 usb_hub_clear_port_power(usb_dev_t *udev, uint8 port_id) {
    return usb_control_msg(udev, NULL,
                           USB_DIR_OUT, USB_REQ_TYPE_CLASS, USB_RECIP_OTHER,
                           USB_REQ_CLEAR_FEATURE, USB_PORT_FEAT_POWER, port_id, 0);
}

/**
 * @brief 触发 Hub 端口硬复位 (将导致挂载的设备进入 Default 状态)
 */
static inline int32 usb_hub_set_port_reset(usb_dev_t *udev, uint8 port_id) {
    return usb_control_msg(udev, NULL,
                           USB_DIR_OUT, USB_REQ_TYPE_CLASS, USB_RECIP_OTHER,
                           USB_REQ_SET_FEATURE, USB_PORT_FEAT_RESET, port_id, 0);
}

// ==========================================
// 🧹 Hub 端口状态变化标志擦除 (Ack Interrupt - 无数据阶段)
// ==========================================

/**
 * @brief 擦除 [复位完成] 变化标志
 */
static inline int32 usb_hub_clear_port_reset_change(usb_dev_t *udev, uint8 port_id) {
    return usb_control_msg(udev, NULL,
                           USB_DIR_OUT, USB_REQ_TYPE_CLASS, USB_RECIP_OTHER,
                           USB_REQ_CLEAR_FEATURE, USB_PORT_FEAT_C_RESET, port_id, 0);
}

/**
 * @brief 擦除 [物理插拔] 变化标志 (防止热插拔中断风暴)
 */
static inline int32 usb_hub_clear_port_connection_change(usb_dev_t *udev, uint8 port_id) {
    return usb_control_msg(udev, NULL,
                           USB_DIR_OUT, USB_REQ_TYPE_CLASS, USB_RECIP_OTHER,
                           USB_REQ_CLEAR_FEATURE, USB_PORT_FEAT_C_CONNECTION, port_id, 0);
}

/**
 * @brief 擦除 [端口启用状态改变] 变化标志
 */
static inline int32 usb_hub_clear_port_enable_change(usb_dev_t *udev, uint8 port_id) {
    return usb_control_msg(udev, NULL,
                           USB_DIR_OUT, USB_REQ_TYPE_CLASS, USB_RECIP_OTHER,
                           USB_REQ_CLEAR_FEATURE, USB_PORT_FEAT_C_ENABLE, port_id, 0);
}

/**
 * @brief 擦除 [端口休眠/唤醒] 变化标志
 */
static inline int32 usb_hub_clear_port_suspend_change(usb_dev_t *udev, uint8 port_id) {
    return usb_control_msg(udev, NULL,
                           USB_DIR_OUT, USB_REQ_TYPE_CLASS, USB_RECIP_OTHER,
                           USB_REQ_CLEAR_FEATURE, USB_PORT_FEAT_C_SUSPEND, port_id, 0);
}

/**
 * @brief 擦除 [过流报警] 变化标志
 */
static inline int32 usb_hub_clear_port_over_current_change(usb_dev_t *udev, uint8 port_id) {
    return usb_control_msg(udev, NULL,
                           USB_DIR_OUT, USB_REQ_TYPE_CLASS, USB_RECIP_OTHER,
                           USB_REQ_CLEAR_FEATURE, USB_PORT_FEAT_C_OVER_CURRENT, port_id, 0);
}

// ==========================================
// 🔍 Hub 端口状态读取 (有数据阶段)
// ==========================================

/**
 * @brief 获取 Hub 端口的 4 字节状态数据
 * @param udev        Hub 设备对象
 * @param port_id     端口号 (从 1 开始)
 * @param port_status 传出参数：用于接收 4 字节的端口状态
 * @return int32      状态码 (0 成功，<0 失败)
 */
static int32 usb_hub_get_port_status(usb_dev_t *udev, uint8 port_id, uint32 *port_status) {
    // 1. 申请用于底层 DMA 传输的 4 字节内存
    uint32 *port_sts = kzalloc_dma(sizeof(uint32));
    if (!port_sts) {
        return -ENOMEM;
    }

    // 2. 直击灵魂的底层调用：读取 4 字节状态 (IN 方向)
    int32 ret = usb_control_msg(udev, port_sts,
                                USB_DIR_IN, USB_REQ_TYPE_CLASS, USB_RECIP_OTHER,
                                USB_REQ_GET_STATUS, 0, port_id, 4);

    // 3. 只有成功时，才将结果透传给调用者
    if (ret >= 0) {
        *port_status = *port_sts;
    }

    // 4. 清理现场
    kfree(port_sts);

    // 🌟 核心修复：必须返回 ret，透传底层的超时/STALL错误！
    return ret;
}

//获取 Device Context 数组中的指定条目
static inline void *usb_get_out_ctx_entry(void* out_ctx,uint32 dci,uint8 ctx_size) {
    return (uint8*)out_ctx + ctx_size * dci;
}


//hub驱动
int32 usb_hub_probe(usb_if_t *uif,usb_id_t *uid) {
    usb_dev_t *udev = uif->udev;
    usb_hub_t *hub = kzalloc(sizeof(usb_hub_t)) ;
    hub->uif = uif;
    udev->is_hub = TRUE;

    if (udev->port_speed > USB_SPEED_HIGH) {
        color_printk(GREEN,BLACK,"hub3.0!!! speed:%d psiv:%d port:%d  \n",udev->port_speed,udev->psiv,udev->root_port_num );
        // ==========================================
        // 🚀 USB 3.0 (SuperSpeed) Hub 处理逻辑
        // 描述符类型：0x2A，长度永远固定为 12 字节！
        // ==========================================
        // usb_hub30_desc_t *hub30_desc = kzalloc_dma(sizeof(usb_hub30_desc_t));

        // 一步到位，直接吞下 12 字节！
        // int32 ret = usb_hub30_get_desc(udev, hub30_desc,12);
        // if (ret < 0) return ret;

    }else {
        color_printk(GREEN,BLACK,"hub2.0!!! speed:%d psiv:%d port:%d  \n",udev->port_speed,udev->psiv,udev->root_port_num );
        // ==========================================
        // 🐢 USB 2.0/1.1 (High/Full/Low Speed) Hub 处理逻辑
        // ==========================================

        // ==========================================
        // 1. 协议降级搜索
        // ==========================================
        usb_if_alt_t *if_alt = NULL;
        // 1. 🥈 尝试寻找 USB 2.0 高级多事务 Hub (MTT, Protocol = 2)
        if (if_alt = usb_find_alt_if(uif,USB_MATCH_ANY,USB_MATCH_ANY,2)) {
            udev->hub_mtt = 1;
        }else if (if_alt =usb_find_alt_if(uif,USB_MATCH_ANY,USB_MATCH_ANY,1)) {
            // 2. 🥉 降级寻找 USB 2.0 单事务 Hub (STT, Protocol = 1)
            udev->hub_mtt = 0;
        }else if (if_alt =usb_find_alt_if(uif,USB_MATCH_ANY,USB_MATCH_ANY,0)){
            // 3. 🪨 终极保底：USB 1.1 全速 Hub 或基础兼容模式 (Protocol = 0)
            udev->hub_mtt = 0;
        }else {
            // 终极防御：如果连 Protocol 0 都找不到，说明这是一个损坏的设备或非 Hub 设备
            color_printk(RED, BLACK, "USB: Failed to find any valid Hub protocol!\n");
            return -ENODEV;
        }

        //获取2.0hub描述符
        usb_hub20_desc_t *hub20_desc = kzalloc_dma(71) ;
        int32 error = usb_hub20_get_desc(udev,hub20_desc, 71);
        if (error < 0) return error;

        //解析hub描述符
        udev->hub_num_ports = hub20_desc->num_ports;
        udev->hub_ttt = (hub20_desc->hub_characteristics >> 5) & 0x03;
        hub->is_individual_pwr = (hub20_desc->hub_characteristics & 0x03) == 0x01;
        hub->is_individual_ocp = ((hub20_desc->hub_characteristics >> 3) & 0x03) == 0x01;
        hub->power_delay_ms = hub20_desc->power_on_to_power_good<<1;

        //设置udev为hub模式
        usb_ctx_slot_cfg(udev);

        xhci_slot_ctx_t *slot_ctx = usb_get_out_ctx_entry(udev->out_ctx,0,udev->xhcd->ctx_size);
        color_printk(RED,BLACK,"is_hub:%d num_port:%d  \n",slot_ctx->is_hub,slot_ctx->num_ports);

        //启用接口
        usb_ep_t *ep1 = &if_alt->eps[0];
        ep1->ring_max_trbs = 32;
        usb_enable_alt_if(if_alt);

        //分配hub端口内存并给端口上电
        hub->ports = kzalloc((udev->hub_num_ports+1)*sizeof(hub_port_t));
        for (uint8 i = 1; i <= udev->hub_num_ports; i++) {
            hub->ports[i].port_id = i;

            // 解析位图 (以 USB 2.0 为例，注意位图是从 Bit 1 开始算的)
            // Bit 1 对应 端口 1，以此类推
            uint8 byte_idx = i / 8;
            uint8 bit_idx  = i % 8;

            // 如果该位是 0，代表 Removable；是 1 代表 Non-Removable (硬接线)
            hub->ports[i].is_removable = !((hub20_desc->device_removable[byte_idx] >> bit_idx) & 1);

            // 暴力上电
            usb_hub_set_port_power(udev, i);

        }

        // 🌟 物理规律：必须等待电容充电完毕！(你的 hub->power_delay_ms 派上用场了)
        uint32 times = 0x5000000;
        while (times) {
            times--;
            asm_pause();
        }

        // ==========================================
        // 2. 开机扫街 (处理端口上遗留设备)
        // ==========================================
        for (uint8 i = 1; i <= udev->hub_num_ports; i++) {
            uint8 port_num = hub->ports[i].port_id;
            uint32 status = 0;

            usb_hub_get_port_status(udev, port_num, &status);
            color_printk(GREEN, BLACK, "[Hub] port: %d Status: %#x  \n", port_num, status);

            // 🔪 擦除开机时产生的插拔变化标志
            if (status & USB_PORT_STAT_C_CONNECTION) {
                usb_hub_clear_port_connection_change(udev, port_num);
            }

            // 如果口子上真的插了设备，开始复位流！
            if (status & USB_PORT_STAT_CONNECTION) {
                // 触发硬复位
                usb_hub_set_port_reset(udev, port_num);

                // 🌟 物理规律：必须死等复位完成！
                uint32 timeout = 10000000; // 最多等 100ms
                while (timeout > 0) {
                    usb_hub_get_port_status(udev, port_num, &status);

                    // 检查 C_RESET 标志位是否被硬件置 1
                    if (status & USB_PORT_STAT_C_RESET) {
                        break;
                    }
                    timeout -= 1;
                }

                if (timeout == 0) {
                    color_printk(RED, BLACK, "[Hub] port: %d reset timeout！\n", port_num);
                    continue;
                }

                // 🔪 擦除复位完成标志
                usb_hub_clear_port_reset_change(udev, port_num);

                // 🔪 顺手擦除随之产生的 Enable 变化标志
                if (status & USB_PORT_STAT_C_ENABLE) {
                    usb_hub_clear_port_enable_change(udev, port_num);
                }

                // 最终确认：是否成功 Enable？
                if (status & USB_PORT_STAT_ENABLE) {
                    usb_hub_get_port_status(udev, port_num, &status);
                    color_printk(GREEN, BLACK, "[Hub] port: %d reset successful！(Status: %#x)\n", port_num, status);

                    //  提取速度 (Full/Low/High Speed)
                    //  为这个新设备分配地址 (SetAddress)，获取它的设备描述符！
                }

            }
        }


        usb_urb_t *urb = usb_alloc_urb();
        uint8 *bitmap = kzalloc(ep1->max_packet_size);
        usb_fill_bulk_urb(urb,udev,ep1,bitmap,ep1->max_packet_size);
        usb_submit_urb(urb);

        // 等结果
        while (urb->is_done == FALSE) {
            asm_pause();
        }

        color_printk(RED,BLACK,"usb_hub2.0 bitmap:%#x  \n",bitmap[0]);

        while (1);


    }

}

void usb_hub_remove(usb_if_t *usb_if) {

}

usb_drv_t *create_usb_hub_driver() {
    usb_drv_t *usb_drv = kzalloc(sizeof(usb_drv_t));
    usb_id_t *id_table = kzalloc(sizeof(usb_id_t)*2);
    id_table[0].match_flags = USB_MATCH_INT_CLASS;
    id_table[0].if_class = 0x9;
    usb_drv->drv.name = "usb_hub";
    usb_drv->drv.id_table = id_table;
    usb_drv->probe = usb_hub_probe;
    usb_drv->remove = usb_hub_remove;
    return usb_drv;
}