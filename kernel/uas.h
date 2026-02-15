#pragma once
#include "moslib.h"
#include "scsi.h"
#include "usb-storage.h"

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
    uint16 len_sense;    // Sense Data 的长度 (Big Endian)
    uint8  scsi_sense[18]; // 具体的错误信息 (Sense Data)
}uas_sense_iu_t;
#define UAS_SENSE_IU_ID    0x03

#pragma pack(pop)

typedef enum {
    UAS_DIR_NONE = 0,
    UAS_DIR_IN=1,
    UAS_DIR_OUT=2
}uas_dir_e;

// 1. 定义事务参数结构体
typedef struct {
    void              *scsi_cdb;
    uint8             scsi_cdb_len;
    uint64            lun;
    void              *data_buf;
    uint32            data_len;
    uas_dir_e         dir;
    scsi_sense_t      *scsi_sense; // 输出参数
} uas_cmd_params_t;


int32 uas_get_capacity(uas_data_t *uas_data, uint8 lun);
int uas_send_inquiry(uas_data_t *uas_data, uint8 lun);
uint32 uas_get_lun_count(uas_data_t *uas_data);
int32 uas_test_unit_ready(uas_data_t *uas_data,uint8 lun);
