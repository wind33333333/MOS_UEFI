#include  "uas.h"

#include "printk.h"
#include  "usb-core.h"
#include "scsi.h"

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
void uas_send_scsi_cmd_sync(scsi_host_t *host, scsi_cmnd_t *cmnd){
    uas_data_t *uas_data = host->hostdata;
    usb_dev_t *usb_dev = uas_data->uif->udev;
    xhci_hcd_t *xhcd = usb_dev->xhcd;
    uint8 slot_id = usb_dev->slot_id;

    trb_t trb;

    uint8 cmd_pipe = uas_data->cmd_pipe;
    uint8 status_pipe = uas_data->status_pipe;
    uint8 data_pipe;

    // 逻辑：如果传入 6, 10, 12, 16 字节，统一分配 16 字节空间 (标准 UAS 要求) 如果传入 > 16 字节，则分配实际长度
    uint16 effective_cdb_len = (cmnd->scsi_cdb_len > 16) ? cmnd->scsi_cdb_len : 16;

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
    cmd_iu->lun = asm_bswap64(cmnd->sdev->lun);   // 默认 LUN 0
    asm_mem_cpy(cmnd->scsi_cdb,cmd_iu->scsi_cdb,cmnd->scsi_cdb_len);    //填充cdb


    // 2. 准备 UAS Sense iu (用于接收状态)
    uas_sense_iu_t *sense_iu = kzalloc(UAS_SENSE_IU_ALLOC_SIZE);

    // 3. 提交 TRB (关键顺序：Status -> Data -> Command) 先准备好“收”，再触发“发”，防止设备回包太快导致溢出
    // [Step A] 提交 Status Pipe 请求 (接收 Sense IU)
    normal_transfer_trb(&trb, va_to_pa(sense_iu), disable_ch, UAS_SENSE_IU_ALLOC_SIZE, ENABLE_IOC);
    uint64 status_trb_ptr = xhci_ring_enqueue(&usb_dev->eps_ring[status_pipe].stream_rings[tag], &trb);

    // [Step B] 提交 Data Pipe 请求 (如果有数据)
    if (cmnd->data_buf && cmnd->data_len) {
        data_pipe = cmnd->dir == SCSI_DIR_IN ? uas_data->data_in_pipe : uas_data->data_out_pipe;
        normal_transfer_trb(&trb, va_to_pa(cmnd->data_buf), disable_ch, cmnd->data_len, DISABLE_IOC);
        xhci_ring_enqueue(&usb_dev->eps_ring[data_pipe].stream_rings[tag], &trb);
    }

    // [Step C] 提交 Command Pipe 请求 (触发执行)
    normal_transfer_trb(&trb, va_to_pa(cmd_iu), disable_ch,uas_cmd_iu_alloc_size, DISABLE_IOC); //如果cdb超过了16字节需要加上扩展字节
    xhci_ring_enqueue(&usb_dev->eps_ring[cmd_pipe].transfer_ring,&trb);

    // [Step D] 敲门铃 (Doorbell) status
    xhci_ring_doorbell(xhcd, slot_id, status_pipe | tag<<16);

    //可选[Step E] 敲门铃 (Doorbell) data
    if (cmnd->data_buf && cmnd->data_len) {
        xhci_ring_doorbell(xhcd, slot_id, data_pipe | tag<<16);
    }

    //[Step F] 敲门铃 (Doorbell) cmd
    xhci_ring_doorbell(xhcd, slot_id, cmd_pipe);
    int32 completion_code = xhci_wait_for_completion(xhcd,status_trb_ptr,0x20000000);

    //检测TRB是否发送成功
    if (completion_code == XHCI_COMP_SUCCESS || completion_code == XHCI_COMP_SHORT_PACKET) {   //TRB发送成功
        if (sense_iu->status == SCSI_STATUS_CHECK_CONDITION && cmnd->sense) {                  //如果sense_iu报错着把错误拷贝传给调用者
            asm_mem_cpy(sense_iu->scsi_sense,cmnd->sense,asm_bswap16(sense_iu->scsi_sense_len));
        }
    }else {//TRB发送错误处理流程
        color_printk(RED,BLACK,"UAS send Error: %#x   \n",completion_code);
        while (1);
    }

    cmnd->status = sense_iu->status;
    kfree(sense_iu);
    kfree(cmd_iu);
    uas_free_tag(uas_data, tag);
    return;
}


//uas协议模板
scsi_host_template_t uas_host_template = {
    .name = "uas",
    .queue_command = uas_send_scsi_cmd_sync,
    .reset_host = NULL,
    .abort_command = NULL,
};
