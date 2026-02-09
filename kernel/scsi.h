#pragma once
#include "moslib.h"
#pragma pack(push,1)

// SCSI TEST UNIT READY Opcode
#define SCSI_TEST_UNIT_READY  0x00



// 常用 Sense Key 定义
#define SK_NO_SENSE         0x00
#define SK_RECOVERED_ERROR  0x01
#define SK_NOT_READY        0x02
#define SK_MEDIUM_ERROR     0x03
#define SK_HARDWARE_ERROR   0x04
#define SK_ILLEGAL_REQUEST  0x05
#define SK_UNIT_ATTENTION   0x06
#define SK_DATA_PROTECT     0x07
#define SK_BLANK_CHECK      0x08
#define SK_ABORTED_COMMAND  0x0B
typedef struct {
    // [Byte 0] 响应代码 (Response Code)
    // ------------------------------------------------------------
    // Bit 7 (Valid):
    //     1 = [Byte 3-6] Information 字段包含有效数据 (如出错的 LBA)
    //     0 = [Byte 3-6] 无效，应忽略
    // Bit 0-6 (Error Code):
    //     0x70 = Current Error (当前命令产生的错误)
    //     0x71 = Deferred Error (之前命令遗留的错误，如缓存写入失败)
    uint8 response_code;

    // [Byte 1] 段号 (Segment Number)
    // ------------------------------------------------------------
    // 废弃字段 (Obsolete)，在现代 SCSI/UAS 设备中通常恒为 0
    uint8 segment_number;

    // [Byte 2] 标志位与核心错误类 (Sense Key)
    // ------------------------------------------------------------
    // Bit 7 (Filemark):  磁带设备专用，磁盘/U盘为 0
    // Bit 6 (EOM):       End of Medium，磁带/打印机专用，U盘为 0
    // Bit 5 (ILI):       Incorrect Length Indicator (长度错误指示)
    //                    1 = 实际传输数据块长度与请求不符 (如读到了文件末尾)
    // Bit 4:             Reserved (保留)
    // Bit 0-3 (SENSE KEY): 核心错误分类 【关键】
    //     0x0 = NO SENSE (无错误)
    //     0x1 = RECOVERED ERROR (成功但有纠错，如重试后读取成功)
    //     0x2 = NOT READY (未就绪，如无盘、正在启动)
    //     0x3 = MEDIUM ERROR (介质错误，如坏块、校验失败)
    //     0x4 = HARDWARE ERROR (硬件故障，如机械臂坏、电压异常)
    //     0x5 = ILLEGAL REQUEST (非法请求，如参数不支持、CDB 错误)
    //     0x6 = UNIT ATTENTION (单元注意，如刚上电、被复位、介质更换)
    //     0x7 = DATA PROTECT (写保护)
    //     0xB = ABORTED COMMAND (命令被中止)
    uint8 flags_key;

    // [Byte 3-6] 信息字段 (Information) - 【大端序 Big Endian】
    // ------------------------------------------------------------
    // 仅当 [Byte 0] 的 Valid 位为 1 时有效。
    // 通常存放：
    //     1. 出错的逻辑块地址 (LBA)，用于告知是哪个扇区坏了。
    //     2. 剩余未传输的字节数 (当 ILI 位为 1 时)。
    uint32 information;

    // [Byte 7] 附加感测长度 (Additional Sense Length)
    // ------------------------------------------------------------
    // 指示本字节之后还有多少字节的数据。
    // 固定格式通常为 10 (0x0A)，表示总长度 = 7 + 1 + 10 = 18 字节。
    // 驱动程序利用此字段判断是否接收到了完整的 Sense Data。
    uint8 add_sense_len;

    // [Byte 8-11] 命令相关信息 (Command Specific Information) - 【大端序】
    // ------------------------------------------------------------
    // 视具体命令而定。
    // 对于 EXTENDED COPY 等命令，指向参数列表；普通读写通常为 0。
    uint32 cmd_specific_info;

    // [Byte 12] 附加感测码 (ASC - Additional Sense Code)
    // ------------------------------------------------------------
    // 【核心错误子码】 详细描述错误原因。
    // 例如：Key=06 时，ASC=29 表示 "Power On/Reset" (复位)。
    uint8 asc;

    // [Byte 13] 附加感测码限定符 (ASCQ - Additional Sense Code Qualifier)
    // ------------------------------------------------------------
    // 【核心错误详情码】 进一步细化 ASC。
    // 组合示例：
    //     ASC=29, ASCQ=00: 已发生电源开启、复位或总线设备复位。
    //     ASC=3A, ASCQ=00: Medium not present (没插盘)
    uint8 ascq;

    // [Byte 14] 现场可替换单元代码 (FRU Code)
    // ------------------------------------------------------------
    // 指示哪个硬件部件坏了 (Field Replaceable Unit)。
    // 仅高端存储阵列使用，普通 U 盘通常为 0。
    uint8 fru_code;

    // [Byte 15-17] 特定感测键信息 (SKS - Sense Key Specific)
    // ------------------------------------------------------------
    // 这是一个 3 字节的复合字段，含义取决于 Sense Key。
    //
    // [Byte 15] Bit 7 (SKSV): Valid 位。
    //     1 = SKS 字段有效
    //     0 = SKS 字段无效
    //
    // 如果 SKSV=1 且 Key=0x05 (ILLEGAL REQUEST):
    //     表示哪个参数错了。
    //     Byte 15 Bit 6 (C/D): 1=CDB错, 0=Data错。
    //     Byte 16-17: 错误字节的偏移量 (Field Pointer)。
    //
    // 如果 SKSV=1 且 Key=0x02 (NOT READY - Formatting):
    //     表示进度条。
    //     Byte 16-17: 进度计数 (0~65535)。
    uint8 sks[3];
} scsi_sense_data_t;

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