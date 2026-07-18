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


struct usb_if_t;

// 描述“一段特定含义的数据”的结构体
typedef struct {
    list_head_t node;  // ★ 内核标准的双向链表节点
    uint32 bit_offset;   // 这段数据在盲盒里的起始 bit 位置
    uint32 bit_size;     // 这段数据占多少个 bit
    uint32 report_count;
    uint32 report_id;
    uint32 usage_page;   // 字典大类 (比如 0x07 代表键盘)
    uint32 usage_min;    // 字典具体词条范围起始
    uint32 usage_max;    // 字典具体词条范围结束
    uint8  is_array;     // 0: 位图(如Ctrl/Shift)  1: 数组(如普通按键)
    uint8  dir;          // 0: Input (读按键), 1: Output (写指示灯)
} hid_field_t;

// 设备结构体：只保留一个链表头
typedef struct {
    list_head_t field_list_head; // ★ 链表头
    uint8       has_report_id;   // 0 表示无 ID，1 表示有 ID
    uint32      field_count;     // 纯粹用来做统计
    usb_urb_t   *int_urb;
    uint8       *report_buf;
    struct usb_if_t *uif;
} hid_dev_t;

usb_drv_t *create_usb_hid_driver();