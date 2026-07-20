#pragma once

#include "moslib.h"


/*
 * =========================================================================
 * USB HID Report Descriptor Item Types (bType)
 * 占用 Prefix 的 Bit 2-3
 * =========================================================================
 */
#define HID_ITEM_TYPE_MAIN      0x00  // 主项目 (定义数据字段或集合)
#define HID_ITEM_TYPE_GLOBAL    0x01  // 全局项目 (定义数据解析环境)
#define HID_ITEM_TYPE_LOCAL     0x02  // 局部项目 (定义紧接着的下一个Main项目的特性)
#define HID_ITEM_TYPE_RESERVED  0x03  // 保留 (当 Tag 也是 0x0F 时代表长项目)


/*
 * =========================================================================
 * USB HID Main Item Tags (bType == 0)
 * 占用 Prefix 的 Bit 4-7
 * =========================================================================
 */
#define HID_MAIN_TAG_INPUT          0x08  // 输入 (设备 -> 主机)
#define HID_MAIN_TAG_OUTPUT         0x09  // 输出 (主机 -> 设备)
#define HID_MAIN_TAG_COLLECTION     0x0A  // 集合开始 (将多个项目打包成一组)
#define HID_MAIN_TAG_FEATURE        0x0B  // 特征 (双向，常用于设备配置)
#define HID_MAIN_TAG_END_COLLECTION 0x0C  // 集合结束


/*
 * =========================================================================
 * USB HID Global Item Tags (bType == 1)
 * 占用 Prefix 的 Bit 4-7
 * =========================================================================
 */
#define HID_GLOBAL_TAG_USAGE_PAGE       0x00  // 用途页 (如通用桌面、键盘等)
#define HID_GLOBAL_TAG_LOGICAL_MIN      0x01  // 逻辑最小值
#define HID_GLOBAL_TAG_LOGICAL_MAX      0x02  // 逻辑最大值
#define HID_GLOBAL_TAG_PHYSICAL_MIN     0x03  // 物理最小值
#define HID_GLOBAL_TAG_PHYSICAL_MAX     0x04  // 物理最大值
#define HID_GLOBAL_TAG_UNIT_EXPONENT    0x05  // 单位指数 (10^x)
#define HID_GLOBAL_TAG_UNIT             0x06  // 物理单位
#define HID_GLOBAL_TAG_REPORT_SIZE      0x07  // 报告大小 (每个数据字段占用的位数)
#define HID_GLOBAL_TAG_REPORT_ID        0x08  // 报告 ID (区分多设备报告的标识符)
#define HID_GLOBAL_TAG_REPORT_COUNT     0x09  // 报告数量 (该字段的重复次数)
#define HID_GLOBAL_TAG_PUSH             0x0A  // 将当前全局状态压入栈
#define HID_GLOBAL_TAG_POP              0x0B  // 从栈中弹出并恢复全局状态


/*
 * =========================================================================
 * USB HID Local Item Tags (bType == 2)
 * 占用 Prefix 的 Bit 4-7
 * =========================================================================
 */
#define HID_LOCAL_TAG_USAGE             0x00  // 用途 (具体的按键或控制轴)
#define HID_LOCAL_TAG_USAGE_MIN         0x01  // 用途最小值 (批量定义起始)
#define HID_LOCAL_TAG_USAGE_MAX         0x02  // 用途最大值 (批量定义结束)
#define HID_LOCAL_TAG_DESIGNATOR_INDEX  0x03  // 指示器索引 (物理结构标识)
#define HID_LOCAL_TAG_DESIGNATOR_MIN    0x04  // 指示器最小值
#define HID_LOCAL_TAG_DESIGNATOR_MAX    0x05  // 指示器最大值
#define HID_LOCAL_TAG_STRING_INDEX      0x07  // 字符串索引 (对应固件里的描述字符串)
#define HID_LOCAL_TAG_STRING_MIN        0x08  // 字符串最小值
#define HID_LOCAL_TAG_STRING_MAX        0x09  // 字符串最大值
#define HID_LOCAL_TAG_DELIMITER         0x0A  // 定界符 (定义一组互斥控制)


/*
 * =========================================================================
 * 常用的长项目 (Long Item) 定义
 * =========================================================================
 */
#define HID_LONG_ITEM_PREFIX            0xFE  // 长项目的固定头部前缀字节


// === 最常用的核心 Usage Pages ===
#define HID_USAGE_PAGE_GENERIC          0x01  // 通用桌面控制 (鼠标, 键盘主设备, 摇杆)
#define HID_USAGE_PAGE_SIMULATION       0x02  // 模拟飞行/赛车控制
#define HID_USAGE_PAGE_VR               0x03  // 虚拟现实控制
#define HID_USAGE_PAGE_SPORT            0x04  // 体育运动控制 (如高尔夫模拟器)
#define HID_USAGE_PAGE_GAME             0x05  // 游戏控制 (手柄)
#define HID_USAGE_PAGE_KEYBOARD         0x07  // 键盘/键盘按键 (标准的 A-Z, 0-9 都在这里)
#define HID_USAGE_PAGE_LEDS             0x08  // LED 指示灯 (如键盘的 CapsLock 灯)
#define HID_USAGE_PAGE_BUTTON           0x09  // 通用按键 (比如鼠标的左键、右键)
#define HID_USAGE_PAGE_ORDINAL          0x0A  // 序号 (通常表示 "第N个实例")
#define HID_USAGE_PAGE_CONSUMER         0x0C  // 消费者设备 (非常重要！音量加减、静音、播放暂停等媒体键)

// === 现代输入与传感器 ===
#define HID_USAGE_PAGE_DIGITIZER        0x0D  // 数位板/触摸屏 (包含触控点、笔压等)
#define HID_USAGE_PAGE_HAPTICS          0x0E  // 触觉反馈 (设备震动)
#define HID_USAGE_PAGE_PHYSICAL_INPUT   0x0F  // 物理输入反馈 (如力回馈方向盘)
#define HID_USAGE_PAGE_BATTERY_SYSTEM   0x85  // 电池系统 (获取无线鼠标的剩余电量)

/* =========================================================
 * Generic Desktop Usages (Usage Page: 0x01)
 * ========================================================= */
// --- 设备类别 (Application Collections) ---
#define HID_USAGE_GENERIC_POINTER           0x01    // 物理指针
#define HID_USAGE_GENERIC_MOUSE             0x02    // 鼠标
#define HID_USAGE_GENERIC_JOYSTICK          0x04    // 摇杆
#define HID_USAGE_GENERIC_GAMEPAD           0x05    // 游戏手柄
#define HID_USAGE_GENERIC_KEYBOARD          0x06    // 键盘
#define HID_USAGE_GENERIC_KEYPAD            0x07    // 小键盘
#define HID_USAGE_GENERIC_MULTI_AXIS        0x08    // 多轴控制器

// --- 坐标与轴向 (Dynamic Values) ---
#define HID_USAGE_GENERIC_X                 0x30    // X轴 (通常是左右)
#define HID_USAGE_GENERIC_Y                 0x31    // Y轴 (通常是上下)
#define HID_USAGE_GENERIC_Z                 0x32    // Z轴
#define HID_USAGE_GENERIC_RX                0x33    // X轴旋转
#define HID_USAGE_GENERIC_RY                0x34    // Y轴旋转
#define HID_USAGE_GENERIC_RZ                0x35    // Z轴旋转
#define HID_USAGE_GENERIC_SLIDER            0x36    // 滑块
#define HID_USAGE_GENERIC_DIAL              0x37    // 拨盘
#define HID_USAGE_GENERIC_WHEEL             0x38    // 滚轮 (常见的鼠标滚轮)
#define HID_USAGE_GENERIC_HATSWITCH         0x39    // 苦力帽 (手柄上的十字键)

// --- 系统控制 (System Controls) ---
#define HID_USAGE_GENERIC_SYS_CONTROL       0x80    // 系统控制总称
#define HID_USAGE_GENERIC_SYS_POWER_DOWN    0x81    // 关机键
#define HID_USAGE_GENERIC_SYS_SLEEP         0x82    // 睡眠键
#define HID_USAGE_GENERIC_SYS_WAKE_UP       0x83    // 唤醒键

/* =========================================================
 * Button Usages (Usage Page: 0x09)
 * ========================================================= */
#define HID_USAGE_BUTTON_1                  0x01    // 按钮1 (鼠标左键)
#define HID_USAGE_BUTTON_2                  0x02    // 按钮2 (鼠标右键)
#define HID_USAGE_BUTTON_3                  0x03    // 按钮3 (鼠标中键)
#define HID_USAGE_BUTTON_4                  0x04    // 按钮4 (鼠标侧键：后退)
#define HID_USAGE_BUTTON_5                  0x05    // 按钮5 (鼠标侧键：前进)
// ... 手柄会有 Button 6 到 Button 16 甚至更多，按需扩展即可

/* =========================================================
 * Keyboard Usages (Usage Page: 0x07)
 * ========================================================= */
#define HID_USAGE_KEYBOARD_NOEVENT          0x00    // 无按键按下
#define HID_USAGE_KEYBOARD_ROLLOVER         0x01    // 按键冲突 (无键位冲突破解失败时发送)
#define HID_USAGE_KEYBOARD_POSTFAIL         0x02    // 自检失败

// --- 标准字母与数字 (示例) ---
#define HID_USAGE_KEYBOARD_A                0x04    // 字母 A
#define HID_USAGE_KEYBOARD_Z                0x1D    // 字母 Z
#define HID_USAGE_KEYBOARD_1                0x1E    // 数字 1
#define HID_USAGE_KEYBOARD_0                0x27    // 数字 0
#define HID_USAGE_KEYBOARD_RETURN           0x28    // 回车 (Enter)
#define HID_USAGE_KEYBOARD_ESCAPE           0x29    // Esc
#define HID_USAGE_KEYBOARD_BACKSPACE        0x2A    // 退格 (Backspace)
#define HID_USAGE_KEYBOARD_TAB              0x2B    // Tab
#define HID_USAGE_KEYBOARD_SPACE            0x2C    // 空格 (Space)

// --- 修饰键 (Modifiers，固定映射在 8 个 bit 里) ---
#define HID_USAGE_KEYBOARD_LCTRL            0xE0    // 左 Ctrl
#define HID_USAGE_KEYBOARD_LSHIFT           0xE1    // 左 Shift
#define HID_USAGE_KEYBOARD_LALT             0xE2    // 左 Alt
#define HID_USAGE_KEYBOARD_LGUI             0xE3    // 左 GUI (Windows键/Command键)
#define HID_USAGE_KEYBOARD_RCTRL            0xE4    // 右 Ctrl
#define HID_USAGE_KEYBOARD_RSHIFT           0xE5    // 右 Shift
#define HID_USAGE_KEYBOARD_RALT             0xE6    // 右 Alt
#define HID_USAGE_KEYBOARD_RGUI             0xE7    // 右 GUI

/* =========================================================
 * LED Usages (Usage Page: 0x08)
 * ========================================================= */
#define HID_USAGE_LED_NUM_LOCK              0x01    // 小键盘锁灯
#define HID_USAGE_LED_CAPS_LOCK             0x02    // 大写锁灯
#define HID_USAGE_LED_SCROLL_LOCK           0x03    // 滚动锁灯
#define HID_USAGE_LED_COMPOSE               0x04    // 组合键灯
#define HID_USAGE_LED_KANA                  0x05    // 日文 Kana 模式灯

/* =========================================================
 * Consumer Control Usages (Usage Page: 0x0C)
 * ========================================================= */
#define HID_USAGE_CONSUMER_CONTROL          0x01    // 消费类控制总类
#define HID_USAGE_CONSUMER_PLAY_PAUSE       0xCD    // 播放/暂停
#define HID_USAGE_CONSUMER_SCAN_NEXT_TRK    0xB5    // 下一曲
#define HID_USAGE_CONSUMER_SCAN_PREV_TRK    0xB6    // 上一曲
#define HID_USAGE_CONSUMER_MUTE             0xE2    // 静音
#define HID_USAGE_CONSUMER_VOLUME_INC       0xE9    // 音量增加
#define HID_USAGE_CONSUMER_VOLUME_DEC       0xEA    // 音量减少
#define HID_USAGE_CONSUMER_AC_HOME          0x223   // 浏览器主页 (AC = Application Control)
#define HID_USAGE_CONSUMER_AC_BACK          0x224   // 浏览器后退


struct usb_if_t;

// HID 报告类型
#define HID_REPORT_TYPE_INPUT   0
#define HID_REPORT_TYPE_OUTPUT  1
#define HID_REPORT_TYPE_FEATURE 2


/* STREAMING_CHUNK:定义私有属性(Usage)与公有属性(Field)结构体... */
// 数据的【私有属性】 (What it means)
typedef struct {
    uint32 hid_id; // 硬件协议里的完整用途 ID (Page | ID)
    uint16 event_type; // TheresaOS 内部事件类型 (如 EV_KEY, EV_REL)
    uint16 event_code; // TheresaOS 内部具体键码 (如 TOS_KEY_A, TOS_REL_X)
} hid_usage_t;


typedef struct hid_field_t {
    list_head_t node; // 挂载到设备链表

    // === 通道与路由属性 ===
    uint8 report_type; // 0:INPUT, 1:OUTPUT, 2:FEATURE
    uint8 report_id; // 报文 ID

    // === 物理寻址地图 (核心) ===
    uint32 bit_offset; // 在 raw buffer 中的绝对起始偏移量
    uint32 bit_size; // 单个元素的 bit 数量
    uint32 report_count; // 元素个数 (以此为准严格分配内存)

    // === 硬件语义属性 ===
    uint32 application_id;
    uint16 usage_page; // 全局用途页
    uint32 flags; // Data/Const, Array/Var 等标志位
    uint16 usage_min; // Array 模式下的身份下限
    uint16 usage_max; // Array 模式下的身份上限

    // === ★ 私有属性路由表 (强制按 report_count 分配，紧接在结构体尾部) ===
    hid_usage_t *usages;

    // === 量纲与物理范围 ===
    int32 logical_min; // 发送的数值逻辑下限
    int32 logical_max;
    int32 physical_min; // 代表的物理真实下限
    int32 physical_max;
    uint32 unit; // 物理单位
    int32 unit_exponent; // 单位指数
} hid_field_t;


/*
 * hid_dev: HID 物理设备实例
 * 作为底层 USB 驱动和上层输入子系统之间的桥梁。
 */
typedef struct {
    list_head_t field_list_head; // 解析出的全部数据块 (Block) 链表头
    uint32 field_count; // 数据块的数量统计

    // 以下为 TheresaOS 底层 USB 通信所需的上下文
    void *int_urb; // 中断传输的 URB 指针
    uint8 *report_buf; // 接收数据的 Raw Buffer
    void *uif; // 绑定的 USB 接口实例 (usb_if)
} hid_dev_t;

usb_drv_t *create_usb_hid_driver();
