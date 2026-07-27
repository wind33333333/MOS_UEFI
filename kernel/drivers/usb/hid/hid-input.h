#pragma once

#include "moslib.h"

#define KEY_RESERVED        0

// ==========================================
// 1. 主键盘区 (Alphanumeric & Punctuation)
// ==========================================
#define KEY_ESC             1
#define KEY_1               2
#define KEY_2               3
#define KEY_3               4
#define KEY_4               5
#define KEY_5               6
#define KEY_6               7
#define KEY_7               8
#define KEY_8               9
#define KEY_9               10
#define KEY_0               11
#define KEY_MINUS           12      // -
#define KEY_EQUAL           13      // =
#define KEY_BACKSPACE       14

#define KEY_TAB             15
#define KEY_Q               16
#define KEY_W               17
#define KEY_E               18
#define KEY_R               19
#define KEY_T               20
#define KEY_Y               21
#define KEY_U               22
#define KEY_I               23
#define KEY_O               24
#define KEY_P               25
#define KEY_LEFTBRACE       26      // [
#define KEY_RIGHTBRACE      27      // ]
#define KEY_ENTER           28

#define KEY_A               30
#define KEY_S               31
#define KEY_D               32
#define KEY_F               33
#define KEY_G               34
#define KEY_H               35
#define KEY_J               36
#define KEY_K               37
#define KEY_L               38
#define KEY_SEMICOLON       39      // ;
#define KEY_APOSTROPHE      40      // '
#define KEY_GRAVE           41      // ` (波浪号那个键)

#define KEY_BACKSLASH       43      // \ (反斜杠)
#define KEY_Z               44
#define KEY_X               45
#define KEY_C               46
#define KEY_V               47
#define KEY_B               48
#define KEY_N               49
#define KEY_M               50
#define KEY_COMMA           51      // ,
#define KEY_DOT             52      // .
#define KEY_SLASH           53      // /

#define KEY_SPACE           57

// ==========================================
// 2. 控制键与修饰键 (Modifiers)
// ==========================================
#define KEY_LEFTCTRL        29
#define KEY_LEFTSHIFT       42
#define KEY_RIGHTSHIFT      54
#define KEY_LEFTALT         56
#define KEY_CAPSLOCK        58
#define KEY_102ND           86
#define KEY_RIGHTCTRL       97
#define KEY_RIGHTALT        100
#define KEY_POWER           116
#define KEY_KPEQUAL         117
#define KEY_LEFTMETA        125     // 左 Windows/Super 键
#define KEY_RIGHTMETA       126     // 右 Windows/Super 键
#define KEY_COMPOSE         127     // 菜单键 (右侧 Win 和 Ctrl 之间)

// ==========================================
// 3. 功能键区 (Function Keys)
// ==========================================
#define KEY_F1              59
#define KEY_F2              60
#define KEY_F3              61
#define KEY_F4              62
#define KEY_F5              63
#define KEY_F6              64
#define KEY_F7              65
#define KEY_F8              66
#define KEY_F9              67
#define KEY_F10             68
#define KEY_F11             87
#define KEY_F12             88

// ==========================================
// 4. 控制面板区 (Navigation & Control)
// ==========================================
#define KEY_SYSRQ           99      // Print Screen / SysRq
#define KEY_SCROLLLOCK      70
#define KEY_PAUSE           119
#define KEY_INSERT          110
#define KEY_HOME            102
#define KEY_PAGEUP          104
#define KEY_DELETE          111
#define KEY_END             107
#define KEY_PAGEDOWN        109
#define KEY_UP              103
#define KEY_LEFT            105
#define KEY_RIGHT           106
#define KEY_DOWN            108

// ==========================================
// 5. 数字小键盘区 (Numpad)
// ==========================================
#define KEY_NUMLOCK         69
#define KEY_KPSLASH         98      // 小键盘 /
#define KEY_KPASTERISK      55      // 小键盘 *
#define KEY_KPMINUS         74      // 小键盘 -
#define KEY_KPPLUS          78      // 小键盘 +
#define KEY_KPENTER         96      // 小键盘 Enter
#define KEY_KPDOT           83      // 小键盘 .
#define KEY_KP0             82
#define KEY_KP1             79
#define KEY_KP2             80
#define KEY_KP3             81
#define KEY_KP4             75
#define KEY_KP5             76
#define KEY_KP6             77
#define KEY_KP7             71
#define KEY_KP8             72
#define KEY_KP9             73

// ==========================================
// 6. 鼠标按键 (复用 Key Code 体系)
// ==========================================
#define BTN_MOUSE           0x110
#define BTN_LEFT            0x110
#define BTN_RIGHT           0x111
#define BTN_MIDDLE          0x112
#define BTN_SIDE            0x113
#define BTN_EXTRA           0x114

// ==========================================
// 7. 最大值定义 (用于能力位图数组计算)
// ==========================================
// 标准按键到此为止，128~255 以及以上通常是多媒体键(音量加减等)、
// 手柄按键(BTN_GAMEPAD)和一些极其特殊的硬件开关。
#define KEY_MAX             0x2FF   // 767
#define KEY_CNT             (KEY_MAX + 1)



// 1. 定义事件的大类 (Event Types)
#define EV_SYN          0x00    // 同步事件 (用于分割一组完整的数据包)
#define EV_KEY          0x01    // 按键事件 (键盘、鼠标左右键)
#define EV_REL          0x02    // 相对坐标 (鼠标移动)
#define EV_ABS          0x03    // 绝对坐标 (触摸屏、手柄摇杆)
#define EV_LED          0x11    // LED 状态 (键盘大小写灯)

#define EV_MAX          0x1F    // 支持的最大事件类型数量
#define EV_CNT          (EV_MAX + 1) // 32

// 3. 定义相对轴坐标 (Relative Axes)
#define REL_X           0x00
#define REL_Y           0x01
#define REL_WHEEL       0x08
#define REL_MAX         0x0F
#define REL_CNT         (REL_MAX + 1) // 16

// 4. 定义鼠标按键 (复用 EV_KEY 的体系)
#define BTN_MOUSE       0x110
#define BTN_LEFT        0x110
#define BTN_RIGHT       0x111
#define BTN_MIDDLE      0x112

/*
 * LEDs (设备状态指示灯)
 */
#define LED_NUML        0x00    // 小键盘锁 (Num Lock)
#define LED_CAPSL       0x01    // 大写锁 (Caps Lock)
#define LED_SCROLLL     0x02    // 滚动锁 (Scroll Lock)
#define LED_COMPOSE     0x03    // 组合键灯 (Compose)
#define LED_KANA        0x04    // 日文假名切换灯 (Kana)
#define LED_SLEEP       0x05    // 睡眠指示灯
#define LED_SUSPEND     0x06    // 挂起指示灯
#define LED_MUTE        0x07    // 静音指示灯
#define LED_MISC        0x08    // 杂项/自定义灯
#define LED_MAIL        0x09    // 邮件指示灯 (以前的旧键盘常有)
#define LED_CHARGING    0x0A    // 充电指示灯
#define LED_MAX         0x0F    // 最大支持 15
#define LED_CNT         (LED_MAX + 1) // 16



// ==========================================
// 位图操作宏 (内核常用技巧：将位数转换为 long 的数组大小)
// x64 平台下，unsigned long 是 64 位 (8 字节)
// ==========================================
#define BITS_PER_LONG 64
#define BITS_TO_LONGS(nr)       (((nr) + BITS_PER_LONG - 1) / BITS_PER_LONG)

// 置位、清零、测试位 (位操作极其考验基础，写成内联函数或宏)
#define SET_BIT(nr, addr)       ((addr)[(nr) / BITS_PER_LONG] |= (1UL << ((nr) % BITS_PER_LONG)))
#define CLEAR_BIT(nr, addr)     ((addr)[(nr) / BITS_PER_LONG] &= ~(1UL << ((nr) % BITS_PER_LONG)))
#define TEST_BIT(nr, addr)      (!!((addr)[(nr) / BITS_PER_LONG] & (1UL << ((nr) % BITS_PER_LONG))))


typedef struct input_event {
    uint16 type;  // 事件大类 (例如: EV_KEY)
    uint16 code;  // 事件编号 (例如: KEY_A)
    int32  value; // 事件值 (按键: 1按下/0松开, 鼠标: x偏移量)
} input_event_t;

#define INPUT_BUFFER_SIZE 64 // 环形缓冲区大小 (必须是 2 的幂，方便位运算取模)

typedef struct hid_input_dev_t {
    char name[64];              // 设备名称，例如 "USB Keyboard"

    // 能力位图 (告诉系统这个设备能干什么)
    uint64 evbit[BITS_TO_LONGS(EV_CNT)];
    uint64 keybit[BITS_TO_LONGS(KEY_CNT)];
    uint64 relbit[BITS_TO_LONGS(REL_CNT)];
    uint64 ledbit[BITS_TO_LONGS(LED_CNT)];

    // 无锁环形缓冲区 (Ring Buffer)，ISR 负责生产，上层负责消费
    input_event_t buffer[INPUT_BUFFER_SIZE];
    volatile uint32 head;     // 写入指针 (ISR 修改)
    volatile uint32 tail;     // 读取指针 (上层应用修改)

    // 私有数据与链表
    void *private_data;         // 反向指向你底层的 hid_device_t
    list_head_t   node;     // 挂载到全局设备链表
} hid_input_dev_t;

struct hid_dev_t;

void hid_create_input_dev(struct hid_dev_t *hdev);