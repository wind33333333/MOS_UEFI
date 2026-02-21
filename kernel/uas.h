#pragma once
#include "moslib.h"

#pragma pack(push,1)
//UAS Command IU (主机 -> 设备)
typedef struct{
    uint8  iu_id;         // 0x01
    uint8  rsvd0;
    uint16 tag;           // 使用 cpu_to_be16() 赋值
    uint8  prio_attr;     // Bits 2:0 = Attribute (0: Simple, 1: Head of Queue, 2: Ordered)
    uint8  rsvd1;
    uint8  add_cdb_len;   // 只有当 CDB > 16字节时才填 (len-16)/4，否则填 0
    uint8  rsvd2;
    uint64 lun;           // 使用 cpu_to_be64() 赋值
    uint8  scsi_cdb[];    // 存放标准 SCSI 命令
} uas_cmd_iu_t;
#define UAS_CMD_IU_ID       0x01

// UAS Sense IU (0x03) - 设备向主机报告状态
typedef struct {
    uint8  iu_id;        // 0x03
    uint8  rsvd0;
    uint16 tag;          // Big Endian
    uint16 status_qual;  // 状态限定符
    uint8  status;       // SCSI 状态 // 0x00 = GOOD (成功) 0x02 = CHECK_CONDITION (出错，需查看 sense_data) 0x08 = BUSY (忙) 0x18 = RESERVATION_CONFLICT (预留冲突)
    uint8  rsvd1[7];
    uint16 scsi_sense_len; // SCSI Sense Data 的长度 (Big Endian)
    uint8  scsi_sense[];   // 具体的错误信息 (Sense Data)
}uas_sense_iu_t;
#define UAS_SENSE_IU_ID    0x03
#define UAS_SENSE_IU_ALLOC_SIZE  256 // 定义足够大的缓冲区大小 (256字节) 包含 UAS Header (16) + Max SCSI Sense (240+)

#pragma pack(pop)

// UAS (USB Attached SCSI) 专用结构
typedef struct uas_data_t {
    // --- 硬件拓扑 ---
    struct usb_if_t     *usb_if;          // 绑定的 USB 接口

    // --- SCSI / LUN 管理 ---
    struct scsi_device_t *scsi_dev;
    list_head_t         lun_list;       // 挂载的逻辑单元 (LUN) 链表
    uint8               max_lun;        // 最大 LUN 编号

    // --- 管道 (Pipes) ---
    // UAS 分离了命令、状态和数据管道
    uint8            cmd_pipe;       // Bulk OUT (发送 Command IU)
    uint8            status_pipe;    // Bulk IN (接收 Sense IU)
    uint8            data_in_pipe;   // Bulk IN (Read Data)
    uint8            data_out_pipe;  // Bulk OUT (Write Data)
    uint64           tag_bitmap;      // UAS Tag管理,tag号对应stream
} uas_data_t;

struct scsi_task_t;
void uas_send_scsi_cmd_sync(void *dev_context, struct scsi_task_t *task);
