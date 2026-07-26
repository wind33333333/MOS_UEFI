#include "hid-parser.h"
#include "hid-core.h"
#include "slub.h"


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


/* STREAMING_CHUNK:实现数据提取与符号扩展辅助函数... */
// 辅助函数：符号扩展 (将负数正确恢复为 32位 有符号整数)
static inline  int32 sign_extend(uint32 data, uint8 bytes) {
    if (bytes == 1 && (data & 0x80)) return (int32) (data | 0xFFFFFF00);
    if (bytes == 2 && (data & 0x8000)) return (int32) (data | 0xFFFF0000);
    return (int32) data;
}

// 辅助函数：从字节流中提取指定长度的数据
static inline uint32 fetch_item_data(uint8 *ptr, uint8 size) {
    uint32 data = 0;
    for (uint8 i = 0; i < size; i++) {
        data |= (ptr[i] << (i << 3));
    }
    return data;
}


// 重构后的核心解析引擎
// =========================================================================
int32 hid_parse_report_desc(hid_dev_t *hdev, uint8 *desc, uint32 desc_len) {
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
uint32 hid_extract_bits(const uint8 *report, uint32 bit_offset, uint32 bit_size) {
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