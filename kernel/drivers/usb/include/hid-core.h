#pragma once

#include "moslib.h"

struct usb_if_t;

// 描述“一段特定含义的数据”的结构体
typedef struct {
    list_head_t node;  // ★ 内核标准的双向链表节点
    uint32 bit_offset;   // 这段数据在盲盒里的起始 bit 位置
    uint32 bit_size;     // 这段数据占多少个 bit
    uint32 report_count;
    uint32 usage_page;   // 字典大类 (比如 0x07 代表键盘)
    uint32 usage_min;    // 字典具体词条范围起始
    uint32 usage_max;    // 字典具体词条范围结束
    uint8  is_array;     // 0: 位图(如Ctrl/Shift)  1: 数组(如普通按键)
    uint8  dir;          // 0: Input (读按键), 1: Output (写指示灯)
} hid_field_t;

// 设备结构体：只保留一个链表头
typedef struct {
    list_head_t field_list_head; // ★ 链表头
    uint32      field_count;     // 纯粹用来做统计
    usb_urb_t   *int_urb;
    uint8       *report_buf;
    struct usb_if_t *uif;
} hid_dev_t;

usb_drv_t *create_usb_hid_driver();