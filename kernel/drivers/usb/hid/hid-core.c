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

uint32 hid_count_fields(const uint8 *desc, uint32 desc_len) {
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
static int hid_parse_report_desc(hid_dev_t *hdev, uint8 *desc, uint32 desc_len) {
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

                    uint32 alloc_size = sizeof(hid_field_t) + (state->global.report_count * sizeof(hid_usage_t));
                    hid_field_t *field = kzalloc(alloc_size);
                    if (!field) {
                        // OOM 时跳转清理（目前仅退出，后续可加上释放链表的逻辑）
                        goto parse_end;
                    }
                    hdev->fields[hdev->field_count++] = field;

                    field->report_type = type_idx;
                    field->report_id = state->global.report_id;
                    field->flags = raw_data;
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

                    if (!(raw_data & 0x01)) {
                        if (!(field->flags & 0x02)) {
                            // Array 模式保持为空，留给提取引擎记录状态
                        } else {
                            // ★ 修复：只要 max >= min 且 max 不为 0，就优先认为是区间声明 (Min/Max)
                            if (state->usage_max >= state->usage_min && state->usage_max != 0) {
                                for (uint32 i = 0; i < state->global.report_count; i++) {
                                    if (state->usage_min + i <= state->usage_max) {
                                        field->usages[i].hid_id =
                                                (state->global.usage_page << 16) | (state->usage_min + i);
                                    }
                                }
                            } else if (state->local_usage_count > 0) {
                                // 离散声明 (Usage)
                                uint32 copy_count = (state->local_usage_count < state->global.report_count)
                                                        ? state->local_usage_count
                                                        : state->global.report_count;
                                for (uint32 i = 0; i < copy_count; i++) {
                                    field->usages[i].hid_id = state->local_usages[i];
                                }
                                // HID规范补充：如果离散 Usage 数量少于 Report Count，最后一个 Usage 自动延展
                                uint32 last_usage = state->local_usages[copy_count - 1];
                                for (uint32 i = copy_count; i < state->global.report_count; i++) {
                                    field->usages[i].hid_id = last_usage;
                                }
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





// 定义几个常见的 USB HID 用途页 (Usage Pages) 规范宏
#define HID_UP_GENDESK   0x00010000 // 通用桌面设备 (鼠标X/Y轴等)
#define HID_UP_KEYBOARD  0x00070000 // 标准键盘
#define HID_UP_LED       0x00080000 // LED 状态灯
#define HID_UP_BUTTON    0x00090000 // 鼠标/手柄物理按键

// 假设我们有一个键盘 HID ID 到 TheresaOS 键码的映射表
// (因为 USB 的键码和系统键码并不完全是一一对应，需要查表)
uint16 hid_keyboard_map[256];

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
        for (int j = 0; j < field->max_usage; j++) {
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
                    // 查表将 USB ID 转换成系统的 KEY_* 码 (比如 0x04 -> KEY_A)
                    if (usage_id < 256) {
                        usage->event_type = EV_KEY;
                        usage->event_code = hid_keyboard_map[usage_id];
                    }

                    // 如果这个按键是系统认识的合规按键
                    if (usage->event_code != 0) {
                        // 宣告能力：这台设备支持发按键事件 (大类)
                        SET_BIT(EV_KEY, idev->evbit);
                        // 宣告能力：这台设备具体支持这个键码 (细节)
                        SET_BIT(usage->event_code, idev->keybit);
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
                    // USB 规范中 LED 编号 1 是 Num Lock, 2 是 Caps Lock
                    if (usage_id == 0x01) { usage->event_code = LED_NUML; }
                    else if (usage_id == 0x02) { usage->event_code = LED_CAPSL; }
                    else if (usage_id == 0x03) { usage->event_code = LED_SCROLLL; }

                    SET_BIT(EV_LED, idev->evbit);
                    SET_BIT(usage->event_code, idev->ledbit);
                    break;

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
    // 告诉系统：我这里有一个新设备准备好了，以后我的数据会发给 input_router
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
            // idev->next = g_input_device_list;
            // g_input_device_list = idev;
        }


    // ==========================================
    // Phase 6: 启动引擎！投递第一个 URB
    // ==========================================
    hdev->report_buf = kzalloc_dma(ep1->max_packet_size);
    usb_fill_int_urb(hdev->int_urb, udev, ep1, hdev->report_buf, ep1->max_packet_size, ep1->interval);
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
