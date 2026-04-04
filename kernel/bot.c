#include "bot.h"

#include "errno.h"
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
    usb_setup_packet_t usb_setup_pkg = {0};
    usb_setup_pkg.recipient = USB_RECIP_INTERFACE;
    usb_setup_pkg.req_type = USB_REQ_TYPE_CLASS;
    usb_setup_pkg.dtd = USB_DIR_OUT;
    usb_setup_pkg.request = BOT_REQ_MASS_STORAGE_RESET;
    usb_setup_pkg.value = 0;
    usb_setup_pkg.index = if_num;
    usb_setup_pkg.length = 0;

    usb_control_msg_sync(udev,&usb_setup_pkg,NULL);

    // 动作 2：清理硬件与协议层面的 IN 管道挂起状态
    xhci_cmd_reset_ep(xhcd, slot_id, pipe_in);
    usb_control_feature_halt(udev, pipe_in,USB_REQ_CLEAR_FEATURE);

    // 动作 3：清理硬件与协议层面的 OUT 管道挂起状态
    xhci_cmd_reset_ep(xhcd, slot_id, pipe_out);
    usb_control_feature_halt(udev, pipe_out,USB_REQ_CLEAR_FEATURE);

    color_printk(GREEN, BLACK, "BOT: Reset Request!\n");
}


/**
 * BOT 协议同步发送函数
 * 逻辑：CBW -> Data(可选) -> CSW
 */
int32 bot_bulk_transport_sync(scsi_host_t *host, scsi_cmnd_t *cmnd) {
    bot_data_t *bot_data = host->hostdata;
    usb_dev_t *udev = bot_data->uif->udev;

    usb_ep_t *in_ep = bot_data->in_ep;
    usb_ep_t *out_ep = bot_data->out_ep;

    bot_cbw_t *cbw = bot_data->cbw;
    bot_csw_t *csw = bot_data->csw;
    asm_mem_set(bot_data->cbw, 0, sizeof(bot_cbw_t));
    asm_mem_set(bot_data->csw, 0, sizeof(bot_csw_t));

    // 1. 生成 Tag
    uint32 tag = ++bot_data->tag;
    int32 posix_err;

    // 👑 动态申请通用 URB 面单 (整个函数生命周期内复用)
    usb_urb_t *urb = usb_alloc_urb();
    if (urb == NULL) {
        return -ENOMEM;
    }

    // ============================================================
    // Stage 1: 发送 CBW (Command Block Wrapper)
    // ============================================================
    cbw->signature = BOT_CBW_SIGNATURE; // "USBC"
    cbw->tag       = tag;
    cbw->data_tran_len = cmnd->data_len;
    cbw->flags     = (cmnd->dir == SCSI_DIR_IN) ? BOT_CBW_DATA_IN : BOT_CBW_DATA_OUT;
    cbw->lun       = cmnd->sdev->lun;
    cbw->scsi_cdb_len  = cmnd->scsi_cdb_len;
    asm_mem_cpy(cmnd->scsi_cdb, cbw->scsi_cdb, cmnd->scsi_cdb_len);

    // 👑 填单并发车：使用内联助手自动打包参数
    usb_fill_bulk_urb(urb, udev, out_ep, cbw, sizeof(bot_cbw_t));
    posix_err = usb_submit_urb(urb);
    if (posix_err < 0) {
        goto cleanup;
    }

    // 等待 CBW 发送完成 (利用 URB 回填的 last_trb_pa)
    posix_err = xhci_wait_urb_group(udev,&urb,1);
    if (posix_err < 0) {
        bot_recovery_reset(udev, bot_data->uif->if_num, in_ep->ep_dci, out_ep->ep_dci);
        goto cleanup;
    }

    // ============================================================
    // Stage 2: 数据传输 (Data Stage) - 可选
    // ============================================================
    if (cmnd->data_buf && cmnd->data_len) {
        usb_ep_t *ep = (cmnd->dir == SCSI_DIR_IN) ? in_ep : out_ep;

        // 👑 原地复用 URB，重新填单，之前的 flag 和状态会被安全重置
        usb_fill_bulk_urb(urb, udev, ep, cmnd->data_buf, cmnd->data_len);
        posix_err = usb_submit_urb(urb);
        if (posix_err < 0) {
            goto cleanup;
        }

        posix_err= xhci_wait_urb_group(udev,&urb,1);
        if (posix_err < 0) {
            if (posix_err == -EPIPE) {
                color_printk(YELLOW, BLACK, "BOT Stage 2: STALL. Clearing Halt...\n");
                // ★ 核心修复 1：必须真刀真枪地去清除硬件 STALL 状态！
                // 不然端点一直处于 Halted 状态，Stage 3 绝对会失败！
                usb_control_feature_halt(udev, ep->ep_dci,USB_REQ_CLEAR_FEATURE);

                // STALL 并不意味着整个 SCSI 流程终结，强行放行，去拿 CSW 看具体死因
                posix_err = 0;
            } else {
                bot_recovery_reset(udev, bot_data->uif->if_num, in_ep->ep_dci, out_ep->ep_dci);
                goto cleanup;
            }
        }
    }

    // ============================================================
    // Stage 3: 接收 CSW (Command Status Wrapper)
    // ============================================================
    uint8 csw_retry_count = 0;
retry_csw:
    // 👑 再次复用 URB，一键下达接收指令
    usb_fill_bulk_urb(urb, udev, in_ep, csw, sizeof(bot_csw_t));
    posix_err = usb_submit_urb(urb);
    if (posix_err < 0) {
        goto cleanup;
    }

    posix_err = xhci_wait_urb_group(udev,&urb,1);

    if (posix_err == -EPIPE && csw_retry_count == 0) {
        color_printk(YELLOW, BLACK, "BOT Stage 3: CSW STALL. Clearing and retrying...\n");
        csw_retry_count++;
        goto retry_csw;
    }else if (posix_err < 0) {
        bot_recovery_reset(udev, bot_data->uif->if_num, in_ep->ep_dci, out_ep->ep_dci);
        goto cleanup;
    }

    // ============================================================
    // Stage 4: 解析 CSW 结果 (在这里才终于动用 cmnd->status！)
    // ============================================================
    if (csw->signature != BOT_CSW_SIGNATURE || csw->tag != tag) {
        bot_recovery_reset(udev, bot_data->uif->if_num, in_ep->ep_dci, out_ep->ep_dci);
        posix_err = -EPROTO; // 签名不对，依然归类为底层的协议错误
        goto cleanup;
    }

    // 👑 只有大巴车完美抵达，包裹也没破损，我们才拆开包裹，看里面的回执！
    switch (csw->status) {
        case BOT_CSW_PASS:
            cmnd->status = SCSI_STATUS_GOOD;
            break;
        case BOT_CSW_FAIL:
            cmnd->status = SCSI_STATUS_CHECK_CONDITION;
            // （注意：标准的内核中，获取 Sense 通常由外层的 SCSI 错误处理线程来做，不在此处阻塞调用）
            // bot_request_sense(bot_data, cmnd);
            break;
        case BOT_CSW_PHASE:
            bot_recovery_reset(udev, bot_data->uif->if_num, in_ep->ep_dci, out_ep->ep_dci);
            posix_err = -EILSEQ;
            break;
        default:
            posix_err = -EPROTO;
            break;
    }

cleanup:
    // 👑 过河拆桥：无论是成功还是报错跳到这里，统一销毁动态申请的 URB 面单
    if (urb != NULL) usb_free_urb(urb);
    return posix_err;
}


//uas协议模板
scsi_host_template_t bot_host_template = {
    .name = "bot",
    .max_sectors = 256, //bot协议一次传输128kb 256*512字节
    .queue_command = bot_bulk_transport_sync,
    .reset_host = NULL,
    .abort_command = NULL,
};
