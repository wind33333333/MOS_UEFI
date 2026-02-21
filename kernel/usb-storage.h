#pragma once
#include "moslib.h"
#include "usb.h"

struct scsi_device_t;

// USB 存储设备公共基类
typedef struct us_common_data_t {
    // --- 硬件拓扑 ---
    usb_if_t     *usb_if;          // 绑定的 USB 接口

    // --- SCSI / LUN 管理 ---
    struct scsi_device_t *scsi_dev;

    list_head_t         lun_list;       // 挂载的逻辑单元 (LUN) 链表
    uint8               max_lun;        // 最大 LUN 编号

} us_common_data_t;

// BOT (Bulk-Only Transport) 专用结构
typedef struct bot_data_t {
    // [继承] 必须放在第一个，实现类似 C++ 的继承转换
    us_common_data_t common;

    // --- 管道 (Pipes) ---
    // BOT 只有两个管道，数据和状态共用
    uint8             pipe_in;        // Bulk IN (Data Read + CSW)
    uint8             pipe_out;       // Bulk OUT (CBW + Data Write)
    uint32            tag;

    // --- 协议缓冲区 (DMA Coherent) ---
    // BOT 协议每次传输都需要这两个包头
    struct bot_cbw    *cbw;           // Command Block Wrapper (31 Bytes)
    struct bot_csw    *csw;           // Command Status Wrapper (13 Bytes)

} bot_data_t;

// UAS (USB Attached SCSI) 专用结构
typedef struct uas_data_t {
    // [继承] 公共基类
    us_common_data_t common;

    // --- 管道 (Pipes) ---
    // UAS 分离了命令、状态和数据管道
    uint8            cmd_pipe;       // Bulk OUT (发送 Command IU)
    uint8            status_pipe;    // Bulk IN (接收 Sense IU)
    uint8            data_in_pipe;   // Bulk IN (Read Data)
    uint8            data_out_pipe;  // Bulk OUT (Write Data)
    uint64           tag_bitmap;      // UAS Tag管理,tag号对应stream
} uas_data_t;


int32 usb_storage_probe(usb_if_t *usb_if, usb_id_t *id);