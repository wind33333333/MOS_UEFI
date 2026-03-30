#pragma once
#include "moslib.h"
#include "usb-core.h"

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
    uint8  scsi_cdb[16];    // 存放标准 SCSI 命令
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
    uint8  scsi_sense[252];   // 具体的错误信息 (Sense Data)
}__attribute__((aligned(512))) uas_sense_iu_t;
#define UAS_SENSE_IU_ID    0x03
#define UAS_MAX_SENSE_LEN 268

// UAS 任务管理信息单元 (Task Management IU  0x02)
typedef struct uas_task_mgmt_iu_t{
    uint8   iu_id;          // 固定为 0x02 (代表这是一个 Task Management 包)
    uint8   reserved0;      // 保留位
    uint16  tag;            // 【自身的编号】这个复位任务本身的 Tag 编号 (如 0x0099)
    uint8   function;       // 【核心：你要执行什么手术？】Task Management Function (TMF)
    uint8   reserved1;      // 保留位
    uint16  task_tag;       // 【目标：你要干掉谁？】你要中止的那个倒霉命令的 Tag 编号
    uint64  lun;            // 目标逻辑单元号 (8 字节的 SCSI LUN 格式)
}uas_task_mgmt_iu_t;
#define UAS_TASK_MGMT_IU_ID    0x02
// ============================================================================
// UAS Task Management Functions (TMF) / 任务管理功能码
// 适用于 Task Management IU 的 function 字段，用于实现精确的并发异常控制
// ============================================================================
#define UAS_TMF_ABORT_TASK               0x01  // 中止特定任务 (精准狙杀某个卡死的 Tag，不影响其他并发命令)
#define UAS_TMF_ABORT_TASK_SET           0x02  // 中止任务集 (清空当前 LUN 的所有未完成命令，适用于强制 umount)
#define UAS_TMF_LOGICAL_UNIT_RESET       0x08  // 逻辑单元复位 (仅复位指定的 LUN，不影响设备上的其他 LUN)
#define UAS_TMF_IT_NEXUS_RESET           0x10  // I_T 连接复位 (Initiator-Target 软重启，比物理拔插优雅得多)

#pragma pack(pop)

// UAS (USB Attached SCSI) 专用结构
typedef struct uas_data_t {
    // --- 硬件拓扑 ---
    struct usb_if_t     *uif;          // 绑定的 USB 接口

    // --- SCSI / LUN 管理 ---
    struct scsi_device_t *sdev;
    list_head_t         lun_list;       // 挂载的逻辑单元 (LUN) 链表
    uint8               max_lun;        // 最大 LUN 编号

    // UAS 命令、状态和数据
    usb_ep_t         *cmd_ep;       // Bulk OUT (发送 Command IU)
    usb_ep_t         *status_ep;    // Bulk IN (接收 Sense IU)
    usb_ep_t         *data_in_ep;   // Bulk IN (Read Data)
    usb_ep_t         *data_out_ep;  // Bulk OUT (Write Data)
    uas_cmd_iu_t     *cmd_iu_pool;
    uas_sense_iu_t   *sense_iu_pool;
    uint64           tag_bitmap;      // UAS Tag管理,tag号对应stream
    uint32           max_streams;
} uas_data_t;

