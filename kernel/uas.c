#define "uas.h“

// 1. 定义事务参数结构体
typedef struct {
    uint8             *scsi_cdb;
    uint8             scsi_cdb_len;
    uint64            lun;
    void              *data_buf;
    uint32            data_len;
    uas_dir_e         dir;
    scsi_sense_data_t *scsi_sense; // 输出参数
} uas_cmd_params_t;
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
    uint32 iu_alloc_size = sizeof(uas_cmd_iu_t) + effective_cdb_len;

    // 获取一个空闲的 Tag (事务 ID)
    uint16 tag = uas_alloc_tag(uas_data);

    // 1. 准备 UAS Command IU
    uas_cmd_iu_t *cmd_iu = kzalloc(iu_alloc_size);
    cmd_iu->tag = asm_bswap16(tag);
    cmd_iu->iu_id = UAS_CMD_IU_ID;  // Command IU
    cmd_iu->prio_attr = 0x00;       // Simple Task
    cmd_iu->add_cdb_len = (effective_cdb_len - 16) >> 2;        // cdb_len <= 16字节填 0， cdb_len > 16字节填 (cdb_len-16)>>2
    cmd_iu->lun = asm_bswap64(params->lun);   // 默认 LUN 0
    asm_mem_cpy(params->scsi_cdb,cmd_iu->cdb,params->scsi_cdb_len);    //填充cdb


    // 2. 准备 UAS Sense iu (用于接收状态)
    uas_sense_iu_t *sense_iu = kzalloc(sizeof(uas_sense_iu_t));

    // 3. 提交 TRB (关键顺序：Status -> Data -> Command) 先准备好“收”，再触发“发”，防止设备回包太快导致溢出


    // [Step A] 提交 Status Pipe 请求 (接收 Sense IU)
    normal_transfer_trb(&trb, va_to_pa(sense_iu), disable_ch, sizeof(uas_sense_iu_t), ENABLE_IOC);
    uint64 status_trb_ptr = xhci_ring_enqueue(&usb_dev->eps[status_pipe].stream_rings[tag], &trb);

    // [Step B] 提交 Data Pipe 请求 (如果有数据)
    if (is_data_stage) {
        data_pipe = params->dir == UAS_DIR_IN ? uas_data->data_in_pipe : uas_data->data_out_pipe;
        normal_transfer_trb(&trb, va_to_pa(params->data_buf), disable_ch, params->data_len, DISABLE_IOC);
        xhci_ring_enqueue(&usb_dev->eps[data_pipe].stream_rings[tag], &trb);
    }

    // [Step C] 提交 Command Pipe 请求 (触发执行)
    normal_transfer_trb(&trb, va_to_pa(cmd_iu), disable_ch,iu_alloc_size, DISABLE_IOC); //如果cdb超过了16字节需要加上扩展字节
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
            asm_mem_cpy(sense_iu->sense_data,params->scsi_sense,18);
        }
    }

    kfree(sense_iu);
    kfree(cmd_iu);
    uas_free_tag(uas_data, tag);
    return status;
}