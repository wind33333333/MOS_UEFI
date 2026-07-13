#pragma once
#include "moslib.h"
#include "usb-def.h"

struct usb_dev_t;
struct usb_ep_t;

/* ========================================================================
 * URB 传输控制标志位 (Transfer Flags)
 * ======================================================================== */

// 1. 数据完整性控制
#define URB_SHORT_NOT_OK        0x0001  // ★ 严格模式：发生短包 (Short Packet) 时直接视为错误。仅适用于 IN 传输。

// 2. 内存与 DMA 管理
#define URB_NO_TRANSFER_DMA_MAP 0x0004  // 驱动已自行完成物理内存映射，底层请直接使用 urb->transfer_dma，不要再调 IOMMU/映射函数。
#define URB_NO_SETUP_DMA_MAP    0x0008  // 驱动已自行完成 Setup 包的物理映射，底层请直接使用 urb->setup_dma。
#define URB_FREE_BUFFER         0x0100  // 内核托管：当这个 URB 被销毁时，请帮我自动 kfree 掉 transfer_buffer。

// 3. 协议边缘情况处理
#define URB_ZERO_PACKET         0x0040  // ★ Bulk OUT 极客专属：如果传输长度刚好是 MaxPacketSize 的整数倍，强制在末尾追加一个 0 字节包 (ZLP)，防止设备死等。

// 4. 硬件中断调度控制 (替换原先的 ioc)
#define URB_NO_INTERRUPT        0x0080  // ★ 幽灵模式：静默传输，传输完成时【不触发】硬件中断 (即清零最后一个 TRB 的 IOC 位)。通常用于大批量 URB 连续提交的场景，只在最后一个 URB 开启中断。

// 5. 传输方向强制覆盖 (可选，通常优先以端点描述符方向为准)
#define URB_DIR_IN              0x0200  // 强制标明方向为：设备 -> 主机
#define URB_DIR_OUT             0x0000  // 强制标明方向为：主机 -> 设备
#define URB_DIR_MASK            0x0200  // 用于提取方向的掩码




/**
 * @brief USB 请求块 (USB Request Block) - 纯逻辑版
 */
typedef struct usb_urb_t {
    // === 1. 路由寻址区 ===
    struct usb_dev_t   *udev;        // 目标设备上下文
    struct usb_ep_t    *ep;          // 目标端点
    uint16      stream_id;    // UAS 协议专用的 Stream ID

    // === 2. 业务载荷区 ===
    usb_setup_packet_t *setup_packet;// EP0 控制包指针
    void        *transfer_buf;  // 数据缓冲区虚拟地址
    uint32      transfer_len;   // 期望传输总长度
    uint32      interval;

    // 👑 核心换血：用标志位取代具体的硬件配置
    uint32      transfer_flags; // 传输控制组合掩码 (如 URB_SHORT_NOT_OK | URB_NO_INTERRUPT)

    // === 3. 状态与回调区 ===
    uint32      actual_length;  // [新增] 实际成功传输的字节数 (硬件回填)
    uint64      last_trb_pa;    // 最后一个 TRB 的物理地址 (仅做底层同步过渡用)
    int         status;         // URB 状态码

    // void (*complete)(struct usb_urb *urb); // 未来做全异步驱动时，这里放回调函数

    list_head_t node;         // 挂载到端点 pending_urbs 的链表节点

    // 🌟 单任务环境的终极同步神器
    volatile boolean is_done;
} usb_urb_t;


//端点转Dci
static inline uint8 epaddr_to_epdci(uint8 ep) {
    asm volatile(
        "rolb $1,%0"
        :"+q"(ep)
        :
        :"cc");
    return ep;
}

//Dci转端点
static inline uint8 epdci_to_epaddr(uint8 dci) {
    asm volatile(
        "rorb $1,%0"
        :"+q"(dci)
        :
        :"cc");
    return dci;

}



usb_urb_t *usb_alloc_urb(void);
void usb_free_urb(usb_urb_t *urb);
void usb_fill_control_urb(usb_urb_t *urb,struct usb_dev_t *udev,struct usb_ep_t *ep,usb_setup_packet_t *setup_packet,void *transfer_buf,uint32 transfer_len);
void usb_fill_bulk_urb(usb_urb_t *urb,struct usb_dev_t *udev,struct usb_ep_t *ep,void *transfer_buf,uint32 transfer_len);
void usb_fill_int_urb(usb_urb_t *urb,struct usb_dev_t *udev,struct usb_ep_t *ep,void *transfer_buf,uint32 transfer_len,uint32 interval);
int32 usb_control_msg(struct usb_dev_t *udev, void *data_buf,uint8 request_type,uint8 request, uint16 value, uint16 index, uint16 length);


