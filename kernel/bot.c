#include "bot.h"
#include "scsi.h"

/**
 * 执行 Request Sense 命令获取错误详情
 */
int32 bot_request_sense(bot_data_t *bot_data,scsi_task_t *task) {
    if (!task->sense || *(uint8*)task->cdb == SCSI_REQUEST_SENSE) return -1;

    scsi_sense_t *sense = kzalloc(SCSI_SENSE_ALLOC_SIZE);

    int32 status = scsi_request_sense(bot_data->scsi_dev,sense);

    if (status == 0) {
        asm_mem_cpy(sense,task->sense,8+sense->add_sense_len);
    }else {
        //连获取错误信息都失败了设备可能挂了
    }

    kfree(sense);
    return status;
}

/**
 * BOT 协议同步发送函数
 * 逻辑：CBW -> Data(可选) -> CSW
 */
void bot_send_scsi_cmd_sync(void *dev_context, scsi_task_t *task) {
    bot_data_t *bot_data =dev_context;
    usb_dev_t *usb_dev = bot_data->common.usb_if->usb_dev;
    xhci_controller_t *xhci = usb_dev->xhci_controller;

    // 管道定义 (BOT 通常只用两个 Bulk 管道)
    uint8 pipe_out = bot_data->pipe_out;
    uint8 pipe_in  = bot_data->pipe_in;

    trb_t trb;
    int32 completion_code;

    // 1. 生成 Tag (BOT 的 Tag 只是为了配对 CSW，不用像 UAS 那样管理 Slot)
    uint32 tag = ++bot_data->tag;

    // ============================================================
    // Stage 1: 发送 CBW (Command Block Wrapper)
    // ============================================================
    bot_cbw_t *cbw = kzalloc(sizeof(bot_cbw_t));

    // 填充 CBW
    cbw->signature = BOT_CBW_SIGNATURE; // "USBC"
    cbw->tag       = tag;
    cbw->data_tran_len = task->data_len;

    // 方向标志: IN(设备->主机) = 0x80, OUT(主机->设备) = 0x00
    cbw->flags    = (task->dir == SCSI_DIR_IN) ? BOT_CBW_DATA_IN : BOT_CBW_DATA_OUT;

    cbw->lun       = task->lun;
    cbw->scsi_cdb_len  = task->cdb_len; // 有效 CDB 长度

    asm_mem_cpy(task->cdb, cbw->scsi_cdb, task->cdb_len);

    // 提交 TRB 到 Bulk OUT
    normal_transfer_trb(&trb, va_to_pa(cbw), disable_ch, sizeof(bot_cbw_t), ENABLE_IOC);
    uint64 cbw_trb_ptr = xhci_ring_enqueue(&usb_dev->eps[pipe_out].transfer_ring, &trb);
    xhci_ring_doorbell(xhci, usb_dev->slot_id, pipe_out);

    // 等待 CBW 发送完成
    completion_code = xhci_wait_for_completion(xhci, cbw_trb_ptr, 200000); // 2秒超时
    if (completion_code != XHCI_COMP_SUCCESS) {
        kfree(cbw);
        task->status = -1;
        return; // 发送命令失败
    }

    // ============================================================
    // Stage 2: 数据传输 (Data Stage) - 可选
    // ============================================================
    if (task->data_buf && task->data_len) {
        uint8 data_pipe = (task->dir == SCSI_DIR_IN) ? pipe_in : pipe_out;

        normal_transfer_trb(&trb, va_to_pa(task->data_buf), disable_ch, task->data_len, ENABLE_IOC);
        uint64 data_trb_ptr = xhci_ring_enqueue(&usb_dev->eps[data_pipe].transfer_ring, &trb);
        xhci_ring_doorbell(xhci, usb_dev->slot_id, data_pipe);

        // 等待数据传输完成
        completion_code = xhci_wait_for_completion(xhci, data_trb_ptr, 200000); // 5秒超时
        if (completion_code != XHCI_COMP_SUCCESS) {
            // 注意：BOT 协议中，如果数据长度不匹配，设备可能会 STALL 端点
            // 这里应该加入 Clear Stall (Endpoint Halt) 的逻辑
            kfree(cbw);
            task->status = -2;
            return;
        }
    }

    // ============================================================
    // Stage 3: 接收 CSW (Command Status Wrapper)
    // ============================================================
    bot_csw_t *csw = kzalloc(sizeof(bot_csw_t));

    // 提交 TRB 到 Bulk IN (不管刚才数据是读是写，CSW 永远是读)
    normal_transfer_trb(&trb, va_to_pa(csw), disable_ch, sizeof(bot_csw_t), ENABLE_IOC);
    uint64 csw_trb_ptr = xhci_ring_enqueue(&usb_dev->eps[pipe_in].transfer_ring, &trb);
    xhci_ring_doorbell(xhci, usb_dev->slot_id, pipe_in);

    // 等待 CSW 接收完成
    completion_code = xhci_wait_for_completion(xhci, csw_trb_ptr, 200000);

    //如果成功则 校验 CSW 有效性,Tag 匹配
    if (completion_code == XHCI_COMP_SUCCESS && csw->signature == BOT_CSW_SIGNATURE && csw->tag == tag) {
        switch (csw->status) {
            case BOT_CSW_PASS:
                task->status = 0; //成功返回0
                break;
            case BOT_CSW_FAIL:
                task->status = 0x02; // 如果失败 (0x01)，调用者应该发送 REQUEST SENSE 映射为 SCSI Check Condition
                bot_request_sense(bot_data, task);
                break;
            case BOT_CSW_PHASE:
                //相位错误重启设备
                break;
        }
    }

    // 释放资源
    kfree(cbw);
    kfree(csw);

    return;
}

