#pragma once
#include "moslib.h"
#include "usb.h"

#pragma pack(push,1)

/* CBW 结构（31 字节） */
typedef struct {
    uint32 cbw_signature; // 固定为 0x43425355 ('USBC')
    uint32 cbw_tag; // 命令标签，唯一标识
    uint32 cbw_data_transfer_length; // 数据传输长度
    uint8  cbw_flags; // 传输方向（0x80=IN，0x00=OUT）
    uint8  cbw_lun; // 逻辑单元号（通常为 0）
    uint8  cbw_cb_length; // SCSI 命令长度（10 字节 for READ(10)）
    uint8  cbw_cb[16]; // SCSI 命令块（READ(10) 命令）
} usb_cbw_t;

/* CSW 结构（13 字节） */
typedef struct {
    uint32 csw_signature; // 固定为 0x53425355 ('USBS')
    uint32 csw_tag; // 匹配 CBW 的标签
    uint32 csw_data_residue; // 未传输的数据长度
    uint8  csw_status; // 命令状态（0=成功，1=失败，2=相位错误）
} usb_csw_t;

/* READ CAPACITY (16) 返回数据（32 字节） */
typedef struct {
    uint64 last_lba; // 最后一个逻辑块地址（块数量 - 1，64 位）
    uint32 block_size; // 逻辑块大小（字节）
    uint8  reserved[20]; // 保留字段（包括保护信息等）
} read_capacity_16_t;

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


/*typedef struct{
    uint8  iu_id;          // 0x01 = Command IU
    uint8  reserved1;
    uint16 tag;            // 大端序
    uint16 len;            // 大端序，长度，通常 16
    uint8  priority_attr;  // 优先级 / 属性（一般填 0）
    uint8  reserved2;
    uint8  lun[8];
    uint8  cdb[16];        // 完整 16 字节 SCSI CDB
}uas_cmd_iu_t;*/

typedef struct {
    uint8  iu_id;       // 0x01
    uint8  reserved1;
    uint16 tag;         // 大端
    uint8  priority_attr;  // 任务属性 + 优先级，0 = SIMPLE
    uint8  reserved5;
    uint8  len;         // 额外 CDB 字节数（4 字节对齐），通常 0
    uint8  reserved7;
    uint8  lun[8];      // SAM-4 LUN 编码
    uint8  cdb[16];     // 固定 16 字节 CDB 区
}uas_cmd_iu_t;

typedef struct{
    uint8   pdt_pq;
    uint8   rmb;
    uint8   version;
    uint8   resp_fmt;
    uint8   add_len;
    uint8   flags[3];
    char    vendor[8];
    char    product[16];
    char    revision[4];
} scsi_inquiry_std_t;


/* Status(Sense) IU：见 UAS 规范 Table 13 */
typedef struct {
    uint8  iu_id;       /* 0x03 = Sense IU (Status IU) */
    uint8  reserved1;
    uint16 tag;         /* COMMAND IDENTIFIER，大端 */
    uint16 length;      /* LENGTH，大端；本字段之后的字节数 */
    uint8  status;      /* SCSI Status，例如 0x00 GOOD，0x02 CHECK CONDITION */
    uint8  reserved2;
    /* 简单预留一点空间放 Sense Data；真的要用可以再扩展 */
    uint8  sense_data[18];
} uas_status_iu_t;

#pragma pack(one)


// 前置声明
struct scsi_cmnd;

// USB 存储设备公共基类
typedef struct us_common_data_t {
    // --- 硬件拓扑 ---
    usb_if_t     *usb_if;          // 绑定的 USB 接口

    // --- SCSI / LUN 管理 ---
    list_head_t         lun_list;       // 挂载的逻辑单元 (LUN) 链表
    uint8               max_lun;        // 最大 LUN 编号

    // --- 统一的操作接口 (虚函数表) ---
    // 通过这组函数指针，屏蔽 BOT 和 UAS 的差异
    struct us_protocol_ops {
        int (*queue_command)(struct us_common_data_t *us, struct scsi_cmnd *cmnd);
        int (*device_reset)(struct us_common_data_t *us);
        void (*abort_command)(struct us_common_data_t *us, struct scsi_cmnd *cmnd);
    } *ops;
} us_common_data_t;

// BOT (Bulk-Only Transport) 专用结构
typedef struct bot_data_t {
    // [继承] 必须放在第一个，实现类似 C++ 的继承转换
    us_common_data_t common;

    // --- 管道 (Pipes) ---
    // BOT 只有两个管道，数据和状态共用
    uint8             pipe_in;        // Bulk IN (Data Read + CSW)
    uint8             pipe_out;       // Bulk OUT (CBW + Data Write)

    // --- 协议缓冲区 (DMA Coherent) ---
    // BOT 协议每次传输都需要这两个包头
    struct bulk_cbw     *cbw;           // Command Block Wrapper (31 Bytes)
    uint64              cbw_dma;        // CBW 物理地址
    struct bulk_csw     *csw;           // Command Status Wrapper (13 Bytes)
    uint64              csw_dma;        // CSW 物理地址
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

    // --- 扩展特性 ---
    uint16           stream_id;      // USB 3.0 Stream ID (如果 endpoint 支持)
    uint32           flags;          // UAS 特有标志 (如 USE_STREAMS)
} uas_data_t;

////////////////////////////////////////////////
//逻辑单元
typedef struct {
    uint64          block_count;            // 块数量
    uint32          block_size;             // 块大小
    char8           vid[25];                // 厂商ascii码
    uint8           lun_id;                 // 逻辑单元
} usb_lun_t;

//bot协议u盘
typedef struct usb_bot_msc_t{
    usb_dev_t*      usb_dev;                // 父设备指针
    //usb_endpoint_t  in_ep;                  // 输入端点
    //usb_endpoint_t  out_ep;                 // 输出端点
    int32 (*scsi_read)(xhci_controller_t *xhci_controller,usb_dev_t *usb_dev,struct usb_bot_msc_t *bot_msc,uint8 lun_id,uint64 lba,uint32 block_count,uint32 block_size,void *buf);
    int32 (*scsi_write)(xhci_controller_t *xhci_controller,usb_dev_t *usb_dev,struct usb_bot_msc_t *bot_msc,uint8 lun_id,uint64 lba,uint32 block_count,uint32 block_size,void *buf);
    uint8           interface_num;          // 接口号
    usb_lun_t*      lun;                    // 逻辑单元组
    uint8           lun_count;              // 逻辑单元实际个数
    uint32          tag;                    // 全局标签
} usb_bot_msc_t;

typedef struct {
    xhci_ring_t  transfer_ring;
    uint8        ep_num;
    xhci_ring_t* stream_rings;   // per-stream rings数组 (如果启用流)
    uint32 streams_count;        // 2^max_streams_exp+1
}usb_uas_endpoint_t;

//uas协议u盘
typedef struct {
    usb_dev_t*      usb_dev;                // 父设备指针
    usb_uas_endpoint_t cmd_out_ep;
    usb_uas_endpoint_t sta_in_ep;
    usb_uas_endpoint_t bluk_in_ep;
    usb_uas_endpoint_t bluk_out_ep;
    uint8           interface_num;          // 接口号
    usb_lun_t*      lun;                    // 逻辑单元组
    uint8           lun_count;              // 逻辑单元实际个数
    uint16          tag;                    // 全局标签
} usb_uas_msc_t;

// 通用的 USB Mass Storage Class (MSC) 结构
// 整合 BOT 和 UASP 的共同字段，并使用联合体处理协议特定部分
typedef struct usb_msc_t {
    usb_dev_t* usb_dev;          // 父 USB 设备指针（通用）
    uint8 interface_num;         // 接口号（通用）
    usb_lun_t* lun;              // 逻辑单元组指针（通用）
    uint8 lun_count;             // 逻辑单元实际个数（通用）
    uint32 tag;                  // 全局标签（兼容 BOT uint32 和 UASP uint16，使用 uint32）

    // 通用的 SCSI 读写函数指针（根据协议内部分支实现）
    int32 (*scsi_read)(xhci_controller_t *xhci_controller, usb_dev_t *usb_dev, struct usb_msc_t *msc, uint8 lun_id, uint64 lba, uint32 block_count, uint32 block_size, void *buf);
    int32 (*scsi_write)(xhci_controller_t *xhci_controller, usb_dev_t *usb_dev, struct usb_msc_t *msc, uint8 lun_id, uint64 lba, uint32 block_count, uint32 block_size, void *buf);

    // 协议特定字段，使用联合体节省内存
    union {
        // BOT 协议特定部分
        struct {
            //usb_endpoint_t in_ep;   // Bulk-In 端点
            //usb_endpoint_t out_ep;  // Bulk-Out 端点
        } bot;

        // UASP 协议特定部分
        struct {
            usb_uas_endpoint_t cmd_out_ep;  // Command Pipe (Bulk-Out)
            usb_uas_endpoint_t sta_in_ep;   // Status Pipe (Bulk-In)
            usb_uas_endpoint_t bulk_in_ep;  // Data-In Pipe (Bulk-In，支持 Streams)
            usb_uas_endpoint_t bulk_out_ep; // Data-Out Pipe (Bulk-Out，支持 Streams)
        } uasp;
    } protocol_specific;  // 访问示例：msc->protocol_specific.bot.in_ep
} usb_msc_t;

int32 usb_storage_probe(usb_if_t *usb_if, usb_id_t *id);