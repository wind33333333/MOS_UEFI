#include "usb-storage.h"
#include "xhci.h"
#include "usb.h"
#include "printk.h"
#include "scsi.h"

//测试逻辑单元是否有效
/*
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
*/







/**
 * 发送 TEST UNIT READY 命令
 */
int uas_test_unit_ready(uas_data_t *uas_data,uint8 lun_id) {
    scsi_sense_data_t sense_data;
    uint8   cdb = SCSI_TEST_UNIT_READY;
    // 1. 准备 UAS Command IU
    uas_cmd_iu_t *cmd_iu = kzalloc(sizeof(uas_cmd_iu_t));
    cmd_iu->iu_id = 0x01;           // Command IU
    cmd_iu->prio_attr = 0x00;       // Simple Task
    cmd_iu->add_cdb_len = 0;        // 6字节命令 < 16，填 0
    cmd_iu->lun = asm_bswap64(lun_id);   // 默认 LUN 0

    // 2. 填充 CDB (SCSI Command Descriptor Block)
    // TEST UNIT READY 的 CDB 非常简单，全是 0
    // Byte 0: Opcode = 0x00
    // Byte 1-4: Reserved = 0
    // Byte 5: Control = 0
    cmd_iu->cdb[0] = SCSI_TEST_UNIT_READY;
    // memset 已经把 cdb[1]...cdb[5] 清零了，所以不用再写

    // 3. 发送命令 (关键点：无数据传输)
    // data_buf = NULL
    // data_len = 0
    // dir = UAS_DIR_NONE (或者 0)

    uas_send_scsi_cmd_sync(uas_data, &sense_data,cmd_iu, NULL, 0, UAS_DIR_NONE);

    return 0;
}

/**
 * 使用 UAS 协议获取 LUN 数量
 * @param dev: USB 设备指针
 * @return: LUN 的数量 (至少为 1)
*/
uint32 uas_get_lun_count(uas_data_t *uas_data) {
    scsi_sense_data_t sense_data;
    uas_cmd_iu_t *cmd_iu = kzalloc(sizeof(uas_cmd_iu_t));
    scsi_cdb_report_luns_t *repotr_luns_cdb = (scsi_cdb_report_luns_t*)cmd_iu->cdb;

    // 准备接收缓冲区 (512字节足够容纳几十个 LUN 了)
    // 必须是 DMA 安全的内存
    #define BUF_LEN 512
    scsi_report_luns_data_header_t *report_luns_data = kzalloc(BUF_LEN);

    // ================================
    // 1. 构造 UAS Command IU
    // ================================
    cmd_iu->iu_id = UAS_CMD_IU_ID; // UAS_CMD_IU_ID = 1
    cmd_iu->add_cdb_len = 0;
    cmd_iu->lun = 0;               // 注意：REPORT LUNS 总是发给 LUN 0 (Well Known LUN)所以 cmd_iu.lun 设为 0 即可

    // ==========================================
    // 2. 构建 SCSI CDB
    // ==========================================
    repotr_luns_cdb->opcode = SCSI_REPORT_LUNS;        // REPORT LUNS
    repotr_luns_cdb->alloc_len = asm_bswap32(BUF_LEN); // 告诉设备我能收多少数据

    // ==========================================
    // 2. 发送 UAS 命令
    // ==========================================
    uas_send_scsi_cmd_sync(uas_data,&sense_data,cmd_iu, report_luns_data, BUF_LEN,UAS_DIR_IN);

    // ==========================================
    // 3. 解析结果
    // ==========================================
    // 获取列表字节长度 (Big Endian -> Host Endian)
    uint32 list_bytes = asm_bswap32(report_luns_data->lun_list_length);
    color_printk(GREEN,BLACK,"list_bytes:%d     \n",list_bytes);
    // 计算 LUN 数量
    // 每个 LUN 占 8 字节
    uint32 luns_count = list_bytes >> 3;

    kfree(report_luns_data);

    // 规范修正：如果列表长度为0，意味着只有 LUN 0 存在
    if (luns_count == 0) return 1;

    return luns_count;
}

//uas协议获取u盘信息
int uas_send_inquiry(uas_data_t *uas_data, uint8 lun_id, scsi_inquiry_data_t *inquiry_data_buf) {
    scsi_sense_data_t sense_data;
    uas_cmd_iu_t *cmd_iu = kzalloc(sizeof(uas_cmd_iu_t));
    scsi_cdb_inquiry_t *inquiry_cdb = (scsi_cdb_inquiry_t*)cmd_iu->cdb;

    cmd_iu->iu_id = UAS_CMD_IU_ID;
    cmd_iu->add_cdb_len = 0;
    cmd_iu->lun = asm_bswap64(lun_id);

    inquiry_cdb->opcode = SCSI_INQUIRY;
    inquiry_cdb->alloc_len = sizeof(scsi_inquiry_data_t);

    uas_send_scsi_cmd_sync(uas_data,&sense_data,cmd_iu,inquiry_data_buf,sizeof(scsi_inquiry_data_t),UAS_DIR_IN);

    return 0;
}

/**
 * 获取 U 盘容量
 * @param dev: UAS 设备句柄
 * @param capacity_bytes: 输出参数，返回总字节数
 * @param block_size: 输出参数，返回扇区大小 (通常 512 或 4096)
 * @return 0 成功, 非 0 失败
 */
int uas_get_capacity(uas_data_t *uas_data, uint8 lun_id) {
    uint64 max_lba;
    uint32 blk_size;
    scsi_sense_data_t sense_data;
    uas_cmd_iu_t *cmd_iu = kzalloc(sizeof(uas_cmd_iu_t));

    // 1. 准备接收数据的 Buffer (必须是 DMA 安全的)
    // 返回数据只有 8 字节，但也建议用 kzalloc 分配以保证缓存一致性
    scsi_read_capacity10_data_t *read_capacity10_buf = kzalloc(sizeof(scsi_read_capacity10_data_t));

    // 2. 准备 CDB (SCSI 命令)
    scsi_read_capacity10_cdb_t *read_capacity10_cdb = (scsi_read_capacity10_cdb_t *)cmd_iu->cdb;

    read_capacity10_cdb->opcode = SCSI_READ_CAPACITY10;
    // lba, pmi 均为 0，表示查询整个设备的容量

    // 3. 填充 UAS Command IU
    cmd_iu->iu_id = UAS_CMD_IU_ID;
    cmd_iu->prio_attr = 0x00; // Simple
    cmd_iu->add_cdb_len = 0;  // 10字节命令 < 16字节，填 0
    cmd_iu->lun = asm_bswap64(lun_id);

    // 4. 发送命令 (同步等待)
    // 这里的长度必须是 8 (sizeof resp_buf)
    // 方向是 UAS_DIR_IN (读取数据)
    retransmit:
    uint32 status = uas_send_scsi_cmd_sync(uas_data, &sense_data,cmd_iu, read_capacity10_buf, sizeof(scsi_read_capacity10_data_t), UAS_DIR_IN);
    if (status == 2) {
        if (sense_data.flags_key == 0x6 && sense_data.asc == 0x29 && sense_data.ascq == 0)
            goto retransmit;
    }

    max_lba = asm_bswap32(read_capacity10_buf->max_lba);
    blk_size = asm_bswap32(read_capacity10_buf->block_size);

    if (max_lba < 0xFFFFFFFF) return (max_lba);

    scsi_read_capacity16_data_t *read_capacity16_buf = kzalloc(sizeof(scsi_read_capacity16_data_t));
    scsi_read_capacity16_cdb_t *read_capacity16_cdb = (scsi_read_capacity16_cdb_t*)cmd_iu->cdb;

    read_capacity16_cdb->opcode = SCSI_READ_CAPACITY16;
    read_capacity16_cdb->service_action = SA_READ_CAPACITY_16;
    read_capacity16_cdb->lba = 0;
    read_capacity16_cdb->alloc_len = asm_bswap32(sizeof(scsi_read_capacity16_data_t));
    uas_send_scsi_cmd_sync(uas_data, &sense_data,cmd_iu, read_capacity16_buf, sizeof(scsi_read_capacity16_data_t), UAS_DIR_IN);

    return 0;
}


//u盘驱动程序
int32 usb_storage_probe(usb_if_t *usb_if, usb_id_t *id) {
    usb_dev_t *usb_dev = usb_if->usb_dev;

    //u盘是否支持uas协议，优先设置为uas协议
    usb_if_alt_t *alts = usb_if->alts;
    for (uint8 i = 0; i < usb_if->alt_count; i++) {
        if (alts[i].if_protocol == 0x62) usb_if->cur_alt = &alts[i];
    }
    usb_set_interface(usb_if);   //切换接口备用配置
    usb_endpoint_init(usb_if->cur_alt);   //初始化端点

    if (usb_if->cur_alt->if_protocol == 0x62) {
        //uas协议初始化流程
        uas_data_t *uas_data = kzalloc(sizeof(uas_data_t));
        uas_data->common.usb_if = usb_if;

        uint32 mini_streams = 1<<MAX_STREAMS;
        //解析pipe端点
        for (uint8 i = 0; i < 4; i++) {
            usb_ep_t *ep = &usb_if->cur_alt->eps[i];
            uint8 ep_num = ep->ep_num;
            uint32 streams = usb_dev->eps[ep_num].streams_count;
            if (streams && streams < mini_streams) mini_streams = streams;
            usb_uas_pipe_usage_descriptor_t *pipe_usage_desc = ep->extras_desc;
            switch (pipe_usage_desc->pipe_id) {
                case USB_PIPE_COMMAND_OUT:
                    uas_data->cmd_pipe = ep_num ;     //命令pipe
                    break;
                case USB_PIPE_STATUS_IN:
                    uas_data->status_pipe = ep_num ; //状态pipe
                    break;
                case USB_PIPE_BULK_IN:
                    uas_data->data_in_pipe = ep_num ; //接收数据pipe
                    break;
                case USB_PIPE_BULK_OUT:
                    uas_data->data_out_pipe = ep_num ; //发送数据pipe
            }
        }

        //初始化tag_bitmap
        uas_data->tag_bitmap = 0xFFFFFFFFFFFFFFFFUL;
        uas_data->tag_bitmap <<= (mini_streams-1);
        uas_data->tag_bitmap <<= 1;

        //获取lun数量
        uint8 lun_count = uas_get_lun_count(uas_data);

        scsi_inquiry_data_t *inquiry_data = kzalloc(sizeof(scsi_inquiry_data_t));
        uas_send_inquiry(uas_data,0,inquiry_data);

        uas_get_capacity(uas_data,0);


        while (1);

/*
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
        color_printk(RED,BLACK,"in_trb m0:%#lx m1:%#lx   \n",in_trb.member0,in_trb.member1);


        //读u盘 10
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
        color_printk(RED,BLACK,"in_trb m0:%#lx m1:%#lx   \n",in_trb.member0,in_trb.member1);

        //写u盘 16
        ciu->iu_id = 1;
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
        color_printk(RED,BLACK, "in_trb m0:%#lx m1:%#lx   \n", in_trb.member0, in_trb.member1);

        //读u盘 16
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
        color_printk(RED,BLACK, "in_trb m0:%#lx m1:%#lx   \n", in_trb.member0, in_trb.member1);
        while (1);*/

    } else {
        //bot协议初始化流程
        bot_data_t *bot_data = kzalloc(sizeof(bot_data_t));
        bot_data->common.usb_if = usb_if;
        for (uint8 i = 0; i < 2; i++) {
            usb_ep_t *ep_phy = &usb_if->cur_alt->eps[i];
            uint8 ep_num = ep_phy->ep_num;
            if (ep_num & 1) {
                bot_data->pipe_in = ep_num;
            } else {
                bot_data->pipe_out = ep_num;
            }
        }


        /*
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
                     usb_dev->pid,bot_msc->lun[0].vid, bot_msc->lun[0].block_count, bot_msc->lun[0].block_size);
        while (1);*/
    }
}

void usb_storage_remove(usb_if_t *usb_if) {

}

usb_drv_t *create_us_driver() {
    usb_drv_t *usb_drv = kzalloc(sizeof(usb_drv_t));
    usb_id_t *id_table = kzalloc(sizeof(usb_id_t)*3);
    id_table[0].if_class = 0x8;
    id_table[0].if_subclass = 0x6;
    id_table[0].if_protocol = 0x50;
    id_table[1].if_class = 0x8;
    id_table[1].if_subclass = 0x6;
    id_table[1].if_protocol = 0x62;
    usb_drv->drv.name = "usb_storage";
    usb_drv->drv.id_table = id_table;
    usb_drv->probe = usb_storage_probe;
    usb_drv->remove = usb_storage_remove;
    return usb_drv;
}
