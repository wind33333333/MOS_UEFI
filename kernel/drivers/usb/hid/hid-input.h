#pragma once

#include "moslib.h"

#define HID_ID(page, usage) (((uint32_t)(page) << 16) | ((uint32_t)(usage) & 0xFFFF))

// 定义几个常见的 USB HID 用途页 (Usage Pages) 规范宏
#define HID_UP_GENDESK   0x01 // 通用桌面设备 (鼠标X/Y轴等)
#define HID_UP_KEYBOARD  0x07 // 标准键盘
#define HID_UP_LED       0x08 // LED 状态灯
#define HID_UP_BUTTON    0x09 // 鼠标/手柄物理按键

/* =========================================================
 * HID Usage Page 0x01: Generic Desktop Page Usages
 * ========================================================= */
#define HID_GD_POINTER          0x01    // 指针设备
#define HID_GD_MOUSE            0x02    // 鼠标
#define HID_GD_JOYSTICK         0x04    // 摇杆
#define HID_GD_GAMEPAD          0x05    // 游戏手柄
#define HID_GD_KEYBOARD         0x06    // 键盘
#define HID_GD_KEYPAD           0x07    // 小键盘

/* === 空间与运动轴线 (Axes) === */
#define HID_GD_X                0x30    // X 轴 (平移)
#define HID_GD_Y                0x31    // Y 轴 (平移)
#define HID_GD_Z                0x32    // Z 轴 (平移/下压)
#define HID_GD_RX               0x33    // X 轴旋转 (Pitch)
#define HID_GD_RY               0x34    // Y 轴旋转 (Roll)
#define HID_GD_RZ               0x35    // Z 轴旋转 (Yaw)
#define HID_GD_SLIDER           0x36    // 滑动轴
#define HID_GD_DIAL             0x37    // 旋钮
#define HID_GD_WHEEL            0x38    // 垂直滚轮 (Mouse Wheel)
#define HID_GD_HATSWITCH        0x39    // 苦无帽/方向帽 (Hat Switch)

struct hid_dev_t;

void hid_create_input_dev(struct hid_dev_t *hdev);