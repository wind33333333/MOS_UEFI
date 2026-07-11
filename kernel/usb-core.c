#include "slub.h"
#include "printk.h"
#include "errno.h"
#include "xhci-ring.h"
#include "xhci-ctx.h"
#include "xhci-hcd.h"
#include "usb-core.h"
#include "pcie.h"


/**
 * @brief 动态分配一个纯净的 URB 面单
 * @return usb_urb_t* 成功返回指针，失败返回 NULL
 */
usb_urb_t *usb_alloc_urb(void) {
    // 1. 从内核堆内存中申请一块空间
    usb_urb_t *urb = kzalloc(sizeof(usb_urb_t));
    if (urb == NULL) {
        color_printk(RED, BLACK, "USB Core: Failed to allocate URB!\n");
        return NULL;
    }

    // 以后如果引入了引用计数 (kref) 或自旋锁，也会在这里初始化

    return urb;
}

/**
 * @brief 安全销毁一个 URB 面单，并智能回收载荷内存
 * @param urb 需要销毁的 URB 指针
 */
void usb_free_urb(usb_urb_t *urb) {
    if (urb == NULL) return; // 防御性拦截

    // 👑 架构师彩蛋：智能内存托管 (对应 Linux 的 URB_FREE_BUFFER)
    // 如果上层驱动在提交时打了这个标志，USB Core 会在销毁 URB 时，
    // “顺手”把挂载的数据缓冲区也给释放掉，极大减轻上层驱动的内存管理心智负担！
    if (urb->transfer_flags & URB_FREE_BUFFER) {
        if (urb->transfer_buf != NULL) {
            kfree(urb->transfer_buf);
            urb->transfer_buf = NULL;
        }
    }

    // 彻底释放 URB 面单本身的内存
    kfree(urb);
}


/**
 * @brief 初始化控制传输 URB (Control Transfer)
 * * @note  专为 Endpoint 0 和枚举协议设计。
 * 必须强制传入 8 字节的 setup_packet。
 *
 * @param urb            需要被初始化的 URB 指针
 * @param udev           目标 USB 设备上下文
 * @param ep             目标控制端点 (通常是 udev->ep0)
 * @param setup_packet   指向 8 字节标准请求协议头的指针 (核心必填)
 * @param transfer_buf   控制传输数据阶段 (Data Stage) 的缓冲区。如无数据阶段传 NULL。
 * @param transfer_len   数据缓冲区的长度。如无数据阶段传 0。
 */
void usb_fill_control_urb(usb_urb_t *urb,
                          usb_dev_t *udev,
                          usb_ep_t *ep,
                          usb_setup_packet_t *setup_packet,
                          void *transfer_buf,
                          uint32 transfer_len) {
    urb->udev           = udev;
    urb->ep             = ep;
    urb->stream_id      = 0;            // 仅 UAS 协议使用，控制传输恒为 0

    // 👑 控制传输灵魂：必须挂载 Setup 包
    urb->setup_packet   = setup_packet;

    urb->transfer_buf   = transfer_buf;
    urb->transfer_len   = transfer_len;

    // 状态与标志位复位，准备发车
    urb->transfer_flags = 0;
    urb->status         = 0;
    urb->actual_length  = 0;
    urb->is_done        = FALSE;
}

/**
 * @brief 初始化批量传输 URB (Bulk Transfer)
 *
 * @note  专用于 U盘 (Mass Storage) 或网卡等对吞吐量要求高、对时间不敏感的设备。
 * 绝对不允许传入 setup_packet，也没有时间间隔的概念。
 *
 * @param urb            需要被初始化的 URB 指针
 * @param udev           目标 USB 设备上下文
 * @param ep             目标批量端点 (Bulk IN 或 Bulk OUT)
 * @param transfer_buf   存放或接收数据的缓冲区指针
 * @param transfer_len   期望发送或接收的总字节数
 */
void usb_fill_bulk_urb(usb_urb_t *urb,
                       usb_dev_t *udev,
                       usb_ep_t *ep,
                       void *transfer_buf,
                       uint32 transfer_len) {
    urb->udev           = udev;
    urb->ep             = ep;
    urb->stream_id      = 0;

    // 🚫 严格防呆：批量传输在物理层绝对没有 Setup 阶段，强制封死
    urb->setup_packet   = NULL;

    urb->transfer_buf   = transfer_buf;
    urb->transfer_len   = transfer_len;

    urb->transfer_flags = 0;
    urb->status         = 0;
    urb->actual_length  = 0;
    urb->is_done        = FALSE;
}

/**
 * @brief 初始化中断传输 URB (Interrupt Transfer)
 *
 * @note  专用于 Hub 状态上报、鼠标、键盘等数据量极小，但对延迟极其敏感的设备。
 * 必须强制指定 interval (轮询间隔)，指示 xHCI 硬件隔多久去查一次。
 *
 * @param urb            需要被初始化的 URB 指针
 * @param udev           目标 USB 设备上下文
 * @param ep             目标中断端点 (Interrupt IN 或 Interrupt OUT)
 * @param transfer_buf   存放或接收数据的缓冲区指针
 * @param transfer_len   期望发送或接收的总字节数 (通常 <= ep->max_packet_size)
 * @param interval       轮询间隔时间 (硬件将依此频率向设备发出 IN Token)
 */
void usb_fill_int_urb(usb_urb_t *urb,
                      usb_dev_t *udev,
                      usb_ep_t *ep,
                      void *transfer_buf,
                      uint32 transfer_len,
                      uint32 interval) {
    urb->udev           = udev;
    urb->ep             = ep;
    urb->stream_id      = 0;

    // 🚫 严格防呆：中断传输同样没有 Setup 包
    urb->setup_packet   = NULL;

    urb->transfer_buf   = transfer_buf;
    urb->transfer_len   = transfer_len;

    // ⏱️ 核心独占字段：配置硬件轮询频率
    // 注意：请确保你的 usb_urb_t 结构体中已经添加了 uint32 interval; 字段！
    urb->interval       = interval;

    urb->transfer_flags = 0;
    urb->status         = 0;
    urb->actual_length  = 0;
    urb->is_done        = FALSE;
}

/**
 * @brief 核心控制传输枢纽 (大一统接口)
 * @param request_type
 * @param request    请求代码 (如 USB_REQ_GET_DESCRIPTOR)
 * @param value      wValue 参数
 * @param index      wIndex 参数
 * @param length     期待传输的数据长度
 * @return int32     0 表示成功，负数表示各种错误码 (-ETIMEDOUT, -EPIPE 等)
 */
int32 usb_control_msg(usb_dev_t *udev, void *data_buf,
                      uint8 request_type,uint8 request, uint16 value, uint16 index, uint16 length) {

    // =======================================================
    // 1. 在这里统一组装 Setup 包！(全面适配无位域的新架构)
    // =======================================================
    usb_setup_packet_t setup_pkg = {
        .request_type = request_type,
        .request      = request,
        .value        = value,
        .index        = index,
        .length       = length
    };

    // 2. 动态申请 URB 面单
    usb_urb_t *urb = usb_alloc_urb();
    if (!urb) return -ENOMEM;

    // 3. 使用填单助手压制参数 (ep0 = udev->eps[1])
    usb_fill_control_urb(urb, udev, udev->eps[1], &setup_pkg, data_buf, length);

    // 4. 将面单抛给底层调度引擎
    int32 posix_err = xhci_submit_urb(urb);
    if (posix_err < 0) {
        usb_free_urb(urb);
        return posix_err; // 提交入队直接失败
    }

    // =======================================================
    // 5. 阻塞等待与超时控制
    // =======================================================
    uint32 times = 0x30000000;
    while (urb->is_done == FALSE && times--) {
        asm_pause(); // 提示 CPU 让出流水线资源
    }

    // 6. 🌟 核心修复：结算真实状态，而不是只返回“提交成功”
    if (urb->is_done == FALSE) {
        // 严重超时：硬件压根没响应！
        posix_err = -ETIMEDOUT;

        // TODO: 这里极其危险！URB 超时意味着底层 TRB 还在 DMA 环里挂着。
        // 真实 OS 必须在这里调用 xhci_cmd_stop_ep 强行刹车，并清理事件环，
        // 否则直接 free URB 会导致内存被 DMA 踩踏（Use-After-Free）！
    } else {
        // 完美执行完毕，提取中断回调中写回的底层真实状态码 (比如 0 代表成功，-EPIPE 代表 STALL)
        posix_err = urb->status;
    }

    usb_free_urb(urb);
    return posix_err;
}

// ============================================================================
// 📄 设备级描述符获取 API (依托 7 参数大一统枢纽)
// ==========================================

/**
 * @brief 获取设备描述符 (Device Descriptor)
 * @note 全局唯一，不需要索引，不需要语言 ID (wIndex = 0)
 */
static inline int32 _usb_get_dev_desc(usb_dev_t *udev, void *buf, uint16 len) {
    // 🌟 一键生成 bmRequestType: 10000000b (0x80)
    uint8 req_type = USB_BM_REQ_TYPE(USB_REQ_DIR_IN, USB_REQ_TYPE_STANDARD, USB_REQ_REC_DEVICE);

    return usb_control_msg(udev, buf,
                           req_type,
                           USB_REQ_GET_DESCRIPTOR,
                           (USB_DESC_TYPE_DEVICE << 8) | 0, // wValue: 高字节类型，低字节索引 0
                           0,                               // wIndex: 0
                           len);                            // wLength
}

/**
 * @brief 获取配置描述符 (Configuration Descriptor)
 * @param config_index 配置的索引 (通常为 0，代表第 1 个配置)
 */
static inline int32 _usb_get_cfg_desc(usb_dev_t *udev, uint8 config_index, void *buf, uint16 len) {
    uint8 req_type = USB_BM_REQ_TYPE(USB_REQ_DIR_IN, USB_REQ_TYPE_STANDARD, USB_REQ_REC_DEVICE);

    return usb_control_msg(udev, buf,
                           req_type,
                           USB_REQ_GET_DESCRIPTOR,
                           (USB_DESC_TYPE_CONFIG << 8) | config_index, // wValue: 高字节类型，低字节指定配置
                           0,                                          // wIndex: 0
                           len);
}

/**
 * @brief 获取字符串描述符 (String Descriptor)
 * @param string_index 设备描述符中指定的字符串索引 (如 iManufacturer)
 * @param lang_id      语言 ID (通常传入 0x0409 代表美式英语)
 */
static inline int32 _usb_get_string_desc(usb_dev_t *udev, uint8 string_index, uint16 lang_id, void *buf, uint16 len) {
    uint8 req_type = USB_BM_REQ_TYPE(USB_REQ_DIR_IN, USB_REQ_TYPE_STANDARD, USB_REQ_REC_DEVICE);

    return usb_control_msg(udev, buf,
                           req_type,
                           USB_REQ_GET_DESCRIPTOR,
                           (USB_DESC_TYPE_STRING << 8) | string_index, // wValue: 指定要拿几号字符串
                           lang_id,                                    // 🌟 wIndex: 字符串特例，这里放语言 ID!
                           len);
}

/**
 * @brief 获取 BOS 描述符 (Binary Object Store - USB 3.0+ 专属)
 * @note 全局唯一，不需要索引 (wIndex = 0)
 */
static inline int32 usb_get_bos_desc(usb_dev_t *udev, void *buf, uint16 len) {
    uint8 req_type = USB_BM_REQ_TYPE(USB_REQ_DIR_IN, USB_REQ_TYPE_STANDARD, USB_REQ_REC_DEVICE);

    return usb_control_msg(udev, buf,
                           req_type,
                           USB_REQ_GET_DESCRIPTOR,
                           (USB_DESC_TYPE_BOS << 8) | 0,
                           0,
                           len);
}



//=============================================================================================================



device_type_t usb_dev_type = {"usb-dev"};
device_type_t usb_if_type = {"usb-if"};

/**
 * @brief 在接口中寻找匹配指定 Class/SubClass/Protocol 的备用接口
 * @param uif      目标 USB 接口对象
 * @param class    匹配大类 (传 USB_MATCH_ANY 表示忽略该条件)
 * @param subclass 匹配子类 (传 USB_MATCH_ANY 表示忽略)
 * @param protocol 匹配协议 (传 USB_MATCH_ANY 表示忽略)
 * @return usb_if_alt_t* 找到的备用接口指针。未找到则返回 NULL。
 */
usb_if_alt_t* usb_find_alt_if(usb_if_t *uif, int16 class, int16 subclass, int16 protocol) {
    // 防野指针
    if (!uif || !uif->if_alts) return NULL;

    for (uint8 i = 0; i < uif->num_if_alts; i++) {
        usb_if_desc_t *if_desc = uif->if_alts[i].if_desc;

        // 注意：现在比较是 int16 级别，-1 和 0xFF(255) 是彻底不同的两个值！
        if (class != USB_MATCH_ANY && if_desc->interface_class != class) continue;
        if (subclass != USB_MATCH_ANY && if_desc->interface_subclass != subclass) continue;
        if (protocol != USB_MATCH_ANY && if_desc->interface_protocol != protocol) continue;

        return &uif->if_alts[i]; // 完美匹配！返回指针
    }
    return NULL; // 查找失败
}



/**
 * @brief 启用/切换备用接口 (Set Alternate Setting)
 * @param new_alt 上层驱动通过 find_alt 系列函数搜索到的目标备用接口句柄
 * @return 0 表示成功，非 0 表示失败
 * @note 这是 USB 接口切换的核心函数，包含完整的资源分配、xHCI 上下文更新、
 *       设备端 Set Interface 命令以及双向回滚机制
 */
int32 usb_enable_alt_if(usb_if_alt_t *new_alt) {
    // 1. 终极防御：空指针拦截
    if (new_alt == NULL || new_alt->uif == NULL) return -EINVAL;

    int32 posix = 0;
    usb_if_t *uif = new_alt->uif;
    usb_dev_t *udev = uif->udev;
    usb_if_alt_t *old_alt = uif->activity_if_alt;

    // 2. 性能优化：同位切换直接视为成功 (No-op)
    if (old_alt == new_alt) return 0;

    // ==========================================================
    // 阶段 1：[预分配] 为新端点画图纸并分配内存 (软件层准备)
    // ==========================================================
    uint8 new_num_eps = new_alt->if_desc->num_endpoints;
    for (uint8 i = 0; i < new_num_eps; i++) {
        // ★ OOM 防御：分配环可能因为物理 DMA 内存耗尽而失败
        posix = xhci_alloc_ep_ring(&new_alt->eps[i]);
        if (posix < 0) {
            color_printk(RED, BLACK, "USB: OOM allocating rings during Alt setting!\n");
            // 局部回滚：释放刚才循环里已经分配成功的前几个端点
            for (uint8 j = 0; j < i; j++) xhci_free_ep_ring(&new_alt->eps[j]);
            return posix;
        }
    }

    // ==========================================================
    // 阶段 2：[硬件预演] 向 xHCI 提交图纸，等待主板总线裁决
    // ==========================================================
    posix = xhci_ctx_eps_cfg(old_alt, new_alt);
    if (posix < 0) {
        color_printk(RED, BLACK, "xHCI: Switch AltSetting failed, bandwidth rejected!\n");
        // 主板拒绝了这份图纸（通常是总线带宽不足），安全释放刚分配的 RAM
        for (uint8 i = 0; i < new_num_eps; i++) xhci_free_ep_ring(&new_alt->eps[i]);
        return posix;
    }

    // ==========================================================
    // 阶段 3：[物理生效] 通过 EP0 通知 USB 物理外设切换频道！
    // ==========================================================
    posix = usb_set_if(udev, new_alt->if_desc->interface_number, new_alt->if_desc->alternate_setting);
    if (posix < 0) {
        color_printk(RED, BLACK, "USB: Device rejected Set Interface command! Rolling back...\n");

        // ★ 核心修复：外设抗旨不尊，必须强制让 xHCI 主板回滚到旧状态！
        xhci_ctx_eps_cfg(new_alt, old_alt);

        // 释放为新端点分配的废弃环内存
        for (uint8 i = 0; i < new_num_eps; i++) xhci_free_ep_ring(&new_alt->eps[i]);
        return posix; // 操作系统、主板、物理外设毫发无伤地回到了切换前的健康状态！
    }

    // ==========================================================
    // 阶段 4：[过河拆桥] 切换彻底成功，安全收缴旧端点的尸体
    // ==========================================================
    if (old_alt != NULL) {
        uint8 old_num_eps = old_alt->if_desc->num_endpoints;
        for (uint8 i = 0; i < old_num_eps; i++) {
            usb_ep_t *ep = &old_alt->eps[i];
            udev->eps[ep->ep_dci] = NULL; // 从全局路由表摘除
            xhci_free_ep_ring(ep);         // 彻底释放旧物理内存
        }
    }

    // ==========================================================
    // 阶段 5：[更新账本] 挂载新端点，状态机正式翻页
    // ==========================================================
    for (uint8 i = 0; i < new_num_eps; i++) {
        usb_ep_t *ep = &new_alt->eps[i];
        udev->eps[ep->ep_dci] = ep;       // 挂载到 O(1) 全局路由表
    }
    uif->activity_if_alt = new_alt;

    return 0;
}

/**
 * @brief [驱动层 API] 协商并配置备用接口的“流 (Streams)”能力
 * @return int32  最终成功协商出的流指数。如果为 0，表示全线降级为普通 Bulk。
 */
int32 usb_cfg_alt_streams(usb_if_alt_t *alt, uint8 want_streams_exp) {
    // 🌟 优化 1：增加 uif 判空，防止后续多级指针解引用崩溃
    if (!alt || !alt->uif || !alt->uif->udev || !alt->if_desc) return -EINVAL;

    uint8 num_ep = alt->if_desc->num_endpoints;

    // =========================================================================
    // 阶段 0：【光速退场】驱动本身不想要流，直接清零返回
    // =========================================================================
    if (want_streams_exp == 0) {
        for (uint8 i = 0; i < num_ep; i++) {
            alt->eps[i].enable_streams_exp = 0;
        }
        return 0;
    }

    // =========================================================================
    // 阶段 1：【探底博弈】寻找端点硬件的最短板 (使用哨兵机制)
    // =========================================================================
    uint8 ep_min_exp = 0xFF; // 🌟 优化 2：用 0xFF 作为哨兵，干掉 boolean 标志位

    for (uint8 i = 0; i < num_ep; i++) {
        uint8 ep_max = alt->eps[i].max_streams_exp;
        if (ep_max > 0 && ep_max < ep_min_exp) {
            ep_min_exp = ep_max;
        }
    }

    // =========================================================================
    // 阶段 2：【三方会谈】综合 设备端点、主板控制器、上层驱动 的诉求
    // =========================================================================
    uint8 final_exp = 0;

    if (ep_min_exp != 0xFF) { // 至少存在一个支持流的端点
        final_exp = ep_min_exp;

        // 🌟 优化 3：延迟解引用 xhcd，避免 99% 的普通设备触发缓存未命中
        uint8 host_max = alt->uif->udev->xhcd->max_streams_exp;

        if (host_max < final_exp) final_exp = host_max;
        if (want_streams_exp < final_exp) final_exp = want_streams_exp;
    }

    // =========================================================================
    // 阶段 3：【图纸重绘】精准覆盖
    // =========================================================================
    for (uint8 i = 0; i < num_ep; i++) {
        usb_ep_t *ep = &alt->eps[i];
        // 🌟 优化 4：利用三目运算符折叠分支，消除 if-else 带来的指令预测开销
        ep->enable_streams_exp = (ep->max_streams_exp > 0) ? final_exp : 0;
    }

    return final_exp;
}

//===================================================== 解析描述符非配资源 ============================================

/**
 * @brief [内部辅助] 解析并装填 USB 标准端点参数 (USB 2.0 规格底稿)
 */
static inline void usb_ep_desc_params(usb_ep_t *cur_ep, usb_ep_desc_t *ep_desc) {
    // 提取纯粹的 USB 传输类型 (0~3)
    uint8 usb_trans_type = ep_desc->attributes & 3;

    // 基础物理映射
    cur_ep->ep_dci = epaddr_to_epdci(ep_desc->endpoint_address);
    cur_ep->ep_type = ((ep_desc->endpoint_address & 0x80) >> 5) + usb_trans_type;
    cur_ep->max_packet_size = ep_desc->max_packet_size & 0x07FF;
    cur_ep->mult = (ep_desc->max_packet_size >> 11) & 0x3;
    cur_ep->interval = ep_desc->interval;

    // 清空高阶扩展字段 (防野指针)
    cur_ep->max_burst = 0;
    cur_ep->max_streams_exp = 0;
    cur_ep->bytes_per_interval = 0;
    cur_ep->extras_desc = NULL;
    cur_ep->lsa = 0;
    cur_ep->hid = 0;

    cur_ep->ring_arr = NULL;
    cur_ep->streams_ctx_array = NULL;
    cur_ep->enable_streams_exp = 0;

    // --- ★ 衍生参数与 DMA 启发值联合推导 (基于 USB 2.0 规格底稿) ---
    switch (usb_trans_type) {
        case USB_EP_TYPE_ISOCH:
            // Isochronous 阵营：音视频流，要求极高的周期吞吐量
            cur_ep->max_esit_payload = cur_ep->max_packet_size * (cur_ep->mult + 1);
            // 等时流永远是满载发送，直接使用最大周期负荷作为平均值
            cur_ep->average_trb_length = cur_ep->max_esit_payload;
            break;

        case USB_EP_TYPE_BULK:
            // Bulk 阵营：吃总线闲置带宽，无固定 ESIT 周期限制
            cur_ep->max_esit_payload = 0;
            // 黄金魔法值：3072 (3 个 USB 3.0 数据包) 完美平衡 PCIe 突发与硬件 FIFO
            cur_ep->average_trb_length = 3072;
            break;

        case USB_EP_TYPE_INTR:
            // Interrupt 阵营：要求极其严苛的周期性带宽保证
            cur_ep->max_esit_payload = cur_ep->max_packet_size * (cur_ep->mult + 1);
            // 数据量极小，暗示主板硬件只需分配最小 SRAM 缓存即可
            cur_ep->average_trb_length = cur_ep->max_packet_size;
            break;

        default:                 // 兜底 Control 及其它
            cur_ep->max_esit_payload = 0;
            cur_ep->average_trb_length = 8;
            break;
    }
}

/**
 * @brief [内部辅助] 解析并升级 USB 3.0 超高速伴随参数
 */
static inline void usb_ss_desc_params(usb_ep_t *cur_ep, usb_ss_comp_desc_t *ss_desc) {
    // ★ 极客解码：直接从已经映射好的 xHCI 类型中，反向剥离出纯 USB 传输类型
    uint8 usb_trans_type = cur_ep->ep_type & 3;

    cur_ep->max_burst = ss_desc->max_burst;
    cur_ep->bytes_per_interval = ss_desc->bytes_per_interval;

    // ★ 物理隔离与参数覆写：基于端点类型的高内聚升级
    switch (usb_trans_type) {
        case USB_EP_TYPE_BULK:
            // Bulk 阵营：提取最大支持的并发流数量 (Streams)
            cur_ep->max_streams_exp = ss_desc->attributes & 0x1F;
            break;

        case USB_EP_TYPE_ISOCH:
            // Isochronous 阵营：提取真实乘数，原地覆写掉第一阶段的 USB 2.0 伪值
            cur_ep->mult = ss_desc->attributes & 0x03;

            // 衍生参数升级：直接用硬件出厂标定的周期诉求，替换掉 USB 2.0 的计算公式
            if (cur_ep->bytes_per_interval > 0) {
                cur_ep->max_esit_payload = cur_ep->bytes_per_interval;
                cur_ep->average_trb_length = cur_ep->max_esit_payload; // 同步升级 DMA 估算值
            }
            break;

        case USB_EP_TYPE_INTR:
            // Interrupt 阵营：规范铁律要求伴随属性为保留位。强行清零防止主板报错
            cur_ep->mult = 0;
            cur_ep->max_streams_exp = 0;

            // 衍生参数升级：只升级周期诉求带宽，中断端点的 average_trb_length 保持极小值不变
            if (cur_ep->bytes_per_interval > 0) {
                cur_ep->max_esit_payload = cur_ep->bytes_per_interval;
            }
            break;
    }
}


/**
 * @brief [终极精简版] 零碎片、一次性分配的 USB 描述符解析引擎
 * @param udev USB 设备对象
 * @return 0 表示成功，负数表示失败
 */
int32 usb_if_create(usb_dev_t *udev) {
    usb_cfg_desc_t *cfg_desc = udev->config_desc;
    if (!cfg_desc || cfg_desc->head.length < sizeof(usb_cfg_desc_t)) return -EINVAL;

    // =========================================================================
    // 阶段 1：[纯净统计] 只数人头，确认拓扑规模
    // =========================================================================
    uint32 total_alts = 0, total_eps = 0 ,num_ifs = 0;
    uint8 max_if_num = 0;

    usb_desc_head_t *desc_head = (usb_desc_head_t *)cfg_desc;
    void *cfg_end = (uint8 *)cfg_desc + cfg_desc->total_length;

    while ((void *)desc_head < cfg_end) {
        if (desc_head->length < 2) return -EINVAL; // 必不可少的防死锁

        if (desc_head->desc_type == USB_DESC_TYPE_INTERFACE) {
            total_alts++;
            uint8 if_num = ((usb_if_desc_t *)desc_head)->interface_number;
            if (if_num > max_if_num) max_if_num = if_num;
        }
        else if (desc_head->desc_type == USB_DESC_TYPE_ENDPOINT) {
            total_eps++;
        }
        desc_head = usb_get_next_desc(desc_head);
    }

    num_ifs = max_if_num + 1; // 真实接口数量

    // =========================================================================
    // 阶段 2 & 3：[切割地盘] 连续内存分配与总线拓扑绑定
    // =========================================================================
    uint32 mem_size = (num_ifs * sizeof(usb_if_t)) +
                      (total_alts * sizeof(usb_if_alt_t)) +
                      (total_eps * sizeof(usb_ep_t));

    void *mem_block = kzalloc(mem_size);
    if (!mem_block) return -ENOMEM;

    udev->ifs = (usb_if_t *)mem_block;
    usb_if_alt_t *alts_pool = (usb_if_alt_t *)(udev->ifs + num_ifs);
    usb_ep_t *eps_pool = (usb_ep_t *)(alts_pool + total_alts);

    // 绑定拓扑 (信任 kzalloc: num_if_alts 和 if_alts 已默认清零/为空，无需重复赋值)
    for (uint32 i = 0; i < num_ifs; i++) {
        udev->ifs[i].udev = udev;
        udev->ifs[i].dev.type = &usb_if_type;
        udev->ifs[i].dev.parent = &udev->dev;
        udev->ifs[i].dev.bus = &usb_bus_type;
    }

    // =========================================================================
    // 阶段 4：[血肉装填] 真 O(N) 状态机解析
    // =========================================================================
    desc_head = (usb_desc_head_t *)cfg_desc;
    usb_if_alt_t *cur_alt = NULL;
    usb_ep_t *cur_ep = NULL; // 🌟 核心优化：引入独立的端点游标

    while ((void *)desc_head < cfg_end) {
        if (desc_head->length < 2) break;

        switch (desc_head->desc_type) {
            case USB_DESC_TYPE_INTERFACE: {
                uint8 if_num = ((usb_if_desc_t *)desc_head)->interface_number;
                if (if_num < num_ifs) {
                    usb_if_t *cur_if = &udev->ifs[if_num];

                    cur_alt = alts_pool++;
                    if (cur_if->num_if_alts == 0) cur_if->if_alts = cur_alt;
                    cur_if->num_if_alts++;

                    cur_alt->uif = cur_if;
                    cur_alt->if_desc = (usb_if_desc_t *)desc_head;
                    cur_alt->eps = eps_pool;

                    cur_ep = NULL; // 🌟 跨入新接口辖区，清空当前端点游标！
                }
                break;
            }
            case USB_DESC_TYPE_ENDPOINT: {
                if (cur_alt) {
                    cur_ep = eps_pool++; // 🌟 获取当前端点实体
                    usb_ep_desc_params(cur_ep, (usb_ep_desc_t *)desc_head);
                }
                break;
            }
            case USB_DESC_TYPE_SS_ENDPOINT_COMPANION: {
                if (cur_ep) usb_ss_desc_params(cur_ep, (usb_ss_comp_desc_t *)desc_head);
                break;
            }
            default: {
                // 🌟 私有描述符挂载变得极其优雅，告别烧脑的指针地址比较
                if (cur_ep) {
                    if (!cur_ep->extras_desc) cur_ep->extras_desc = desc_head;
                    cur_ep->extras_len += desc_head->length;
                } else if (cur_alt) {
                    if (!cur_alt->extras_desc) cur_alt->extras_desc = desc_head;
                    cur_alt->extras_len += desc_head->length;
                }
                break;
            }
        }

        desc_head = usb_get_next_desc(desc_head);
    }

    return 0;
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

/**
 * @brief 阶段 2：通过 EP0 获取设备描述符，并动态修正全速设备的 MPS
 * @param udev USB 设备对象
 * @return int32 0 表示成功
 */
static inline int32 usb_get_dev_desc(usb_dev_t *udev) {
    // 分配设备描述符的 DMA 内存
    usb_dev_desc_t *dev_desc = kzalloc_dma(sizeof(usb_dev_desc_t));
    // ============================
    // 全速设备 (FS) 的 8 字节刺探与修正逻辑
    // ============================
    if (udev->port_speed == USB_SPEED_FULL) {
        // 探针：只拿前 8 字节
        _usb_get_dev_desc(udev,dev_desc,8);
        if (dev_desc->max_packet_size0 != 8) {
            usb_ep_t *ep0 = udev->eps[1];
            ep0->max_packet_size = dev_desc->max_packet_size0;
            xhci_ctx_slot_ep0_eval(udev);
        }
    }
    // ============================
    // 获取完整的 18 字节设备描述符
    // ============================
    _usb_get_dev_desc(udev,dev_desc,sizeof(usb_dev_desc_t));
    // 挂载到内核对象树上
    udev->dev_desc = dev_desc;
    return 0;
}

//获取usb配置描述符
static inline int usb_get_cfg_desc(usb_dev_t *udev) {
    usb_cfg_desc_t *config_desc = kzalloc_dma(sizeof(usb_cfg_desc_t));
    //第一次先获取配置描述符前9字节
    _usb_get_cfg_desc(udev,0,config_desc,9);
    //第二次从配置描述符中得到总长度获取整个配置描述符
    uint16 config_desc_length = config_desc->total_length;
    kfree(config_desc);
    config_desc = kzalloc_dma(config_desc_length);
    _usb_get_cfg_desc(udev,0,config_desc,config_desc_length);
    udev->config_desc = config_desc;
    usb_set_cfg(udev);          //启用配置
    return 0;
}

//获取字符串描述符
static inline int usb_get_string_desc(usb_dev_t *udev) {
    usb_desc_head_t *desc_head = kzalloc_dma(2);

    //获取语言ID描述符
    uint16 language_id;
    _usb_get_string_desc(udev,0,0,desc_head,2);
    usb_string_desc_t *language_desc = kzalloc_dma(desc_head->length);    // 分配真实长度的 DMA 内存

    // 正式拉取
    _usb_get_string_desc(udev,0,0,language_desc,desc_head->length);
    if (language_desc->head.desc_type == USB_DESC_TYPE_STRING) {
        language_id = language_desc->string[0];
        udev->language_desc = language_desc;
    }else {
        language_id = 0x0409;
        udev->language_desc = 0;
        kfree(language_desc);
    }

    //默认设备都支持美式英语
    uint8 string_index[3] = {
        udev->dev_desc->manufacturer_index, udev->dev_desc->product_index,udev->dev_desc->serial_number_index
    };
    usb_string_desc_t *string_desc[3];
    uint8 *string_ascii[3];

    //获取制造商/产品型号/序列号字符串描述符
    for (uint8 i = 0; i < 3; i++) {
        if (string_index[i]) {
            //第一次先获取长度
            _usb_get_string_desc(udev,string_index[i],language_id,desc_head,2);

            //分配内存
            string_desc[i] = kzalloc_dma(desc_head->length);

            //第二次先正式获取字符串描述符N
            _usb_get_string_desc(udev,string_index[i],language_id,string_desc[i],desc_head->length);

            //解析字符串描述符
            uint8 string_ascii_length = (desc_head->length-2)/2;
            string_ascii[i] = kzalloc(string_ascii_length+1);
            utf16le_to_ascii(string_desc[i]->string,string_ascii[i],string_ascii_length);
        }else {
            string_desc[i] = NULL;
        }
    }

    udev->manufacturer_desc = string_desc[0];
    udev->product_desc = string_desc[1];
    udev->serial_number_desc = string_desc[2];
    udev->manufacturer = string_ascii[0];
    udev->product = string_ascii[1];
    udev->serial_number = string_ascii[2];
    kfree(desc_head);
    return 0;
}

// =========================================================================
// 🚀 终极版：统一设备创建引擎 (智能适配原生端口与级联Hub)
// =========================================================================
void usb_dev_init(usb_dev_t *udev) {
    // ==========================================================
    // 🚀 生命周期初始化 (Life Cycle)
    // ==========================================================
    // USB 2.0/1.1 规范：复位结束后，必须给设备固件 10ms~50ms 的清醒时间。USB 3.0 直接跳过
    if (udev->port_speed <= USB_SPEED_HIGH) {
        uint32 times = 0x5000000;
        while (times) {
            times--;
            asm_pause();
        }
    }

    // 挂载总线对象模型
    udev->dev.type = &usb_dev_type;
    udev->dev.parent = &udev->xhcd->xdev->dev;
    udev->dev.bus = &usb_bus_type;

    //设备初始化
    xhci_enable_slot_ep0(udev);
    usb_get_dev_desc(udev);
    usb_get_cfg_desc(udev);
    usb_get_string_desc(udev);

    //注册设备
    usb_if_create(udev);
    usb_dev_register(udev);
    usb_if_register(udev);

    //打印设备信息
    color_printk(YELLOW,BLACK,"usb-dev:%s %s %s root_port_num:%d tt_hub_slot_id:%d tt_port_num:%d hub_depth:%d rout_string:%#x port_speed:%d  \n", \
        udev->manufacturer,udev->product,udev->serial_number,udev->root_hub_port_num,udev->tt_hub_slot_id,udev->tt_port_num,udev->hub_depth,udev->route_string,udev->port_speed);
    return;
}

//======================================= 驱动======================================

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