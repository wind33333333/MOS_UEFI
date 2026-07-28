#pragma once

#include "moslib.h"

/* =========================================================================
 * HID 字段标志位定义 (严格对应 USB HID Specification v1.11, Section 6.2.2.5)
 * ========================================================================= */
#define HID_FLAG_CONSTANT       (1 << 0)  // 0: Data            | 1: Constant
#define HID_FLAG_VARIABLE       (1 << 1)  // 0: Array           | 1: Variable
#define HID_FLAG_RELATIVE       (1 << 2)  // 0: Absolute        | 1: Relative
#define HID_FLAG_WRAP           (1 << 3)  // 0: No Wrap         | 1: Wrap
#define HID_FLAG_NONLINEAR      (1 << 4)  // 0: Linear          | 1: Non Linear
#define HID_FLAG_NOPREFERRED    (1 << 5)  // 0: Preferred State | 1: No Preferred
#define HID_FLAG_NULLSTATE      (1 << 6)  // 0: No Null position| 1: Null state
#define HID_FLAG_VOLATILE       (1 << 7)  // 0: Non Volatile    | 1: Volatile
#define HID_FLAG_BUFFERED_BYTES (1 << 8)  // 0: Bit Field       | 1: Buffered Bytes

/* =========================================================================
 * HID 标志位判断操作宏 (极速内联展开，专治位运算魔法数字)
 * ========================================================================= */
// 数据类型检查
#define HID_FIELD_IS_CONSTANT(flags)   (((flags) & HID_FLAG_CONSTANT) != 0)
#define HID_FIELD_IS_DATA(flags)       (((flags) & HID_FLAG_CONSTANT) == 0)

// 阵列与变量检查 (解决之前 Array/Variable 提取 BUG 的核心)
#define HID_FIELD_IS_VARIABLE(flags)   (((flags) & HID_FLAG_VARIABLE) != 0)
#define HID_FIELD_IS_ARRAY(flags)      (((flags) & HID_FLAG_VARIABLE) == 0)

// 相对与绝对坐标检查 (鼠标属于 Relative，触控板/手柄属于 Absolute)
#define HID_FIELD_IS_RELATIVE(flags)   (((flags) & HID_FLAG_RELATIVE) != 0)
#define HID_FIELD_IS_ABSOLUTE(flags)   (((flags) & HID_FLAG_RELATIVE) == 0)


struct hid_dev_t;

int32 hid_parse_report_desc(struct hid_dev_t *hdev, uint8 *desc, uint32 desc_len);
uint32 hid_extract_bits(const uint8 *report, uint32 bit_offset, uint32 bit_size);