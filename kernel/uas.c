#include  "uas.h"

#include <asm-generic/errno-base.h>

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
 * @brief 动态分配 UAS 请求图纸 (Command IU & Sense IU)
 * @return int32 0 表示成功，负数表示 POSIX 错误码
 */
static inline int32 uas_alloc_request(uas_data_t *uas_data, uas_cmd_iu_t **cmd_out, uas_sense_iu_t **sense_out, uint16 *tag_out, uint16 *stream_id_out) {

    // 1. 极速扫描 Bitmap 寻找空闲 Tag (内部数组索引从 0 开始)
    uint16 tag = uas_alloc_tag(uas_data);

    // 2. ★ POSIX 修正：硬件队列防满拦截
    // 如果返回的索引大于等于池子的容量，说明当前没有空闲的并发槽位了
    if (tag > uas_data->max_streams) {
        // 返回 -EBUSY，明确告诉上层 SCSI 调度引擎：“U盘并发队列满了，请稍等片刻再发新指令！”
        return -EBUSY;
    }

    // 3. O(1) 极速寻址获取物理图纸 (内部使用 0-based 索引)
    uas_cmd_iu_t   *cmd   = &uas_data->cmd_iu_pool[tag];
    uas_sense_iu_t *sense = &uas_data->sense_iu_pool[tag];

    // 4. 瞬间抹除上一次 I/O 的历史痕迹 (清零，防止脏数据干扰)
    asm_mem_set(cmd, 0, sizeof(uas_cmd_iu_t));
    asm_mem_set(sense, 0, sizeof(uas_sense_iu_t));

    // 5. 将图纸交接给上层调用者
    // UAS 规范要求 Task Tag 不能为 0，所以对外暴露的真实 Tag 必须 +1
    ++tag;

    *cmd_out   = cmd;
    *sense_out = sense;
    *tag_out   = tag;

    // 只有在 Bulk Endpoint 支持 Streams 特性时，Stream ID 才生效并与 Tag 对齐绑定；否则为 0
    *stream_id_out = uas_data->max_streams == 0 ? 0 : tag;

    return 0; // 完美分配
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
 * UAS 协议同步发送函数 (流式并发引擎)
 * @return int32: 0 表示 USB 总线与端点通信成功，负数表示底层 POSIX 错误。
 */
int32 uas_bulk_transport_sync(scsi_host_t *host, scsi_cmnd_t *cmnd) {
    uas_data_t *uas_data = host->hostdata;
    usb_dev_t *udev = uas_data->uif->udev;

    int32 posix_err = 0;

    uint16 tag, stream_id;
    uas_cmd_iu_t *cmd_iu;
    uas_sense_iu_t *sense_iu;

    // 1. 获取图纸与并发槽位
    posix_err = uas_alloc_request(uas_data, &cmd_iu, &sense_iu, &tag, &stream_id);
    if (posix_err < 0) {
        return posix_err; // 槽位耗尽 (-EBUSY)，直接向上抛出，让调度器重试
    }

    // 2. 准备 UAS Command IU (命令信息单元)
    // 👑 架构师点赞：UAS 规范强制大端序 (Big Endian)，这里的 asm_bswap 处理得极其专业！
    cmd_iu->tag = asm_bswap16(tag);
    cmd_iu->iu_id = UAS_CMD_IU_ID;  // Command IU
    cmd_iu->prio_attr = 0x00;       // Simple Task
    cmd_iu->add_cdb_len = 0;
    cmd_iu->lun = asm_bswap64(cmnd->sdev->lun);
    asm_mem_cpy(cmnd->scsi_cdb, cmd_iu->scsi_cdb, cmnd->scsi_cdb_len);

    // ============================================================
    // 3. 面单分发 (分配独立 URB)
    // ============================================================
    usb_urb_t *urb_status = usb_alloc_urb();
    usb_urb_t *urb_data   = NULL;
    usb_urb_t *urb_cmd    = usb_alloc_urb();

    if (urb_status == NULL || urb_cmd == NULL) {
        posix_err = -ENOMEM;
        goto cleanup;
    }

    if (cmnd->data_buf && cmnd->data_len) {
        urb_data = usb_alloc_urb();
        if (urb_data == NULL) {
            posix_err = -ENOMEM;
            goto cleanup;
        }
    }

    // ============================================================
    // 4. 提交传输并自动敲门铃 (逆序投递流水线)
    // ============================================================

    // [Step A] 提交 Status Pipe (唯一需要打断 CPU 的哨兵)
    usb_fill_bulk_urb(urb_status, udev, uas_data->status_ep, sense_iu, UAS_MAX_SENSE_LEN);
    urb_status->stream_id = stream_id;
    posix_err = usb_submit_urb(urb_status);
    if (posix_err < 0) goto cleanup;

    // [Step B] 提交 Data Pipe (静音传输)
    if (urb_data != NULL) {
        usb_ep_t *data_ep = (cmnd->dir == SCSI_DIR_IN) ? uas_data->data_in_ep : uas_data->data_out_ep;
        usb_fill_bulk_urb(urb_data, udev, data_ep, cmnd->data_buf, cmnd->data_len);
        urb_data->stream_id = stream_id;
        urb_data->transfer_flags |= URB_NO_INTERRUPT; // 静音！
        posix_err = usb_submit_urb(urb_data);
        if (posix_err < 0) {
            // ★ 深度防御提示：在真正的工业级驱动中，如果这里失败了，
            // 应该立刻调用 xhci_cmd_stop_ep 强行停止前面的 Status Pipe，防止硬件跑飞。
            goto cleanup;
        }
    }

    // [Step C] 提交 Command Pipe (静音下达开火指令)
    usb_fill_bulk_urb(urb_cmd, udev, uas_data->cmd_ep, cmd_iu, sizeof(uas_cmd_iu_t));
    urb_cmd->stream_id = 0; // Command Pipe 没有 Stream
    urb_cmd->transfer_flags |= URB_NO_INTERRUPT; // 静音！
    posix_err = usb_submit_urb(urb_cmd);
    if (posix_err < 0) goto cleanup;


    // ============================================================
    // 5. 挂起等待 (只盯紧 Status Pipe 的最后一张回执)
    // ============================================================
    posix_err = xhci_wait_transfer_comp(udev, uas_data->status_ep->ep_dci, urb_status->last_trb_pa);

    // 如果总线发生超时、STALL 等物理故障，直接跳走，绝不碰 cmnd->status！
    if (posix_err < 0) {
        color_printk(RED, BLACK, "UAS: Bus transfer failed with POSIX error: %d\n", posix_err);
        goto cleanup;
    }

    // ============================================================
    // 6. 状态机解析 (总线完美通信，开始解析包裹里的 SCSI 状态)
    // ============================================================
    // UAS 规范：成功收到 Sense IU，它内部的 status 字段就是 SCSI 状态
    cmnd->status = sense_iu->status;

    if (cmnd->status == SCSI_STATUS_CHECK_CONDITION && cmnd->sense) {
        // 提取具体的 Sense 报错详情 (UAS 协议直接把 Sense 附带回传了，极其高效)
        uint16 actual_sense_len = asm_bswap16(sense_iu->scsi_sense_len);
        asm_mem_cpy(sense_iu->scsi_sense, cmnd->sense, actual_sense_len);
    }

cleanup:
    // 👑 过河拆桥：统一回收所有并发申请的面单与槽位
    if (urb_status) usb_free_urb(urb_status);
    if (urb_data)   usb_free_urb(urb_data);
    if (urb_cmd)    usb_free_urb(urb_cmd);

    uas_free_request(uas_data, tag);

    // 👑 唯一出口：向 SCSI 中间层汇报总线物理状态
    return posix_err;
}

//uas协议模板
scsi_host_template_t uas_host_template = {
    .name = "uas",
    .queue_command = uas_bulk_transport_sync,
    .reset_host = NULL,
    .abort_command = NULL,
};
