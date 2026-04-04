#include  "uas.h"
#include "printk.h"
#include  "usb-core.h"
#include "scsi.h"
#include "errno.h"



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
 * @brief 泛型化 UAS 槽位分配器 (支持 Command IU 与 Task Management IU)
 * @param req_out  泛型输出：可以接收 uas_cmd_iu_t** 或 uas_tm_iu_t**
 * @param resp_out 泛型输出：可以接收 uas_sense_iu_t** 或 uas_response_iu_t**
 */
static int32 uas_alloc_request(
    uas_data_t *uas_data,
    void **req_out,
    void **resp_out,
    uint16 *tag_out,
    uint16 *stream_id_out)
{
    // 1. 极速扫描 Bitmap 寻找空闲 Tag (内部数组索引从 0 开始)
    uint16 tag = uas_alloc_tag(uas_data);

    // 2. ★ POSIX 修正：硬件队列防满拦截
    if (tag > uas_data->max_streams) {
        return -EBUSY;
    }

    // 3. O(1) 极速寻址获取物理图纸 (内部使用 0-based 索引)
    uas_cmd_iu_t   *cmd   = &uas_data->cmd_iu_pool[tag];
    uas_sense_iu_t *sense = &uas_data->sense_iu_pool[tag];

    // 4. ★ 架构师核心防御：最大化内存抹除
    // 不管外层是申请小巧的 TM_IU 还是庞大的 CMD_IU，底层统统按照最大尺寸 (cmd/sense) 清空。
    // 这绝对保证了即使强转为 TM_IU，其尾部的残留内存也是 0，绝不会干扰总线。
    asm_mem_set(cmd, 0, sizeof(uas_cmd_iu_t));
    asm_mem_set(sense, 0, sizeof(uas_sense_iu_t));

    // 5. 将图纸交接给上层调用者
    // UAS 规范要求 Task Tag 不能为 0，所以对外暴露的真实 Tag 必须 +1
    ++tag;

    // ★ 泛型指针交接：外层传什么类型的指针地址，这里就塞入什么
    if (req_out)  *req_out  = cmd;
    if (resp_out) *resp_out = sense;
    if (tag_out) *tag_out = tag;

    // 只有在 Bulk Endpoint 支持 Streams 特性时，Stream ID 才生效并与 Tag 对齐绑定；否则为 0
    if (stream_id_out) {
        *stream_id_out = (uas_data->max_streams == 0) ? 0 : tag;
    }

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
 * @brief 执行 UAS 任务中止 (精准狙击僵尸任务)
 * @param host        SCSI 主机上下文
 * @param scsi_lun    发生错误的逻辑单元号
 * @param target_tag  需要被中止的死亡任务的 Tag (0~63)
 * @return int32      0 表示击杀成功，负数表示底层崩溃
 */
int32 uas_abort_task(scsi_host_t *host, uint64 scsi_lun, uint16 target_tag) {
    uas_data_t *uas_data = host->hostdata;
    usb_dev_t *udev = uas_data->uif->udev;
    int32 posix_err = 0;

    uint16 tag, stream_id;
    uas_tm_iu_t *tm_iu ;
    uas_response_iu_t *resp_iu;

    // 1. 为我们的“杀手任务”也去申请一套合法的 Tag 和并发槽位
    posix_err = uas_alloc_request(uas_data, (void **)&tm_iu, (void **)&resp_iu, &tag, &stream_id);
    if (posix_err < 0) {
        return posix_err; // 槽位耗尽，连发 TMF 的资源都没了
    }

    // 2. 填写暗杀名单 (TM IU 填充，严格大端序)
    asm_mem_set(tm_iu, 0, sizeof(uas_tm_iu_t));
    tm_iu->iu_id = UAS_TASK_MGMT_IU_ID; // 0x02 = Task Management IU
    tm_iu->tag = asm_bswap16(tag);
    tm_iu->tm_function = UAS_TMF_ABORT_TASK;
    tm_iu->task_tag = asm_bswap16(target_tag); // ★ 告诉固件：杀掉它！
    tm_iu->lun = asm_bswap64(scsi_lun);

    // 3. 构建 URB 监听组 (TMF 只需要 Status 和 Command 两个管子)
    usb_urb_t *urb_status = usb_alloc_urb();
    usb_urb_t *urb_cmd    = usb_alloc_urb();
    usb_urb_t *urb_arr[2] = {urb_status, urb_cmd};

    if (!urb_status || !urb_cmd) {
        posix_err = -ENOMEM;
        goto cleanup;
    }

    // [Step A] 提前放置 Status 哨兵，等 U 盘的回执
    usb_fill_bulk_urb(urb_status, udev, uas_data->status_ep, resp_iu, sizeof(uas_response_iu_t));
    urb_status->stream_id = stream_id; // TMF 也是走流并发的
    posix_err = usb_submit_urb(urb_status);
    if (posix_err < 0) goto cleanup;

    // [Step B] 投递 TMF 暗杀令
    usb_fill_bulk_urb(urb_cmd, udev, uas_data->cmd_ep, tm_iu, sizeof(uas_tm_iu_t));
    urb_cmd->stream_id = 0; // Command 管道规范要求流 ID 填 0
    urb_cmd->transfer_flags |= URB_NO_INTERRUPT; // 静音传输
    posix_err = usb_submit_urb(urb_cmd);

    if (posix_err < 0) {
        // TMF 发送失败，必须把前面的 Status 哨兵也撤回来
        xhci_cmd_stop_ep(udev->xhcd, udev->slot_id, uas_data->status_ep->ep_dci);
        goto cleanup;
    }

    // ============================================================
    // 4. 等待 U 盘执行处决
    // ============================================================
    posix_err = xhci_wait_urb_group(udev, urb_arr, 2);

    if (posix_err < 0) {
        // TMF 自己在传输时都崩了，说明 U 盘内部状态机彻底烂掉，不接受任何指令。
        // 猛踩刹车撤回 TMF，并准备向上层汇报最坏的消息
        color_printk(RED, BLACK, "UAS: TMF transfer completely failed (%d)!\n", posix_err);
        xhci_cmd_stop_ep(udev->xhcd, udev->slot_id, uas_data->status_ep->ep_dci);
        xhci_cmd_stop_ep(udev->xhcd, udev->slot_id, uas_data->cmd_ep->ep_dci);
        goto cleanup;
    }

    // ============================================================
    // 5. 检查 U 盘主控的回执确认单
    // ============================================================
    if (resp_iu->response_code == 0x00) {
        // 0x00 成功，僵尸已被清理
        color_printk(GREEN, BLACK, "UAS: Successfully aborted zombie Task %d\n", target_tag);
    } else if (resp_iu->response_code == 0x04) {
        // 0x04 任务不存在。这也是好消息，说明僵尸自己死透了，达到了我们要的目的
        color_printk(YELLOW, BLACK, "UAS: Task %d already dead/not found\n", target_tag);
    } else {
        // 其他回复（如 0x05 非法 LUN，或 U 盘拒绝处理）
        color_printk(RED, BLACK, "UAS: Abort Task rejected, Response Code: 0x%02x\n", resp_iu->response_code);
        posix_err = -EIO; // 升维为不可逆 I/O 错误
    }

cleanup:
    // 回收资源
    if (urb_status) usb_free_urb(urb_status);
    if (urb_cmd)    usb_free_urb(urb_cmd);

    // 把“杀手任务”的 tag 还给 UAS 并发槽
    uas_free_request(uas_data, tag);

    return posix_err;
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
    posix_err = uas_alloc_request(uas_data, (void **)&cmd_iu, (void **)&sense_iu, &tag, &stream_id);
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
    usb_urb_t *urb_arr[3] = {urb_status, urb_cmd,urb_data};
    uint8 urb_arr_count = 2;

    if (urb_status == NULL || urb_cmd == NULL) {
        posix_err = -ENOMEM;
        goto cleanup;
    }

    if (cmnd->data_buf && cmnd->data_len) {
        urb_data = usb_alloc_urb();
        urb_arr[2] = urb_data;
        urb_arr_count++;
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
    // 5. 挂起等待
    // ============================================================
    posix_err = xhci_wait_urb_group(udev,urb_arr,urb_arr_count);
    if (posix_err < 0) {
        color_printk(RED, BLACK, "UAS: Bus transfer failed (%d). Initiating Error Handling...\n", posix_err);
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
