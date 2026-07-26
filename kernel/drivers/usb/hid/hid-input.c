#include "hid-input.h"
#include "hid-core.h"

// 定义几个常见的 USB HID 用途页 (Usage Pages) 规范宏
#define HID_UP_GENDESK   0x00010000 // 通用桌面设备 (鼠标X/Y轴等)
#define HID_UP_KEYBOARD  0x00070000 // 标准键盘
#define HID_UP_LED       0x00080000 // LED 状态灯
#define HID_UP_BUTTON    0x00090000 // 鼠标/手柄物理按键


list_head_t g_input_device_list;

/*
 * USB HID 键盘 Usage ID 到 TheresaOS 内部键码 (KEY_*) 的映射表
 *
 * 依据: USB HID Usage Tables - Page 0x07 (Keyboard/Keypad)
 * 注意: 数组中未显式指定的索引，C 编译器会自动将其初始化为 0 (即 KEY_RESERVED)
 */
static const uint16 hid_keyboard_map[256] = {
    // ---------------------------------------------------------
    // 0x00 - 0x03: 错误码与保留位 (USB 协议规定)
    // ---------------------------------------------------------
    [0x00] = KEY_RESERVED,         // Reserved (no event indicated)
    [0x01] = KEY_RESERVED,         // ErrorRollOver (按键冲突，超出全键无冲上限)
    [0x02] = KEY_RESERVED,         // POSTFail (键盘自检失败)
    [0x03] = KEY_RESERVED,         // ErrorUndefined (未定义错误)

    // ---------------------------------------------------------
    // 0x04 - 0x1D: 26个英文字母 (注意：它不是按 ASCII 码排列的！)
    // ---------------------------------------------------------
    [0x04] = KEY_A,
    [0x05] = KEY_B,
    [0x06] = KEY_C,
    [0x07] = KEY_D,
    [0x08] = KEY_E,
    [0x09] = KEY_F,
    [0x0A] = KEY_G,
    [0x0B] = KEY_H,
    [0x0C] = KEY_I,
    [0x0D] = KEY_J,
    [0x0E] = KEY_K,
    [0x0F] = KEY_L,
    [0x10] = KEY_M,
    [0x11] = KEY_N,
    [0x12] = KEY_O,
    [0x13] = KEY_P,
    [0x14] = KEY_Q,
    [0x15] = KEY_R,
    [0x16] = KEY_S,
    [0x17] = KEY_T,
    [0x18] = KEY_U,
    [0x19] = KEY_V,
    [0x1A] = KEY_W,
    [0x1B] = KEY_X,
    [0x1C] = KEY_Y,
    [0x1D] = KEY_Z,

    // ---------------------------------------------------------
    // 0x1E - 0x27: 主键盘数字键 (注意：0 排在 9 的后面)
    // ---------------------------------------------------------
    [0x1E] = KEY_1,
    [0x1F] = KEY_2,
    [0x20] = KEY_3,
    [0x21] = KEY_4,
    [0x22] = KEY_5,
    [0x23] = KEY_6,
    [0x24] = KEY_7,
    [0x25] = KEY_8,
    [0x26] = KEY_9,
    [0x27] = KEY_0,

    // ---------------------------------------------------------
    // 0x28 - 0x38: 主键盘控制键与标点符号
    // ---------------------------------------------------------
    [0x28] = KEY_ENTER,            // 回车键 (Return)
    [0x29] = KEY_ESC,              // Escape 键
    [0x2A] = KEY_BACKSPACE,        // 退格键
    [0x2B] = KEY_TAB,              // Tab 键
    [0x2C] = KEY_SPACE,            // 空格键
    [0x2D] = KEY_MINUS,            // - 和 _
    [0x2E] = KEY_EQUAL,            // = 和 +
    [0x2F] = KEY_LEFTBRACE,        // [ 和 {
    [0x30] = KEY_RIGHTBRACE,       // ] 和 }
    [0x31] = KEY_BACKSLASH,        // \ 和 |
    [0x32] = KEY_BACKSLASH,        // Non-US # 和 ~
    [0x33] = KEY_SEMICOLON,        // ; 和 :
    [0x34] = KEY_APOSTROPHE,       // ' 和 "
    [0x35] = KEY_GRAVE,            // ` 和 ~ (波浪号)
    [0x36] = KEY_COMMA,            // , 和 <
    [0x37] = KEY_DOT,              // . 和 >
    [0x38] = KEY_SLASH,            // / 和 ?

    // ---------------------------------------------------------
    // 0x39 - 0x45: 大写锁定与 F1-F12 功能键
    // ---------------------------------------------------------
    [0x39] = KEY_CAPSLOCK,         // Caps Lock
    [0x3A] = KEY_F1,
    [0x3B] = KEY_F2,
    [0x3C] = KEY_F3,
    [0x3D] = KEY_F4,
    [0x3E] = KEY_F5,
    [0x3F] = KEY_F6,
    [0x40] = KEY_F7,
    [0x41] = KEY_F8,
    [0x42] = KEY_F9,
    [0x43] = KEY_F10,
    [0x44] = KEY_F11,
    [0x45] = KEY_F12,

    // ---------------------------------------------------------
    // 0x46 - 0x52: 系统控制台键与导航区 (方向键)
    // ---------------------------------------------------------
    [0x46] = KEY_SYSRQ,            // Print Screen
    [0x47] = KEY_SCROLLLOCK,       // Scroll Lock
    [0x48] = KEY_PAUSE,            // Pause / Break
    [0x49] = KEY_INSERT,           // Insert
    [0x4A] = KEY_HOME,             // Home
    [0x4B] = KEY_PAGEUP,           // Page Up
    [0x4C] = KEY_DELETE,           // Delete Forward
    [0x4D] = KEY_END,              // End
    [0x4E] = KEY_PAGEDOWN,         // Page Down
    [0x4F] = KEY_RIGHT,            // 右方向键
    [0x50] = KEY_LEFT,             // 左方向键
    [0x51] = KEY_DOWN,             // 下方向键
    [0x52] = KEY_UP,               // 上方向键

    // ---------------------------------------------------------
    // 0x53 - 0x63: 小键盘区 (Keypad)
    // ---------------------------------------------------------
    [0x53] = KEY_NUMLOCK,          // Num Lock
    [0x54] = KEY_KPSLASH,          // 小键盘 /
    [0x55] = KEY_KPASTERISK,       // 小键盘 *
    [0x56] = KEY_KPMINUS,          // 小键盘 -
    [0x57] = KEY_KPPLUS,           // 小键盘 +
    [0x58] = KEY_KPENTER,          // 小键盘 Enter
    [0x59] = KEY_KP1,              // 小键盘 1
    [0x5A] = KEY_KP2,              // 小键盘 2
    [0x5B] = KEY_KP3,              // 小键盘 3
    [0x5C] = KEY_KP4,              // 小键盘 4
    [0x5D] = KEY_KP5,              // 小键盘 5
    [0x5E] = KEY_KP6,              // 小键盘 6
    [0x5F] = KEY_KP7,              // 小键盘 7
    [0x60] = KEY_KP8,              // 小键盘 8
    [0x61] = KEY_KP9,              // 小键盘 9
    [0x62] = KEY_KP0,              // 小键盘 0
    [0x63] = KEY_KPDOT,            // 小键盘 .

    // ---------------------------------------------------------
    // 0x64 - 0x67: 其他国际按键
    // ---------------------------------------------------------
    [0x64] = KEY_102ND,            // 欧洲键盘常见的 < > | 键
    [0x65] = KEY_COMPOSE,          // Application / 菜单键 (右Win和右Ctrl之间那个)
    [0x66] = KEY_POWER,            // 键盘上的电源键
    [0x67] = KEY_KPEQUAL,          // 小键盘 = (常用于 Mac 键盘)

    // 0x68 到 0xDF 通常是 F13-F24、国际语言键盘特殊按键等。
    // 在普通的桌面级操作系统中，为了节约映射资源，这部分可以暂不映射。
    // 编译器会自动将它们初始化为 KEY_RESERVED (0)。

    // ---------------------------------------------------------
    // 0xE0 - 0xE7: 左右修饰键 (Modifiers)
    // 注意：如果是标准的 Boot Protocol 键盘，这几个键一般不通过 Usage ID 上报，
    // 而是压缩在数据包的第 0 字节的 8 个 Bit 中。但如果是 Report 协议，依然会用这些 ID。
    // ---------------------------------------------------------
    [0xE0] = KEY_LEFTCTRL,         // Left Control
    [0xE1] = KEY_LEFTSHIFT,        // Left Shift
    [0xE2] = KEY_LEFTALT,          // Left Alt
    [0xE3] = KEY_LEFTMETA,         // Left GUI (Windows键 / Mac Command键)
    [0xE4] = KEY_RIGHTCTRL,        // Right Control
    [0xE5] = KEY_RIGHTSHIFT,       // Right Shift
    [0xE6] = KEY_RIGHTALT,         // Right Alt (Alt Gr)
    [0xE7] = KEY_RIGHTMETA,        // Right GUI
};

/**
 * @brief 将 HID 设备的 Usage 映射为 Input 系统的事件，并填充能力位图
 *
 * @param hdev  已经解析完 Report Descriptor 的 HID 设备指针
 * @param idev  即将要向内核注册的 Input 系统设备指针
 */
void hid_usage_to_input(hid_dev_t *hdev, hid_input_dev_t *idev) {
    // 1. 遍历这个设备所有的 Field (数据切片模具)
    for (int i = 0; i < hdev->field_count; i++) {
        hid_field_t *field = hdev->fields[i];

        // 2. 遍历这个 Field 下所有的 Usage (标签)
        for (int j = 0; j < field->max_usages; j++) {
            hid_usage_t *usage = &field->usages[j];

            // 提取出高 16 位的 Usage Page
            uint32 usage_page = usage->hid_id & 0xFFFF0000;
            // 提取出低 16 位的 Usage ID
            uint16 usage_id   = usage->hid_id & 0x0000FFFF;

            // 默认情况下，先将其标记为系统不认识的无用事件
            usage->event_type = 0;
            usage->event_code = 0;

            // 3. 开始核心路由与映射逻辑
            switch (usage_page) {

                // ==========================================
                // 场景 A：这是一个标准键盘的按键
                // ==========================================
                case HID_UP_KEYBOARD:
                    if (usage_id < 256) {
                        usage->event_code = hid_keyboard_map[usage_id];

                        if (usage->event_code != 0) { // 自动过滤 0x00 ~ 0x03 的无效键
                            usage->event_type = EV_KEY;
                            SET_BIT(EV_KEY, idev->evbit);
                            SET_BIT(usage->event_code, idev->keybit); // 完美的一对一能力注册
                        } else {
                            usage->event_type = 0;
                        }
                    }
                    break;
                // ==========================================
                // 场景 B：通用桌面设备 (主要是鼠标移动)
                // ==========================================
                case HID_UP_GENDESK:
                    if (usage_id == 0x30) { // 0x30 代表 X 轴
                        usage->event_type = EV_REL;
                        usage->event_code = REL_X;
                        SET_BIT(EV_REL, idev->evbit);
                        SET_BIT(REL_X, idev->relbit);
                    }
                    else if (usage_id == 0x31) { // 0x31 代表 Y 轴
                        usage->event_type = EV_REL;
                        usage->event_code = REL_Y;
                        SET_BIT(EV_REL, idev->evbit);
                        SET_BIT(REL_Y, idev->relbit);
                    }
                    else if (usage_id == 0x38) { // 0x38 代表鼠标滚轮
                        usage->event_type = EV_REL;
                        usage->event_code = REL_WHEEL;
                        SET_BIT(EV_REL, idev->evbit);
                        SET_BIT(REL_WHEEL, idev->relbit);
                    }
                    break;

                // ==========================================
                // 场景 C：鼠标或手柄的点击按键
                // ==========================================
                case HID_UP_BUTTON:
                    // USB 规范里，Button 1 通常是鼠标左键，Button 2 是右键
                    usage->event_type = EV_KEY;
                    // BTN_MOUSE = 0x110，减 1 是因为 usage_id 是从 1 开始的
                    usage->event_code = BTN_MOUSE + (usage_id - 1);

                    // 确保计算出来的键码没有越界
                    if (usage->event_code <= KEY_MAX) {
                        SET_BIT(EV_KEY, idev->evbit);
                        SET_BIT(usage->event_code, idev->keybit);
                    }
                    break;

                // ==========================================
                // 场景 D：键盘的 LED 指示灯 (反向控制使用)
                // ==========================================
                case HID_UP_LED:
                    usage->event_type = EV_LED;
                    switch (usage_id) {
                        // --- 传统三大键盘指示灯 ---
                        case 0x01: usage->event_code = LED_NUML;      break; // Num Lock
                        case 0x02: usage->event_code = LED_CAPSL;     break; // Caps Lock
                        case 0x03: usage->event_code = LED_SCROLLL;   break; // Scroll Lock

                            // --- 国际化及特殊键盘指示灯 ---
                        case 0x04: usage->event_code = LED_COMPOSE;   break; // Compose 键指示灯
                        case 0x05: usage->event_code = LED_KANA;      break; // 日语 Kana(假名) 输入指示灯

                            // --- 现代多媒体及电源状态指示灯 ---
                        case 0x09: usage->event_code = LED_MUTE;      break; // 静音指示灯 (Mute)
                        case 0x19: usage->event_code = LED_MAIL;      break; // 消息等待 (Message Waiting / 邮件灯)
                        case 0x27: usage->event_code = LED_SLEEP;     break; // 待机指示灯 (Stand-By)
                        case 0x4B: usage->event_code = LED_MISC;      break; // 通用指示灯 (Generic Indicator)
                        case 0x4C: usage->event_code = LED_SUSPEND;   break; // 系统挂起 (System Suspend)
                        case 0x4D: usage->event_code = LED_CHARGING;  break; // 外接电源/充电 (External Power Connected)

                            // --- 兜底处理 ---
                        default:
                            /*
                             * 对于我们不关心（或不需要支持）的其他几十种冷门指示灯，
                             * 直接将 event_type 清零，或者标记为非法。
                             * 这样你在第二遍解析（挂载字段）时，就可以通过判断 event_type == 0
                             * 来跳过这个无效的 Usage，避免浪费你的 flexible array 内存和遍历时间。
                             */
                            usage->event_type = 0;
                            usage->event_code = 0;
                            break;
                    }

                // ==========================================
                // 场景 E：系统无法识别的私有硬件数据
                // ==========================================
                default:
                    // 这个设备可能是水冷头温度传感器、也可能是显卡灯光控制板
                    // 我们不需要在 input_dev 里给它位图置位。
                    // 直接跳过即可，保留它 event_type = 0 的状态。
                    // 以后交给 hidraw 去原封不动地发给用户态程序。
                    break;
            }
        }
    }
}