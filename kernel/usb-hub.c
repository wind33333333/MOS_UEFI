#include "usb-hub.h"
#include "errno.h"
#include "slub.h"

// ==========================================
// 📦 Hub 专属类描述符获取 (Class Descriptor)
// ==========================================

/**
 * 获取普通 hub描述符
 */
static inline int32 usb_hub_get_desc(usb_dev_t *udev, void *buf, uint16 len) {
    return usb_control_msg(udev, buf,
                           USB_DIR_IN, USB_REQ_TYPE_CLASS, USB_RECIP_DEVICE,
                           USB_REQ_GET_DESCRIPTOR, (USB_DESC_TYPE_HUB  << 8) | 0, 0, len);
}

// 获取 ss hub描述符
static inline int32 usb_hub_get_ss_desc(usb_dev_t *udev, void *buf, uint16 len) {
    return usb_control_msg(udev, buf,
                           USB_DIR_IN, USB_REQ_TYPE_CLASS, USB_RECIP_DEVICE,
                           USB_REQ_GET_DESCRIPTOR, (USB_DESC_TYPE_SS_HUB << 8) | 0, 0, len);
}

// ==========================================
// 🔌 Hub 端口控制核心动作 (Action - 无数据阶段)
// ==========================================

/**
 * @brief 给 Hub 端口上电
 */
static inline int32 usb_hub_set_port_power(usb_dev_t *udev, uint8 port_no) {
    return usb_control_msg(udev, NULL,
                           USB_DIR_OUT, USB_REQ_TYPE_CLASS, USB_RECIP_OTHER,
                           USB_REQ_SET_FEATURE, USB_PORT_FEAT_POWER, port_no, 0);
}

/**
 * @brief 给 Hub 端口断电
 */
static inline int32 usb_hub_clear_port_power(usb_dev_t *udev, uint8 port_no) {
    return usb_control_msg(udev, NULL,
                           USB_DIR_OUT, USB_REQ_TYPE_CLASS, USB_RECIP_OTHER,
                           USB_REQ_CLEAR_FEATURE, USB_PORT_FEAT_POWER, port_no, 0);
}

/**
 * @brief 触发 Hub 端口硬复位 (将导致挂载的设备进入 Default 状态)
 */
static inline int32 usb_hub_set_port_reset(usb_dev_t *udev, uint8 port_no) {
    return usb_control_msg(udev, NULL,
                           USB_DIR_OUT, USB_REQ_TYPE_CLASS, USB_RECIP_OTHER,
                           USB_REQ_SET_FEATURE, USB_PORT_FEAT_RESET, port_no, 0);
}

// ==========================================
// 🧹 Hub 端口状态变化标志擦除 (Ack Interrupt - 无数据阶段)
// ==========================================

/**
 * @brief 擦除 [复位完成] 变化标志
 */
static inline int32 usb_hub_clear_port_reset_change(usb_dev_t *udev, uint8 port_no) {
    return usb_control_msg(udev, NULL,
                           USB_DIR_OUT, USB_REQ_TYPE_CLASS, USB_RECIP_OTHER,
                           USB_REQ_CLEAR_FEATURE, USB_PORT_FEAT_C_RESET, port_no, 0);
}

/**
 * @brief 擦除 [物理插拔] 变化标志 (防止热插拔中断风暴)
 */
static inline int32 usb_hub_clear_port_connection_change(usb_dev_t *udev, uint8 port_no) {
    return usb_control_msg(udev, NULL,
                           USB_DIR_OUT, USB_REQ_TYPE_CLASS, USB_RECIP_OTHER,
                           USB_REQ_CLEAR_FEATURE, USB_PORT_FEAT_C_CONNECTION, port_no, 0);
}

/**
 * @brief 擦除 [端口启用状态改变] 变化标志
 */
static inline int32 usb_hub_clear_port_enable_change(usb_dev_t *udev, uint8 port_no) {
    return usb_control_msg(udev, NULL,
                           USB_DIR_OUT, USB_REQ_TYPE_CLASS, USB_RECIP_OTHER,
                           USB_REQ_CLEAR_FEATURE, USB_PORT_FEAT_C_ENABLE, port_no, 0);
}

/**
 * @brief 擦除 [端口休眠/唤醒] 变化标志
 */
static inline int32 usb_hub_clear_port_suspend_change(usb_dev_t *udev, uint8 port_no) {
    return usb_control_msg(udev, NULL,
                           USB_DIR_OUT, USB_REQ_TYPE_CLASS, USB_RECIP_OTHER,
                           USB_REQ_CLEAR_FEATURE, USB_PORT_FEAT_C_SUSPEND, port_no, 0);
}

/**
 * @brief 擦除 [过流报警] 变化标志
 */
static inline int32 usb_hub_clear_port_over_current_change(usb_dev_t *udev, uint8 port_no) {
    return usb_control_msg(udev, NULL,
                           USB_DIR_OUT, USB_REQ_TYPE_CLASS, USB_RECIP_OTHER,
                           USB_REQ_CLEAR_FEATURE, USB_PORT_FEAT_C_OVER_CURRENT, port_no, 0);
}

// ==========================================
// 🔍 Hub 端口状态读取 (有数据阶段)
// ==========================================

/**
 * @brief 获取 Hub 端口的 4 字节状态数据
 * @param udev        Hub 设备对象
 * @param port_no     端口号 (从 1 开始)
 * @param port_status 传出参数：用于接收 4 字节的端口状态
 * @return int32      状态码 (0 成功，<0 失败)
 */
int32 usb_hub_get_port_status(usb_dev_t *udev, uint8 port_no, uint32 *port_status) {
    // 1. 申请用于底层 DMA 传输的 4 字节内存
    uint32 *port_sts = kzalloc_dma(sizeof(uint32));
    if (!port_sts) {
        return -ENOMEM;
    }

    // 2. 直击灵魂的底层调用：读取 4 字节状态 (IN 方向)
    int32 ret = usb_control_msg(udev, port_sts,
                                USB_DIR_IN, USB_REQ_TYPE_CLASS, USB_RECIP_OTHER,
                                USB_REQ_GET_STATUS, 0, port_no, 4);

    // 3. 只有成功时，才将结果透传给调用者
    if (ret >= 0) {
        *port_status = *port_sts;
    }

    // 4. 清理现场
    kfree(port_sts);

    // 🌟 核心修复：必须返回 ret，透传底层的超时/STALL错误！
    return ret;
}


// =========================================================================
// 🌳 xHCI 虚拟 Root Hub 抽象层
// =========================================================================

/*
// 提前声明 Root Hub 专属的寄存器读写操作集 (下文会详细说明)
extern usb_hub_ops_t xhci_roothub_ops;

/**
 * @brief 根据 xHCI 的协议支持表 (SPC)，凭空捏造虚拟的 Root Hub
 * @param xhcd xHCI 控制器上下文
 * @return int32 0 表示成功
 #1#
int32 usb_create_root_hub(xhci_hcd_t *xhcd) {
    if (!xhcd || xhcd->spc_count == 0) return -EINVAL;

    // 遍历我们在 xhci_probe 阶段解析出来的 SPC (Supported Protocol) 数组
    // 通常循环两次：一次造 USB 2.0 Root Hub，一次造 USB 3.0 Root Hub
    for (uint8 i = 0; i < xhcd->spc_count; i++) {
        xhci_spc_t *spc = &xhcd->spc[i];

        // ---------------------------------------------------------
        // 1. 凭空捏造设备对象 (udev)
        // ---------------------------------------------------------
        usb_dev_t *root_udev = kzalloc(sizeof(usb_dev_t));
        if (!root_udev) return -ENOMEM;

        root_udev->is_root_hub = TRUE;
        root_udev->bus_id      = xhcd->xdev->bus; // 绑定到宿主 PCIe 总线

        // 强行赋予灵魂速度
        if (spc->major_bcd == 0x03) {
            root_udev->speed = USB_SPEED_SUPER; // USB 3.0 (5Gbps)
        } else {
            root_udev->speed = USB_SPEED_HIGH;  // USB 2.0 (480Mbps)
        }

        // 伪造 Device Descriptor，以防某些极端的上层软件非要查水表
        root_udev->dev_desc.bLength = 18;
        root_udev->dev_desc.bDescriptorType = USB_DESC_TYPE_DEVICE;
        root_udev->dev_desc.bcdUSB = spc->major_bcd << 8 | spc->minor_bcd;
        root_udev->dev_desc.bDeviceClass = 0x09; // Hub Class

        // ---------------------------------------------------------
        // 2. 凭空捏造 Hub 控制块
        // ---------------------------------------------------------
        usb_hub_t *root_hub = kzalloc(sizeof(usb_hub_t));
        if (!root_hub) {
            kfree(root_udev);
            return -ENOMEM;
        }

        // 互相绑定
        root_hub->udev = root_udev;
        root_udev->priv_data = root_hub;

        // 🌟 核心拓扑注入：告诉这个 Hub，它管理哪几个物理端口
        root_hub->num_ports = spc->port_count;
        root_hub->port_start_idx = spc->port_first; // 物理索引 (通常从 1 开始)

        // ---------------------------------------------------------
        // 3. 灵魂注入：挂载 Root Hub 专属拦截器！
        // ---------------------------------------------------------
        // 外挂 Hub 走控制传输，而 Root Hub 必须走这套直接读写寄存器的 API
        root_hub->hub_ops = &xhci_roothub_ops;

        // 把 xhcd 的指针藏在 root_hub 里，方便操作集函数拿到底层寄存器
        root_hub->hcd_priv = xhcd;

        // ---------------------------------------------------------
        // 4. 交给 USB 核心层挂载，并在内核中注册
        // ---------------------------------------------------------
        // 这个函数内部应该负责给这棵刚出生的树分配一个编号 (比如 usb0, usb1)
        // 并且启动 hub_worker 守护线程
        int32 ret = usb_core_register_roothub(root_hub);
        if (ret < 0) {
            kfree(root_hub);
            kfree(root_udev);
            continue; // 容错：如果 3.0 挂载失败，尝试继续挂载 2.0
        }

        color_printk(GREEN, BLACK, "[Root Hub] 成功孵化虚拟 %s Hub，接管主板物理端口 [%d - %d]\n",
                     spc->major_bcd == 0x03 ? "USB 3.0" : "USB 2.0",
                     spc->port_first, spc->port_first + spc->port_count - 1);
    }

    return 0;
}
*/


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
        int32 ret = usb_hub_get_ss_desc(udev, ss_hub_desc,12);
        if (ret < 0) return ret;

    }else {
        // ==========================================
        // 🐢 USB 2.0/1.1 (High/Full/Low Speed) Hub 处理逻辑
        // 描述符类型：0x29，变长地雷，必须踩两步！
        // ==========================================
        usb_hub_desc_t *hub_desc = kzalloc_dma(71) ;

        // 第一步：先读 8 字节探路
        int32 ret = usb_hub_get_desc(udev,hub_desc, 8);
        if (ret < 0) return ret;

        // 第二步：算出真实物理长度，再次读取
        uint8 num_ports = hub_desc->num_ports;
        uint16 real_len = 7 + ((num_ports / 8) + 1) * 2;

        ret = usb_hub_get_desc(udev,hub_desc, real_len);
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