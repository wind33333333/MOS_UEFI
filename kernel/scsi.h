#pragma once
#include "moslib.h"
#pragma pack(push,1)

// 1. SCSI INQUIRY CDB (6字节标准)
typedef struct{
    uint8 opcode;       // [0] 0x12
    uint8 flags;        // [1] Bit0=EVPD
    uint8 page_code;    // [2]
    uint8 reserved;     // [3] 必须为 0
    uint8 alloc_len;    // [4] 分配长度 (最大 255)
    uint8 control;      // [5] 通常为 0
} scsi_cdb_inquiry_t;
#define SCSI_INQUIRY 0x12

//inquiry返回数据36字节
typedef struct {
    uint8 device_type; // byte 0: Peripheral Device Type (0x00 = Block Device)
    uint8 rmb; // byte 1: Bit 7 = RMB (1 = Removable)
    uint8 version; // byte 2: SCSI 版本
    uint8 response_format; // byte 3: 响应格式（通常 2）
    uint8 additional_len; // byte 4: 附加数据长度（通常 31）
    uint8 reserved[3]; // byte 5-7
    char  vendor_id[8]; // byte 8-15: 厂商 (ASCII)
    char  product_id[16]; // byte 16-31: 产品型号 (ASCII)
    char  revision[4]; // byte 32-35: 固件版本 (ASCII)
} scsi_inquiry_data_t;

// 1. SCSI REPORT LUNS CDB (12 字节)
typedef struct {
    uint8 opcode;          // 0xA0
    uint8 rsvd1;
    uint8 select_report;   // 0x00 = 报告所有 LUN (通常用这个)
    uint8 rsvd2[3];
    uint32 alloc_len;      // Big Endian: 分配的数据长度
    uint8 rsvd3;
    uint8 control;         // 0x00
}scsi_cdb_report_luns_t;
#define SCSI_REPORT_LUNS 0xA0

// 2. 返回的数据头部格式
// SCSI 返回的数据是一个列表，前 8 字节是头
typedef struct {
    uint32 lun_list_length; // Big Endian: LUN 列表的字节总长 (不包含这4个字节)
    uint32 rsvd;
    // 后面跟着一个数组：uint64_t lun_list[];
}scsi_report_luns_data_header_t;

// 1. 命令包定义 (CDB) - 10 字节
typedef struct __attribute__((packed)) {
    uint8  opcode;      // 0x25
    uint8  rsvd1;       // Reserved / RelAddr
    uint32 lba;         // Logical Block Address (通常填 0)
    uint16 rsvd2;       // Reserved
    uint8  pmi;         // Partial Medium Indicator (通常填 0)
    uint8  control;     // Control (通常填 0)
} scsi_read_capacity10_cdb_t;
#define SCSI_READ_CAPACITY10 0x25

// 2. 返回数据定义 - 8 字节
typedef struct __attribute__((packed)) {
    uint32 max_lba_be;     // Max Logical Block Address (Big Endian)
    uint32 block_size_be;  // Block Length in Bytes (Big Endian)
} scsi_read_capacity10_data_t;

#pragma pack(one)