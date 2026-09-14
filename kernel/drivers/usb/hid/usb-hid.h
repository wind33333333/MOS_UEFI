#pragma once
#include "moslib.h"

// 假设 TheresaOS 最大支持 64 字节的 HID 报文（足够绝大部分键鼠）
#define MAX_HID_RAW_PAYLOAD 64
#define HID_RAW_QUEUE_SIZE  128

struct hid_dev_t;

// "hid原始数据"事件包定义
typedef struct {
    struct hid_dev_t *hdev;                 // 这个数据归属于哪个 HID 设备
    uint8 raw_data[MAX_HID_RAW_PAYLOAD]; // 从 URB 拷贝出来的原始数据
    uint32 data_len;                    // 实际拷贝的数据长度
} hid_raw_event_t;

// 专门为 HID 准备原始数据环形队列
typedef struct {
    hid_raw_event_t events[HID_RAW_QUEUE_SIZE];
    uint32 head; // 生产者写入位置
    uint32 tail; // 消费者读取位置
    // spinlock_t lock; // 如果你不用无锁算法，就需要一把自旋锁
    // semaphore_t wait_sem; // 信号量，用于在队列空时休眠消费者线程
} hid_raw_queue_t;

void hid_irq_complete(void *urb);