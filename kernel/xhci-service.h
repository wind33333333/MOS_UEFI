#pragma once
#include "moslib.h"
#include "interrupt.h"

struct xhci_hcd_t;
irqreturn_e xhci_isr(cpu_registers_t *regs,void *dev_id);
void xhci_port_scan(struct xhci_hcd_t *xhcd);
void xhci_process_port_event(struct xhci_hcd_t *xhcd, uint8 port_num);


/**
 * @brief 事件类型枚举，用于区分是主板直连还是外接Hub
 */
typedef enum {
    USB_EVENT_NONE = 0,
    USB_EVENT_XHCI_ROOT_PORT, // 来源：xHCI 原生根端口中断
    USB_EVENT_HUB_WORK        // 来源：外接 USB Hub 状态中断
} usb_event_type_e;

/**
 * @brief 队列中传递的“小纸条” (事件载体)
 */
typedef struct {
    usb_event_type_e type;    // 事件类型
    void *parent_dev;         // 上下文指针 (若是根端口则指向 xhci_hcd_t；若是Hub则指向 usb_dev_t)
    uint8 port_num;           // 发生状态改变的物理端口号 (从 1 开始)
} usb_port_event_t;

// 预分配环形队列大小 (必须足够大以容纳插拔瞬间的中断风暴)
#define USB_EVENT_QUEUE_SIZE 256

/**
 * @brief 静态环形队列结构
 */
typedef struct {
    usb_port_event_t events[USB_EVENT_QUEUE_SIZE];
    volatile uint32 head; // 消费者 (主循环) 读取位置
    volatile uint32 tail; // 生产者 (ISR/轮询) 写入位置
} usb_event_queue_t;

boolean usb_event_queue_pop(usb_port_event_t *out_event);
boolean usb_event_queue_push(usb_event_type_e type, void *parent, uint8 port_num);
void usb_event_queue_init(void);
