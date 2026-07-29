#include "hid-input.h"
#include "hid-core.h"
#include "input.h"
#include "slub.h"


list_head_t g_input_dev_list;

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
 * @brief 将 HID 设备的 Usage 映射为 TheresaOS Input 系统的事件，并填充驱动能力位图
 *
 * @param hdev  已经完成 Report Descriptor 解析的 HID 硬件设备结构体指针
 * @param idev  即将要向内核 input 子系统注册的输入设备节点指针
 */
static inline void hid_usage_to_input(hid_dev_t *hdev, input_dev_t *idev) {
    // 1. 遍历设备描述符解析出的所有逻辑字段 (Field 可以理解为比特流的解包模具)
    for (int i = 0; i < hdev->field_count; i++) {
        hid_field_t *field = hdev->fields[i];

        // 2. 遍历当前 Field 下绑定的所有用途子项 (Usage)
        for (int j = 0; j < field->max_usages; j++) {
            hid_usage_t *usage = &field->usages[j];

            /*
             * 提取 32 位 HID_ID 中的高低字节：
             *   - 高 16 位：Usage Page (功能字典类别)，例如 0x0007 (键盘页)
             *   - 低 16 位：Usage ID   (具体字段序号)，例如 0x0004 (字母 A 键)
             * 注意：高位一定要用 '>> 16' 右移，否则结果为 0x00070000 无法匹配 switch 分支
             */
            uint32 usage_page = (usage->hid_id >> 16) & 0xFFFF;
            uint16 usage_id   = usage->hid_id & 0xFFFF;

            // 默认先重置为无效事件，方便 ISR 中断响应函数查表时以 (event_type == 0) 为条件极速忽略
            usage->event_type = 0;
            usage->event_code = 0;

            // 3. 根据 Usage Page 进入分类映射路由
            switch (usage_page) {

            // ==========================================
            // 场景 A：键盘及小键盘按键页 (0x07)
            // ==========================================
            case HID_UP_KEYBOARD:
                // 标准主键盘区域序号范围在 0x00 ~ 0xFF 内
                if (usage_id < 256) {
                    // 通过查表将 USB HID 键盘键码转化为 TheresaOS 的物理键码 (例如 KEY_A)
                    usage->event_code = hid_keyboard_map[usage_id];

                    // 0x00(无事件)、0x01~0x03(错误占位符) 转化后为 0，需滤除
                    if (usage->event_code != 0) {
                        usage->event_type = EV_KEY;
                        // 向 input_dev 宣称设备支持按键事件类型
                        SET_BIT(EV_KEY, idev->evbit);
                        // 向 input_dev 的按键位图注册此具体键码
                        SET_BIT(usage->event_code, idev->keybit);
                    }
                }
                break;

            // ==========================================
            // 场景 B：通用桌面设备页 (0x01: 包含轴、滚轮、方向帽等)
            // ==========================================
            case HID_UP_GENDESK:
                switch (usage_id) {
                case HID_GD_X: // 0x30: X 轴
                case HID_GD_Y: // 0x31: Y 轴
                    /*
                     * 关键消歧逻辑：同样的 Usage Page 和 Usage ID，量纲可能完全相反！
                     * 必须通过 field->application_id 查询当前是“鼠标”还是“手柄/数位板”
                     */
                    if (field->application_id == HID_ID(HID_UP_GENDESK, HID_GD_MOUSE) ||
                        field->application_id == HID_ID(HID_UP_GENDESK, HID_GD_POINTER)) {
                        // 鼠标平移是相对运动，报告的是 delta 偏移量
                        usage->event_type = EV_REL;
                        usage->event_code = (usage_id == HID_GD_X) ? REL_X : REL_Y;
                        SET_BIT(EV_REL, idev->evbit);
                        SET_BIT(usage->event_code, idev->relbit);
                    } else {
                        // 手柄摇杆、触摸屏、绘图板是绝对位置
                        usage->event_type = EV_ABS;
                        usage->event_code = (usage_id == HID_GD_X) ? ABS_X : ABS_Y;
                        SET_BIT(EV_ABS, idev->evbit);
                        SET_BIT(usage->event_code, idev->absbit);

                        // 注意：这里后续要将 field->logical_min / logical_max 写入 idev 的 ABS 参数集
                        // 以便 OS 用户态窗口计算对物理屏幕坐标的缩放比例
                    }
                    break;

                case HID_GD_WHEEL: // 0x38: 鼠标垂直滚轮
                    usage->event_type = EV_REL;
                    usage->event_code = REL_WHEEL;
                    SET_BIT(EV_REL, idev->evbit);
                    SET_BIT(REL_WHEEL, idev->relbit);
                    break;
                }
                break;

            // ==========================================
            // 场景 C：鼠标、手柄及微动开关按键页 (0x09)
            // ==========================================
            case HID_UP_BUTTON:
                usage->event_type = EV_KEY;
                /*
                 * HID 标准中 Button 从 1 开始 (1=左键, 2=右键, 3=中键)
                 * 内核中通常按 BTN_MOUSE (鼠标按键基址 0x110) 进行连续递增映射
                 */
                usage->event_code = BTN_MOUSE + (usage_id - 1);

                // 边界防御：保证计算出的物理键码没超出最大支持范围
                if (usage->event_code <= KEY_MAX) {
                    SET_BIT(EV_KEY, idev->evbit);
                    SET_BIT(usage->event_code, idev->keybit);
                }
                break;

            // ==========================================
            // 场景 D：键盘 LED 指示灯控制页 (0x08 - Output Report)
            // ==========================================
            case HID_UP_LED:
                switch (usage_id) {
                // --- 常规三大功能锁状态灯 ---
                case 0x01: usage->event_code = LED_NUML;    break; // Num Lock
                case 0x02: usage->event_code = LED_CAPSL;   break; // Caps Lock
                case 0x03: usage->event_code = LED_SCROLLL; break; // Scroll Lock

                // --- 国际化及输入法灯 ---
                case 0x04: usage->event_code = LED_COMPOSE; break; // Compose 键
                case 0x05: usage->event_code = LED_KANA;    break; // 日语 Kana 切换灯

                // --- 兜底过滤 ---
                default:   usage->event_code = 0;           break;
                }

                // 完成对应事件的位图注册
                if (usage->event_code != 0) {
                    usage->event_type = EV_LED;
                    SET_BIT(EV_LED, idev->evbit);
                    SET_BIT(usage->event_code, idev->ledbit);
                }
                break;

            // ==========================================
            // 场景 E：未定义或厂商自定义页 (0xFF00 等)
            // ==========================================
            default:
                /*
                 * 私有协议字段（如水冷驱动、宏按键板等）不在系统通用 input 事件树中做位图注册，
                 * 保持 event_type = 0。后续中断数据直接留给 /dev/hidrawX 透传到应用层态程序处理。
                 */
                break;
            }
        }
    }
}

void hid_create_input_dev(hid_dev_t *hdev) {
    // 1. 向输入子系统申请一个干净的“账本”
    input_dev_t *idev = kzalloc(sizeof(input_dev_t));

    // 2. 填写设备基本信息
    // 你可以从 Phase 1 获取的 USB 字符串描述符里把设备名字拷过来
    asm_strcpy(idev->name, "USB HID Device\n");
    idev->private_data = hdev; // 互相绑定
    hdev->input = idev;        // 存入你自己的 hid_device_t 里

    // 3. ★ 核心转换：把 Phase 4 的模具，翻译成 idev 的能力位图
    // 需要你自己写一个函数，遍历 hdev 里的 hid_field_t，调用 SET_BIT()
    hid_usage_to_input(hdev, idev);

    // 4. 空账本拦截：检查这个设备到底是不是输入设备
    if (!TEST_BIT(EV_KEY, idev->evbit) &&
       !TEST_BIT(EV_REL, idev->evbit) &&
       !TEST_BIT(EV_ABS, idev->evbit)) {

        // 如果啥输入能力都没有 (比如是纯 RGB 调光器)
        // 就销毁账本，不向 Input 子系统注册
        kfree(idev);
        hdev->input = NULL;

        // 可以在这里走 hidraw 通道分支
        // register_hidraw(hdev);

       } else {
           // 5. 正式注册：挂载到系统的全局 input 链表
           // (注：如果是多核系统，这里需要加自旋锁)
           list_add_tail(&g_input_dev_list,&idev->node);
       }
}