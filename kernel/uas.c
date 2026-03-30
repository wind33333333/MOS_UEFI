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
    uas_data->tag_bitmap = asm_btr(uas_data->tag_bitmap,--tag);
}

/**
 * @brief 一站式分配 UAS 事务 (Tag + Cmd槽位 + Sense槽位) 并自动清零
 * @param uas_data  设备上下文
 * @param cmd_out   [出参] 用于接收分配好的 Command IU 指针
 * @param sense_out [出参] 用于接收分配好的 Sense IU 指针
 * @return int16    返回分配到的 Tag (0~63)，如果池满则返回 -1
 */
static inline int32 uas_alloc_request(uas_data_t *uas_data, uas_cmd_iu_t **cmd_out, uas_sense_iu_t **sense_out, uint16 *tag_out,uint16 *stream_id_out) {

    // 1. 极速扫描 Bitmap 寻找空闲 Tag
    uint16 tag = uas_alloc_tag(uas_data);
    //如果tag大于等于64表示没有空闲tag
    if (tag > uas_data->max_streams) {
        return -1;
    }

    // 3. O(1) 极速寻址获取物理图纸 /tag从1开始而
    uas_cmd_iu_t   *cmd = &uas_data->cmd_iu_pool[tag];
    uas_sense_iu_t *sense = &uas_data->sense_iu_pool[tag];

    // 4. 瞬间抹除上一次 I/O 的历史痕迹 (清零)
    asm_mem_set(cmd, 0, sizeof(uas_cmd_iu_t));
    asm_mem_set(sense, 0, sizeof(uas_sense_iu_t));

    // 5. 通过双重指针将图纸交接给上层调用者
    ++tag;
    *cmd_out = cmd;
    *sense_out = sense;
    *tag_out = tag;
    *stream_id_out = uas_data->max_streams == 0 ? 0:tag;

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
 * @brief 异步并发投递、同步等待结果 (UAS 协议标准事务执行器)
 */
void uas_bulk_transport_sync(scsi_host_t *host, scsi_cmnd_t *cmnd) {
    uas_data_t *uas_data = host->hostdata;
    usb_dev_t *udev = uas_data->uif->udev;

    uint16 tag,stream_id;
    uas_cmd_iu_t *cmd_iu;
    uas_sense_iu_t *sense_iu;
    uas_alloc_request(uas_data, &cmd_iu, &sense_iu, &tag,&stream_id);

    // 1. 准备 UAS Command IU (命令信息单元)
    cmd_iu->tag = asm_bswap16(tag);
    cmd_iu->iu_id = UAS_CMD_IU_ID;  // Command IU
    cmd_iu->prio_attr = 0x00;       // Simple Task
    cmd_iu->add_cdb_len = 0;
    cmd_iu->lun = asm_bswap64(cmnd->sdev->lun);
    asm_mem_cpy(cmnd->scsi_cdb, cmd_iu->scsi_cdb, cmnd->scsi_cdb_len);

    // ============================================================
    // 👑 2. 面单分发 (UAS 的精髓在于并发投递，需分配独立 URB)
    // ============================================================
    usb_urb_t *urb_status = usb_alloc_urb();
    usb_urb_t *urb_data   = NULL;
    usb_urb_t *urb_cmd    = usb_alloc_urb();

    if (!urb_status || !urb_cmd) {
        color_printk(RED, BLACK, "UAS: URB allocation failed!\n");
        cmnd->status = -1;
        goto cleanup_alloc;
    }

    if (cmnd->data_buf && cmnd->data_len) {
        urb_data = usb_alloc_urb();
        if (!urb_data) {
            cmnd->status = -1;
            goto cleanup_alloc;
        }
    }

    // ============================================================
    // 👑 3. 提交传输并自动敲门铃 (关键顺序：Status -> Data -> Command)
    // ============================================================

    // [Step A] 提交 Status Pipe (等待设备回传 Sense/Status)
    usb_fill_bulk_urb(urb_status, udev, uas_data->status_ep, sense_iu, UAS_MAX_SENSE_LEN);
    urb_status->stream_id = stream_id; // ★ 核心：绑定 Stream ID (引擎会自动算门铃)
    // 默认不加 URB_NO_INTERRUPT，因为我们就是要等它的硬件中断来唤醒！
    if (usb_submit_urb(urb_status) < 0) goto cleanup_submit;

    // [Step B] 提交 Data Pipe (准备好 DMA 数据通道)
    if (urb_data) {
        usb_ep_t *data_ep = (cmnd->dir == SCSI_DIR_IN) ? uas_data->data_in_ep : uas_data->data_out_ep;
        usb_fill_bulk_urb(urb_data, udev, data_ep, cmnd->data_buf, cmnd->data_len);
        urb_data->stream_id = stream_id; // ★ 绑定 Stream ID

        // 👑 性能黑科技：数据传完不需要打扰 CPU，静音！
        urb_data->transfer_flags |= URB_NO_INTERRUPT;
        if (usb_submit_urb(urb_data) < 0) goto cleanup_submit;
    }

    // [Step C] 提交 Command Pipe (下达开火指令)
    usb_fill_bulk_urb(urb_cmd, udev, uas_data->cmd_ep, cmd_iu, sizeof(uas_cmd_iu_t));
    urb_cmd->stream_id = 0; // Command Pipe 通常不是 Stream 端点

    // 👑 性能黑科技：命令发完也不需要打扰 CPU，静音！
    urb_cmd->transfer_flags |= URB_NO_INTERRUPT;
    if (usb_submit_urb(urb_cmd) < 0) goto cleanup_submit;


    // ============================================================
    // 4. 挂起等待 (只认 Status Pipe 的最后一个 TRB 中断)
    // ============================================================
    xhci_trb_comp_code_e comp_code = xhci_wait_transfer_comp(udev, uas_data->status_ep->ep_dci, urb_status->last_trb_pa);

    // 5. 状态机解析
    if (comp_code == XHCI_COMP_SUCCESS || comp_code == XHCI_COMP_SHORT_PACKET) {
        if (sense_iu->status == SCSI_STATUS_CHECK_CONDITION && cmnd->sense) {
            asm_mem_cpy(sense_iu->scsi_sense, cmnd->sense, asm_bswap16(sense_iu->scsi_sense_len));
        }
    } else {
        color_printk(RED, BLACK, "UAS Exec Error: %#x\n", comp_code);
        // 此处未来可拓展 Error Recovery (如 Abort Task)
        cmnd->status = -2;
        goto cleanup_submit;
    }

    cmnd->status = sense_iu->status;

cleanup_submit:
cleanup_alloc:
    // 👑 过河拆桥：统一回收所有并发申请的面单
    if (urb_status) usb_free_urb(urb_status);
    if (urb_data)   usb_free_urb(urb_data);
    if (urb_cmd)    usb_free_urb(urb_cmd);

    uas_free_request(uas_data, tag);
}


//uas协议模板
scsi_host_template_t uas_host_template = {
    .name = "uas",
    .queue_command = uas_bulk_transport_sync,
    .reset_host = NULL,
    .abort_command = NULL,
};
