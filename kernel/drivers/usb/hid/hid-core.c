#include "usb-core.h"
#include "usb-dev.h"
#include "usb-def.h"
#include "usb-bus.h"
#include "hid-core.h"
#include "slub.h"
#include "errno.h"
#include "xhci-hcd.h"
#include "hid-parser.h"
#include "hid-input.h"

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
    hid_input_dev_t *idev = kzalloc(sizeof(hid_input_dev_t));

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
