#pragma once
#include "moslib.h" // 包含你的基本类型如 uint32, uint64 等

typedef struct xhci_trb_t xhci_trb_t;
typedef struct xhci_submit_ring_t xhci_submit_ring_t;
typedef struct xhci_event_ring_t xhci_event_ring_t;
typedef struct xhci_hcd_t xhci_hcd_t;
typedef struct xhci_command_t xhci_command_t;



// 计算步进后的索引，自动跨越 Link TRB
static inline uint32 xhci_submit_ring_next_idx(uint32 cur_idx,uint32 size) {
    // 如果走到倒数第一个位置 (Link TRB)，直接绕回 0
    return (++cur_idx == size - 1) ? 0 : cur_idx;
}

uint64 xhci_submit_ring_enq(xhci_submit_ring_t *ring, xhci_trb_t *trb_push);
int32 xhci_event_ring_deq(xhci_event_ring_t *ring, xhci_trb_t *out_evt);
int32 xhci_alloc_submit_ring(xhci_submit_ring_t *ring,uint32 size);  //分配发送环
int32 xhci_free_submit_ring(xhci_submit_ring_t *ring); //释放发送环
int32 xhci_alloc_event_ring(xhci_event_ring_t *ring,uint32 ring_size); //分配事件环
int32 xhci_free_event_ring(xhci_event_ring_t *ring); //释放事件环
int32 xhci_submit_cmd(xhci_hcd_t *xhcd, xhci_trb_t *cmd_trb,xhci_command_t *out_command);

struct usb_ep_t;
struct usb_urb_t;
int32 xhci_alloc_ep_ring(struct usb_ep_t *ep);
int32 xhci_free_ep_ring(struct usb_ep_t *ep);
int32 xhci_submit_urb(struct usb_urb_t *urb);

