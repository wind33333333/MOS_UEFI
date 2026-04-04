#pragma once
#include "moslib.h"
#include "bus.h"
#include "device.h"
#include "driver.h"

#pragma pack(push,1)

// ============================================================================
// SCSI Status Codes (SCSI 状态码)
// ============================================================================
#define SCSI_STATUS_GOOD                        0x00  // 完美成功 (命令正常执行完毕，无报错)
#define SCSI_STATUS_CHECK_CONDITION             0x02  // 检查条件 (★最常见的错误！说明出错或有警告，必须解析后面的 Sense Data)
#define SCSI_STATUS_CONDITION_MET               0x04  // 条件满足 (用于早期的搜索预取命令，现代存储极少遇到)
#define SCSI_STATUS_BUSY                        0x08  // 设备忙碌 (硬盘主控太忙，拒绝接客，主机应稍后重试该命令)
#define SCSI_STATUS_INTERMEDIATE                0x10  // 中间状态 (用于老旧的"链接命令"机制，现代设备基本废弃)
#define SCSI_STATUS_INTERMEDIATE_COND_MET       0x14  // 中间状态且条件满足 (同上，已废弃)
#define SCSI_STATUS_RESERVATION_CONFLICT        0x18  // 预留冲突 (★双机热备/集群常见：另一个主机锁死了这个 LUN，你无权读写)
#define SCSI_STATUS_COMMAND_TERMINATED          0x22  // 命令被终止 (已被规范废弃，被 Task Aborted 取代)
#define SCSI_STATUS_TASK_SET_FULL               0x28  // 任务集已满 (★UAS 并发极速读写时常见：硬盘内部的 NCQ/TCQ 队列塞满了，主机应减速下发新 Tag)
#define SCSI_STATUS_ACA_ACTIVE                  0x30  // ACA 激活 (发生了极严重的连锁错误，设备进入封锁状态，只接收清理命令)
#define SCSI_STATUS_TASK_ABORTED                0x40  // 任务被中止 (说明你之前发了 Task Management IU 的 Abort 指令，这个任务被成功强杀了)

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
} scsi_sense_t;

// SCSI TEST UNIT CDB （6字节）
typedef struct {
    uint8 opcode;
    uint8 rsvd0[4];
    uint8 control;
}scsi_cdb_test_unit_t;
#define SCSI_TEST_UNIT_READY  0x00

typedef struct {
    uint8    opcode;       // 固定值: 0x03 (SCSI_REQUEST_SENSE)
    uint8    desc; //[Byte 1] 描述符格式标志 (DESC) & 保留位 Bit 0: DESC (Descriptor Format) ,0 = Fixed Format (标准格式，U盘/移动硬盘绝大多数用这个),1 = Descriptor Format (描述符格式，部分企业级设备用)
    uint16   rsvd0;
    uint8    alloc_len; // [Byte 4] 分配长度 (Allocation Length)告诉设备：我为你准备了多少字节的缓冲区来接收 Sense Data。注意：这是一个 1 字节字段，所以最大只能请求 255 字节。
    uint8    control;
} scsi_cdb_request_sense_t;
#define SCSI_REQUEST_SENSE 0x03
#define SCSI_SENSE_ALLOC_SIZE 252

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
} scsi_inquiry_t;

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
    uint64 lun_list[];  //一个数组：uint64_t lun_list[];
}scsi_report_luns_t;
#define SCSI_LUN_BUF_LEN 64

// 1. 命令包定义 (CDB) - 10 字节
typedef struct __attribute__((packed)) {
    uint8  opcode;      // 0x25
    uint8  rsvd1;       // Reserved / RelAddr
    uint32 lba;         // Logical Block Address (通常填 0)
    uint16 rsvd2;       // Reserved
    uint8  pmi;         // Partial Medium Indicator (通常填 0)
    uint8  control;     // Control (通常填 0)
} scsi_cdb_read_capacity10_t;
#define SCSI_READ_CAPACITY10 0x25

// 2. 返回数据定义 - 8 字节
typedef struct __attribute__((packed)) {
    uint32 max_lba;     // Max Logical Block Address (Big Endian)
    uint32 block_size;  // Block Length in Bytes (Big Endian)
} scsi_read_capacity10_t;

typedef struct {

    // [Byte 0] Opcode
    // 固定值：0x9E (SCSI_SERVICE_ACTION_IN_16)
    // 注意：0x9E 是一个通用操作码，具体功能由 Service Action 决定
    uint8  opcode;

    // [Byte 1] Service Action
    // 低 5 位必须是 0x10 (SCSI_SA_READ_CAPACITY_16)
    // 高 3 位通常为 Reserved (0)
    uint8  service_action;

    // [Byte 2-9] Logical Block Address (Big Endian)
    // 起始 LBA，通常填 0 表示查询整个磁盘
    // 这是 64 位的，支持 ZB 级容量
    uint64 lba;

    // [Byte 10-13] Allocation Length (Big Endian)
    // 分配长度。你希望设备返回多少字节的数据。
    // 标准 READ CAPACITY (16) 的返回数据是 32 字节。
    // 所以这里通常填 cpu_to_be32(32)。
    uint32 alloc_len;

    // [Byte 14] PMI (Partial Medium Indicator)
    // Bit 0: PMI. 通常填 0。
    uint8  pmi;

    // [Byte 15] Control
    // 通常填 0
    uint8  control;

} scsi_cdb_read_capacity16_t;
#define SCSI_READ_CAPACITY16 0x9E
#define SA_READ_CAPACITY_16  0x10

typedef struct {

    // [Byte 0-7] Max Logical Block Address (Big Endian)
    // 最大 LBA 地址 (64位)。
    // 真正的容量 = (max_lba + 1) * block_length
    uint64 max_lba;

    // [Byte 8-11] Block Length (Big Endian)
    // 扇区大小，通常是 512 或 4096
    uint32 block_len;

    // [Byte 12] Protection & Flags
    // Bit 0: PROT_EN (保护使能)
    // Bit 1-3: P_TYPE (保护类型)
    uint8  prot_en;

    // [Byte 13] Logical Blocks per Physical Block Exponent
    // 用于对齐优化 (例如 4K 对齐)
    uint8  p_type_exponent;

    // [Byte 14-15] Lowest Aligned Logical Block Address
    // 对齐基准偏移
    uint16 lowest_aligned_lba;

    // [Byte 16-31] Reserved
    // 保留字段，通常为 0
    uint8  reserved[16];

} scsi_read_capacity16_t;

// READ(10) 和 WRITE(10) 共享的 10 字节 CDB 结构
typedef struct {
    // [Byte 0] 操作码 (Opcode)
    // 读: 0x28 (SCSI_CMD_READ10) | 写: 0x2A (SCSI_CMD_WRITE10)
    uint8  opcode;

    // [Byte 1] 标志位 (Flags)
    // 包含 RDPROTECT(3位), DPO(1位), FUA(1位), FUA_NV(1位) 等。
    // 对于普通的 U 盘和硬盘读写，直接填 0 即可（走设备默认缓存策略）。
    uint8  flags;

    // [Byte 2-5] 逻辑块地址 (Logical Block Address, LBA)
    // 【极度警告】这里必须是 大端序 (Big-Endian)！
    // 决定了你从哪个扇区开始读写。
    uint32 lba;

    // [Byte 6] 组号 (Group Number) / 保留位
    // 通常填 0。
    uint8  group;

    // [Byte 7-8] 传输长度 (Transfer Length)
    // 【警告】大端序 (Big-Endian)！
    // 决定了你要连续读写多少个扇区（块）。
    // 注意：如果是 0，表示传输 0 个块（不传输数据），有些老规范表示 65536 块，但现代设备通常视为 0。
    uint16 transfer_length;

    // [Byte 9] 控制字节 (Control)
    // 填 0 即可。
    uint8  control;

}scsi_cdb_rw10_t;
#define SCSI_READ10  0x28
#define SCSI_WRITE10 0x2A

// READ(16) 和 WRITE(16) 共享的 16 字节 CDB 结构
typedef struct {
    // [Byte 0] 操作码 (Opcode)
    // 读: 0x88 (SCSI_CMD_READ16) | 写: 0x8A (SCSI_CMD_WRITE16)
    uint8  opcode;

    // [Byte 1] 标志位 (Flags)
    // 同 10 字节命令，普通读写填 0。
    uint8  flags;

    // [Byte 2-9] 逻辑块地址 (Logical Block Address, LBA)
    // 【警告】这里是 64 位的大端序 (Big-Endian)！
    // 突破 2TB 限制的核心字段。
    uint64 lba;

    // [Byte 10-13] 传输长度 (Transfer Length)
    // 【警告】这里是 32 位的大端序 (Big-Endian)！
    // 支持单次发起超过 65535 个扇区的超级大块读写。
    uint32 transfer_length;

    // [Byte 14] 组号 (Group Number) / 保留位
    // 填 0。
    uint8  group;

    // [Byte 15] 控制字节 (Control)
    // 填 0 即可。
    uint8  control;

}scsi_cdb_rw16_t;
#define SCSI_READ16  0x88
#define SCSI_WRITE16 0x8A

#pragma pack(pop)

// ============================================================================
// 1. SCSI 主机操作模板 (SCSI Host Template)
// 本质：驱动的“虚函数表”。
// 必须被声明为 static const，存放在内核的 .rodata (只读数据段) 中以防止被黑客篡改。
// ============================================================================
typedef struct scsi_host_template_t {
    const char *name;                    // 驱动名称，例如 "usb-bot", "usb-uas", "ahci"

    uint32 max_sectors;

    // 【核心接口】派发 SCSI 任务给底层硬件
    // 返回值通常为 0 (成功接收并处理) 或错误码
    int32 (*queue_command)(struct scsi_host_t *host, struct scsi_cmnd_t *cmnd);

    // 【可选接口】复位整个主机控制器 (当设备彻底卡死时调用)
    int32 (*reset_host)(struct scsi_host_t *host);

    // 【可选接口】中止单个超时的 SCSI 命令
    int32 (*abort_command)(struct scsi_host_t *host, struct scsi_cmnd_t *task);

} scsi_host_template_t;

// ============================================================================
// 2. SCSI 主机实例 (SCSI Host)
// 本质：代表一个物理控制器或传输通道 (如一根 USB 线、一个 SATA 控制器)。
// 内存位置：由底层驱动在堆 (Heap) 上动态 kzalloc 分配。
// ============================================================================
typedef struct scsi_host_t {
    // --- 1. 继承与设备树模型 ---
    device_t              dev;          // 通用设备节点。
    // 极其重要：
    // dev.parent 必须指向物理设备 (如 usb_if->dev)
    // dev.bus 必须设为 NULL (它不参与总线相亲)

    // --- 2. 接口与私有数据 (桥梁核心) ---
    scsi_host_template_t *hostt;        // 指向绑定的操作模板 (虚函数表)
    void                 *hostdata;     // 底层驱动的私有数据指针！
    // 如果是 BOT，它会被强转为 bot_data_t*
    // 如果是 UAS，它会被强转为 uas_data_t*

    // --- 3. 硬件状态与属性 ---
    uint32                host_no;      // 系统分配的全局主机编号 (如 0 代表 host0)
    uint8                 max_lun;      // 探测到的真实硬件 LUN 上限 (例如 BOT 通过 0xFE 请求获取)
    uint32                host_status;  // 主机当前状态 (0=运行中, 1=正在复位, 2=已拔出)

    // --- 4. 子设备管理 ---
    list_head_t           devices_list; // 链表头：挂载在此 Host 下的所有逻辑单元 (scsi_device_t)

    // uint64             lock;         // 未来加入自旋锁，保护并发读写时的 devices_list 和状态
} scsi_host_t;

// ============================================================================
// 3. SCSI 逻辑设备 (SCSI Device / LUN)
// 本质：代表一个具体的逻辑磁盘 (如 U 盘的一个分区、光驱、SATA 硬盘)
// 内存位置：在 scsi_scan_host 扫描到有效 LUN 时，由 SCSI 中间层在堆上动态分配。
// ============================================================================
typedef struct scsi_device_t{
    // --- 1. 设备树模型继承 ---
    device_t            dev;          // 通用的设备节点结构体
    // 关键配置：
    // dev.parent = &host->dev (认 Host 为父)
    // dev.bus = &scsi_bus_type (挂在 SCSI 总线上等待相亲)

    // --- 2. 拓扑与寻址结构 ---
    scsi_host_t         *host;        // 【反向指针】指向孕育它的 SCSI Host
    uint16              channel;      // 通道号 (通常为 0)
    uint16              id;           // Target ID (通常为 0)
    uint32              lun;          // 逻辑单元号 (如 0, 1, 2...)

    // --- 3. 硬件身份信息 (通过 INQUIRY 命令获取) ---
    uint8               type;         // 设备类型 (0x00=磁盘, 0x05=光驱)
    uint8               removable;    // 是否为可移动介质 (RMB 位: 1=是, 0=否)
    char                vendor[9];    // 厂商名 (8 字节 ASCII + 1 字节 '\0')
    char                model[17];    // 产品型号 (16 字节 ASCII + 1 字节 '\0')
    char                rev[5];       // 固件版本 (4 字节 ASCII + 1 字节 '\0')

    // --- 4. 运行时状态与容量 (通过 READ CAPACITY 命令获取) ---
    uint32              block_size;   // 扇区大小 (通常 512 或 4096 字节)
    uint64              max_lba;      // 最大逻辑块地址 (总容量 = (max_lba + 1) * block_size)
    uint8               is_ready;     // 介质是否就绪 (0=未就绪/无盘, 1=就绪)

    // --- 5. 链表与高层绑定 ---
    list_head_t         siblings;     // 兄弟节点链表 (用于挂在 host->devices_list 上)
    void                *disk_data;   // 【极度关键】指向上层的高级对象！
    // 当 sd_driver 认领它后，这里会指向 block_device_t (如 sda)
} scsi_device_t;

// 1. 数据传输方向枚举
typedef enum {
    SCSI_DIR_NONE = 0, // 无数据传输 (如 TEST UNIT READY)
    SCSI_DIR_IN   = 1, // 设备到主机 (读)
    SCSI_DIR_OUT  = 2  // 主机到设备 (写)
} scsi_dir_t;

// 2. 通用 SCSI 任务结构体
typedef struct scsi_cmnd_t{
    scsi_device_t *sdev;

    // --- SCSI 命令部分 ---
    void   *scsi_cdb;        // SCSI 命令块
    uint8  scsi_cdb_len;      // 有效命令长度 (通常为 6, 10, 12, 16)

    // --- 数据传输部分 ---
    scsi_dir_t dir;        // 数据传输方向
    void       *data_buf;    // 数据缓冲区指针 (必须是 DMA 安全的物理/虚拟地址)
    uint32     data_len;     // 期望传输的数据总长度 (字节数)

    // --- 错误处理部分 ---
    scsi_sense_t  *sense;       // [可选] 用于接收 Auto-Sense 数据的缓冲区 (至少 18 字节)
    int32         status;       // [输出] 命令执行完成后的 SCSI 状态码 (0=成功, 2=Check Condition)
} scsi_cmnd_t;


int32 scsi_test_unit_ready(scsi_device_t *sdev);
int32 scsi_request_sense(scsi_device_t *scsi_dev,scsi_sense_t *sense);
int32 scsi_send_inquiry(scsi_device_t *sdev, scsi_inquiry_t *inquiry);
int32 scsi_report_luns(scsi_device_t *sdev,scsi_report_luns_t *report_luns);
int32 scsi_read_capacity10(scsi_device_t *scsi_dev,scsi_read_capacity10_t *read_capacity10);
int32 scsi_read_capacity16(scsi_device_t *scsi_dev,scsi_read_capacity16_t *read_capacity16);
int32 scsi_read10(scsi_device_t *scsi_dev,void *data_buf,uint32 lba,uint16 block_count);
int32 scsi_write10(scsi_device_t *scsi_dev,void *data_buf,uint32 lba,uint16 block_count);
int32 scsi_read16(scsi_device_t *scsi_dev,void *data_buf,uint64 lba,uint32 block_count);
int32 scsi_write16(scsi_device_t *scsi_dev,void *data_buf,uint64 lba,uint32 block_count);

scsi_host_t *scsi_create_host(scsi_host_template_t *host_template,void* host_data,device_t *parent,uint8 max_lun,char *name);
int32 scsi_add_host(scsi_host_t *shost);
