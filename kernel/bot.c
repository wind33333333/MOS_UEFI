#include "bot.h"
#include "errno.h"
#include "printk.h"
#include "usb-core.h"
#include "scsi.h"
#include "slub.h"

/* @brief 执行 Request Sense 命令获取错误详情
 * @return int32 0 表示成功获取 Sense 数据，负数表示 POSIX 错误码
 */
static int32 bot_request_sense(scsi_cmnd_t *cmnd,scsi_sense_t *sense_buf) {
    // 1. 防死循环拦截与空指针校验
    if (cmnd->sense == NULL || *(uint8*)cmnd->scsi_cdb == SCSI_REQUEST_SENSE) {
        // ★ 规范化：使用 -EINVAL (Invalid argument) 代替 -1
        return -EINVAL;
    }


    // 2. 架构提升：直接使用该设备在 Probe 时预分配好的专属抢救内存 (DMA 安全)
    asm_mem_set(sense_buf, 0, SCSI_SENSE_ALLOC_SIZE);

    // 3. 发起请求 (假设底层的 scsi_request_sense 已经正确返回 POSIX 错误码)
    int32 posix_err = scsi_request_sense(cmnd->sdev, sense_buf);

    if (posix_err == 0) {
        // 4. 防御性长度计算
        uint32 copy_len = 8 + sense_buf->add_sense_len;
        if (copy_len > SCSI_SENSE_ALLOC_SIZE) {
            copy_len = SCSI_SENSE_ALLOC_SIZE;
        }

        asm_mem_cpy(sense_buf, cmnd->sense, copy_len);

    } else {
        color_printk(RED, BLACK, "BOT: Failed to fetch Sense Data (%d). Device may be dead.\n", posix_err);

        // ★ 规范化：确保向上层传递的一定是负数的 POSIX 错误码
        // 如果底层的 status 是非 0 的正数（某些怪异的设备返回状态），将其强转为底层 I/O 错误
        if (posix_err > 0) {
            posix_err = -EIO; // EIO: I/O error
        }
    }

    // 无需 kfree，这块内存永远属于 bot_data，随设备拔出才销毁
    return posix_err;
}

/**
 * @brief 获取 BOT 协议设备最大 LUN 数量
 * @return uint8 哪怕失败了，也要做最坏的打算 (返回 1 个 LUN)
 */
uint8 bot_get_max_lun(usb_dev_t *udev, uint8 if_num) {
    uint8 *max_lun = kzalloc_dma(64);
    if (!max_lun) {
        return -ENOMEM; // ★ 物理防御：内存分配失败时，保底认为有 1 个 LUN
    }

    usb_setup_packet_t setup_pkg = {0};
    setup_pkg.recipient = USB_RECIP_INTERFACE;
    setup_pkg.req_type  = USB_REQ_TYPE_CLASS;
    setup_pkg.dtd       = USB_DIR_IN;
    setup_pkg.request   = BOT_REQ_GET_MAX_LUN;
    setup_pkg.value     = 0;
    setup_pkg.index     = if_num;
    setup_pkg.length    = 1;

    // ★ 架构防御：必须捕获错误码！因为 90% 的低端 U 盘会在这里返回 STALL (-EPIPE)
    int32 posix_err = usb_control_msg_sync(udev, &setup_pkg, max_lun);
    uint8 lun_count;
    if (posix_err < 0) {
        // U 盘傲娇抗议 (STALL) 或通信失败，根据 USB 规范，原谅它并默认为 0（加上面的+1后为1个LUN）
        color_printk(YELLOW, BLACK, "BOT: Get Max LUN failed/STALL (%d). Defaulting to 1 LUN.\n", posix_err);
        lun_count = 1;
    } else {
        lun_count = (*max_lun) + 1;
    }

    kfree(max_lun);
    return lun_count;
}


//清除xhci和u盘端点挂起状态
static int32 bot_ep_reset(usb_dev_t *udev,uint8 ep_dci) {
    xhci_hcd_t *xhcd = udev->xhcd;
    uint8 slot_id = udev->slot_id;
    uint32 posix_err = 0;
    posix_err = xhci_cmd_reset_ep(xhcd, slot_id, ep_dci);
    if (posix_err > 0) {
       color_printk(RED,BLACK,"XHCI CMD RESET EP posix_err:%d slot_id:%d ep_dci:%d  \n",posix_err,slot_id,ep_dci);
    }

    posix_err = usb_ep_halt_control(udev, ep_dci,USB_REQ_CLEAR_FEATURE);
    if (posix_err > 0) {
        color_printk(RED,BLACK,"USB EP HALT CONTROL  \n!  \n");
    }
    return 0;
}

/**
 * bot协议u盘重置
 */
static int32 bot_mass_storage_reset(usb_dev_t *udev,uint8 if_num) {

    // 动作 1：发送特定的 0xFF 控制命令，将 U 盘内部状态机重置
    usb_setup_packet_t usb_setup_pkg = {0};
    usb_setup_pkg.recipient = USB_RECIP_INTERFACE;
    usb_setup_pkg.req_type = USB_REQ_TYPE_CLASS;
    usb_setup_pkg.dtd = USB_DIR_OUT;
    usb_setup_pkg.request = BOT_REQ_MASS_STORAGE_RESET;
    usb_setup_pkg.value = 0;
    usb_setup_pkg.index = if_num;
    usb_setup_pkg.length = 0;

    int32 posix_err =usb_control_msg_sync(udev,&usb_setup_pkg,NULL);
    if (posix_err < 0) {
        color_printk(RED,BLACK,"Bot Recovery Reset Fail! \n");
        return posix_err;
    }

    return 0;
}


/**
 * @brief BOT 终极错误恢复完整序列 (The 3-Step Reset Sequence)
 * 当 CBW 发送失败，或 CSW 发生 Phase Error 时调用。
 * 规范强硬要求：0xFF -> 清 IN 端点 -> 清 OUT 端点。
 */
int32 bot_execute_full_recovery(usb_dev_t *udev, uint8 if_num, uint8 in_ep_dci, uint8 out_ep_dci) {
    int32 err;

    // 第一步：砸碎内部状态机 (0xFF)
    err = bot_mass_storage_reset(udev, if_num);
    if (err < 0) return err; // 如果连 EP0 控制通道都断了，这 U 盘可以直接拔了

    // 第二步：解挂 IN 数据管道
    err = bot_ep_reset(udev, in_ep_dci);
    if (err < 0) return err;

    // 第三步：解挂 OUT 数据管道
    err = bot_ep_reset(udev, out_ep_dci);
    if (err < 0) return err;

    color_printk(GREEN, BLACK, "BOT: Full Error Recovery Sequence Completed!\n");
    return 0;
}


/**
 * @brief BOT 协议同步发送引擎 (The Bulk-Only Transport State Machine)
 * 严格遵循 USB Mass Storage Class BOT 规范进行 3 阶段传输与错误抢救。
 */
int32 bot_bulk_transport_sync(scsi_host_t *host, scsi_cmnd_t *cmnd) {
    bot_data_t *bot_data = host->hostdata;
    usb_dev_t *udev = bot_data->uif->udev;
    uint8 if_num = bot_data->uif->if_num;

    usb_ep_t *in_ep = bot_data->in_ep;
    usb_ep_t *out_ep = bot_data->out_ep;

    bot_cbw_t *cbw = bot_data->cbw;
    bot_csw_t *csw = bot_data->csw;
    asm_mem_set(cbw, 0, sizeof(bot_cbw_t));
    asm_mem_set(csw, 0, sizeof(bot_csw_t));

    // 1. 生成全局唯一 Tag
    uint32 tag = ++bot_data->tag;
    int32 posix_err = 0;

    // 动态申请通用 URB 面单 (整个函数生命周期内复用)
    usb_urb_t *urb = usb_alloc_urb();
    if (urb == NULL) {
        return -ENOMEM;
    }

    // ============================================================
    // 🔴 Stage 1: 发送 CBW (Command Block Wrapper)
    // ============================================================
    cbw->signature     = BOT_CBW_SIGNATURE; // "USBC"
    cbw->tag           = tag;
    cbw->data_tran_len = cmnd->data_len;
    cbw->flags         = (cmnd->dir == SCSI_DIR_IN) ? BOT_CBW_DATA_IN : BOT_CBW_DATA_OUT;
    cbw->lun           = cmnd->sdev->lun;
    cbw->scsi_cdb_len  = cmnd->scsi_cdb_len;
    asm_mem_cpy(cmnd->scsi_cdb, cbw->scsi_cdb, cmnd->scsi_cdb_len);

    usb_fill_bulk_urb(urb, udev, out_ep, cbw, sizeof(bot_cbw_t));
    posix_err = usb_submit_urb(urb);
    if (posix_err < 0) goto cleanup;

    while (urb->is_done == FALSE) {
        asm_pause();
    }

    // if (posix_err < 0) {
    //     color_printk(RED, BLACK, "BOT: CBW Failed (%d). Executing Full Recovery...\n", posix_err);
    //     // ⚔️ 错误处理：CBW 失败，主机与设备完全失步，直接呼叫“核弹”全局重置
    //     bot_execute_full_recovery(udev, if_num, in_ep->ep_dci, out_ep->ep_dci);
    //     goto cleanup;
    // }


    // ============================================================
    // 🟡 Stage 2: 数据传输 (Data Stage) - 可选
    // ============================================================
    if (cmnd->data_buf && cmnd->data_len) {
        usb_ep_t *ep = (cmnd->dir == SCSI_DIR_IN) ? in_ep : out_ep;

        usb_fill_bulk_urb(urb, udev, ep, cmnd->data_buf, cmnd->data_len);
        posix_err = usb_submit_urb(urb);
        if (posix_err < 0) goto cleanup;

        while (urb->is_done == FALSE) {
            asm_pause();
        }

        if (posix_err < 0) {
            if (posix_err == -EPIPE) {
                color_printk(YELLOW, BLACK, "BOT Stage 2: Data STALL. Clearing Halt...\n");
                // ⚔️ 错误处理：短包早退是常态。使用“狙击枪”单点疏通 (软硬双解锁)
                bot_ep_reset(udev, ep->ep_dci);

                // 强制放行，必须去拿 CSW 探明死因
                posix_err = 0;
            } else {
                color_printk(RED, BLACK, "BOT Stage 2: Fatal Error (%d).\n", posix_err);
                // ⚔️ 错误处理：非 STALL 的严重错误 (如总线断开)，呼叫“核弹”
                bot_execute_full_recovery(udev, if_num, in_ep->ep_dci, out_ep->ep_dci);
                goto cleanup;
            }
        }
    }

    // ============================================================
    // 🟠 Stage 3: 接收 CSW (Command Status Wrapper)
    // ============================================================
    uint8 csw_retry_count = 0;
retry_csw:
    usb_fill_bulk_urb(urb, udev, in_ep, csw, sizeof(bot_csw_t));
    posix_err = usb_submit_urb(urb);
    if (posix_err < 0) goto cleanup;

    while (urb->is_done == FALSE) {
        asm_pause();
    }


    if (posix_err == -EPIPE && csw_retry_count == 0) {
        color_printk(YELLOW, BLACK, "BOT Stage 3: CSW STALL. Clearing and retrying...\n");
        // ⚔️ 错误处理：第一次读回执失败，给一次机会。必须先开锁，再重试！
        bot_ep_reset(udev, in_ep->ep_dci);
        csw_retry_count++;
        goto retry_csw;
    } else if (posix_err < 0) {
        color_printk(RED, BLACK, "BOT Stage 3: CSW Failed (%d).\n", posix_err);
        // ⚔️ 错误处理：回执死活拿不到 (二次 STALL 或其他错误)，直接呼叫“核弹”
        bot_execute_full_recovery(udev, if_num, in_ep->ep_dci, out_ep->ep_dci);
        goto cleanup;
    }


    // ============================================================
    // 🟢 Stage 4: 解析 CSW 结果
    // ============================================================
    if (csw->signature != BOT_CSW_SIGNATURE || csw->tag != tag) {
        color_printk(RED, BLACK, "BOT: CSW Signature/Tag mismatch! Phase Error.\n");
        // ⚔️ 错误处理：包裹送错人 (Phase Error)，呼叫“核弹”
        bot_execute_full_recovery(udev, if_num, in_ep->ep_dci, out_ep->ep_dci);
        posix_err = -EPROTO;
        goto cleanup;
    }

    // 翻译设备状态码到 SCSI 状态码
    switch (csw->status) {
        case BOT_CSW_PASS:
            cmnd->status = SCSI_STATUS_GOOD;
            break;
        case BOT_CSW_FAIL:
            cmnd->status = SCSI_STATUS_CHECK_CONDITION;
            bot_request_sense(cmnd,bot_data->sense);
            break;
        case BOT_CSW_PHASE:
            color_printk(RED, BLACK, "BOT: Device reported Phase Error.\n");
            // ⚔️ 错误处理：设备主动承认自己大脑错乱，满足它，呼叫“核弹”
            bot_execute_full_recovery(udev, if_num, in_ep->ep_dci, out_ep->ep_dci);
            posix_err = -EILSEQ;
            break;
        default:
            posix_err = -EPROTO;
            break;
    }

cleanup:
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
