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


/* =========================================================================
 * HID 字段标志位定义 (严格对应 USB HID Specification v1.11, Section 6.2.2.5)
 * ========================================================================= */
#define HID_FLAG_CONSTANT       (1 << 0)  // 0: Data            | 1: Constant
#define HID_FLAG_VARIABLE       (1 << 1)  // 0: Array           | 1: Variable
#define HID_FLAG_RELATIVE       (1 << 2)  // 0: Absolute        | 1: Relative
#define HID_FLAG_WRAP           (1 << 3)  // 0: No Wrap         | 1: Wrap
#define HID_FLAG_NONLINEAR      (1 << 4)  // 0: Linear          | 1: Non Linear
#define HID_FLAG_NOPREFERRED    (1 << 5)  // 0: Preferred State | 1: No Preferred
#define HID_FLAG_NULLSTATE      (1 << 6)  // 0: No Null position| 1: Null state
#define HID_FLAG_VOLATILE       (1 << 7)  // 0: Non Volatile    | 1: Volatile
#define HID_FLAG_BUFFERED_BYTES (1 << 8)  // 0: Bit Field       | 1: Buffered Bytes

/* =========================================================================
 * HID 标志位判断操作宏 (极速内联展开，专治位运算魔法数字)
 * ========================================================================= */
// 数据类型检查
#define HID_FIELD_IS_CONSTANT(flags)   (((flags) & HID_FLAG_CONSTANT) != 0)
#define HID_FIELD_IS_DATA(flags)       (((flags) & HID_FLAG_CONSTANT) == 0)

// 阵列与变量检查 (解决之前 Array/Variable 提取 BUG 的核心)
#define HID_FIELD_IS_VARIABLE(flags)   (((flags) & HID_FLAG_VARIABLE) != 0)
#define HID_FIELD_IS_ARRAY(flags)      (((flags) & HID_FLAG_VARIABLE) == 0)

// 相对与绝对坐标检查 (鼠标属于 Relative，触控板/手柄属于 Absolute)
#define HID_FIELD_IS_RELATIVE(flags)   (((flags) & HID_FLAG_RELATIVE) != 0)
#define HID_FIELD_IS_ABSOLUTE(flags)   (((flags) & HID_FLAG_RELATIVE) == 0)


struct hid_dev_t;

int32 hid_parse_report_desc(struct hid_dev_t *hdev, uint8 *desc, uint32 desc_len);
uint32 hid_extract_bits(const uint8 *report, uint32 bit_offset, uint32 bit_size);