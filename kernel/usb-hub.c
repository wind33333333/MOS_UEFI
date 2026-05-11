#include "usb-hub.h"
#include "errno.h"
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


/* =========================================================
 * 虚拟 USB 3.0 Root Hub 设备描述符 (SuperSpeed)
 * =========================================================
 * 硬件规范参考：USB 3.1 Specification, Table 9-8
 */
static const usb_dev_desc_t root_hub30_dev_desc = {
    .head = {
        .length    = sizeof(usb_dev_desc_t), // 固定为 18 (0x12)
        .desc_type = USB_DESC_TYPE_DEVICE    // 0x01 = USB_DT_DEVICE
    },
    .usb_version         = 0x0300,           // BCD 编码 3.00 (USB 3.0)
    .device_class        = 0x09,             // 0x09 = USB_CLASS_HUB
    .device_subclass     = 0x00,             // Hub 子类必须为 0
    .device_protocol     = 0x03,             // 0x03 = USB 3.x SuperSpeed Hub 协议

    // 🌟 核心差异：USB 3.x 的端点 0 最大包长规定必须填 9 (代表 2^9 = 512 字节)
    .max_packet_size0    = 9,

    .vendor_id           = 0x1D6B,           // 厂商 ID (借用 Linux Foundation, 或申请属于你的 ID)
    .product_id          = 0x0003,           // 产品 ID (代表 3.0 Root Hub)
    .device_version      = 0x0100,           // 固件版本 1.00

    // 字符串描述符索引 (告诉 USB Core 分别去第几个抽屉拿字符串)
    .manufacturer_index  = 1,
    .product_index       = 2,
    .serial_number_index = 3,

    .num_configurations  = 1                 // Hub 通常只有 1 种配置
};

/* =========================================================
 * 虚拟 USB 2.0 Root Hub 设备描述符 (High-Speed)
 * =========================================================
 * 硬件规范参考：USB 2.0 Specification, Table 9-8
 */
static const usb_dev_desc_t root_hub20_dev_desc = {
    .head = {
        .length    = sizeof(usb_dev_desc_t), // 固定为 18 (0x12)
        .desc_type = USB_DESC_TYPE_DEVICE    // 0x01 = USB_DT_DEVICE
    },
    .usb_version         = 0x0200,           // BCD 编码 2.00 (USB 2.0)
    .device_class        = 0x09,             // 0x09 = USB_CLASS_HUB
    .device_subclass     = 0x00,             // Hub 子类必须为 0

    // 🌟 核心差异：对于 2.0 的 Hub，如果支持多个 Transaction Translator (MTT)，
    // 可以填 2，如果只支持单 TT，填 1。通常虚拟 Root Hub 支持 MTT，填 2 或 1 均可。
    // 在最基础的实现中，我们先宣称它是一个 Full/High-Speed Hub (填 1)
    .device_protocol     = 0x01,             // 0x01 = Single TT High-Speed Hub

    // 🌟 核心差异：USB 2.0 时代的端点 0 最大包长填的是真实的字节数 (如 64)
    .max_packet_size0    = 64,

    .vendor_id           = 0x1D6B,
    .product_id          = 0x0002,           // 产品 ID (代表 2.0 Root Hub)
    .device_version      = 0x0100,

    .manufacturer_index  = 1,
    .product_index       = 2,
    .serial_number_index = 3,

    .num_configurations  = 1
};


#pragma pack(push, 1)

// USB 2.0 Root Hub 配置包 (9 + 9 + 7 = 25 字节)
typedef struct {
    usb_cfg_desc_t  config;
    usb_if_desc_t   interface;
    usb_ep_desc_t   endpoint;
} xhci_rhub20_cfg_bundle_t;

// USB 3.0 Root Hub 配置包 (9 + 9 + 7 + 6 = 31 字节)
typedef struct {
    usb_cfg_desc_t          config;
    usb_if_desc_t           interface;
    usb_ep_desc_t           endpoint;
    usb_ss_comp_desc_t      ss_companion;
} xhci_rhub30_cfg_bundle_t;

#pragma pack(pop)

/* =========================================================
 * 虚拟 USB 3.0 Root Hub 完整配置“糖葫芦” (31 字节)
 * =========================================================
 */
static const xhci_rhub30_cfg_bundle_t root_hub30_cfg_bundle = {
    // 1. 配置描述符 (9 字节)
    .config = {
        .head = { .length = 9, .desc_type = USB_DESC_TYPE_CONFIG },
        .total_length        = sizeof(xhci_rhub30_cfg_bundle_t), // 自动计算为 31
        .num_interfaces      = 1,
        .configuration_value = 1,
        .configuration_index = 0,
        .attributes          = 0xE0, // 自供电 + 远程唤醒
        .max_power           = 0
    },

    // 2. 接口描述符 (9 字节)
    .interface = {
        .head = { .length = 9, .desc_type = USB_DESC_TYPE_INTERFACE }, // 0x04 = Interface
        .interface_number    = 0,
        .alternate_setting   = 0,
        .num_endpoints       = 1,    // Hub 需要 1 个中断输入端点！
        .interface_class     = 0x09, // 🌟 宣告自己是 Hub 功能！
        .interface_subclass  = 0x00,
        .interface_protocol  = 0x00,
        .interface_index     = 0
    },

    // 3. 端点描述符 (7 字节) - 这是一个 中断输入 (Interrupt IN) 端点
    .endpoint = {
        .head = { .length = 7, .desc_type = USB_DESC_TYPE_ENDPOINT }, // 0x05 = Endpoint
        .endpoint_address    = 0x81, // 🌟 0x81 代表：方向 IN (0x80) | 端点号 1
        .attributes          = 0x03, // 0x03 代表：Interrupt 传输类型

        // 2 字节最大包长：USB规范规定，Hub状态变化位图中，第0位是Hub自身状态，
        // 第1~N位是端口1~N状态。2字节=16位，足够表示最多 15 个端口的状态改变。
        .max_packet_size     = 2,

        // 轮询间隔：USB 3.0 的间隔计算公式是 2^(bInterval-1) * 125us。
        // 填 12，意味着每 256ms 轮询一次端口状态。
        .interval            = 12
    },

    // 4. 超速端点伴随描述符 (6 字节)
    .ss_companion = {
        .head = { .length = 6, .desc_type = USB_DESC_TYPE_SS_ENDPOINT_COMPANION }, // 0x30 = SuperSpeed Endpoint Companion
        .max_burst           = 0,    // 一次传 1 个包足矣
        .attributes          = 0,    // 中断端点不使用 Streams，填 0
        .bytes_per_interval  = 2     // 每次间隔最多传 2 字节
    }
};


/* =========================================================
 * 虚拟 USB 2.0 Root Hub 完整配置“糖葫芦” (25 字节)
 * =========================================================
 */
static const xhci_rhub20_cfg_bundle_t root_hub20_cfg_bundle = {
    // 1. 配置描述符 (9 字节)
    .config = {
        .head = { .length = 9, .desc_type = USB_DESC_TYPE_CONFIG },
        .total_length        = sizeof(xhci_rhub20_cfg_bundle_t), // 自动计算为 25
        .num_interfaces      = 1,
        .configuration_value = 1,
        .configuration_index = 0,
        .attributes          = 0xE0, // 自供电 + 远程唤醒
        .max_power           = 0
    },

    // 2. 接口描述符 (9 字节)
    .interface = {
        .head = { .length = 9, .desc_type = USB_DESC_TYPE_INTERFACE }, // 0x04 = Interface
        .interface_number    = 0,
        .alternate_setting   = 0,
        .num_endpoints       = 1,    // Hub 需要 1 个中断输入端点！
        .interface_class     = 0x09, // 🌟 宣告自己是 Hub 功能！
        .interface_subclass  = 0x00,
        .interface_protocol  = 0x00,
        .interface_index     = 0
    },

    // 3. 端点描述符 (7 字节) - 这是一个 中断输入 (Interrupt IN) 端点
    .endpoint = {
        .head = { .length = 7, .desc_type = USB_DESC_TYPE_ENDPOINT }, // 0x05 = Endpoint
        .endpoint_address    = 0x81, // 🌟 0x81 代表：方向 IN (0x80) | 端点号 1
        .attributes          = 0x03, // 0x03 代表：Interrupt 传输类型

        // 2 字节最大包长：USB规范规定，Hub状态变化位图中，第0位是Hub自身状态，
        // 第1~N位是端口1~N状态。2字节=16位，足够表示最多 15 个端口的状态改变。
        .max_packet_size     = 2,

        // 轮询间隔：USB 3.0 的间隔计算公式是 2^(bInterval-1) * 125us。
        // 填 12，意味着每 256ms 轮询一次端口状态。
        .interval            = 12
    },
};

extern device_type_t usb_dev_type;
extern device_type_t usb_if_type;

void usb_roothub_create(xhci_hcd_t *xhcd,xhci_rhub_t *rhub,usb_dev_desc_t *rhub_dev_desc,usb_cfg_desc_t* rhub_cfg_desc,char *product) {
    if (rhub->logic_port_count == 0) return;
    // 分配 USB 核心层通用设备结构体 (NULL 表示没有父节点，它就是 Root)
    usb_dev_t *udev = kzalloc(sizeof(usb_dev_t));

    udev->slot_id = 0;
    udev->port_id = 0;
    udev->dev_type = USB_DEV_TYPE_ROOTHUB;        // 强制roothub
    udev->hub_num_ports = rhub->logic_port_count; // 注入端口数
    udev->port_speed = rhub->max_psi->mapped_speed;    //最高速率
    udev->speed_kbps = rhub->max_psi->speed_kbps;      //最高带宽

    // 强绑定：将底层的 rhub_20 对象与上层的 usb_dev 对象互相绑定
    rhub->udev = udev;
    udev->drv_data= rhub; // hcpriv 是我们在 USB Core 留的底层私有指针
    udev->xhcd = xhcd;
    udev->dev.type = &usb_dev_type;
    udev->dev.parent = &xhcd->xdev->dev;
    udev->dev.bus = &usb_bus_type;

    // 🌟 挂载伪造的静态描述符
    udev->dev_desc = rhub_dev_desc;
    udev->config_desc = rhub_cfg_desc;

    udev->manufacturer = (uint8 *)"MOS Kernel";
    udev->product      = product;
    udev->serial_number= (uint8 *)"xhci-3.0";

    // 敲锣打鼓，向操作系统的设备树注册这个 Hub！
    usb_dev_register(udev);
    usb_if_create(udev);
    usb_if_register(udev);
}

/**
 * @brief 初始化并注册 xHCI 虚拟 Root Hub
 * @param xhcd xHCI 控制器核心上下文
 * @return int32 0 表示成功，负数表示失败
 */
int32 xhci_init_root_hubs(xhci_hcd_t *xhcd) {
    if (!xhcd) return -EINVAL;

    // =========================================================================
    // 阶段 1：解析 SPC 并建立双向 O(1) 路由映射表
    // =========================================================================
    uint8 logic_port_20 = 1;
    uint8 logic_port_30 = 1;

    // 用于记录 3.0 Hub 的全局最高速率
    xhci_psi_t *absolute_max_psi_30 = NULL;
    xhci_psi_t *absolute_max_psi_20 = NULL;

    for (int i = 0; i < xhcd->spc_count; i++) {
        xhci_spc_t *spc = &xhcd->spc[i];
        uint8 start = spc->port_first;
        uint8 end = start+spc->port_count;
        xhci_psi_t *spc_max = xhci_spc_get_max_speed_entry(spc);

        if (xhcd->spc[i].major_bcd == 0x02) {
            for (; start < end; start++) {
                xhcd->physical_to_logical[start] = logic_port_20;
                xhcd->rhub_20.logical_to_physical[logic_port_20++] = start;
            }
            if (spc_max) {
                if (!absolute_max_psi_20 || spc_max->speed_kbps > absolute_max_psi_20->speed_kbps) {
                    absolute_max_psi_20 = spc_max;
                }
            }
        } else if (xhcd->spc[i].major_bcd == 0x03) {
            // 顺手计算 3.0 家族的最高绝对速率 (比如可能混杂了 5G, 10G, 20G 的 SPC)
            if (spc_max) {
                if (!absolute_max_psi_30 || spc_max->speed_kbps > absolute_max_psi_30->speed_kbps) {
                    absolute_max_psi_30 = spc_max;
                }
            }

            for (; start < end; start++) {
                // 🌟 核心：把分散的物理端口，映射到连续的 3.0 逻辑端口上
                xhcd->physical_to_logical[start] = logic_port_30;
                xhcd->rhub_30.logical_to_physical[logic_port_30++] = start;
            }
        }
    }

    // 保存最终的逻辑端口总数
    xhcd->rhub_20.logic_port_count = logic_port_20 - 1;
    xhcd->rhub_30.logic_port_count = logic_port_30 - 1;

    // =========================================================================
    // 阶段 2：向 USB Core 分配并注册 USB 2.0 Root Hub
    // =========================================================================
    if (xhcd->rhub_20.logic_port_count > 0) {
        // 分配 USB 核心层通用设备结构体 (NULL 表示没有父节点，它就是 Root)
        usb_dev_t *udev_20 = kzalloc(sizeof(usb_dev_t));
        if (!udev_20) return -ENOMEM;

        udev_20->slot_id = 0;
        udev_20->port_id = 0;
        udev_20->dev_type = USB_DEV_TYPE_ROOTHUB;
        udev_20->hub_num_ports = xhcd->rhub_20.logic_port_count; // 注入端口数
        udev_20->port_speed = USB_SPEED_HIGH;                    // 2.0 基础速率
        udev_20->speed_kbps = 480000;                            // 扁平化绝对带宽

        // 强绑定：将底层的 rhub_20 对象与上层的 usb_dev 对象互相绑定
        xhcd->rhub_20.udev = udev_20;
        udev_20->drv_data= &xhcd->rhub_20; // hcpriv 是我们在 USB Core 留的底层私有指针
        udev_20->xhcd = xhcd;
        udev_20->dev.type = &usb_dev_type;
        udev_20->dev.parent = &xhcd->xdev->dev;
        udev_20->dev.bus = &usb_bus_type;

        // 🌟 挂载伪造的静态描述符
        udev_20->dev_desc = (usb_dev_desc_t *)&root_hub30_dev_desc;
        udev_20->config_desc = (usb_cfg_desc_t*)&root_hub30_cfg_bundle;

        udev_20->manufacturer = (uint8 *)"MOS Kernel";
        udev_20->product      = (uint8 *)"xHCI Higt Root Hub";
        udev_20->serial_number= (uint8 *)"xhci-3.0";

        // 敲锣打鼓，向操作系统的设备树注册这个 Hub！
        usb_dev_register(udev_20);
        usb_if_create(udev_20);
        usb_if_register(udev_20);
    }

    // =========================================================================
    // 阶段 3：向 USB Core 分配并注册 USB 3.0 Root Hub
    // =========================================================================
    if (xhcd->rhub_30.logic_port_count > 0) {
        usb_dev_t *udev_30 = kzalloc(sizeof(usb_dev_t));
        if (!udev_30) {
            // 如果 3.0 申请失败，记得回滚 2.0 (微内核的严谨性)
            return -ENOMEM;
        }

        udev_30->slot_id = 0;
        udev_30->port_id = 0;
        udev_30->dev_type = USB_DEV_TYPE_ROOTHUB;
        udev_30->hub_num_ports = xhcd->rhub_30.logic_port_count;

        // 🌟 动态提权：根据 SPC 遍历结果，赋予这个 Hub 最高级别的宣称速率
        if (absolute_max_psi_30) {
            udev_30->port_speed = absolute_max_psi_30->mapped_speed;
            udev_30->speed_kbps = absolute_max_psi_30->speed_kbps;
        } else {
            // 兜底方案
            udev_30->port_speed = USB_SPEED_SUPER;
            udev_30->speed_kbps = 5000000;
        }

        // 🌟 挂载伪造的静态描述符
        udev_30->dev_desc = (usb_dev_desc_t *)&root_hub30_dev_desc;
        udev_30->config_desc = (usb_cfg_desc_t*)&root_hub30_cfg_bundle;

        udev_30->manufacturer = (uint8 *)"MOS Kernel";
        udev_30->product      = (uint8 *)"xHCI SuperSpeed Root Hub";
        udev_30->serial_number= (uint8 *)"xhci-3.0";

        // 互相绑定
        xhcd->rhub_30.udev = udev_30;
        udev_30->drv_data = &xhcd->rhub_30;
        udev_30->xhcd = xhcd;
        udev_30->dev.type = &usb_dev_type;
        udev_30->dev.parent = &xhcd->xdev->dev;
        udev_30->dev.bus = &usb_bus_type;

        // 注册到设备树！
        usb_dev_register(udev_30);
        usb_if_create(udev_30);
        usb_if_register(udev_30);
    }

    return 0; // 虚拟 Hub 大厦落成！
}


//hub驱动
int32 usb_hub_probe(usb_if_t *uif,usb_id_t *uid) {
    usb_dev_t *udev = uif->udev;
    usb_hub_t *hub = kzalloc(sizeof(usb_hub_t)) ;
    hub->uif = uif;

    if (udev->port_speed > USB_SPEED_HIGH) {
        // ==========================================
        // 🚀 USB 3.0 (SuperSpeed) Hub 处理逻辑
        // 描述符类型：0x2A，长度永远固定为 12 字节！
        // ==========================================
        usb_ss_hub_desc_t *ss_hub_desc = kzalloc_dma(sizeof(usb_ss_hub_desc_t));

        // 一步到位，直接吞下 12 字节！
        int32 ret = usb_hub30_get_desc(udev, ss_hub_desc,12);
        if (ret < 0) return ret;

    }else {
        // ==========================================
        // 🐢 USB 2.0/1.1 (High/Full/Low Speed) Hub 处理逻辑
        // 描述符类型：0x29，变长地雷，必须踩两步！
        // ==========================================
        usb_hub_desc_t *hub_desc = kzalloc_dma(71) ;

        // 第一步：先读 8 字节探路
        int32 ret = usb_hub20_get_desc(udev,hub_desc, 8);
        if (ret < 0) return ret;

        // 第二步：算出真实物理长度，再次读取
        uint8 num_ports = hub_desc->num_ports;
        uint16 real_len = 7 + ((num_ports / 8) + 1) * 2;

        ret = usb_hub20_get_desc(udev,hub_desc, real_len);
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