#include  "uas.h"

//获取一个tag
static inline uint16 uas_alloc_tag(uas_data_t *uas_data) {
    uint16 nr = asm_tzcnt(~uas_data->tag_bitmap);
    uas_data->tag_bitmap = asm_bts(uas_data->tag_bitmap,nr);
    return ++nr;
}

//释放一个tag
static inline void uas_free_tag(uas_data_t *uas_data,uint16 nr) {
    uas_data->tag_bitmap = asm_btr(uas_data->tag_bitmap,nr-1);
}

/**
 * 同步发送 SCSI 命令并等待结果 (UAS 协议)
 */
uint32 uas_send_scsi_cmd_sync(uas_data_t *uas_data, uas_cmd_params_t *params){
    usb_dev_t *usb_dev = uas_data->common.usb_if->usb_dev;
    xhci_controller_t *xhci_controller = usb_dev->xhci_controller;
    uint8 slot_id = usb_dev->slot_id;

    trb_t trb;

    uint8 cmd_pipe = uas_data->cmd_pipe;
    uint8 status_pipe = uas_data->status_pipe;
    uint8 data_pipe;

    boolean is_data_stage = params->data_buf && params->data_len ? TRUE : FALSE;

    // 逻辑：如果传入 6, 10, 12, 16 字节，统一分配 16 字节空间 (标准 UAS 要求) 如果传入 > 16 字节，则分配实际长度
    uint16 effective_cdb_len = (params->scsi_cdb_len > 16) ? params->scsi_cdb_len : 16;

    // 总大小 = 头部(16字节) + 有效CDB长度 注意：sizeof(uas_cmd_iu_t) 因为 cdb[] 是柔性数组，所以等于 16
    uint32 uas_cmd_iu_alloc_size = sizeof(uas_cmd_iu_t) + effective_cdb_len;

    // 获取一个空闲的 Tag (事务 ID)
    uint16 tag = uas_alloc_tag(uas_data);

    // 1. 准备 UAS Command IU
    uas_cmd_iu_t *cmd_iu = kzalloc(uas_cmd_iu_alloc_size);
    cmd_iu->tag = asm_bswap16(tag);
    cmd_iu->iu_id = UAS_CMD_IU_ID;  // Command IU
    cmd_iu->prio_attr = 0x00;       // Simple Task
    cmd_iu->add_cdb_len = (effective_cdb_len - 16) >> 2;        // cdb_len <= 16字节填 0， cdb_len > 16字节填 (cdb_len-16)>>2
    cmd_iu->lun = asm_bswap64(params->lun);   // 默认 LUN 0
    asm_mem_cpy(params->scsi_cdb,cmd_iu->scsi_cdb,params->scsi_cdb_len);    //填充cdb


    // 2. 准备 UAS Sense iu (用于接收状态)
    uas_sense_iu_t *sense_iu = kzalloc(UAS_SENSE_IU_ALLOC_SIZE);

    // 3. 提交 TRB (关键顺序：Status -> Data -> Command) 先准备好“收”，再触发“发”，防止设备回包太快导致溢出


    // [Step A] 提交 Status Pipe 请求 (接收 Sense IU)
    normal_transfer_trb(&trb, va_to_pa(sense_iu), disable_ch, UAS_SENSE_IU_ALLOC_SIZE, ENABLE_IOC);
    uint64 status_trb_ptr = xhci_ring_enqueue(&usb_dev->eps[status_pipe].stream_rings[tag], &trb);

    // [Step B] 提交 Data Pipe 请求 (如果有数据)
    if (is_data_stage) {
        data_pipe = params->dir == UAS_DIR_IN ? uas_data->data_in_pipe : uas_data->data_out_pipe;
        normal_transfer_trb(&trb, va_to_pa(params->data_buf), disable_ch, params->data_len, DISABLE_IOC);
        xhci_ring_enqueue(&usb_dev->eps[data_pipe].stream_rings[tag], &trb);
    }

    // [Step C] 提交 Command Pipe 请求 (触发执行)
    normal_transfer_trb(&trb, va_to_pa(cmd_iu), disable_ch,uas_cmd_iu_alloc_size, DISABLE_IOC); //如果cdb超过了16字节需要加上扩展字节
    xhci_ring_enqueue(&usb_dev->eps[cmd_pipe].transfer_ring,&trb);

    // [Step D] 敲门铃 (Doorbell) status
    xhci_ring_doorbell(xhci_controller, slot_id, status_pipe | tag<<16);

    //可选[Step E] 敲门铃 (Doorbell) data
    if (is_data_stage) {
        xhci_ring_doorbell(xhci_controller, slot_id, data_pipe | tag<<16);
    }

    //[Step F] 敲门铃 (Doorbell) cmd
    xhci_ring_doorbell(xhci_controller, slot_id, cmd_pipe);
    int32 completion_code = xhci_wait_for_completion(xhci_controller,status_trb_ptr,0x20000000);

    uint32 status = sense_iu->status;
    if (completion_code == XHCI_COMP_SUCCESS) {
        if (status == 2 && params->scsi_sense) {       //如果有错误把错误信息传给调用者处理
            asm_mem_cpy(sense_iu->scsi_sense,params->scsi_sense,asm_bswap16(sense_iu->scsi_sense_len));
        }
    }

    kfree(sense_iu);
    kfree(cmd_iu);
    uas_free_tag(uas_data, tag);
    return status;
}

/**
 * 发送 TEST UNIT READY 命令
 */
int32 uas_test_unit_ready(uas_data_t *uas_data,uint8 lun) {
    scsi_sense_t scsi_sense;
    scsi_cdb_test_unit_t scsi_cdb_test_unit = {0};
    scsi_cdb_test_unit.opcode = SCSI_TEST_UNIT_READY;
    uas_cmd_params_t uas_cmd_params={&scsi_cdb_test_unit,sizeof(scsi_cdb_test_unit_t),lun,NULL,0,UAS_DIR_NONE,&scsi_sense};
    uint32 status = 0;
    do {
        status = uas_send_scsi_cmd_sync(uas_data, &uas_cmd_params);
    } while (status == 2 && scsi_sense.flags_key == 0x6 && scsi_sense.asc == 0x29 && scsi_sense.ascq == 0);
    return 0;
}


/*
 * 使用 UAS 协议获取 LUN 数量
*/
#define LUN_BUF_LEN 512
uint32 uas_get_lun_count(uas_data_t *uas_data) {
    scsi_sense_t scsi_sense;
    scsi_cdb_report_luns_t scsi_cdb_repotr_luns={0};
    scsi_cdb_repotr_luns.opcode = SCSI_REPORT_LUNS;        // REPORT LUNS
    scsi_cdb_repotr_luns.alloc_len = asm_bswap32(LUN_BUF_LEN); // 告诉设备我能收多少数据
    scsi_report_luns_t *scsi_report_luns = kzalloc(LUN_BUF_LEN);
    uas_cmd_params_t uas_cmd_params={&scsi_cdb_repotr_luns,sizeof(scsi_cdb_report_luns_t),0,scsi_report_luns,LUN_BUF_LEN,UAS_DIR_IN,&scsi_sense};
    uas_send_scsi_cmd_sync(uas_data,&uas_cmd_params);
    uint32 list_bytes = asm_bswap32(scsi_report_luns->lun_list_length);
    uint32 luns_count = list_bytes >> 3;
    kfree(scsi_report_luns);
    if (luns_count == 0) return 1;
    return luns_count;
}

//uas协议获取u盘信息
int uas_send_inquiry(uas_data_t *uas_data, uint8 lun) {
    scsi_sense_t scsi_sense;
    scsi_cdb_inquiry_t scsi_cdb_inquiry = {0};
    scsi_cdb_inquiry.opcode = SCSI_INQUIRY;
    scsi_cdb_inquiry.alloc_len = sizeof(scsi_inquiry_t);
    scsi_inquiry_t *scsi_inquiry = kzalloc(sizeof(scsi_inquiry_t));
    uas_cmd_params_t uas_cmd_params = {&scsi_cdb_inquiry,sizeof(scsi_cdb_inquiry),lun,scsi_inquiry,sizeof(scsi_inquiry_t),UAS_DIR_IN,&scsi_sense};
    uas_send_scsi_cmd_sync(uas_data,&uas_cmd_params);
    kfree(scsi_inquiry);
    return 0;
}

/**
 * 获取 U 盘容量
 */
int32 uas_get_capacity(uas_data_t *uas_data, uint8 lun) {
    uint64 max_lba;
    uint32 blk_size;
    scsi_sense_t scsi_sense;
    scsi_read_capacity10_t *scsi_read_capacity10 = kzalloc(sizeof(scsi_read_capacity10_t));
    scsi_cdb_read_capacity10_t scsi_cdb_read_capacity10 = {0};
    scsi_cdb_read_capacity10.opcode = SCSI_READ_CAPACITY10;
    uas_cmd_params_t uas_cmd_params = {&scsi_cdb_read_capacity10,sizeof(scsi_cdb_read_capacity10),lun,scsi_read_capacity10,sizeof(scsi_read_capacity10_t),UAS_DIR_IN,&scsi_sense};
    uas_send_scsi_cmd_sync(uas_data, &uas_cmd_params);
    max_lba = asm_bswap32(scsi_read_capacity10->max_lba);
    blk_size = asm_bswap32(scsi_read_capacity10->block_size);
    kfree(scsi_read_capacity10);
    if (max_lba < 0xFFFFFFFF) return (max_lba);

    scsi_read_capacity16_t *scsi_read_capacity16 = kzalloc(sizeof(scsi_read_capacity16_t));
    scsi_cdb_read_capacity16_t scsi_cdb_read_capacity16 = {0};
    scsi_cdb_read_capacity16.opcode = SCSI_READ_CAPACITY16;
    scsi_cdb_read_capacity16.service_action = SA_READ_CAPACITY_16;
    scsi_cdb_read_capacity16.lba = 0;
    scsi_cdb_read_capacity16.alloc_len = asm_bswap32(sizeof(scsi_read_capacity16_t));
    uas_cmd_params.scsi_cdb = &scsi_cdb_read_capacity16;
    uas_cmd_params.scsi_cdb_len = sizeof(scsi_cdb_read_capacity16);
    uas_cmd_params.lun = lun;
    uas_cmd_params.data_buf = scsi_read_capacity16;
    uas_cmd_params.data_len = sizeof(scsi_read_capacity16_t);
    uas_cmd_params.dir = UAS_DIR_IN;
    uas_cmd_params.scsi_sense = &scsi_sense;
    uas_send_scsi_cmd_sync(uas_data, &uas_cmd_params);
    kfree(scsi_read_capacity16);
    return 0;
}