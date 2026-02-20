#include "bot.h"
#include "uas.h"

int32 bot_send_scsi_cmd_sync(bot_data_t *bot_data, uas_cmd_params_t *params);

/**
 * 执行 Request Sense 命令获取错误详情
 */
int32 bot_request_sense(bot_data_t *bot_data, uas_cmd_params_t *params) {
    // 1. 检查是否需要获取 Sense (上层提供了缓冲区)
    // 2. 【防死锁守卫】: 检查当前命令是否已经是 Request Sense (Opcode 0x03)
    //    如果当前正在执行 0x03 命令却失败了，绝对不能再调 bot_perform_request_sense，
    //    否则会造成无限递归死循环 (Stack Overflow)。
    if (!params->scsi_sense || *(uint8*)params->scsi_cdb == SCSI_REQUEST_SENSE) return -1;

    scsi_cdb_request_sense_t cdb = {SCSI_REQUEST_SENSE,0,0,BOT_SENSE_ALLOC_SIZE,0};

    // 1. 准备临时接收缓冲区
    // 虽然可以直接用 sense_data_out，但在栈上开辟一个小buffer更安全，防止 DMA 污染用户内存
    scsi_sense_t *scsi_sense = kzalloc(BOT_SENSE_ALLOC_SIZE);

    // 2. 构造参数包
    uas_cmd_params_t sense_params = {&cdb, sizeof(scsi_cdb_request_sense_t),params->lun,scsi_sense,BOT_SENSE_ALLOC_SIZE,UAS_DIR_IN,NULL};

    // 3. 递归调用主发送函数
    // 这里的逻辑是：把 Request Sense 当作一个普通的 SCSI 读命令发送出去
    int status = bot_send_scsi_cmd_sync(bot_data, &sense_params);
    if (status == 0) {
        asm_mem_cpy(scsi_sense,params->scsi_sense,8+scsi_sense->add_sense_len);
    }else {
        //连获取错误信息都失败了设备可能挂了
    }
    kfree(scsi_sense);
    return status;
}

/**
 * BOT 协议同步发送函数
 * 逻辑：CBW -> Data(可选) -> CSW
 */
int32 bot_send_scsi_cmd_sync(bot_data_t *bot_data, uas_cmd_params_t *params) {
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
    cbw->data_tran_len = params->data_len;

    // 方向标志: IN(设备->主机) = 0x80, OUT(主机->设备) = 0x00
    cbw->flags    = (params->dir == UAS_DIR_IN) ? BOT_CBW_DATA_IN : BOT_CBW_DATA_OUT;

    cbw->lun       = params->lun;
    cbw->scsi_cdb_len  = params->scsi_cdb_len; // 有效 CDB 长度

    asm_mem_cpy(params->scsi_cdb, cbw->scsi_cdb, params->scsi_cdb_len);

    // 提交 TRB 到 Bulk OUT
    normal_transfer_trb(&trb, va_to_pa(cbw), disable_ch, sizeof(bot_cbw_t), ENABLE_IOC);
    uint64 cbw_trb_ptr = xhci_ring_enqueue(&usb_dev->eps[pipe_out].transfer_ring, &trb);
    xhci_ring_doorbell(xhci, usb_dev->slot_id, pipe_out);

    // 等待 CBW 发送完成
    completion_code = xhci_wait_for_completion(xhci, cbw_trb_ptr, 200000); // 2秒超时
    if (completion_code != XHCI_COMP_SUCCESS) {
        kfree(cbw);
        return -1; // 发送命令失败
    }

    // ============================================================
    // Stage 2: 数据传输 (Data Stage) - 可选
    // ============================================================
    if (params->data_buf && params->data_len) {
        uint8 data_pipe = (params->dir == UAS_DIR_IN) ? pipe_in : pipe_out;

        normal_transfer_trb(&trb, va_to_pa(params->data_buf), disable_ch, params->data_len, ENABLE_IOC);
        uint64 data_trb_ptr = xhci_ring_enqueue(&usb_dev->eps[data_pipe].transfer_ring, &trb);
        xhci_ring_doorbell(xhci, usb_dev->slot_id, data_pipe);

        // 等待数据传输完成
        completion_code = xhci_wait_for_completion(xhci, data_trb_ptr, 200000); // 5秒超时
        if (completion_code != XHCI_COMP_SUCCESS) {
            // 注意：BOT 协议中，如果数据长度不匹配，设备可能会 STALL 端点
            // 这里应该加入 Clear Stall (Endpoint Halt) 的逻辑
            kfree(cbw);
            return -2;
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

    int32 final_status = -1;

    //如果成功则 校验 CSW 有效性,Tag 匹配
    if (completion_code == XHCI_COMP_SUCCESS && csw->signature == BOT_CSW_SIGNATURE && csw->tag == tag) {
        switch (csw->status) {
            case BOT_CSW_PASS:
                final_status = 0; //成功返回0
                break;
            case BOT_CSW_FAIL:
                final_status = 0x02; // 如果失败 (0x01)，调用者应该发送 REQUEST SENSE 映射为 SCSI Check Condition
                bot_request_sense(bot_data, params);
                break;
            case BOT_CSW_PHASE:
                //相位错误重启设备
                break;
        }
    }

    // 释放资源
    kfree(cbw);
    kfree(csw);

    return final_status;
}

//bot协议获取u盘信息
int bot_send_inquiry(bot_data_t *bot_data, uint8 lun) {
    scsi_sense_t scsi_sense;
    scsi_cdb_inquiry_t scsi_cdb_inquiry = {0};
    scsi_cdb_inquiry.opcode = 0xf0;
    scsi_cdb_inquiry.alloc_len = sizeof(scsi_inquiry_t);
    scsi_inquiry_t *scsi_inquiry = kzalloc(sizeof(scsi_inquiry_t));
    uas_cmd_params_t uas_cmd_params = {&scsi_cdb_inquiry,sizeof(scsi_cdb_inquiry),lun,scsi_inquiry,sizeof(scsi_inquiry_t),UAS_DIR_IN,&scsi_sense};
    bot_send_scsi_cmd_sync(bot_data,&uas_cmd_params);
    kfree(scsi_inquiry);
    return 0;
}
