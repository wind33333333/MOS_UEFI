#pragma once

#include "moslib.h"
#include "input.h"

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