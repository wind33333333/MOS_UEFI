#pragma once
#include "moslib.h"
#pragma pack(push,1)

// 1. SCSI INQUIRY CDB (6字节标准)
typedef struct {
    uint8 opcode;       // 0x12
    uint8 evpd;         // Enable Vital Product Data (通常为0)
    uint8 page_code;    // (通常为0)
    uint16 alloc_len;   // Big Endian: 分配长度 (通常 36 字节)
    uint8 control;      // 0x00
}scsi_cdb_inquiry_t;
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
} inquiry_data_t;

// 1. SCSI REPORT LUNS CDB (12 字节)
typedef struct {
    uint8_t opcode;          // 0xA0
    uint8_t rsvd1;
    uint8_t select_report;   // 0x00 = 报告所有 LUN (通常用这个)
    uint8_t rsvd2[3];
    uint32_t alloc_len;      // Big Endian: 分配的数据长度
    uint8_t rsvd3;
    uint8_t control;         // 0x00
}scsi_cdb_report_luns_t;

// 2. 返回的数据头部格式
// SCSI 返回的数据是一个列表，前 8 字节是头
typedef struct {
    uint32_t lun_list_length; // Big Endian: LUN 列表的字节总长 (不包含这4个字节)
    uint32_t rsvd;
    // 后面跟着一个数组：uint64_t lun_list[];
} PACKED scsi_report_luns_data_header_t;

#pragma pack(one)