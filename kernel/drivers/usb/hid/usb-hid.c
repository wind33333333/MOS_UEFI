#include "usb-hid.h"
#include "../../../include/printk.h"
#include "../core/usb-core.h"
#include "hid-core.h"
#include "../xhci/xhci-hcd.h"
#include "hid-parser.h"

// 全局唯一的 HID 生肉队列
hid_raw_queue_t g_hid_raw_queue;


void hid_irq_complete(usb_urb_t *urb) {
    hid_dev_t *hdev = urb->private_data;

    // 2. 将数据推入生肉队列 (这里用伪代码示意锁操作，具体看你的内核基建)
    // spin_lock(&g_hid_raw_queue.lock);

    uint32 next_head = (g_hid_raw_queue.head + 1) % HID_RAW_QUEUE_SIZE;
    if (next_head != g_hid_raw_queue.tail) { // 队列没满
        hid_raw_event_t *event = &g_hid_raw_queue.events[g_hid_raw_queue.head];
        event->hdev = hdev;
        event->data_len = urb->actual_length;
        // 极速内存拷贝 (8 字节通常只要几个 CPU 时钟周期)
        asm_mem_cpy(hdev->report_buf,event->raw_data,  urb->actual_length);
        g_hid_raw_queue.head = next_head;

        // 唤醒可能正在沉睡的后台解析线程
        // semaphore_up(&g_hid_raw_queue.wait_sem);
    } else {
        // 队列满了，直接丢弃（总比内核卡死好）
        // color_printk(RED, BLACK, "HID raw queue overflow!\n");
    }
    // spin_unlock(&g_hid_raw_queue.lock);

    // 3. 极速续命：在硬中断中立刻将 URB 交还给 xHCI，保证键盘不会“掉线”
    xhci_submit_urb(urb);
}


// =======================================================
// 🐌 底半部 (Bottom Half)：专用内核线程，执行复杂逻辑
// =======================================================
void hid_worker_thread_main(void *arg) {
    // 1. 如果队列为空，线程在这里休眠，不消耗 CPU
    // semaphore_down(&g_hid_raw_queue.wait_sem);

    // 2. 从生肉队列中取出一盘菜
    // spin_lock(&g_hid_raw_queue.lock);
    if (g_hid_raw_queue.head == g_hid_raw_queue.tail) {
        // spin_unlock(&g_hid_raw_queue.lock);
        return;
    }

    hid_raw_event_t event; // 拷贝到局部变量，尽量缩短锁占用的时间
    event = g_hid_raw_queue.events[g_hid_raw_queue.tail];
    g_hid_raw_queue.tail = (g_hid_raw_queue.tail + 1) % HID_RAW_QUEUE_SIZE;
    // spin_unlock(&g_hid_raw_queue.lock);

    // =======================================================
    // 🎯 3. 真正的重活儿来了：暴力展开与查表比对
    // =======================================================
    hid_dev_t *hdev = event.hdev;
    uint8 *raw_data = event.raw_data; // 注意这里用的是刚刚从队列里取出来的备份数据

    // 外层循环：遍历所有 field
    for (int i = 0; i < hdev->field_count; i++) {
        hid_field_t *field = hdev->fields[i];
        if (field->report_type != HID_MAIN_TAG_INPUT) continue;

        uint32 bit_pos = field->bit_offset;

        // 内层循环：根据 report_count 切肉
        for (uint32 j = 0; j < field->report_count; j++) {
            uint32 val = hid_extract_bits(raw_data, bit_pos, field->bit_size);
            bit_pos += field->bit_size;

            // 过滤掉无效值 (0 通常代表无动作 / 没有按键)
            if (val == 0) continue;

            color_printk(RED,BLACK,"%d ",val);

        }
    }

    // 4. 对比 current_value 和 previous_value
    // 5. 调用 input_report_key(...) 把标准事件发给 TheresaOS 的应用层
    //hid_process_state_and_report(hdev);
}