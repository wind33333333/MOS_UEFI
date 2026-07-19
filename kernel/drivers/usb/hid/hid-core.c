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

/* STREAMING_CHUNK:定义Global栈与解析器State结构体... */
// 独立的 Global 状态结构，为了方便 Push/Pop 时进行整块内存拷贝
typedef struct {
    uint16 usage_page;
    int32 logical_min;
    int32 logical_max;
    int32 physical_min;
    int32 physical_max;
    uint32 unit;
    int32 unit_exponent;
    uint32 report_size;
    uint32 report_count;
    uint8 report_id;
} hid_global_state_t;

// 解析引擎的完整上下文
typedef struct {
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
        data |= (ptr[i] << (i * 8));
    }
    return data;
}

// 重构后的核心解析引擎
// =========================================================================
int hid_parse_report_desc(hid_dev_t *hdev, uint8 *desc, uint32 desc_len) {
    // ★ 修复 2：防范内核栈溢出 (Kernel Stack Overflow)
    // 升级二维数组后，结构体大小超 4KB，必须改用堆分配！
    hid_parser_state_t *state = kzalloc(sizeof(hid_parser_state_t));
    if (!state) return -1; // OOM 保护

    list_head_init(&hdev->field_list_head);
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
                if (item_tag == HID_MAIN_TAG_INPUT || item_tag == HID_MAIN_TAG_OUTPUT || item_tag ==
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

                    field->usages = (hid_usage_t *) (field + 1);

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

                    list_add_tail(&hdev->field_list_head, &field->node);
                    hdev->field_count++;
                }
                reset_local:
                state->local_usage_count = 0;
                state->usage_min = 0;
                state->usage_max = 0;
                break;
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


// 中断底半部回调函数 (每当收到 USB 数据包时被调用)
void hid_irq_handler(hid_dev_t *hdev) {
    uint8 *raw_data = hdev->report_buf; // 拿到最新传来的盲盒数据

    // 遍历我们当初生成的“模具”规则表
    list_head_t *cur_node;
    list_for_each(cur_node, &hdev->field_list_head) {
        hid_field_t *field = CONTAINER_OF(cur_node, hid_field_t, node);

        // 只处理我们需要关心的部分：普通键盘按键 (Usage Page 0x07)
        if (field->usage_page == 0x07) {
            if (1) {
                // 处理 Array 模式：比如 6键无冲的 6 个字节
                // 这里我们知道这块区域有多个按键，需要循环提取
                // (为演示精简，假设我们只读这块区域的第一个按键)
                // uint32 key_code = hid_extract_bits(hdev->report_buf,
                //                                    CONTAINER_OF(hdev->field_list_head.next, hid_field_t, node));
                //
                // if (key_code > 0) {
                //     color_printk(GREEN,BLACK, "Key Pressed! USB Scancode: 0x%x\n", key_code);
                // }
            } else {
                // 处理 Variable 模式：比如那 8 个独立的修饰键
                // 同样可以使用 extract_bits 一位一位去抠出来看是 0 还是 1
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
    hid_dev_t *hdev = NULL;

    // ==========================================
    // Phase 1: 分配驱动私有数据结构并绑定
    // ==========================================
    hdev = kzalloc(sizeof(hid_dev_t));

    hdev->uif = uif;

    // 将我们自己的 hdev 挂载到 USB 接口的私有指针上，方便后续中断里拿出来用
    uif->drv_data = hdev;

    usb_if_alt_t *if_alt = &uif->if_alts[0];

    //3.启用接口
    usb_ep_t *ep1 = &if_alt->eps[0];
    ep1->ring_max_trbs = 32;
    usb_enable_alt_if(if_alt);

    // ==========================================
    // Phase 3: 索要“报告描述符 (说明书)”
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
    // Phase 4: 运行解析引擎，建立“数据模具”
    // ==========================================
    // 这里调用咱们之前写好的解析状态机
    // 解析结果（所有的 hid_field_t）会被保存在 hdev 内部的链表或数组中
    hid_parse_report_desc(hdev, report_desc_buf, report_desc_len);

    // 报告描述符已经翻译成模具存到 hdev 里了，原始的说明书就可以扔掉了
    kfree(report_desc_buf);
    report_desc_buf = NULL;

    // ==========================================
    // Phase 5: 初始化日常中断接收的内存 (盲盒)
    // ==========================================
    // 申请一块内存，用来存放以后每次中断发来的数据流 (注意：必须是支持 DMA 的内存)
    hdev->report_buf = kzalloc_dma(ep1->max_packet_size);

    // ==========================================
    // Phase 6: 注册到 TheresaOS 的 Input Subsystem (输入子系统)
    // ==========================================
    // 告诉系统：我这里有一个新设备准备好了，以后我的数据会发给 input_router


    // ==========================================
    // Phase 7: 启动引擎！投递第一个 URB
    // ==========================================
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
