#include "bot.h"
#include "printk.h"
#include "usb-core.h"
#include "scsi.h"
#include "slub.h"

/**
 * 执行 Request Sense 命令获取错误详情
 */
int32 bot_request_sense(bot_data_t *bot_data,scsi_cmnd_t *cmnd) {
    if (!cmnd->sense || *(uint8*)cmnd->scsi_cdb == SCSI_REQUEST_SENSE) return -1;

    scsi_sense_t *sense = kzalloc(SCSI_SENSE_ALLOC_SIZE);

    int32 status = scsi_request_sense(bot_data->sdev,sense);

    if (status == 0) {
        asm_mem_cpy(sense,cmnd->sense,8+sense->add_sense_len);
    }else {
        //连获取错误信息都失败了设备可能挂了
    }

    kfree(sense);
    return status;
}

/**
 * BOT 终极错误恢复 (Bulk-Only Mass Storage Reset)
 * 用于应对协议阶段错误或致命的卡死。
 */
void bot_recovery_reset(usb_dev_t *udev,uint8 if_num, uint8 pipe_in, uint8 pipe_out) {
    xhci_hcd_t *xhcd = udev->xhcd;
    uint8 slot_id = udev->slot_id;

    // 动作 1：发送特定的 0xFF 控制命令，将 U 盘内部状态机重置
    usb_req_pkg_t usb_req_pkg = {0};
    usb_req_pkg.recipient = USB_RECIP_INTERFACE;
    usb_req_pkg.req_type = USB_REQ_TYPE_CLASS;
    usb_req_pkg.dtd = USB_DIR_OUT;
    usb_req_pkg.request = BOT_REQ_MASS_STORAGE_RESET;
    usb_req_pkg.value = 0;
    usb_req_pkg.index = if_num;
    usb_req_pkg.length = 0;

    usb_control_msg(udev,&usb_req_pkg,NULL);

    // 动作 2：清理硬件与协议层面的 IN 管道挂起状态
    xhci_cmd_reset_ep(xhcd, slot_id, pipe_in);
    usb_clear_feature_halt(udev, pipe_in);

    // 动作 3：清理硬件与协议层面的 OUT 管道挂起状态
    xhci_cmd_reset_ep(xhcd, slot_id, pipe_out);
    usb_clear_feature_halt(udev, pipe_out);

    color_printk(GREEN, BLACK, "BOT: Reset Request!\n");
}

/**
 * BOT 协议同步发送函数
 * 逻辑：CBW -> Data(可选) -> CSW
 */
void bot_send_scsi_cmd_sync(scsi_host_t *host, scsi_cmnd_t *cmnd) {
    bot_data_t *bot_data =host->hostdata;
    usb_dev_t *udev = bot_data->uif->udev;
    xhci_hcd_t *xhcd = udev->xhcd;
    uint8 slot_id = udev->slot_id;;

    // 管道定义 (BOT 通常只用两个 Bulk 管道)
    uint8 pipe_out = bot_data->pipe_out;
    uint8 pipe_in  = bot_data->pipe_in;

    xhci_trb_t trb;
    int32 comp_code;
    bot_csw_t *csw = kzalloc_dma(sizeof(bot_csw_t));
    bot_cbw_t *cbw = kzalloc_dma(sizeof(bot_cbw_t));

    // 1. 生成 Tag (BOT 的 Tag 只是为了配对 CSW，不用像 UAS 那样管理 Slot)
    uint32 tag = ++bot_data->tag;

    // ============================================================
    // Stage 1: 填充 CBW 发送 CBW (Command Block Wrapper)
    // ============================================================
    cbw->signature = BOT_CBW_SIGNATURE; // "USBC"
    cbw->tag       = tag;
    cbw->data_tran_len = cmnd->data_len;
    // 方向标志: IN(设备->主机) = 0x80, OUT(主机->设备) = 0x00
    cbw->flags    = (cmnd->dir == SCSI_DIR_IN) ? BOT_CBW_DATA_IN : BOT_CBW_DATA_OUT;
    cbw->lun       = cmnd->sdev->lun;
    cbw->scsi_cdb_len  = cmnd->scsi_cdb_len; // 有效 CDB 长度
    asm_mem_cpy(cmnd->scsi_cdb, cbw->scsi_cdb, cmnd->scsi_cdb_len);

    // 提交 TRB 到 Bulk OUT
    trb.raw[0] = 0;
    trb.raw[1] = 0;

    trb.normal.data_buf_ptr = va_to_pa(cbw);
    trb.normal.trb_tr_len = sizeof(bot_cbw_t);
    trb.normal.int_target = 0;
    trb.normal.ioc = TRB_IOC_ENABLE;
    trb.normal.trb_type = XHCI_TRB_TYPE_NORMAL;
    uint64 cbw_trb_ptr = xhci_ring_enqueue(&udev->eps[pipe_out]->transfer_ring, &trb);
    xhci_ring_doorbell(xhcd, slot_id, pipe_out);

    // 等待 CBW 发送完成
    comp_code = xhci_wait_transfer_comp(udev, pipe_out,cbw_trb_ptr); // 2秒超时
    if (comp_code != XHCI_COMP_SUCCESS) {
        color_printk(RED, BLACK, "BOT Stage 1: CBW Failed (%#x). Resetting...\n", comp_code);
        // CBW 阶段出错是极其致命的，直接上“核弹复位”
        bot_recovery_reset(udev, bot_data->uif->if_num,pipe_in, pipe_out);
        cmnd->status = -1;
        goto cleanup;
    }

    // ============================================================
    // Stage 2: 数据传输 (Data Stage) - 可选
    // ============================================================
    if (cmnd->data_buf && cmnd->data_len) {
        uint8 data_pipe = (cmnd->dir == SCSI_DIR_IN) ? pipe_in : pipe_out;

        trb.raw[0] = 0;
        trb.raw[1] = 0;

        trb.normal.data_buf_ptr = va_to_pa(cmnd->data_buf);
        trb.normal.trb_tr_len = cmnd->data_len;
        trb.normal.int_target = 0;
        trb.normal.ioc = TRB_IOC_ENABLE;
        trb.normal.trb_type = XHCI_TRB_TYPE_NORMAL;
        uint64 data_trb_ptr = xhci_ring_enqueue(&udev->eps[data_pipe]->transfer_ring, &trb);
        xhci_ring_doorbell(xhcd, slot_id, data_pipe);

        // 等待数据传输完成
        comp_code = xhci_wait_transfer_comp(udev, data_pipe,data_trb_ptr);
        if (comp_code != XHCI_COMP_SUCCESS) {
            if (comp_code == XHCI_COMP_SHORT_PACKET) {
                // 完美情况：只是短包，无需处理，直接去读 CSW
                color_printk(YELLOW, BLACK, "BOT Stage 2: Short Packet (Normal).\n");

            } else if (comp_code == XHCI_COMP_STALL_ERROR) {
                // 协议级卡死：数据长度不匹配引发的 STALL
                color_printk(YELLOW, BLACK, "BOT Stage 2: STALL. Clearing Halt...\n");
                //xhci_recover_stalled_endpoint(udev,data_pipe);

            } else {
                // 物理链路错误 (如 TX_ERR) 或者 DMA 错误
                color_printk(RED, BLACK, "BOT Stage 2: Fatal Data Error (%#x).\n", comp_code);
                bot_recovery_reset(udev, bot_data->uif->if_num,pipe_in, pipe_out);
                cmnd->status = -2;
                goto cleanup;
            }
        }
    }

    // ============================================================
    // Stage 3: 接收 CSW (Command Status Wrapper)
    // ============================================================
    uint8 csw_retry_count = 0;
retry_csw:
    // 提交 TRB 到 Bulk IN (不管刚才数据是读是写，CSW 永远是读)
    trb.raw[0] = 0;
    trb.raw[1] = 0;

    trb.normal.data_buf_ptr = va_to_pa(csw);
    trb.normal.trb_tr_len = sizeof(bot_csw_t);
    trb.normal.int_target = 0;
    trb.normal.ioc = TRB_IOC_ENABLE;
    trb.normal.trb_type = XHCI_TRB_TYPE_NORMAL;
    uint64 csw_trb_ptr = xhci_ring_enqueue(&udev->eps[pipe_in]->transfer_ring, &trb);
    xhci_ring_doorbell(xhcd, slot_id, pipe_in);

    // 等待 CSW 接收完成
    comp_code = xhci_wait_transfer_comp(udev, pipe_in,csw_trb_ptr);

    // 异常 1: 请求 CSW 时发生 STALL (有些 U 盘会在发 CSW 前莫名其妙再卡一次)
    if (comp_code == XHCI_COMP_STALL_ERROR && csw_retry_count == 0) {
        color_printk(YELLOW, BLACK, "BOT Stage 3: CSW STALL. Clearing and retrying...\n");
        //xhci_recover_stalled_endpoint(udev, pipe_in);
        csw_retry_count++;
        goto retry_csw; // 撬开门，重试一次 CSW
    }
    // 异常 2: 硬件死机、短包、或者重试后依旧报错
    else if (comp_code != XHCI_COMP_SUCCESS) {
        color_printk(RED, BLACK, "BOT Stage 3: CSW Fetch Failed (%#x). Resetting...\n", comp_code);
        bot_recovery_reset(udev, bot_data->uif->if_num,pipe_in, pipe_out);
        cmnd->status = -3;
        goto cleanup;
    }

    // ============================================================
    // Stage 4: 解析 CSW 结果
    // ============================================================
    // 校验签名和 Tag 是否防伪串线
    if (csw->signature != BOT_CSW_SIGNATURE || csw->tag != tag) {
        color_printk(RED, BLACK, "BOT: CSW Signature/Tag Mismatch! Phase Error.\n");
        bot_recovery_reset(udev, bot_data->uif->if_num,pipe_in, pipe_out);
        cmnd->status = -4;
        goto cleanup;
    }

    // 解析状态机
    switch (csw->status) {
        case BOT_CSW_PASS:       // 0x00
            cmnd->status = SCSI_STATUS_GOOD; // 0
            break;

        case BOT_CSW_FAIL:       // 0x01
            cmnd->status = SCSI_STATUS_CHECK_CONDITION; // 0x02
            // 自动发请求获取错误详情
            bot_request_sense(bot_data, cmnd);
            break;

        case BOT_CSW_PHASE:      // 0x02
            color_printk(RED, BLACK, "BOT: CSW Reported Phase Error! Resetting...\n");
            bot_recovery_reset(udev, bot_data->uif->if_num,pipe_in, pipe_out);
            cmnd->status = -5;
            break;

        default:
            cmnd->status = -6;
            break;
    }

cleanup:
    // 释放资源
    kfree(cbw);
    kfree(csw);

    return;
}


//uas协议模板
scsi_host_template_t bot_host_template = {
    .name = "bot",
    .queue_command = bot_send_scsi_cmd_sync,
    .reset_host = NULL,
    .abort_command = NULL,
};
