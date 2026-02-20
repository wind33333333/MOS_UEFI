#pragma once
#include "moslib.h"
#include "scsi.h"
#include "usb-storage.h"

#pragma pack(push,1)

typedef struct {
    uint32   signature;    // [0-3] 签名标志 (Signature)固定值：0x43425355 (小端序 ASCII = "USBC")
    uint32   tag;          // [4-7] 标签 (Tag)由主机生成的唯一 ID。设备在返回 CSW 时必须原样返回这个值。用于将命令和状态回执配对。
    uint32   data_tran_len; // [8-11] 数据传输长度 (Data Transfer Length)主机期望在数据阶段传输的字节数。如果是 0，表示没有数据阶段（如 TEST UNIT READY）。
    uint8    flags;         // [12] 标志位 (Flags) Bit 7: 数据传输方向 (0 = OUT 主机到设备, 1 = IN 设备到主机)
    uint8    lun;           // [13] 逻辑单元号 (LUN) Bit 0-3: LUN ID, Bit 4-7: 保留
    uint8    scsi_cdb_len;  // [14] CDB 长度 (CDB Length)有效的 SCSI 命令块长度
    uint8    scsi_cdb[16];    //命令块 (Command Block / CDB)
}bot_cbw_t;
#define BOT_CBW_SIGNATURE 0x43425355
#define BOT_CBW_DATA_IN 0x80
#define BOT_CBW_DATA_OUT 0x0

typedef struct {
    uint32 signature; // [0-3] 签名标志 (Signature)固定值：0x53425355 (小端序 ASCII = "USBS")
    uint32 tag; // [4-7] 标签 (Tag)必须与对应的 CBW 中的 dCBWTag 完全一致。主机通过这个值确认这是哪条命令的回执。
    uint32 data_residue; // 未传输的数据长度 如果传输成功且数据量符合预期，这里通常是 0,如果主机想读 512 字节，设备只发了 500 字节，这里就是 12。
    uint8  status;// 命令状态（0=Command Passed成功，1=Command Failed失败主机通常需要发送 Request Sense获取详情 ,2=Phase Error相位错误通常需要复位设备）
}bot_csw_t;
#define BOT_CSW_SIGNATURE 0x53425355

// BOT 状态码定义
#define BOT_CSW_PASS  0x00
#define BOT_CSW_FAIL  0x01
#define BOT_CSW_PHASE 0x02

#define BOT_SENSE_ALLOC_SIZE 255

#pragma pack(pop)


int bot_send_inquiry(bot_data_t *bot_data, uint8 lun);