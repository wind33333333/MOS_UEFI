#include "usb-core.h"
#include "usb-dev.h"
#include "usb-def.h"
#include "usb-bus.h"
#include "hid-core.h"
#include "printk.h"
#include "slub.h"
#include "errno.h"
#include "xhci-hcd.h"

/**
 * @brief 专门获取 HID 报告描述符的命令
 * @param udev           目标 USB 设备
 * @param interface_num  当前 HID 接口的编号 (bInterfaceNumber)
 * @param buf            预先分配好的 DMA 缓冲区指针
 * @param length        从 HID 描述符里解析出来的 wReportDescriptorLength
 */
static inline int32 usb_hid_get_report_desc(usb_dev_t *udev, uint8 interface_num, void *buf, uint16 length) {
    // 1. 组装 bmRequestType:
    //    10000001b (0x81) = 传输方向 IN | 标准请求 | 接收者为 Interface
    uint8 req_type = USB_BM_REQ_TYPE(USB_REQ_DIR_IN,
                                     USB_REQ_TYPE_STANDARD,
                                     USB_REQ_REC_INTERFACE);

    // 2. 发送控制传输指令
    return usb_control_msg(udev, buf,
                           req_type,
                           USB_REQ_GET_DESCRIPTOR, // bRequest: 0x06 (获取描述符)
                           (USB_DESC_TYPE_REPORT << 8) | 0, // wValue: 高字节 0x22 (Report类型)，默认低字节 0 (索引)
                           interface_num, // wIndex: 必须填它所属的接口号！
                           length); // wLength: 想要拉取的字节数 (如 63)
}


/* STREAMING_CHUNK:定义解析器内部常量与状态机上下文... */
#define MAX_GLOBAL_STACK 4     // HID 规范要求的最小压栈深度
#define MAX_LOCAL_USAGES 256   // 防止恶意描述符溢出的 Local 缓存上限
#define MAX_COLLECTIONS 16

/* STREAMING_CHUNK:定义Global栈与解析器State结构体... */
// 独立的 Global 状态结构，为了方便 Push/Pop 时进行整块内存拷贝
typedef struct {
    uint16 usage_page;
    int32 logical_min;
    int32 logical_max;
    int32 physical_min;
    int32 physical_max;
    int32 unit_exponent;
    uint32 unit;
    uint32 report_size;
    uint32 report_count;
    uint8 report_id;
} hid_global_state_t;

// 解析引擎的完整上下文
typedef struct {
    // ★ 新增：用于追踪 Collection 嵌套层级的栈
    uint32 collection_app_stack[MAX_COLLECTIONS];
    uint32 collection_depth;

    // 1. 活动的 Global 属性
    hid_global_state_t global;

    // 2. Global 属性备份栈 (Push/Pop)
    hid_global_state_t global_stack[MAX_GLOBAL_STACK];
    uint32 stack_depth;

    // 3. Local 属性 (不参与压栈，遇 Main 则清空)
    uint32 local_usages[MAX_LOCAL_USAGES];
    uint32 local_usage_count;
    uint16 usage_min;
    uint16 usage_max;

    // 4 代表 Input/Output/Feature，256 代表合法的 Report ID (0~255)
    uint32 channel_bit_offsets[3][256];
} hid_parser_state_t;

// =========================================================================
// 第三部分：核心解析引擎实现
// =========================================================================

/* STREAMING_CHUNK:实现数据提取与符号扩展辅助函数... */
// 辅助函数：符号扩展 (将负数正确恢复为 32位 有符号整数)
static int32 sign_extend(uint32 data, uint8 bytes) {
    if (bytes == 1 && (data & 0x80)) return (int32) (data | 0xFFFFFF00);
    if (bytes == 2 && (data & 0x8000)) return (int32) (data | 0xFFFF0000);
    return (int32) data;
}

// 辅助函数：从字节流中提取指定长度的数据
static uint32 fetch_item_data(uint8 *ptr, uint8 size) {
    uint32 data = 0;
    for (uint8 i = 0; i < size; i++) {
        data |= (ptr[i] << (i << 3));
    }
    return data;
}

//统计field个数
static inline uint32 hid_count_fields(const uint8 *desc, uint32 desc_len) {
    uint32 count = 0;
    uint32 offset = 0;

    while (offset < desc_len) {
        uint8 item = desc[offset];

        if (item == 0xFE) {
            offset += 1 + desc[offset + 1];
            continue;
        }


        uint8 item_type = (item >> 2) & 0x03;
        uint8 item_tag = (item >> 4) & 0x0F;
        uint8 item_size = item & 0x03;
        if (item_size == 3) item_size = 4;

        // 核心逻辑：只要是 Main 项里的 Input, Output, Feature，就是 1 个 Field
        if (item_type == HID_ITEM_TYPE_MAIN ) {
            if (item_tag == HID_MAIN_TAG_INPUT ||
                item_tag == HID_MAIN_TAG_OUTPUT ||
                item_tag == HID_MAIN_TAG_FEATURE) {
                count++;
                }
        }
        offset += (1 + item_size); // 跳过当前 Item 的数据，看下一个
    }
    return count;
}

// 重构后的核心解析引擎
// =========================================================================
static inline int32 hid_parse_report_desc(hid_dev_t *hdev, uint8 *desc, uint32 desc_len) {
    // ★ 修复 2：防范内核栈溢出 (Kernel Stack Overflow)
    // 升级二维数组后，结构体大小超 4KB，必须改用堆分配！
    hid_parser_state_t *state = kzalloc(sizeof(hid_parser_state_t));
    if (!state) return -1; // OOM 保护

    hdev->field_count = 0;

    uint32 offset = 0;
    int ret = -1; // 默认设定为失败状态

    while (offset < desc_len) {
        uint8 item = desc[offset];
        if (item == 0xFE) {
            if (offset >= desc_len) goto parse_end; // 统一通过 goto 退出，确保释放 state
            offset += 1 + desc[offset + 1];
            continue;
        }

        uint8 item_type = (item >> 2) & 0x03;
        uint8 item_tag = (item >> 4) & 0x0F;
        uint8 item_size = item & 0x03;
        if (item_size == 3) item_size = 4;

        if (offset + 1 + item_size > desc_len) goto parse_end;

        uint32 raw_data = fetch_item_data(&desc[offset + 1], item_size);
        int32 signed_data = sign_extend(raw_data, item_size);
        offset += (1 + item_size);

        switch (item_type) {
            case HID_ITEM_TYPE_GLOBAL:
                switch (item_tag) {
                    case HID_GLOBAL_TAG_USAGE_PAGE: state->global.usage_page = raw_data;
                        break;
                    case HID_GLOBAL_TAG_LOGICAL_MIN: state->global.logical_min = signed_data;
                        break;
                    case HID_GLOBAL_TAG_LOGICAL_MAX: state->global.logical_max = signed_data;
                        break;
                    case HID_GLOBAL_TAG_PHYSICAL_MIN: state->global.physical_min = signed_data;
                        break;
                    case HID_GLOBAL_TAG_PHYSICAL_MAX: state->global.physical_max = signed_data;
                        break;
                    case HID_GLOBAL_TAG_UNIT_EXPONENT: state->global.unit_exponent = signed_data;
                        break;
                    case HID_GLOBAL_TAG_UNIT: state->global.unit = raw_data;
                        break;
                    case HID_GLOBAL_TAG_REPORT_SIZE: state->global.report_size = raw_data;
                        break;
                    case HID_GLOBAL_TAG_REPORT_ID: state->global.report_id = (uint8) raw_data;
                        break;
                    case HID_GLOBAL_TAG_REPORT_COUNT: state->global.report_count = raw_data;
                        break;

                    case HID_GLOBAL_TAG_PUSH:
                        if (state->stack_depth < MAX_GLOBAL_STACK) {
                            state->global_stack[state->stack_depth] = state->global;
                            state->stack_depth++;
                        }
                        break;
                    case HID_GLOBAL_TAG_POP:
                        if (state->stack_depth > 0) {
                            state->stack_depth--;
                            state->global = state->global_stack[state->stack_depth];
                        }
                        break;
                }
                break;

            case HID_ITEM_TYPE_LOCAL:
                switch (item_tag) {
                    case HID_LOCAL_TAG_USAGE:
                        if (state->local_usage_count < MAX_LOCAL_USAGES) {
                            uint32 full_id = raw_data;
                            if (item_size <= 2) {
                                full_id = (state->global.usage_page << 16) | (raw_data & 0xFFFF);
                            }
                            state->local_usages[state->local_usage_count++] = full_id;
                        }
                        break;
                    case HID_LOCAL_TAG_USAGE_MIN: state->usage_min = (uint16) raw_data;
                        break;
                    case HID_LOCAL_TAG_USAGE_MAX: state->usage_max = (uint16) raw_data;
                        break;
                }
                break;

            case HID_ITEM_TYPE_MAIN:
                // ==========================================================
                // ★ 新增 1：处理 Collection (0xA)
                // ==========================================================
                if (item_tag == HID_MAIN_TAG_COLLECTION) {
                    if (state->collection_depth < MAX_COLLECTIONS) {
                        // 默认继承上一层的 Application ID (处理嵌套 Physical/Logical 集合的情况)
                        uint32 current_app = (state->collection_depth > 0) ?
                                              state->collection_app_stack[state->collection_depth - 1] : 0;

                        // HID规范：如果当前声明的是 Application Collection (raw_data == 0x01)
                        if (raw_data == 0x01 && state->local_usage_count > 0) {
                            // 提取刚刚通过 Local Tag 压入的完整 32 位 Usage 作为 Application ID
                            current_app = state->local_usages[0];
                        }

                        // 压栈
                        state->collection_app_stack[state->collection_depth++] = current_app;
                    }

                }else if (item_tag == HID_MAIN_TAG_END_COLLECTION) {
                    // ==========================================================
                    // ★ 新增 2：处理 End Collection (0xC)
                    // ==========================================================
                    if (state->collection_depth > 0) {
                        state->collection_depth--; // 出栈，退回上一层
                    }

                }else if (item_tag == HID_MAIN_TAG_INPUT || item_tag == HID_MAIN_TAG_OUTPUT || item_tag ==
                    HID_MAIN_TAG_FEATURE) {
                    if (state->global.report_size == 0 || state->global.report_count == 0) {
                        goto reset_local;
                    }

                    uint8 type_idx = (item_tag == HID_MAIN_TAG_INPUT) ? 0 : (item_tag == HID_MAIN_TAG_OUTPUT) ? 1 : 2;

                    // ==========================================================
                    // ★ 核心重构：计算最大 Usage 数量 (Linux 暴力展开法)
                    // ==========================================================
                    uint32 max_usages = 0;

                    if (state->usage_max >= state->usage_min && state->usage_max != 0) {
                        // 区间声明模式 (如 0x00 ~ 0xFF，展开后是 256 个)
                        max_usages = state->usage_max - state->usage_min + 1;
                    } else if (state->local_usage_count > 0) {
                        // 离散声明模式
                        max_usages = state->local_usage_count;
                    }

                    // 兜底保护：防备某些劣质山寨键盘的描述符乱写
                    // max_usages 绝对不能小于 report_count，否则后面解析 Payload 会越界
                    if (max_usages < state->global.report_count) {
                        max_usages = state->global.report_count;
                    }

                    // 按照 max_usages 分配内存，而不是 report_count！
                    uint32 alloc_size = sizeof(hid_field_t) + (max_usages * sizeof(hid_usage_t));
                    hid_field_t *field = kzalloc(alloc_size);
                    if (!field) {
                        // OOM 时跳转清理（目前仅退出，后续可加上释放链表的逻辑）
                        goto parse_end;
                    }
                    hdev->fields[hdev->field_count++] = field;

                    field->report_type = item_tag;
                    field->report_id = state->global.report_id;
                    field->flags = raw_data;
                    field->max_usages   = max_usages;
                    field->report_count = state->global.report_count;
                    field->bit_size = state->global.report_size;
                    field->usage_page = state->global.usage_page;
                    field->usage_min = state->usage_min;
                    field->usage_max = state->usage_max;

                    field->logical_min = state->global.logical_min;
                    field->logical_max = state->global.logical_max;
                    field->unit = state->global.unit;
                    field->unit_exponent = state->global.unit_exponent;

                    field->physical_min = (state->global.physical_min == 0 && state->global.physical_max == 0)
                                              ? state->global.logical_min
                                              : state->global.physical_min;
                    field->physical_max = (state->global.physical_min == 0 && state->global.physical_max == 0)
                                              ? state->global.logical_max
                                              : state->global.physical_max;

                    // ==========================================================
                    // ★ 修复 3：独立追踪每一个 Report ID 的偏移量
                    // ==========================================================
                    uint8 r_id = state->global.report_id;
                    field->bit_offset = state->channel_bit_offsets[type_idx][r_id];
                    state->channel_bit_offsets[type_idx][r_id] += (
                        state->global.report_size * state->global.report_count);
                    // ==========================================================

                    field->application_id = (state->collection_depth > 0) ?
                                             state->collection_app_stack[state->collection_depth - 1] : 0;

                    field->flags = raw_data;
                    if (HID_FIELD_IS_DATA(field->flags)) {
                        uint32 page_base = state->global.usage_page << 16;

                        // ==========================================================
                        // ★ 暴力展开：无视 Array/Variable，直接为所有可能的键建立实例
                        // ==========================================================
                        if (state->usage_max >= state->usage_min && state->usage_max != 0) {

                            for (uint32 i = 0; i < max_usages; i++) {
                                // 比如 6KRO 键盘，这里会顺滑地生成 0x070000 到 0x0700FF 这 256 个实例
                                field->usages[i].hid_id = page_base | (state->usage_min + i);
                            }

                        } else if (state->local_usage_count > 0) {

                            for (uint32 i = 0; i < max_usages; i++) {
                                // 离散模式，如果不够就用最后一个补齐
                                uint32 idx = (i < state->local_usage_count) ? i : (state->local_usage_count - 1);
                                field->usages[i].hid_id = state->local_usages[idx];
                            }

                        } else {

                            // 极端异常兜底
                            for (uint32 i = 0; i < max_usages; i++) {
                                field->usages[i].hid_id = page_base;
                            }

                        }
                    }
                }

                reset_local:
                state->local_usage_count = 0;
                state->usage_min = 0;
                state->usage_max = 0;
        }
    }

    ret = 0; // 全部执行完毕，标记为成功


parse_end:
    // ★ 修复 4：无论成功还是失败，在此处统一释放堆分配的上下文结构体
    if (state) {
        kfree(state); // 替换为你内核的 free 函数
    }
    return ret;
}


/**
 * @brief 从 HID 原始数据包中安全提取跨字节的位域 (支持小端序)
 *
 * @param report     指向 HID 数据包载荷的起始位置
 * @param bit_offset 该字段在载荷中的绝对起始位 (单位: bit)
 * @param bit_size   需要提取的位数 (最大支持 32 位)
 * @return uint32    提取出的无符号原始数据
 */
static inline uint32 hid_extract_bits(const uint8 *report, uint32 bit_offset, uint32 bit_size) {
    uint32 value = 0;
    uint32 bits_extracted = 0; // 已成功提取的位数

    // 容错处理：TheresaOS 目前的按键/鼠标状态机最大用 uint32 承载
    if (bit_size == 0 || bit_size > 32) {
        return 0;
    }

    // 1. 定位起始位置：计算从哪个字节的第几位开始切
    uint32 current_byte = bit_offset / 8;
    uint32 current_bit_in_byte = bit_offset % 8;

    // 2. 循环提取：应对跨越多字节的情况
    while (bits_extracted < bit_size) {

        // 计算在当前这个字节里，我们能切下多少个 bit？
        // 取“当前字节剩余 bit 数”与“还缺少的 bit 数”两者间的最小值
        uint32 bits_to_take = 8 - current_bit_in_byte;
        if (bits_to_take > (bit_size - bits_extracted)) {
            bits_to_take = bit_size - bits_extracted;
        }

        // 制作截取掩码 (Mask)
        // 例如：我们要取 3 个 bit，掩码就是 (1 << 3) - 1 = 7 (二进制 00000111)
        uint8 mask = (1 << bits_to_take) - 1;

        // 核心切割：
        // a) 读取当前字节: report[current_byte]
        // b) 将有用的数据右移到底部: >> current_bit_in_byte
        // c) 用掩码滤掉高位的无关数据: & mask
        uint8 extracted_chunk = (report[current_byte] >> current_bit_in_byte) & mask;

        // 将切下来的这块肉，左移到最终结果正确的位置上，并拼装
        value |= (extracted_chunk << bits_extracted);

        // 推进状态机，为提取下一个字节做准备
        bits_extracted += bits_to_take;
        current_byte++;
        current_bit_in_byte = 0; // 只要跨入下一个新字节，必然是从第 0 位开始读
    }

    return value;
}


// 定义几个常见的 USB HID 用途页 (Usage Pages) 规范宏
#define HID_UP_GENDESK   0x00010000 // 通用桌面设备 (鼠标X/Y轴等)
#define HID_UP_KEYBOARD  0x00070000 // 标准键盘
#define HID_UP_LED       0x00080000 // LED 状态灯
#define HID_UP_BUTTON    0x00090000 // 鼠标/手柄物理按键

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
void hid_map_usage_to_input(hid_dev_t *hdev, input_dev_t *idev) {
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
        //asm_mem_set(hdev->report_buf, 0, urb->actual_length);

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


list_head_t g_input_device_list;


/**
 * @brief USB HID 驱动的入口函数 (当 USB 核心层发现 HID 接口时调用)
 *
 * @param uif 触发本次探测的 USB 接口结构体指针
 * @return int 0 表示接管成功，非 0 表示失败
 */
static int hid_probe(usb_if_t *uif, usb_id_t *uid) {
    usb_dev_t *udev = uif->udev; // 从接口反向拿到物理设备对象
    usb_if_alt_t *if_alt = &uif->if_alts[0];

    //1.启用接口
    usb_ep_t *ep1 = &if_alt->eps[0];
    ep1->ring_max_trbs = 32;
    usb_enable_alt_if(if_alt);

    // ==========================================
    // Phase 2: 索要“报告描述符 (说明书)”
    // ==========================================
    // 通常我们先通过读取 HID 描述符知道报告描述符的长度，这里假设长度为 64 或 128
    // 我们直接申请一块临时内存用来接说明书
    usb_hid_desc_t *hid_desc = if_alt->extras_desc;
    if (hid_desc->head.desc_type != USB_DESC_TYPE_HID || hid_desc->report_descriptor_type != USB_DESC_TYPE_HID_REPORT) {
        return EPROTO;
    }

    uint16 report_desc_len = hid_desc->report_descriptor_length;
    uint8 *report_desc_buf = kzalloc(report_desc_len);

    // 发送 Control Transfer (控制传输) 向设备索要报告描述符
    usb_hid_get_report_desc(udev, if_alt->if_desc->interface_number, report_desc_buf, report_desc_len);

    // ==========================================
    // Phase 3: 分配驱动私有数据结构并绑定
    // ==========================================
    uint32 fields_count = hid_count_fields(report_desc_buf,report_desc_len);
    hid_dev_t *hdev = kzalloc(sizeof(hid_dev_t)+fields_count*sizeof(hid_field_t*));
    hdev->uif = uif;

    // 将我们自己的 hdev 挂载到 USB 接口的私有指针上，方便后续中断里拿出来用
    uif->drv_data = hdev;

    
    // ==========================================
    // Phase 4: 运行解析引擎，建立“数据模具”
    // ==========================================
    // 这里调用咱们之前写好的解析状态机
    // 解析结果（所有的 hid_field_t）会被保存在 hdev 内部的链表或数组中
    hid_parse_report_desc(hdev, report_desc_buf, report_desc_len);

    // 报告描述符已经翻译成模具存到 hdev 里了，原始的说明书就可以扔掉了
    kfree(report_desc_buf);
    report_desc_buf = NULL;


    // ==========================================
    // Phase 5: 注册到 TheresaOS 的 Input Subsystem (输入子系统)
    // ==========================================
    // 1. 向输入子系统申请一个干净的“账本”
    input_dev_t *idev = kzalloc(sizeof(input_dev_t));

    // 2. 填写设备基本信息
    // 你可以从 Phase 1 获取的 USB 字符串描述符里把设备名字拷过来
    asm_strcpy(idev->name, "USB HID Device\n");
    idev->private_data = hdev; // 互相绑定
    hdev->input = idev;        // 存入你自己的 hid_device_t 里

    // 3. ★ 核心转换：把 Phase 4 的模具，翻译成 idev 的能力位图
    // 需要你自己写一个函数，遍历 hdev 里的 hid_field_t，调用 SET_BIT()
    hid_map_usage_to_input(hdev, idev);

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
            list_add_tail(&g_input_device_list,&idev->node);
        }

    // ==========================================
    // Phase 6: 启动引擎！投递第一个 URB
    // ==========================================
    hdev->report_buf = kzalloc_dma(ep1->max_packet_size);
    hdev->int_urb = usb_alloc_urb();
    usb_fill_int_urb(hdev->int_urb, hid_irq_complete,hdev,udev, ep1, hdev->report_buf, ep1->max_packet_size, ep1->interval);
    xhci_submit_urb(hdev->int_urb);
    return 0; // 成功！
}

static void hid_remove(usb_if_t *uif) {
}


// =========================================================================
// 4. 驱动注册与 ID 匹配表
// =========================================================================

usb_drv_t *create_usb_hid_driver() {
    usb_drv_t *usb_drv = kzalloc(sizeof(usb_drv_t));
    usb_id_t *id_table = kzalloc(sizeof(usb_id_t) * 2);
    id_table[0].match_flags = USB_MATCH_INT_CLASS;
    id_table[0].if_class = 0x3;
    id_table[0].if_subclass = 0x00;
    id_table[0].if_protocol = 0x00;
    usb_drv->drv.name = "usb_hid";
    usb_drv->drv.id_table = id_table;
    usb_drv->probe = hid_probe;
    usb_drv->remove = hid_remove;
    return usb_drv;
}
