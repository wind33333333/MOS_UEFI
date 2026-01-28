#include "usb-storage.h"
#include "xhci.h"
#include "usb.h"
#include "printk.h"

//测试逻辑单元是否有效
static inline boolean bot_msc_test_lun(xhci_controller_t *xhci_controller, usb_dev_t *usb_dev, usb_bot_msc_t *bot_msc,
                                       uint8 lun_id) {
    //测试状态检测3次不成功则视为无效逻辑单元
    boolean flags = FALSE;
    usb_cbw_t *cbw = kzalloc(align_up(sizeof(usb_cbw_t), 64));
    usb_csw_t *csw = kzalloc(align_up(sizeof(usb_csw_t), 64));
    trb_t trb;
    for (uint8 j = 0; j < 3; j++) {
        mem_set(csw, 0, sizeof(usb_csw_t));
        mem_set(cbw, 0, sizeof(usb_cbw_t));
        cbw->cbw_signature = 0x43425355; // 'USBC'
        cbw->cbw_tag = ++bot_msc->tag; // 唯一标签（示例）
        cbw->cbw_data_transfer_length = 0;
        cbw->cbw_flags = 0;
        cbw->cbw_lun = lun_id;
        cbw->cbw_cb_length = 6;

        // 1. 发送 CBW（批量 OUT 端点）
        normal_transfer_trb(&trb, va_to_pa(cbw), disable_ch, sizeof(usb_cbw_t), disable_ioc);
        xhci_ring_enqueue(&bot_msc->out_ep.transfer_ring, &trb);
        // 3. 接收 CSW（批量 IN 端点）
        normal_transfer_trb(&trb, va_to_pa(csw), disable_ch, sizeof(usb_csw_t), enable_ioc);
        xhci_ring_enqueue(&bot_msc->in_ep.transfer_ring, &trb);

        xhci_ring_doorbell(xhci_controller, usb_dev->slot_id, bot_msc->out_ep.ep_num);
        xhci_ring_doorbell(xhci_controller, usb_dev->slot_id, bot_msc->in_ep.ep_num);
        timing();
        xhci_ering_dequeue(xhci_controller, &trb);

        if (!csw->csw_status) {
            flags = TRUE;
            break;
        }
    }
    kfree(cbw);
    kfree(csw);
    return flags;
}

//获取最大逻辑单元
static inline uint8 bot_msc_read_max_lun(xhci_controller_t *xhci_controller, usb_dev_t *usb_dev,
                                         usb_bot_msc_t *bot_msc) {
    trb_t trb;
    setup_stage_trb(&trb, setup_stage_interface, setup_stage_calss, setup_stage_in, usb_req_get_max_lun, 0, 0,
                    bot_msc->interface_num, 8,
                    in_data_stage);
    xhci_ring_enqueue(&usb_dev->ep0, &trb);

    uint8 *max_lun = kzalloc(64);
    data_stage_trb(&trb, va_to_pa(max_lun), 1, trb_in);
    xhci_ring_enqueue(&usb_dev->ep0, &trb);

    status_stage_trb(&trb, enable_ioc, trb_out);
    xhci_ring_enqueue(&usb_dev->ep0, &trb);

    xhci_ring_doorbell(xhci_controller, usb_dev->slot_id, 1);
    timing();
    xhci_ering_dequeue(xhci_controller, &trb);

    uint8 lun_count = ++*max_lun;
    kfree(max_lun);
    return lun_count;
}

//获取u盘厂商信息
static inline uint8 bot_msc_read_vid(xhci_controller_t *xhci_controller, usb_dev_t *usb_dev, usb_bot_msc_t *bot_msc,
                                     uint8 lun_id) {
    usb_lun_t *lun = &bot_msc->lun[lun_id];
    usb_cbw_t *cbw = kzalloc(align_up(sizeof(usb_cbw_t), 64));
    usb_csw_t *csw = kzalloc(align_up(sizeof(usb_csw_t), 64));
    trb_t trb;
    cbw->cbw_signature = 0x43425355; // 'USBC'
    cbw->cbw_tag = ++bot_msc->tag; // 唯一标签（示例）
    cbw->cbw_data_transfer_length = sizeof(inquiry_data_t);
    cbw->cbw_flags = 0x80; // IN 方向
    cbw->cbw_lun = lun->lun_id; // 逻辑单元号
    cbw->cbw_cb_length = 6; //
    cbw->cbw_cb[0] = 0x12;
    cbw->cbw_cb[4] = sizeof(inquiry_data_t);

    // 1. 发送 CBW（批量 OUT 端点)
    normal_transfer_trb(&trb, va_to_pa(cbw), disable_ch, sizeof(usb_cbw_t), disable_ioc);
    xhci_ring_enqueue(&bot_msc->out_ep.transfer_ring, &trb);
    //2. 接收数据（批量 IN 端点）
    inquiry_data_t *inquiry_data = kzalloc(align_up(sizeof(inquiry_data_t), 64));
    normal_transfer_trb(&trb, va_to_pa(inquiry_data), enable_ch, sizeof(inquiry_data_t), disable_ioc);
    xhci_ring_enqueue(&bot_msc->in_ep.transfer_ring, &trb);
    // 3. 接收 CSW（批量 IN 端点）
    normal_transfer_trb(&trb, va_to_pa(csw), disable_ch, sizeof(usb_csw_t), enable_ioc);
    xhci_ring_enqueue(&bot_msc->in_ep.transfer_ring, &trb);

    xhci_ring_doorbell(xhci_controller, usb_dev->slot_id, bot_msc->out_ep.ep_num);
    xhci_ring_doorbell(xhci_controller, usb_dev->slot_id, bot_msc->in_ep.ep_num);
    timing();
    xhci_ering_dequeue(xhci_controller, &trb);

    mem_cpy(&inquiry_data->vendor_id, &lun->vid, 24);
    lun->vid[24] = 0;

    color_printk(GREEN,BLACK, "scsi-version:%d    \n", inquiry_data->version);

    kfree(cbw);
    kfree(csw);
    kfree(inquiry_data);
    return 0;
}

//获取u盘容量信息
static inline uint8 bot_msc_read_capacity(xhci_controller_t *xhci_controller, usb_dev_t *usb_dev,
                                          usb_bot_msc_t *bot_msc, uint8 lun_id) {
    usb_lun_t *lun = &bot_msc->lun[lun_id];
    usb_cbw_t *cbw = kzalloc(align_up(sizeof(usb_cbw_t), 64));
    usb_csw_t *csw = kzalloc(align_up(sizeof(usb_csw_t), 64));
    trb_t trb;

    cbw->cbw_signature = 0x43425355; // 'USBC'
    cbw->cbw_tag = ++bot_msc->tag; // 唯一标签（示例）
    cbw->cbw_data_transfer_length = 32; // READ CAPACITY (16) 返回32 字节
    cbw->cbw_flags = 0x80; // IN 方向
    cbw->cbw_lun = lun->lun_id; // 逻辑单元号
    cbw->cbw_cb_length = 16; // READ CAPACITY (16) 命令长度

    //填充 SCSI READ CAPACITY (16) 命令
    cbw->cbw_cb[0] = 0x9E; // 操作码：READ CAPACITY (16)
    cbw->cbw_cb[1] = 0x10; // 服务动作：0x10
    cbw->cbw_cb[13] = 32; // 分配长度低字节（32 字节）

    // 1. 发送 CBW（批量 OUT 端点）
    normal_transfer_trb(&trb, va_to_pa(cbw), disable_ch, sizeof(usb_cbw_t), disable_ioc);
    xhci_ring_enqueue(&bot_msc->out_ep.transfer_ring, &trb);
    //2. 接收数据（批量 IN 端点
    read_capacity_16_t *capacity_data = kzalloc(align_up(sizeof(read_capacity_16_t), 64));
    normal_transfer_trb(&trb, va_to_pa(capacity_data), enable_ch, sizeof(read_capacity_16_t), disable_ioc);
    xhci_ring_enqueue(&bot_msc->in_ep.transfer_ring, &trb);
    // 3. 接收 CSW（批量 IN 端点
    normal_transfer_trb(&trb, va_to_pa(csw), disable_ch, sizeof(usb_csw_t), enable_ioc);
    xhci_ring_enqueue(&bot_msc->in_ep.transfer_ring, &trb);

    xhci_ring_doorbell(xhci_controller, usb_dev->slot_id, bot_msc->out_ep.ep_num);
    xhci_ring_doorbell(xhci_controller, usb_dev->slot_id, bot_msc->in_ep.ep_num);
    timing();
    xhci_ering_dequeue(xhci_controller, &trb);

    lun->block_count = bswap64(capacity_data->last_lba) + 1;
    lun->block_size = bswap32(capacity_data->block_size);

    kfree(cbw);
    kfree(csw);
    return 0;
}

//读u盘
int32 bot_scsi_read16(xhci_controller_t *xhci_controller, usb_dev_t *usb_dev, usb_bot_msc_t *bot_msc, uint8 lun_id,
                      uint64 lba, uint32 block_count, uint32 block_size, void *buf) {
    usb_lun_t *lun = &bot_msc->lun[lun_id];
    usb_cbw_t *cbw = kzalloc(align_up(sizeof(usb_cbw_t), 64));
    usb_csw_t *csw = kzalloc(align_up(sizeof(usb_csw_t), 64));
    trb_t trb;

    cbw->cbw_signature = 0x43425355; // 'USBC'
    cbw->cbw_tag = ++bot_msc->tag; // 唯一标签（示例）
    cbw->cbw_data_transfer_length = block_count * block_size;
    cbw->cbw_flags = 0x80; // IN 方向
    cbw->cbw_lun = lun->lun_id; // 逻辑单元号
    cbw->cbw_cb_length = 16; // READ CAPACITY (16) 命令长度
    // 构造 READ(16) 命令块
    cbw->cbw_cb[0] = 0x88; //READ(16)
    *(uint64 *) &cbw->cbw_cb[2] = bswap64(lba);
    *(uint32 *) &cbw->cbw_cb[10] = bswap32(block_count);

    // 1. 发送 CBW（批量 OUT 端点）
    normal_transfer_trb(&trb, va_to_pa(cbw), disable_ch, sizeof(usb_cbw_t), disable_ioc);
    xhci_ring_enqueue(&bot_msc->out_ep.transfer_ring, &trb);

    //2. 接收数据（批量 IN 端点
    normal_transfer_trb(&trb, va_to_pa(buf), enable_ch, block_count * block_size, disable_ioc);
    xhci_ring_enqueue(&bot_msc->in_ep.transfer_ring, &trb);

    // 3. 接收 CSW（批量 IN 端点
    normal_transfer_trb(&trb, va_to_pa(csw), disable_ch, sizeof(usb_csw_t), enable_ioc);
    xhci_ring_enqueue(&bot_msc->in_ep.transfer_ring, &trb);

    xhci_ring_doorbell(xhci_controller, usb_dev->slot_id, bot_msc->out_ep.ep_num);
    xhci_ring_doorbell(xhci_controller, usb_dev->slot_id, bot_msc->in_ep.ep_num);
    timing();
    xhci_ering_dequeue(xhci_controller, &trb);
    color_printk(GREEN,BLACK, "read16 m1:%#lx m2:%#lx   \n", trb.member0, trb.member1);

    kfree(cbw);
    kfree(csw);
    return 0;
}

int32 bot_scsi_write16(xhci_controller_t *xhci_controller, usb_dev_t *usb_dev, usb_bot_msc_t *bot_msc, uint8 lun_id,
                       uint64 lba, uint32 block_count, uint32 block_size, void *buf) {
    usb_lun_t *lun = &bot_msc->lun[lun_id];
    usb_cbw_t *cbw = kzalloc(align_up(sizeof(usb_cbw_t), 64));
    usb_csw_t *csw = kzalloc(align_up(sizeof(usb_csw_t), 64));
    trb_t trb;

    cbw->cbw_signature = 0x43425355; // 'USBC'
    cbw->cbw_tag = ++bot_msc->tag; // 唯一标签（示例）
    cbw->cbw_data_transfer_length = block_count * block_size; // READ CAPACITY (16) 返回32 字节
    cbw->cbw_flags = 0x00; // OUT方向（主机->设备）
    cbw->cbw_lun = lun->lun_id; // 逻辑单元号
    cbw->cbw_cb_length = 16; // READ CAPACITY (16) 命令长度
    // 构造 write(16) 命令块
    cbw->cbw_cb[0] = 0x8A; //write(16)
    *(uint64 *) &cbw->cbw_cb[2] = bswap64(lba);
    *(uint32 *) &cbw->cbw_cb[10] = bswap32(block_count);

    // 1. 发送 CBW（批量 OUT 端点）
    normal_transfer_trb(&trb, va_to_pa(cbw), disable_ch, sizeof(usb_cbw_t), disable_ioc);
    xhci_ring_enqueue(&bot_msc->out_ep.transfer_ring, &trb);

    //2. 发送数据（批量 OUT 端点）
    normal_transfer_trb(&trb, va_to_pa(buf), enable_ch, block_count * block_size, disable_ioc);
    xhci_ring_enqueue(&bot_msc->out_ep.transfer_ring, &trb);

    // 3. 接收 CSW（批量 IN 端点
    normal_transfer_trb(&trb, va_to_pa(csw), disable_ch, sizeof(usb_csw_t), enable_ioc);
    xhci_ring_enqueue(&bot_msc->in_ep.transfer_ring, &trb);

    xhci_ring_doorbell(xhci_controller, usb_dev->slot_id, bot_msc->out_ep.ep_num);
    xhci_ring_doorbell(xhci_controller, usb_dev->slot_id, bot_msc->in_ep.ep_num);
    timing();
    xhci_ering_dequeue(xhci_controller, &trb);
    color_printk(GREEN,BLACK, "wirte16 m1:%#lx m2:%#lx   \n", trb.member0, trb.member1);

    kfree(cbw);
    kfree(csw);
    return 0;
}

int32 bot_scsi_read10(xhci_controller_t *xhci_controller,
                      usb_dev_t *usb_dev,
                      usb_bot_msc_t *bot_msc,
                      uint8 lun_id,
                      uint64 lba,
                      uint32 block_count,
                      uint32 block_size,
                      void *buf) {
    usb_lun_t *lun = &bot_msc->lun[lun_id];

    usb_cbw_t *cbw = kzalloc(align_up(sizeof(usb_cbw_t), 64));
    usb_csw_t *csw = kzalloc(align_up(sizeof(usb_csw_t), 64));
    trb_t trb;

    cbw->cbw_signature = 0x43425355; // "USBC"
    cbw->cbw_tag = ++bot_msc->tag;
    cbw->cbw_data_transfer_length = block_count * block_size;
    cbw->cbw_flags = 0x80; // IN 方向
    cbw->cbw_lun = lun->lun_id;
    cbw->cbw_cb_length = 10; // READ(10) 长度

    // READ(10) 命令格式
    cbw->cbw_cb[0] = 0x28; // 操作码：READ(10)
    *(uint32 *) &cbw->cbw_cb[2] = bswap32(lba);
    *(uint16 *) &cbw->cbw_cb[7] = bswap16(block_count); // 要读的块数

    // 1. 发送 CBW（Bulk OUT）
    normal_transfer_trb(&trb, va_to_pa(cbw), disable_ch, sizeof(usb_cbw_t), disable_ioc);
    xhci_ring_enqueue(&bot_msc->out_ep.transfer_ring, &trb);

    // 2. 接收数据（Bulk IN）
    normal_transfer_trb(&trb, va_to_pa(buf), enable_ch, block_count * block_size, disable_ioc);
    xhci_ring_enqueue(&bot_msc->in_ep.transfer_ring, &trb);

    // 3. 接收 CSW（Bulk IN）
    normal_transfer_trb(&trb, va_to_pa(csw), disable_ch, sizeof(usb_csw_t), enable_ioc);
    xhci_ring_enqueue(&bot_msc->in_ep.transfer_ring, &trb);

    // Doorbell
    xhci_ring_doorbell(xhci_controller, usb_dev->slot_id, bot_msc->out_ep.ep_num);
    xhci_ring_doorbell(xhci_controller, usb_dev->slot_id, bot_msc->in_ep.ep_num);

    timing();
    xhci_ering_dequeue(xhci_controller, &trb);
    color_printk(GREEN,BLACK, "read10 m1:%#lx m2:%#lx   \n", trb.member0, trb.member1);

    kfree(cbw);
    kfree(csw);
    return 0;
}

int32 bot_scsi_write10(xhci_controller_t *xhci_controller,
                       usb_dev_t *usb_dev,
                       usb_bot_msc_t *bot_msc,
                       uint8 lun_id,
                       uint64 lba,
                       uint32 block_count,
                       uint32 block_size,
                       void *buf) {
    usb_lun_t *lun = &bot_msc->lun[lun_id];

    usb_cbw_t *cbw = kzalloc(align_up(sizeof(usb_cbw_t), 64));
    usb_csw_t *csw = kzalloc(align_up(sizeof(usb_csw_t), 64));
    trb_t trb;

    cbw->cbw_signature = 0x43425355; // "USBC"
    cbw->cbw_tag = ++bot_msc->tag;
    cbw->cbw_data_transfer_length = block_count * block_size;
    cbw->cbw_flags = 0x00; // OUT 方向
    cbw->cbw_lun = lun->lun_id;
    cbw->cbw_cb_length = 10; // WRITE(10)

    // === 构造 WRITE(10) 命令块 ===
    cbw->cbw_cb[0] = 0x2A; // 操作码：READ(10)
    *(uint32 *) &cbw->cbw_cb[2] = bswap32(lba);
    *(uint16 *) &cbw->cbw_cb[7] = bswap16(block_count); // 要读的块数

    // 1. 发送 CBW（Bulk OUT）
    normal_transfer_trb(&trb, va_to_pa(cbw), disable_ch, sizeof(usb_cbw_t), disable_ioc);
    xhci_ring_enqueue(&bot_msc->out_ep.transfer_ring, &trb);

    // 2. 发送数据（Bulk OUT）
    normal_transfer_trb(&trb, va_to_pa(buf), enable_ch, block_count * block_size, disable_ioc);
    xhci_ring_enqueue(&bot_msc->out_ep.transfer_ring, &trb);

    // 3. 接收 CSW（Bulk IN）
    normal_transfer_trb(&trb, va_to_pa(csw), disable_ch, sizeof(usb_csw_t), enable_ioc);
    xhci_ring_enqueue(&bot_msc->in_ep.transfer_ring, &trb);

    // Doorbell
    xhci_ring_doorbell(xhci_controller, usb_dev->slot_id, bot_msc->out_ep.ep_num);
    xhci_ring_doorbell(xhci_controller, usb_dev->slot_id, bot_msc->in_ep.ep_num);

    timing();
    xhci_ering_dequeue(xhci_controller, &trb);
    color_printk(GREEN,BLACK, "wirte10 m1:%#lx m2:%#lx   \n", trb.member0, trb.member1);

    kfree(cbw);
    kfree(csw);
    return 0;
}

//获取u盘信息（u盘品牌,容量等）bot 协议u盘
void bot_get_msc_info(usb_dev_t *usb_dev, usb_bot_msc_t *bot_msc) {
    xhci_controller_t *xhci_controller = usb_dev->xhci_controller;
    //获取最大逻辑单元
    bot_msc->lun_count = bot_msc_read_max_lun(xhci_controller, usb_dev, bot_msc);
    bot_msc->lun = kzalloc(bot_msc->lun_count * sizeof(usb_lun_t));
    //枚举逻辑单元
    for (uint8 i = 0; i < bot_msc->lun_count; i++) {
        bot_msc->lun[i].lun_id = i;
        if (bot_msc_test_lun(xhci_controller, usb_dev, bot_msc, i) == FALSE) break; //测试逻辑单元是否有效
        bot_msc_read_vid(xhci_controller, usb_dev, bot_msc, i); //获取u盘厂商信息
        bot_msc_read_capacity(xhci_controller, usb_dev, bot_msc, i); //获取u盘容量
    }
}

//u盘驱动程序
int32 usb_storage_probe(usb_if_t *usb_if, usb_id_t *id) {
    usb_dev_t *usb_dev = usb_if->usb_dev;
    xhci_controller_t *xhci_controller = usb_dev->xhci_controller;

    xhci_input_context_t *input_ctx = kzalloc(align_up(sizeof(xhci_input_context_t), xhci_controller->align_size));
    slot64_t slot_ctx = {0};
    ep64_t ep_ctx = {0};
    trb_t trb;

    //u盘是否支持uas协议，优先设置为uas协议
    usb_if_alt_t *alts = usb_if->alts;
    for (uint8 i = 0; i < usb_if->alt_count; i++) {
        if (alts[i].if_protocol == 0x62) usb_if->cur_alt = alts;
    }
    usb_set_interface(usb_if);   //切换接口备用配置

    if (usb_if->cur_alt->if_protocol == 0x62) {        //uas协议初始化流程
        usb_uas_msc_t *uas_msc = kzalloc(sizeof(usb_uas_msc_t));
        uas_msc->usb_dev = usb_dev;
        uas_msc->interface_num = uas_if_desc->interface_number;
        usb_dev->interfaces = uas_msc;
        usb_dev->interfaces_count = 1;
        uint32 context_entries = 0;
        usb_interface_descriptor_t *next_if_desc = uas_if_desc;
        for (uint8 i = 0; i < 4; i++) {
            usb_endpoint_descriptor_t *endpoint_desc = usb_get_next_desc(next_if_desc);
            usb_superspeed_companion_descriptor_t *ss_ep_comp_desc = usb_get_next_desc(endpoint_desc);
            usb_usa_pipe_usage_descriptor_t *pipe_usage_desc = (usb_usa_pipe_usage_descriptor_t *) ss_ep_comp_desc;
            uint32 max_burst = 0;
            uint8 max_streams_exp = 0;
            if (ss_ep_comp_desc->descriptor_type == USB_SUPERSPEED_ENDPOINT_COMPANION_descriptor) {
                pipe_usage_desc = usb_get_next_desc(ss_ep_comp_desc);
                max_burst = ss_ep_comp_desc->max_burst; // 突发包
                max_streams_exp = ss_ep_comp_desc->attributes & 0x1F; // Max Streams指数
            }
            next_if_desc = (usb_interface_descriptor_t *) pipe_usage_desc;
            usb_uas_endpoint_t *endpoint;
            uint32 ep_transfer_type;
            switch (pipe_usage_desc->pipe_id) {
                case USB_PIPE_COMMAND_OUT:
                    endpoint = &uas_msc->cmd_out_ep;
                    ep_transfer_type = EP_TYPE_BULK_OUT;
                    break;
                case USB_PIPE_STATUS_IN:
                    endpoint = &uas_msc->sta_in_ep;
                    ep_transfer_type = EP_TYPE_BULK_IN;
                    break;
                case USB_PIPE_BULK_IN:
                    endpoint = &uas_msc->bluk_in_ep;
                    ep_transfer_type = EP_TYPE_BULK_IN;
                    break;
                case USB_PIPE_BULK_OUT:
                    endpoint = &uas_msc->bluk_out_ep;
                    ep_transfer_type = EP_TYPE_BULK_OUT;
            }
            endpoint->ep_num = ((endpoint_desc->endpoint_address&0xF)<<1) | (endpoint_desc->endpoint_address>>7);
            if (endpoint->ep_num > context_entries) context_entries = endpoint->ep_num;
            ep_ctx.ep_type_size = ep_transfer_type | endpoint_desc->max_packet_size << 16 | max_burst << 8 | 3 << 1;
            if (max_streams_exp == 0) {
                // 无流：单个Transfer Ring
                ep_ctx.ep_config = 0;
                xhci_ring_init(&endpoint->transfer_ring, xhci_controller->align_size);
                ep_ctx.tr_dequeue_ptr = va_to_pa(endpoint->transfer_ring.ring_base) | 1; // DCS=1
            } else {
                ep_ctx.ep_config = (max_streams_exp << 10) | (1 << 15); // MaxPStreams，LSA=1，如果使用线性数组（可选，根据实现）
                // 有流：分配Stream Context Array和per-stream rings
                uint32 streams_count = 1 << max_streams_exp;
                xhci_stream_ctx_t *stream_array = kzalloc((streams_count + 1) * sizeof(xhci_stream_ctx_t));
                endpoint->stream_rings = kzalloc((streams_count + 1) * sizeof(xhci_ring_t));
                endpoint->streams_count = streams_count;

                for (uint32 s = 1; s <= streams_count; s++) {
                    // Stream ID从1开始
                    xhci_ring_init(&endpoint->stream_rings[s], xhci_controller->align_size);
                    stream_array[s].tr_dequeue = va_to_pa(endpoint->stream_rings[s].ring_base) | 1 | 1 << 1;
                    stream_array[s].reserved = 0;
                }
                // Stream ID 0保留，通常设为0或无效
                stream_array[0].tr_dequeue = 0;
                stream_array[0].reserved = 0;
                ep_ctx.tr_dequeue_ptr = va_to_pa(stream_array);
            }
            ep_ctx.trb_payload = 0;
            xhci_input_context_add(input_ctx, &ep_ctx, xhci_controller->dev_ctx_size, endpoint->ep_num);
            color_printk(RED,BLACK, "max_streams_exp:%d ep_num:%d pipe:%d  \n", max_streams_exp, endpoint->ep_num,
                         pipe_usage_desc->pipe_id);
        }
        //更新slot
        slot_ctx.route_speed = (context_entries << 27) | (
                            (usb_dev->xhci_controller->op_reg->portregs[usb_dev->port_id - 1].portsc &
                             0x3C00) << 10);
        slot_ctx.latency_hub = usb_dev->port_id << 16;
        slot_ctx.parent_info = 0;
        slot_ctx.addr_status = 0;
        xhci_input_context_add(input_ctx, &slot_ctx, xhci_controller->dev_ctx_size,0);

        config_endpoint_com_trb(&trb, va_to_pa(input_ctx), usb_dev->slot_id);
        xhci_ring_enqueue(&xhci_controller->cmd_ring, &trb);
        xhci_ring_doorbell(xhci_controller, 0, 0);
        timing();
        xhci_ering_dequeue(xhci_controller, &trb);


        /////////////////////////////////////
        trb_t cmd_trb, sta_trb, in_trb;
        uas_cmd_iu_t *ciu = kzalloc(sizeof(uas_cmd_iu_t));
        ciu->iu_id = 1; // UASP_IU_COMMAND
        ciu->tag = bswap16(1);
        ciu->priority_attr = 0; // SIMPLE
        ciu->len = 0; // CDB 都在前 16 字节
        // LUN0：lun[0..7] 全 0（kzalloc 已经清0，无需额外操作）
        // TEST UNIT READY 6-byte CDB
        ciu->cdb[0] = 0x00; // Opcode = TEST UNIT READY
        while (1) {
            // 1) Status IU buffer
            uas_status_iu_t *status_buf = kzalloc(128);
            mem_set(status_buf, 0, 128);
            normal_transfer_trb(&sta_trb, va_to_pa(status_buf), disable_ch, 128, enable_ioc);
            xhci_ring_enqueue(&uas_msc->sta_in_ep.stream_rings[1], &sta_trb);
            // 2) Command IU
            normal_transfer_trb(&cmd_trb, va_to_pa(ciu), disable_ch, sizeof(uas_cmd_iu_t), enable_ioc);
            xhci_ring_enqueue(&uas_msc->cmd_out_ep.transfer_ring, &cmd_trb);

            xhci_ring_doorbell(xhci_controller, usb_dev->slot_id, uas_msc->cmd_out_ep.ep_num);
            timing();
            xhci_ering_dequeue(xhci_controller, &cmd_trb);
            color_printk(RED,BLACK, "cmd_trb m0:%#lx m1:%#lx   \n", cmd_trb.member0, cmd_trb.member1);

            xhci_ring_doorbell(xhci_controller, usb_dev->slot_id, uas_msc->sta_in_ep.ep_num | 1 << 16);
            timing();
            xhci_ering_dequeue(xhci_controller, &sta_trb);
            color_printk(RED,BLACK, "sta_trb m0:%#lx m1:%#lx   \n", sta_trb.member0, sta_trb.member1);
            if (!status_buf->status) break;
        }

        // INQUIRY 获取u盘基本信息
        ciu->iu_id = 1;
        ciu->tag   = bswap16(1);
        ciu->len   = 0;
        ciu->cdb[0] = 0x12;     // INQUIRY
        ciu->cdb[1] = 1;
        ciu->cdb[2] = 0;
        ciu->cdb[4] = 36;          // alloc len

        uas_status_iu_t* status_buf = kzalloc(128);
        normal_transfer_trb(&sta_trb, va_to_pa(status_buf), disable_ch, 36, enable_ioc);
        xhci_ring_enqueue(&uas_msc->sta_in_ep.stream_rings[1], &sta_trb);

        scsi_inquiry_std_t* inquiry = kzalloc(128);        // 足够放 Data-In IU + 36B payload
        mem_set(inquiry, 0xFF, 128);
        normal_transfer_trb(&in_trb, va_to_pa(inquiry), disable_ch, 96, enable_ioc);
        xhci_ring_enqueue(&uas_msc->bluk_in_ep.stream_rings[1], &in_trb);

        normal_transfer_trb(&cmd_trb, va_to_pa(ciu), disable_ch,sizeof(uas_cmd_iu_t), enable_ioc);
        xhci_ring_enqueue(&uas_msc->cmd_out_ep.transfer_ring, &cmd_trb);

        xhci_ring_doorbell(xhci_controller, usb_dev->slot_id, uas_msc->cmd_out_ep.ep_num);
        timing();
        xhci_ering_dequeue(xhci_controller, &cmd_trb);
        color_printk(RED,BLACK,"cmd_trb m0:%#lx m1:%#lx   \n",cmd_trb.member0,cmd_trb.member1);

        xhci_ring_doorbell(xhci_controller, usb_dev->slot_id, uas_msc->sta_in_ep.ep_num | 1<<16);
        timing();
        xhci_ering_dequeue(xhci_controller, &sta_trb);
        color_printk(RED,BLACK,"sta_trb m0:%#lx m1:%#lx   \n",sta_trb.member0,sta_trb.member1);

        xhci_ring_doorbell(xhci_controller, usb_dev->slot_id, uas_msc->bluk_in_ep.ep_num | 1<<16);
        timing();
        xhci_ering_dequeue(xhci_controller, &in_trb);
        color_printk(RED,BLACK,"in_trb m0:%#lx m1:%#lx   \n",in_trb.member0,in_trb.member1);

        color_printk(RED,BLACK,"sense_iu iu_id:%d tag:%d status:%d   \n",status_buf->iu_id,status_buf->tag,status_buf->status);
        color_printk(RED,BLACK,"inquiy pdt_pq:%#x rmb:%#x ver:%#x resp:%#x add_len:%#x flag1:%#x flag2:%#x flag3:%#x vid:%s pid:%s rev:%s  \n",inquiry->pdt_pq,inquiry->rmb,inquiry->version,inquiry->resp_fmt,inquiry->add_len,inquiry->flags[0],inquiry->flags[1],inquiry->flags[2],inquiry->vendor,inquiry->product,inquiry->revision);


        /*//获取最大lun
        uas_cmd_iu_t* ciu = kzalloc(sizeof(uas_cmd_iu_t));
        ciu->iu_id = 1;
        ciu->tag   = bswap16(1);
        ciu->len   = 0;
        ciu->cdb[0] = 0xA0;   // REPORT LUNS
        ciu->cdb[1] = 0x00;   // SELECT REPORT = 0 -> all luns
        *(uint32*)&ciu->cdb[6] = bswap32(512);

        trb_t cmd_trb,sta_trb,in_trb;

        uas_status_iu_t* status_buf = kzalloc(128);
        normal_transfer_trb(&sta_trb, va_to_pa(status_buf), disable_ch, 128, enable_ioc);
        xhci_ring_enqueue(&uas_msc->sta_in_ep.stream_rings[1], &sta_trb);

        uint64* in_data = kzalloc(512);        // 足够放 Data-In IU + 36B payload
        mem_set(in_data, 0xFF, 512);
        normal_transfer_trb(&in_trb, va_to_pa(in_data), disable_ch, 512, enable_ioc);
        xhci_ring_enqueue(&uas_msc->bluk_in_ep.stream_rings[1], &in_trb);

        normal_transfer_trb(&cmd_trb, va_to_pa(ciu), disable_ch,sizeof(uas_cmd_iu_t), enable_ioc);
        xhci_ring_enqueue(&uas_msc->cmd_out_ep.transfer_ring, &cmd_trb);

        xhci_ring_doorbell(xhci_controller, usb_dev->slot_id, uas_msc->cmd_out_ep.ep_num);
        timing();
        xhci_ering_dequeue(xhci_controller, &cmd_trb);
        color_printk(RED,BLACK,"cmd_trb m0:%#lx m1:%#lx   \n",cmd_trb.member0,cmd_trb.member1);

        xhci_ring_doorbell(xhci_controller, usb_dev->slot_id, uas_msc->sta_in_ep.ep_num | 1<<16);
        timing();
        xhci_ering_dequeue(xhci_controller, &sta_trb);
        color_printk(RED,BLACK,"sta_trb m0:%#lx m1:%#lx   \n",sta_trb.member0,sta_trb.member1);

        xhci_ring_doorbell(xhci_controller, usb_dev->slot_id, uas_msc->bluk_in_ep.ep_num | 1<<16);
        timing();
        xhci_ering_dequeue(xhci_controller, &in_trb);
        color_printk(RED,BLACK,"in_trb m0:%#lx m1:%#lx   \n",in_trb.member0,in_trb.member1);*/

        /*// 获取容量10
        uas_cmd_iu_t* ciu = kzalloc(sizeof(uas_cmd_iu_t));
        ciu->iu_id = 1;
        ciu->tag   = bswap16(1);
        ciu->len   = 0;
        ciu->cdb[0] = 0x25;   // REPORT LUNS

        trb_t cmd_trb,sta_trb,in_trb;

        uas_status_iu_t* status_buf = kzalloc(128);
        normal_transfer_trb(&sta_trb, va_to_pa(status_buf), disable_ch, 128, enable_ioc);
        xhci_ring_enqueue(&uas_msc->sta_in_ep.stream_rings[1], &sta_trb);

        uint64* in_data = kzalloc(64);        // 足够放 Data-In IU + 36B payload
        mem_set(in_data, 0xFF, 64);
        normal_transfer_trb(&in_trb, va_to_pa(in_data), disable_ch, 8, enable_ioc);
        xhci_ring_enqueue(&uas_msc->bluk_in_ep.stream_rings[1], &in_trb);

        normal_transfer_trb(&cmd_trb, va_to_pa(ciu), disable_ch,sizeof(uas_cmd_iu_t), enable_ioc);
        xhci_ring_enqueue(&uas_msc->cmd_out_ep.transfer_ring, &cmd_trb);

        xhci_ring_doorbell(xhci_controller, usb_dev->slot_id, uas_msc->cmd_out_ep.ep_num);
        timing();
        xhci_ering_dequeue(xhci_controller, &cmd_trb);
        color_printk(RED,BLACK,"cmd_trb m0:%#lx m1:%#lx   \n",cmd_trb.member0,cmd_trb.member1);

        xhci_ring_doorbell(xhci_controller, usb_dev->slot_id, uas_msc->sta_in_ep.ep_num | 1<<16);
        timing();
        xhci_ering_dequeue(xhci_controller, &sta_trb);
        color_printk(RED,BLACK,"sta_trb m0:%#lx m1:%#lx   \n",sta_trb.member0,sta_trb.member1);

        xhci_ring_doorbell(xhci_controller, usb_dev->slot_id, uas_msc->bluk_in_ep.ep_num | 1<<16);
        timing();
        xhci_ering_dequeue(xhci_controller, &in_trb);
        color_printk(RED,BLACK,"in_trb m0:%#lx m1:%#lx   \n",in_trb.member0,in_trb.member1);*/


        /*
        // 获取容量16
        ciu->iu_id = 1;
        ciu->tag   = bswap16(1);
        ciu->len   = 0;
        ciu->cdb[0] = 0x9e;   // REPORT LUNS
        ciu->cdb[1] = 0x10;
        ciu->cdb[13] = 32;


        uas_status_iu_t* status_buf = kzalloc(128);
        normal_transfer_trb(&sta_trb, va_to_pa(status_buf), disable_ch, 128, enable_ioc);
        xhci_ring_enqueue(&uas_msc->sta_in_ep.stream_rings[1], &sta_trb);

        uint64* in_data = kzalloc(64);        // 足够放 Data-In IU + 36B payload
        mem_set(in_data, 0xFF, 64);
        normal_transfer_trb(&in_trb, va_to_pa(in_data), disable_ch, 32, enable_ioc);
        xhci_ring_enqueue(&uas_msc->bluk_in_ep.stream_rings[1], &in_trb);

        normal_transfer_trb(&cmd_trb, va_to_pa(ciu), disable_ch,sizeof(uas_cmd_iu_t), enable_ioc);
        xhci_ring_enqueue(&uas_msc->cmd_out_ep.transfer_ring, &cmd_trb);

        xhci_ring_doorbell(xhci_controller, usb_dev->slot_id, uas_msc->cmd_out_ep.ep_num);
        timing();
        xhci_ering_dequeue(xhci_controller, &cmd_trb);
        color_printk(RED,BLACK,"cmd_trb m0:%#lx m1:%#lx   \n",cmd_trb.member0,cmd_trb.member1);

        xhci_ring_doorbell(xhci_controller, usb_dev->slot_id, uas_msc->sta_in_ep.ep_num | 1<<16);
        timing();
        xhci_ering_dequeue(xhci_controller, &sta_trb);
        color_printk(RED,BLACK,"sta_trb m0:%#lx m1:%#lx   \n",sta_trb.member0,sta_trb.member1);

        xhci_ring_doorbell(xhci_controller, usb_dev->slot_id, uas_msc->bluk_in_ep.ep_num | 1<<16);
        timing();
        xhci_ering_dequeue(xhci_controller, &in_trb);
        color_printk(RED,BLACK,"in_trb m0:%#lx m1:%#lx   \n",in_trb.member0,in_trb.member1);*/


        /*// 读u盘 10
        ciu->iu_id = 1;
        ciu->tag   = bswap16(1);
        ciu->len   = 0;
        ciu->cdb[0] = 0x28;   // REPORT LUNS

        *(uint32*)&ciu->cdb[2] = bswap32(0);   // lab
        *(uint16*)&ciu->cdb[7] = bswap16(2);

        uas_status_iu_t* status_buf = kzalloc(128);
        normal_transfer_trb(&sta_trb, va_to_pa(status_buf), disable_ch, 128, enable_ioc);
        xhci_ring_enqueue(&uas_msc->sta_in_ep.stream_rings[1], &sta_trb);

        uint64* in_data = kzalloc(1024);        // 足够放 Data-In IU + 36B payload
        mem_set(in_data, 0xFF, 1024);
        normal_transfer_trb(&in_trb, va_to_pa(in_data), disable_ch, 1024, enable_ioc);
        xhci_ring_enqueue(&uas_msc->bluk_in_ep.stream_rings[1], &in_trb);

        normal_transfer_trb(&cmd_trb, va_to_pa(ciu), disable_ch,sizeof(uas_cmd_iu_t), enable_ioc);
        xhci_ring_enqueue(&uas_msc->cmd_out_ep.transfer_ring, &cmd_trb);

        xhci_ring_doorbell(xhci_controller, usb_dev->slot_id, uas_msc->cmd_out_ep.ep_num);
        timing();
        xhci_ering_dequeue(xhci_controller, &cmd_trb);
        color_printk(RED,BLACK,"cmd_trb m0:%#lx m1:%#lx   \n",cmd_trb.member0,cmd_trb.member1);

        xhci_ring_doorbell(xhci_controller, usb_dev->slot_id, uas_msc->sta_in_ep.ep_num | 1<<16);
        timing();
        xhci_ering_dequeue(xhci_controller, &sta_trb);
        color_printk(RED,BLACK,"sta_trb m0:%#lx m1:%#lx   \n",sta_trb.member0,sta_trb.member1);

        xhci_ring_doorbell(xhci_controller, usb_dev->slot_id, uas_msc->bluk_in_ep.ep_num | 1<<16);
        timing();
        xhci_ering_dequeue(xhci_controller, &in_trb);
        color_printk(RED,BLACK,"in_trb m0:%#lx m1:%#lx   \n",in_trb.member0,in_trb.member1);*/

        //写u盘 16
        /*ciu->iu_id = 1;
        ciu->tag = bswap16(1);
        ciu->len = 0;
        ciu->cdb[0] = 0x8A; // REPORT LUNS
        *(uint64 *) &ciu->cdb[2] = bswap64(0); // lab
        *(uint32 *) &ciu->cdb[10] = bswap32(2);

        uas_status_iu_t *status_buf = kzalloc(128);
        normal_transfer_trb(&sta_trb, va_to_pa(status_buf), disable_ch, 128, enable_ioc);
        xhci_ring_enqueue(&uas_msc->sta_in_ep.stream_rings[1], &sta_trb);

        uint64 *out_data = kzalloc(1024); // 足够放 Data-In IU + 36B payload
        mem_set(out_data, 0xFF, 1024);
        normal_transfer_trb(&in_trb, va_to_pa(out_data), disable_ch, 1024, enable_ioc);
        xhci_ring_enqueue(&uas_msc->bluk_out_ep.stream_rings[1], &in_trb);

        normal_transfer_trb(&cmd_trb, va_to_pa(ciu), disable_ch, sizeof(uas_cmd_iu_t), enable_ioc);
        xhci_ring_enqueue(&uas_msc->cmd_out_ep.transfer_ring, &cmd_trb);

        xhci_ring_doorbell(xhci_controller, usb_dev->slot_id, uas_msc->cmd_out_ep.ep_num);
        timing();
        xhci_ering_dequeue(xhci_controller, &cmd_trb);
        color_printk(RED,BLACK, "cmd_trb m0:%#lx m1:%#lx   \n", cmd_trb.member0, cmd_trb.member1);

        xhci_ring_doorbell(xhci_controller, usb_dev->slot_id, uas_msc->sta_in_ep.ep_num | 1 << 16);
        timing();
        xhci_ering_dequeue(xhci_controller, &sta_trb);
        color_printk(RED,BLACK, "sta_trb m0:%#lx m1:%#lx   \n", sta_trb.member0, sta_trb.member1);

        xhci_ring_doorbell(xhci_controller, usb_dev->slot_id, uas_msc->bluk_out_ep.ep_num | 1 << 16);
        timing();
        xhci_ering_dequeue(xhci_controller, &in_trb);
        color_printk(RED,BLACK, "in_trb m0:%#lx m1:%#lx   \n", in_trb.member0, in_trb.member1);*/

        /*//读u盘 16
        ciu->iu_id = 1;
        ciu->tag = bswap16(1);
        ciu->len = 0;
        ciu->cdb[0] = 0x88; // REPORT LUNS
        *(uint64 *) &ciu->cdb[2] = bswap64(0); // lab
        *(uint32 *) &ciu->cdb[10] = bswap32(2);

        normal_transfer_trb(&sta_trb, va_to_pa(status_buf), disable_ch, 128, enable_ioc);
        xhci_ring_enqueue(&uas_msc->sta_in_ep.stream_rings[1], &sta_trb);

        uint64* in_data = kzalloc(1024);
        mem_set(in_data, 0x0, 1024);
        normal_transfer_trb(&in_trb, va_to_pa(in_data), disable_ch, 1024, enable_ioc);
        xhci_ring_enqueue(&uas_msc->bluk_in_ep.stream_rings[1], &in_trb);

        normal_transfer_trb(&cmd_trb, va_to_pa(ciu), disable_ch, sizeof(uas_cmd_iu_t), enable_ioc);
        xhci_ring_enqueue(&uas_msc->cmd_out_ep.transfer_ring, &cmd_trb);

        xhci_ring_doorbell(xhci_controller, usb_dev->slot_id, uas_msc->cmd_out_ep.ep_num);
        timing();
        xhci_ering_dequeue(xhci_controller, &cmd_trb);
        color_printk(RED,BLACK, "cmd_trb m0:%#lx m1:%#lx   \n", cmd_trb.member0, cmd_trb.member1);

        xhci_ring_doorbell(xhci_controller, usb_dev->slot_id, uas_msc->sta_in_ep.ep_num | 1 << 16);
        timing();
        xhci_ering_dequeue(xhci_controller, &sta_trb);
        color_printk(RED,BLACK, "sta_trb m0:%#lx m1:%#lx   \n", sta_trb.member0, sta_trb.member1);

        xhci_ring_doorbell(xhci_controller, usb_dev->slot_id, uas_msc->bluk_in_ep.ep_num | 1 << 16);
        timing();
        xhci_ering_dequeue(xhci_controller, &in_trb);
        color_printk(RED,BLACK, "in_trb m0:%#lx m1:%#lx   \n", in_trb.member0, in_trb.member1);*/


        while (1);
    } else {
        //bot协议初始化流程
        usb_set_interface(usb_dev, interface_desc->interface_number, interface_desc->alternate_setting);
        usb_bot_msc_t *bot_msc = kzalloc(sizeof(usb_bot_msc_t));
        bot_msc->usb_dev = usb_dev;
        bot_msc->interface_num = interface_desc->interface_number;
        usb_dev->interfaces = bot_msc;
        usb_dev->interfaces_count = 1;
        usb_endpoint_descriptor_t *endpoint_desc = (usb_endpoint_descriptor_t *) interface_desc;
        uint32 context_entries = 0;
        for (uint8 i = 0; i < 2; i++) {
            endpoint_desc = usb_get_next_desc(endpoint_desc);
            usb_endpoint_t *endpoint;
            uint32 ep_transfer_type;
            if (endpoint_desc->endpoint_address & 0x80) {
                endpoint = &bot_msc->in_ep;
                ep_transfer_type = EP_TYPE_BULK_IN;
            } else {
                endpoint = &bot_msc->out_ep;
                ep_transfer_type = EP_TYPE_BULK_OUT;
            }
            endpoint->ep_num = (endpoint_desc->endpoint_address & 0xF) << 1 | endpoint_desc->endpoint_address >> 7;
            context_entries = endpoint->ep_num;
            xhci_ring_init(&endpoint->transfer_ring, xhci_controller->align_size); //初始化端点传输环
            uint32 max_burst = 0;
            if (usb_dev->usb_ver >= 0x300) {
                usb_superspeed_companion_descriptor_t *ss_ep_comp_desc = usb_get_next_desc(endpoint_desc);
                endpoint_desc = (usb_endpoint_descriptor_t *) ss_ep_comp_desc;
                max_burst = ss_ep_comp_desc->max_burst;
            }
            //获取端点类型
            //增加端点
            ep_ctx.ep_config = 0;
            ep_ctx.ep_type_size = ep_transfer_type | endpoint_desc->max_packet_size << 16 | max_burst << 8 | 3 << 1;
            ep_ctx.tr_dequeue_ptr = va_to_pa(endpoint->transfer_ring.ring_base) | 1;
            ep_ctx.trb_payload = 0;
            xhci_input_context_add(input_ctx,&ep_ctx, xhci_controller->dev_ctx_size, endpoint->ep_num);
        }
        //更新slot
        slot_ctx.route_speed = context_entries << 27 | (
                            (usb_dev->xhci_controller->op_reg->portregs[usb_dev->port_id - 1].portsc &
                             0x3C00) << 10);
        slot_ctx.latency_hub = usb_dev->port_id << 16;
        slot_ctx.parent_info = 0;
        slot_ctx.addr_status = 0;
        xhci_input_context_add(input_ctx,&slot_ctx, xhci_controller->context_size, 0);

        config_endpoint_com_trb(&trb, va_to_pa(input_ctx), usb_dev->slot_id);
        xhci_ring_enqueue(&xhci_controller->cmd_ring, &trb);
        xhci_ring_doorbell(xhci_controller, 0, 0);
        timing();
        xhci_ering_dequeue(xhci_controller, &trb);

        //获取u盘基本信息
        bot_get_msc_info(usb_dev, bot_msc);

        //设置u盘读写驱动
        bot_msc->scsi_read = bot_scsi_read10; //默认u盘小于2TB使用scsi10
        bot_msc->scsi_write = bot_scsi_write10;
        for (uint8 i = 0; i < bot_msc->lun_count; i++) {
            uint64 capacity = (bot_msc->lun[i].block_count + 1) * bot_msc->lun[i].block_size;
            if (capacity >= 0x20000000000) {
                bot_msc->scsi_read = bot_scsi_read16; //u盘大于2TB使用scsi16
                bot_msc->scsi_write = bot_scsi_write16;
            }
        }

        uint64 *write = kzalloc(4096);
        mem_set(write, 0x23, 4096);
        bot_msc->scsi_write(xhci_controller, usb_dev, bot_msc, 0, 0, 2, bot_msc->lun[0].block_size, write);
        uint64 *buf = kzalloc(4096);
        bot_msc->scsi_read(xhci_controller, usb_dev, bot_msc, 0, 0, 2, bot_msc->lun[0].block_size, buf);

        color_printk(BLUE,BLACK, "buf:");
        for (uint32 i = 0; i < 100; i++) {
            color_printk(BLUE,BLACK, "%#lx", buf[i]);
        }
        color_printk(BLUE,BLACK, "\n");
        color_printk(GREEN,BLACK, "vid:%#x pid:%#x mode:%s block_num:%#lx block_size:%#x    \n", usb_dev->vid,
                     usb_dev->pid,
                     bot_msc->lun[0].vid, bot_msc->lun[0].block_count, bot_msc->lun[0].block_size);
        //while (1);
    }
    kfree(input_ctx);
}
