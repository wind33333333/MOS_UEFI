#include  "uas.h"

#include "printk.h"
#include  "usb-core.h"
#include "scsi.h"

//获取一个tag
static inline uint16 uas_alloc_tag(uas_data_t *uas_data) {
    uint16 tag = asm_tzcnt(~uas_data->tag_bitmap);
    uas_data->tag_bitmap = asm_bts(uas_data->tag_bitmap,tag);
    return tag;
}

//释放一个tag
static inline void uas_free_tag(uas_data_t *uas_data,uint16 tag) {
    uas_data->tag_bitmap = asm_btr(uas_data->tag_bitmap,tag-1);
}

/**
 * @brief 一站式分配 UAS 事务 (Tag + Cmd槽位 + Sense槽位) 并自动清零
 * @param uas_data  设备上下文
 * @param cmd_out   [出参] 用于接收分配好的 Command IU 指针
 * @param sense_out [出参] 用于接收分配好的 Sense IU 指针
 * @return int16    返回分配到的 Tag (0~63)，如果池满则返回 -1
 */
static inline int32 uas_alloc_request(uas_data_t *uas_data, uas_cmd_iu_t **cmd_out, uas_sense_iu_t **sense_out, uint16 *tag_out) {

    // 1. 极速扫描 Bitmap 寻找空闲 Tag
    uint16 tag = uas_alloc_tag(uas_data);
    //如果tag大于等于64表示没有空闲tag
    if (tag >= (1<<MAX_STREAMS)) {
        return -1;
    }

    // 3. O(1) 极速寻址获取物理图纸 /tag从1开始而
    uas_cmd_iu_t   *cmd = &uas_data->cmd_iu_pool[tag];
    uas_sense_iu_t *sense = &uas_data->sense_iu_pool[tag];

    // 4. 瞬间抹除上一次 I/O 的历史痕迹 (清零)
    asm_mem_set(cmd, 0, sizeof(uas_cmd_iu_t));
    asm_mem_set(sense, 0, sizeof(uas_sense_iu_t));

    // 5. 通过双重指针将图纸交接给上层调用者
    *cmd_out = cmd;
    *sense_out = sense;
    *tag_out = ++tag;  //tag从1开始所以需要+1

    return 0;
}

/**
 * @brief 一站式释放 UAS 事务 (归还通行证，物理图纸自动回归池中)
 * @param uas_data 设备上下文
 * @param tag      需要归还的事务 ID (0~63)
 */
static inline void uas_free_request(uas_data_t *uas_data, int16 tag) {
    uas_free_tag(uas_data,tag);
}

/**
 * 同步发送 SCSI 命令并等待结果 (UAS 协议)
 */
void uas_send_scsi_cmd_sync(scsi_host_t *host, scsi_cmnd_t *cmnd){
    uas_data_t *uas_data = host->hostdata;
    usb_dev_t *udev = uas_data->uif->udev;
    xhci_hcd_t *xhcd = udev->xhcd;
    uint8 slot_id = udev->slot_id;

    uint8 cmd_pipe = uas_data->cmd_pipe;
    uint8 status_pipe = uas_data->status_pipe;
    uint8 data_pipe;

    // 获取一个空闲的 Tag (事务 ID)
    uint16 tag;
    uas_cmd_iu_t *cmd_iu;
    uas_sense_iu_t *sense_iu;
    uas_alloc_request(uas_data,&cmd_iu,&sense_iu,&tag);

    // 1. 准备 UAS Command IU
    cmd_iu->tag = asm_bswap16(tag);
    cmd_iu->iu_id = UAS_CMD_IU_ID;  // Command IU
    cmd_iu->prio_attr = 0x00;       // Simple Task
    cmd_iu->add_cdb_len = 0;        // cdb_len <= 16字节填 0， cdb_len > 16字节填 (cdb_len-16)>>2
    cmd_iu->lun = asm_bswap64(cmnd->sdev->lun);   // 默认 LUN 0
    asm_mem_cpy(cmnd->scsi_cdb,cmd_iu->scsi_cdb,cmnd->scsi_cdb_len);    //填充cdb

    // 3. 提交 TRB (关键顺序：Status -> Data -> Command) 先准备好“收”，再触发“发”，防止设备回包太快导致溢出
    uint64 status_trb_ptr = usb_enqueue_transfer(&udev->eps[status_pipe]->streams_ring_array[tag],sense_iu,UAS_MAX_SENSE_LEN,TRB_IOC_ENABLE);

    // [Step B] 提交 Data Pipe 请求 (如果有数据)
    if (cmnd->data_buf && cmnd->data_len) {
        data_pipe = cmnd->dir == SCSI_DIR_IN ? uas_data->data_in_pipe : uas_data->data_out_pipe;
        usb_enqueue_transfer(&udev->eps[data_pipe]->streams_ring_array[tag],cmnd->data_buf,cmnd->data_len,TRB_IOC_DISABLE);
    }

    // [Step C] 提交 Command Pipe 请求 (触发执行)
    usb_enqueue_transfer(&udev->eps[cmd_pipe]->transfer_ring,cmd_iu,sizeof(uas_cmd_iu_t),TRB_IOC_DISABLE);

    // [Step D] 敲门铃 (Doorbell) status
    xhci_ring_doorbell(xhcd, slot_id, status_pipe | tag<<16);

    //可选[Step E] 敲门铃 (Doorbell) data
    if (cmnd->data_buf && cmnd->data_len) {
        xhci_ring_doorbell(xhcd, slot_id, data_pipe | tag<<16);
    }

    //[Step F] 敲门铃 (Doorbell) cmd
    xhci_ring_doorbell(xhcd, slot_id, cmd_pipe);
    xhci_trb_comp_code_e com_code = xhci_wait_transfer_comp(udev, status_pipe,status_trb_ptr);

    //检测TRB是否发送成功
    if (com_code == XHCI_COMP_SUCCESS || com_code == XHCI_COMP_SHORT_PACKET) {   //TRB发送成功
        if (sense_iu->status == SCSI_STATUS_CHECK_CONDITION && cmnd->sense) {                  //如果sense_iu报错着把错误拷贝传给调用者
            asm_mem_cpy(sense_iu->scsi_sense,cmnd->sense,asm_bswap16(sense_iu->scsi_sense_len));
        }
    }else {//TRB发送错误处理流程
        color_printk(RED,BLACK,"UAS send Error: %#x   \n",com_code);
        while (1);
    }

    cmnd->status = sense_iu->status;
    uas_free_request(uas_data, tag);
    return;
}


//uas协议模板
scsi_host_template_t uas_host_template = {
    .name = "uas",
    .queue_command = uas_send_scsi_cmd_sync,
    .reset_host = NULL,
    .abort_command = NULL,
};
