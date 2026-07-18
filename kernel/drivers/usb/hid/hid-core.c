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
                           USB_REQ_GET_DESCRIPTOR,       // bRequest: 0x06 (获取描述符)
                           (USB_DESC_TYPE_REPORT << 8) | 0, // wValue: 高字节 0x22 (Report类型)，默认低字节 0 (索引)
                           interface_num,                // wIndex: 必须填它所属的接口号！
                           length);                     // wLength: 想要拉取的字节数 (如 63)
}


// Global 状态 (会被 Push/Pop 压栈)
typedef struct {
    uint16 usage_page;
    int32  logical_min;
    int32  logical_max;
    int32  physical_min;
    int32  physical_max;
    uint32 unit;
    int32  unit_exponent;
    uint32 report_size;
    uint32 report_count;
    uint8  report_id;
} hid_global_state_t;

// Local 状态 (遇到 Main Item 后自动清空)
#define MAX_USAGES_PER_ITEM 16
typedef struct {
    uint16 usages[MAX_USAGES_PER_ITEM];
    uint32 usage_count;
    uint16 usage_min;
    uint16 usage_max;
} hid_local_state_t;

/* ==========================================
 * 2. 辅助函数：符号扩展
 * ========================================== */
static int32 sign_extend(uint32 data, uint8 size_bytes) {
    if (size_bytes == 1 && (data & 0x80))     return (int32)(data | 0xFFFFFF00);
    if (size_bytes == 2 && (data & 0x8000))   return (int32)(data | 0xFFFF0000);
    return (int32)data;
}


hid_field_t* hid_parse_report_desc(hid_dev_t *hdev,const uint8* desc, uint32 desc_len) {
    // 状态机寄存器
    hid_global_state_t global = {0};
    hid_global_state_t global_stack[8] = {0}; // 深度 8 的栈
    uint8 stack_ptr = 0;
    hid_local_state_t  local = {0};

    // Bit Offset 追踪器：维度为 [Report Type][Report ID]
    // 索引 0 作为没有 Report ID 时的默认追踪器
    uint32 bit_offsets[3][256] = {0};
    boolean has_report_id = FALSE;

    uint32 ptr = 0;
    while (ptr < desc_len) {
        uint8 header = desc[ptr++];
        uint8 item_size_code = header & 0x03;
        uint8 item_type = (header >> 2) & 0x03;
        uint8 item_tag  = (header >> 4) & 0x0F;

        // Long Item 过滤
        if (item_size_code == 3 && item_type == 3 && item_tag == 15) {
            uint8 data_size = desc[ptr++];
            ptr += 1 + data_size; // 跳过 tag 和 payload
            continue;
        }

        uint8 param_bytes = (item_size_code == 3) ? 4 : item_size_code;
        uint32 raw_data = 0;

        for (int i = 0; i < param_bytes; i++) {
            raw_data |= (desc[ptr + i] << (i * 8));
        }
        int32 signed_data = sign_extend(raw_data, param_bytes);
        ptr += param_bytes;

        // 处理 Local Items
        if (item_type == 2) {
            switch (item_tag) {
                case 0x00: // Usage
                    if (local.usage_count < MAX_USAGES_PER_ITEM)
                        local.usages[local.usage_count++] = raw_data;
                    break;
                case 0x01: local.usage_min = raw_data; break;
                case 0x02: local.usage_max = raw_data; break;
            }
        }
        // 处理 Global Items
        else if (item_type == 1) {
            switch (item_tag) {
                case 0x00: global.usage_page = raw_data; break;
                case 0x01: global.logical_min = signed_data; break;
                case 0x02: global.logical_max = signed_data; break;
                case 0x03: global.physical_min = signed_data; break;
                case 0x04: global.physical_max = signed_data; break;
                case 0x05: global.unit_exponent = signed_data; break;
                case 0x06: global.unit = raw_data; break;
                case 0x07: global.report_size = raw_data; break;
                case 0x08:
                    global.report_id = raw_data;
                    has_report_id = TRUE;
                    // 如果存在 Report ID，该端点的所有数据包前置 8 个 bit 的 ID
                    // 因此我们需要将当前 ID 的追踪器初始偏移设为 8
                    if (bit_offsets[HID_REPORT_TYPE_INPUT][global.report_id] == 0) {
                        bit_offsets[HID_REPORT_TYPE_INPUT][global.report_id] = 8;
                        bit_offsets[HID_REPORT_TYPE_OUTPUT][global.report_id] = 8;
                        bit_offsets[HID_REPORT_TYPE_FEATURE][global.report_id] = 8;
                    }
                    break;
                case 0x09: global.report_count = raw_data; break;
                case 0x0A: // Push
                    if (stack_ptr < 8) global_stack[stack_ptr++] = global;
                    break;
                case 0x0B: // Pop
                    if (stack_ptr > 0) global = global_stack[--stack_ptr];
                    break;
            }
        }
        // 处理 Main Items (执行真正的解析生成逻辑)
        else if (item_type == 0) {
            if (item_tag == 0x08 /* Input */ || item_tag == 0x09 /* Output */ || item_tag == 0x0B /* Feature */) {

                uint8 r_type = (item_tag == 0x08) ? HID_REPORT_TYPE_INPUT :
                                 (item_tag == 0x09) ? HID_REPORT_TYPE_OUTPUT : HID_REPORT_TYPE_FEATURE;

                // 按 Report Count 循环展开节点，实现 O(1) 提取地图
                for (uint32 i = 0; i < global.report_count; i++) {
                    hid_field_t* field = kmalloc(sizeof(hid_field_t));

                    // 1. 记录基础路由信息
                    field->report_type = r_type;
                    field->report_id   = global.report_id;

                    // 2. 核心：位偏移计算与推进
                    field->bit_offset = bit_offsets[r_type][global.report_id];
                    field->bit_size   = global.report_size;
                    bit_offsets[r_type][global.report_id] += global.report_size;

                    // 3. 语义赋予 (解决 Usage 分配问题)
                    field->usage_page = global.usage_page;
                    field->flags      = raw_data; // Constant, Variable, Absolute 等

                    // 如果这是一个常量 (Padding)，系统仍然需要记录它的位移（上面已累加），
                    // 但没必要生成具体的有效 usage，提取时应用层可根据 flag 忽略它
                    if (!(raw_data & 0x01)) { // It is Data (not Constant)
                        if (local.usage_count > 0) {
                            // 优先从列出的 Usage 中取，如果不够用，最后一个 usage 兜底
                            uint32 u_idx = (i < local.usage_count) ? i : (local.usage_count - 1);
                            field->usage = local.usages[u_idx];
                        } else if (local.usage_max > local.usage_min) {
                            // 使用 Usage Min/Max 范围
                            field->usage = local.usage_min + i;
                        } else {
                            field->usage = 0; // 缺乏 usage 信息
                        }
                    }

                    // 4. 写入物理与逻辑边界
                    field->logical_min   = global.logical_min;
                    field->logical_max   = global.logical_max;
                    field->physical_min  = (global.physical_max != 0 || global.physical_min != 0) ? global.physical_min : global.logical_min;
                    field->physical_max  = (global.physical_max != 0 || global.physical_min != 0) ? global.physical_max : global.logical_max;
                    field->unit          = global.unit;
                    field->unit_exponent = global.unit_exponent;

                    // 5. 挂载到链表
                    list_add_tail(&hdev->field_list_head, &field->node);
                    hdev->field_count++;
                }

                // 根据 HID 规范：Main Item 结束后，必须清空 Local 状态
                for(int i = 0; i < MAX_USAGES_PER_ITEM; i++) local.usages[i] = 0;
                local.usage_count = 0;
                local.usage_min = 0;
                local.usage_max = 0;
            }
            // 遇到 Collection (0x0A) 和 End Collection (0x0C)
            // 在简单的平铺解析中，可以不保存 Collection 树结构，因为 Usage 和物理范围已绑定到具体 Field
            else if (item_tag == 0x0A || item_tag == 0x0C) {
                // 清理 Local 状态
                local.usage_count = 0;
                local.usage_min = 0;
                local.usage_max = 0;
            }
        }
    }

    return 0;
}


// 传入 USB 端点读到的 raw buffer 和对应的 field
int32 hid_extract_bits(const uint8* report_buf, const hid_field_t* field) {
    uint32 offset = field->bit_offset;
    uint32 size = field->bit_size;

    // 防御性编程：丢弃无效数据或超大位宽
    if (size == 0 || size > 32) return 0;

    uint32 byte_idx = offset / 8;
    uint32 bit_shift = offset % 8;

    // 1. 跨字节读取数据
    // 即使需要提取 32 位数据，加上偏移可能跨越 5 个字节
    // 因此使用 uint64_t 作为拼装容器可以完美防止溢出
    uint64 raw_data = 0;
    uint32 bytes_to_read = (size + bit_shift + 7) / 8;
    for (uint32 i = 0; i < bytes_to_read; i++) {
        raw_data |= ((uint64)report_buf[byte_idx + i]) << (i * 8);
    }

    // 2. 移位对齐并应用掩码 (Mask)
    raw_data >>= bit_shift;
    uint32 mask = (1ULL << size) - 1;
    uint32 extracted_val = (uint32)(raw_data & mask);

    // 3. 符号扩展判断 (logical_min 的唯一作用)
    if (field->logical_min < 0) {
        uint32 sign_bit = 1 << (size - 1);
        if (extracted_val & sign_bit) {
            // 如果符号位是 1，按位取反掩码，将高位全部填充为 1
            extracted_val |= ~mask;
        }
    }

    return (int32)extracted_val;
}


// 中断底半部回调函数 (每当收到 USB 数据包时被调用)
void hid_irq_handler(hid_dev_t *hdev) {
    uint8 *raw_data = hdev->report_buf; // 拿到最新传来的盲盒数据

    // 遍历我们当初生成的“模具”规则表
    list_head_t *cur_node;
    list_for_each(cur_node,&hdev->field_list_head) {
        hid_field_t *field = CONTAINER_OF(cur_node,hid_field_t, node);

        // 只处理我们需要关心的部分：普通键盘按键 (Usage Page 0x07)
        if (field->usage_page == 0x07) {

            if (1) {
                // 处理 Array 模式：比如 6键无冲的 6 个字节
                // 这里我们知道这块区域有多个按键，需要循环提取
                // (为演示精简，假设我们只读这块区域的第一个按键)
                uint32 key_code = hid_extract_bits(hdev->report_buf,CONTAINER_OF(hdev->field_list_head.next,hid_field_t,node));

                if (key_code > 0) {
                    color_printk(GREEN,BLACK,"Key Pressed! USB Scancode: 0x%x\n", key_code);
                }
            }
            else {
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
static int hid_probe(usb_if_t *uif,usb_id_t *uid) {
    usb_dev_t *udev = uif->udev;   // 从接口反向拿到物理设备对象
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
    usb_hid_get_report_desc(udev,if_alt->if_desc->interface_number,report_desc_buf,report_desc_len);


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
