#include "slub.h"
#include "printk.h"
#include "errno.h"
#include "xhci-ring.h"
#include "usb-core.h"
#include "usb-def.h"
#include "../include/usb-dev.h"


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
    // 5. 阻塞等待
    // =======================================================
    while (urb->is_done == FALSE) {
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
