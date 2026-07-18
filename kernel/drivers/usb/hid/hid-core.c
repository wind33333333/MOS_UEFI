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


// 动态解析器核心引擎
static inline int hid_parse_report_desc(hid_dev_t *hdev, uint8 *desc, uint32 len) {
    uint32 ptr = 0;

    // Global Items (全局状态)
    uint32 global_usage_page = 0;
    uint32 global_report_size = 0;
    uint32 global_report_count = 0;
    uint32 global_report_id = 0;

    // Local Items (局部状态)
    uint32 local_usage = 0;
    uint32 local_usage_min = 0;
    uint32 local_usage_max = 0;

    // 当前位移量记录器
    uint32 current_bit_offset = 0;

    while (ptr < len) {
        uint8 item_header = desc[ptr++];

        // === 长项目防御屏障 ===
        if (item_header == HID_LONG_ITEM_PREFIX) {
            if (ptr + 2 > len) return -1;

            uint8 datasize = desc[ptr];
            uint32 skip_bytes = 2 + datasize;
            if (ptr + skip_bytes > len) return -1;

            ptr += skip_bytes;
            continue;
        }

        uint8 item_type = (item_header >> 2) & 0x03;
        uint8 item_tag  = (item_header >> 4) & 0x0F;
        uint8 item_size = item_header & 0x03;

        int param_bytes = (item_size == 3) ? 4 : item_size;

        if (ptr + param_bytes > len) {
            return -1;
        }

        uint32 data = 0;
        switch (param_bytes) {
            case 1: data = desc[ptr]; break;
            case 2: data = desc[ptr] | (desc[ptr+1] << 8); break;
            case 4: data = desc[ptr] | (desc[ptr+1] << 8) | (desc[ptr+2] << 16) | (desc[ptr+3] << 24); break;
        }
        ptr += param_bytes;

        if (item_type == HID_ITEM_TYPE_GLOBAL) {
            // === Global Items (全局项目) ===
            if (item_tag == HID_GLOBAL_TAG_USAGE_PAGE) {
                global_usage_page = data;
            } else if (item_tag == HID_GLOBAL_TAG_REPORT_SIZE) {
                global_report_size = data;
            } else if (item_tag == HID_GLOBAL_TAG_REPORT_ID) {
                global_report_id = data;
                // [核心修正]：遇到新的 Report ID 时，重置当前位移量。
                // 带有 Report ID 的设备，实际数据包的第 0 个字节(8 bits)固定为 ID 本身。
                // 所以属于该 ID 的真实数据字段必须从偏移量 8 开始。
                current_bit_offset = 8;
            } else if (item_tag == HID_GLOBAL_TAG_REPORT_COUNT) {
                global_report_count = data;
            }

        } else if (item_type == HID_ITEM_TYPE_LOCAL) {
            // === Local Items (局部项目) ===
            if (item_tag == HID_LOCAL_TAG_USAGE) local_usage = data;
            else if (item_tag == HID_LOCAL_TAG_USAGE_MIN) local_usage_min = data;
            else if (item_tag == HID_LOCAL_TAG_USAGE_MAX) local_usage_max = data;

        } else if (item_type == HID_ITEM_TYPE_MAIN) {
            // === Main Items (主项目) ===
            if (item_tag == HID_MAIN_TAG_INPUT ||
                item_tag == HID_MAIN_TAG_OUTPUT ||
                item_tag == HID_MAIN_TAG_FEATURE) {

                if ((data & 0x01) == 0) {
                    hid_field_t *field = kmalloc(sizeof(hid_field_t));
                    if (!field) return -ENOMEM;

                    field->bit_offset = current_bit_offset;
                    field->bit_size   = global_report_size;
                    field->report_count = global_report_count;
                    field->report_id  = global_report_id;  // 记录属于哪个 ID
                    field->usage_page = global_usage_page;

                    field->usage_min  = local_usage_min ? local_usage_min : local_usage;
                    field->usage_max  = local_usage_max ? local_usage_max : local_usage;

                    field->is_array   = (data & 0x02) == 0;
                    field->dir        = (item_tag == HID_MAIN_TAG_OUTPUT) ? 1 : 0;

                    list_add_tail(&hdev->field_list_head, &field->node);
                    hdev->field_count++;
                }

                current_bit_offset += (global_report_size * global_report_count);
            }

            // 3. Local 变量生命周期修正：遇到任何 Main Item (包括 Collection) 必须清空
            local_usage = 0;
            local_usage_min = 0;
            local_usage_max = 0;
        }
    }
    return 0;
}


/**
 * @brief 从原始字节流中提取任意位偏移、任意位宽的值
 *
 * @param buf        原始数据的基地址 (例如 report_buf)
 * @param bit_offset 起始比特位的全局偏移量 (比如 13)
 * @param bit_size   需要提取的比特位长度 (比如 12，最大支持 32)
 * @return uint32_t  提取出的干净数值
 */
uint32 extract_bits(const uint8 *buf, uint32 bit_offset, uint32 bit_size) {
    // 0. 安全性防御：最大只提取 32 位
    if (bit_size == 0) return 0;
    if (bit_size > 32) bit_size = 32;

    // 1. 定位起始字节和在该字节内的局部位偏移
    uint32 byte_index = bit_offset / 8; // 从哪个字节开始读
    uint32 bit_shift  = bit_offset % 8; // 在第一个字节里要扔掉几个低位

    // 2. 准备一个 64 位的超大容器（为了防止跨字节截断）
    uint64 accumulator = 0;

    // 3. 计算需要吞入几个字节
    // 最坏的情况：bit_shift 为 7，bit_size 为 32。
    // 总共需要跨越 (7 + 32) = 39 个 bit。
    // 39 / 8 = 4.875，说明最多需要读取 5 个字节。
    int bytes_to_read = ((bit_shift + bit_size - 1) / 8) + 1;

    // 4. 将这些字节按照小端序拼接到 64 位容器中
    for (int i = 0; i < bytes_to_read; i++) {
        // 将第 i 个字节向左推到对应的位置，然后按位或(OR)进去
        accumulator |= ((uint64)buf[byte_index + i]) << (i * 8);
    }

    // 5. 第一刀：切掉底部的废弃位 (右移)
    accumulator >>= bit_shift;

    // 6. 制作掩码 (Mask)
    // 比如 bit_size = 12，我们要生成 0x00000FFF (低12位全为1)
    // 技巧：1 左移 12 位变成 0x1000，减去 1 得到 0x0FFF
    uint32 mask = (1UL << bit_size) - 1;

    // 7. 第二刀：切掉顶部多吞进去的位 (按位与)
    return (uint32)(accumulator & mask);
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

            if (field->is_array) {
                // 处理 Array 模式：比如 6键无冲的 6 个字节
                // 这里我们知道这块区域有多个按键，需要循环提取
                // (为演示精简，假设我们只读这块区域的第一个按键)
                uint32 key_code = extract_bits(raw_data, field->bit_offset, field->bit_size);

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
