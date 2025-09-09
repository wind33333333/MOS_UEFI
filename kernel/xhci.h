#pragma once

#include "moslib.h"

#define TRB_COUNT 256        //trb个数

#define TRB_RESERVED                (0 << 10)   // 保留
#define TRB_NORMAL                  (1 << 10)   // 普通传输
#define TRB_SETUP_STAGE             (2 << 10)   // 设置阶段
#define TRB_DATA_STAGE              (3 << 10)   // 数据阶段
#define TRB_STATUS_STAGE            (4 << 10)   // 状态阶段
#define TRB_ISOCH                   (5 << 10)   // 等时传输
#define TRB_LINK                    (6 << 10)   // 链接
#define TRB_EVDATA                  (7 << 10)   // 事件数据
#define TRB_NOOP                    (8 << 10)   // 空操作
#define TRB_ENABLE_SLOT             (9 << 10)   // 启用插槽
#define TRB_DISABLE_SLOT            (10 << 10)  // 禁用插槽
#define TRB_ADDRESS_DEVICE          (11 << 10)  // 设备寻址
#define TRB_CONFIGURE_ENDPOINT      (12 << 10)  // 配置端点
#define TRB_EVALUATE_CONTEXT        (13 << 10)  // 评估上下文
#define TRB_RESET_ENDPOINT          (14 << 10)  // 重置端点
#define TRB_STOP_ENDPOINT           (15 << 10)  // 停止端点
#define TRB_SET_TR_DEQUEUE          (16 << 10)  // 设置传输环出队
#define TRB_RESET_DEVICE            (17 << 10)  // 重置设备
#define TRB_FORCE_EVENT             (18 << 10)  // 强制事件
#define TRB_NEGOTIATE_BW            (19 << 10)  // 协商带宽
#define TRB_SET_LATENCY_TOLERANCE   (20 << 10)  // 设置延迟容忍
#define TRB_GET_PORT_BANDWIDTH      (21 << 10)  // 获取端口带宽
#define TRB_FORCE_HEADER            (22 << 10)  // 强制头部
#define TRB_NOOP_COMMAND            (23 << 10)  // 空操作命令
#define TRB_TRANSFER                (32 << 10)  // 传输
#define TRB_COMMAND_COMPLETE        (33 << 10)  // 命令完成
#define TRB_PORT_STATUS_CHANGE      (34 << 10)  // 端口状态改变
#define TRB_BANDWIDTH_REQUEST       (35 << 10)  // 带宽请求
#define TRB_DOORBELL                (36 << 10)  // 门铃
#define TRB_HOST_CONTROLLER         (37 << 10)  // 主机控制器
#define TRB_DEVICE_NOTIFICATION     (38 << 10)  // 设备通知
#define TRB_MFINDEX_WRAP            (39 << 10)  // 主框架索引回绕

#define TRB_TYPE_NORMAL      (1 << 10)
#define TRB_TYPE_SETUP       (2 << 10)
#define TRB_TYPE_DATA        (3 << 10)
#define TRB_TYPE_STATUS      (4 << 10)
#define TRB_IOC              (1 << 5) // Interrupt on Completion
#define TRB_TYPE_LINK        (0x06<<10) //连接trb
#define TRB_CYCLE            (1 << 0)
#define TRB_TOGGLE_CYCLE     (1 << 1)
#define TRB_CHAIN            (1 << 9)
// 建议增加宏
#define TRB_IDT             (1 << 6)     // Immediate Data
#define TRB_TRT_IN_DATA     (3 << 16)    // TRT=3: 有数据阶段，方向 IN
#define TRB_DIR_IN          (1 << 16)    // Data/Status TRB 的方向位（对 Status 则相反）
#define TRB_DIR             (1 << 16) // DIR for Data TRB

#pragma pack(push,1)

// ===== 1. 能力寄存器 (Capability Registers) =====
typedef struct {
    // 00h: 能力长度和版本 (CAPLENGTH/HCIVERSION)
    UINT8   cap_length;        // [7:0] 能力寄存器总长度 (字节)
    UINT8   reserved0;         // 保留
    UINT16  hciversion;        // 控制器版本 (0x100 = 1.0.0, 0x110 = 1.1.0, 0x120 = 1.2.0)

    // 04h: 硬件参数寄存器 (HCSPARAMS1)
    UINT32 hcsparams1;      /*[7:0]   MaxSlots: 支持的最大设备槽数（最大256）
                              [18:8]  MaxIntrs: 支持的中断向量数（最大2048）
                              [24:31] MaxPorts: 支持的根端口数（最大256）*/

    // 08h: 硬件参数寄存器 (HCSPARAMS2)
    UINT32 hcsparams2;      /*[3:0]	    IST 等时调度阈值，单位为微帧（125us）。Host Controller 在这个阈值之后的同一帧内不再调度新的等时传输。常见值：0~8。
                              [7:4]	    ERST Max 硬件支持的事件环段表最大条目数2^n ERST MAX = 8 则条目等于256。
                              [25:21]	Max Scratchpad Buffers (hi  5 bits)	最大暂存器缓冲区数量的高5位（范围 0~1023）。
                              [31:27]	Max Scratchpad Buffers (Lo 5 bits)	最大暂存器缓冲区数量的低5位*/

    // 0Ch: 硬件参数寄存器 (HCSPARAMS3)
    UINT32 hcsparams3;      /*[7:0]     U1DeviceExitLatency: U1设备退出延迟（以微秒为单位）
                              [15:8]    U2DeviceExitLatency: U2设备退出延迟（以微秒为单位）*/

    // 10h: 硬件参数寄存器 (HCCPARAMS1)
    UINT32 hccparams1;      /*- AC64 (位 0): 64位寻址能力（1=支持，0=不支持）
                              - BNC (位 1): 带宽协商能力
                              - CSZ (位 2): 上下文大小（0=32字节，1=64字节）
                              - PPC (位 3): 端口电源控制能力
                              - PIND (位 4): 端口指示器能力
                              - LHRC (位 5): 轻量级主机路由能力
                              - LTC (位 6): 延迟容忍能力
                              - NSS (位 7): 无嗅探能力
                              - MaxPSASize (位 12-15): 最大主控制器流数组大小*/
    #define HCCP1_CSZ   (1<<2)

    UINT32 dboff;           // 0x14 门铃寄存器偏移

    UINT32 rtsoff;          // 0x18 运行时寄存器偏移

    UINT32 hccparams2;      /* - U3C (位 0): U3转换能力
                               - CMC (位 1): 配置最大能力
                               - FSC (位 2): 强制保存上下文能力
                               - CTC (位 3): 符合性测试能力
                               - LEC (位 4): 大型ESIT有效负载能力
                               - CIC (位 5): 配置信息能力*/
}  xhci_cap_regs_t;

// ===== 2. 操作寄存器 (Operational Registers) =====
typedef struct {
    // 00h: 命令寄存器 (USBCMD)
    UINT32 usbcmd;  /*- R/S (位 0): 运行/停止（1=运行，0=停止）
                      - HCRST (位 1): 主机控制器复位（置1触发复位）
                      - INTE (位 2): 中断使能（1=使能，0=禁用）
                      - HSEE (位 3): 主机系统错误使能
                      - LHCRST (位 7): 轻量级主机控制器复位
                      - CSS (位 8): 控制器保存状态
                      - CRS (位 9): 控制器恢复状态
                      - EWE (位 10): 事件中断使能
                      - EU3S (位 11): 启用U3 MMI（电源管理相关）*/
    #define XHCI_CMD_RS (1 << 0)
    #define XHCI_CMD_HCRST (1 << 1)
    #define XHCI_CMD_INTE (1 << 2)
    #define XHCI_CMD_HSEE (1 << 3)
    #define XHCI_CMD_LHCRST (1 << 7)
    #define XHCI_CMD_CSS (1 << 8)
    #define XHCI_CMD_CRS (1 << 9)
    #define XHCI_CMD_EWE (1 << 10)
    #define XHCI_CMD_EU3S (1 << 11)

    // 04h: 状态寄存器 (USBSTS)
    UINT32 usbsts; /* - HCH (位 0): 主机控制器停止（1=已停止，0=运行中）
                      - HSE (位 2): 主机系统错误（1=错误发生）
                      - EINT (位 3): 事件中断（1=有事件中断待处理）
                      - PCD (位 4): 端口变化检测（1=端口状态变化）
                      - SSS (位 8): 保存状态状态
                      - RSS (位 9): 恢复状态状态
                      - SRE (位 10): 保存/恢复错误
                      - CNR (位 11): 控制器未就绪（1=未就绪）
                      - HCE (位 12): 主机控制器错误*/
    #define XHCI_STS_HCH (1 << 0)
    #define XHCI_STS_HSE (1 << 2)
    #define XHCI_STS_EINT (1 << 3)
    #define XHCI_STS_PCD (1 << 4)
    #define XHCI_STS_SSS (1 << 8)
    #define XHCI_STS_RSS (1 << 9)
    #define XHCI_STS_SRE (1 << 10)
    #define XHCI_STS_CNR (1 << 11)
    #define XHCI_STS_HCE (1 << 12)

    // 08h: 页面大小寄存器 (PAGESIZE)
    UINT32 pagesize; // 控制器支持的页面大小*0x1000

    // 0Ch: 保留 [RsvdZ]
    UINT32 reserved0[2];

    //0x14: 设备通知控制寄存器 (DNCTRL)
    UINT32 dnctrl;  // - 每位对应一个设备槽的使能（0=禁用，1=使能）

    //0x18 命令环控制寄存器 (CRCR)
    UINT64 crcr; /*- 位[0] - RCS（Ring Cycle State，环周期状态）：当RCS=1时，主机控制器从命令环中获取的TRB需要其Cycle Bit为1才会被处理；当RCS=0时，则处理Cycle Bit为0的TRB。
                   - 位[1] - CS（Command Stop，命令停止）：当置1时，命令环在完成当前命令后停止运行。
                   - 位[2] - CA（Command Abort，命令中止）：当置1时，命令环立即停止，当前正在执行的命令被中止。
                   - 位[3] - CRR（Command Ring Running，命令环运行状态）：为1时表示命令环正在运行，为0时表示命令环已停止。
                   - 位[6:63] - Command Ring Pointer（命令环指针）：指向命令环的64位基地址（物理地址）。低6位必须为0（即地址必须64字节对齐）*/

    //0x20: 保留字段
    UINT64 reserved1[2];

    //0x30h: 设备上下文基础地址数组指针 (DCBAAP)
    UINT64 dcbaap;  // DCBAA的物理地址指针

    // 38h: 配置寄存器 (CONFIG)
    UINT32 config;   // [7:0] 启用的设备槽数 (值≤MaxSlots)

    // 保留字段 (Reserved), 偏移 0x3C-0x3FF, 填充到端口寄存器之前
    UINT32 reserved2[241];

    // 端口寄存器数组 (PORTSC, PORTPMSC, PORTLI, PORTHLPMC), 偏移 0x400起,每个端口占用16字节，按端口数量动态分配
    struct {
        // 端口状态和控制寄存器 (PORTSC), 32位
        UINT32 portsc;     /*  - CCS (位 0): 当前连接状态（1=设备连接 0=设备未连接）
                               - PED (位 1): 端口已启用/禁用 （1=启用 0=禁用）
                               - TM  (位 2)：1=隧道模式 0=本地模式
                               - OCA (位 3)：过电流激活 （1=该端口处于过流状态 0=该端口不存在过流情况）
                               - PR (位 4): 端口复位 （1=端口复位信令已断言 0=端口未复位）
                               - PLS (位 5-8): 端口链路状态
                               - PP (位 9): 端口电源 默认=1
                               - PortSpeed (位 10-13): 端口速度
                               - PIC (位 14-15): 端口指示器控制（0=端口指示灯关闭 1=琥珀色 2=绿色 3=未定义）
                               - LWS (位 16): 链路状态写入选通
                               - CSC (位 17): 连接状态变化
                               - PEC (位 18): 端口使能/禁用变化
                               - WRC (位 19): 热重置变化
                               - OCC (位 20): 过流变化
                               - PRC (位 21): 端口复位变化
                               - PLC (位 22): 端口链路状态变化
                               - CEC (位 23): 配置错误变化
                               - CAS (位 24): 冷连接状态
                               - WCE (位 25): 唤醒连接启用
                               - WDE (位 26): 断线唤醒启用
                               - WOE (位 27)：过电流唤醒使能
                               - DR  (位 30)：1=设备不可拆卸 0=设备可移动
                               - WPR (位 31)：热端口复位 */
        #define XHCI_PORTSC_CCS (1 << 0)
        #define XHCI_PORTSC_PED (1 << 1)
        #define XHCI_PORTSC_OCA (1 << 3)
        #define XHCI_PORTSC_PR (1 << 4)
        #define XHCI_PORTSC_PLS_SHIFT 5
        #define XHCI_PORTSC_PLS_MASK 0xf
        #define XHCI_PORTSC_PP (1 << 9)
        #define XHCI_PORTSC_SPEED_SHIFT 10
        #define XHCI_PORTSC_SPEED_MASK 0xf
        #define XHCI_PORTSC_SPEED_FULL (1 << 10)
        #define XHCI_PORTSC_SPEED_LOW (2 << 10)
        #define XHCI_PORTSC_SPEED_HIGH (3 << 10)
        #define XHCI_PORTSC_SPEED_SUPER (4 << 10)
        #define XHCI_PORTSC_PIC_SHIFT 14
        #define XHCI_PORTSC_PIC_MASK 0x3
        #define XHCI_PORTSC_LWS (1 << 16)
        #define XHCI_PORTSC_CSC (1 << 17)
        #define XHCI_PORTSC_PEC (1 << 18)
        #define XHCI_PORTSC_WRC (1 << 19)
        #define XHCI_PORTSC_OCC (1 << 20)
        #define XHCI_PORTSC_PRC (1 << 21)
        #define XHCI_PORTSC_PLC (1 << 22)
        #define XHCI_PORTSC_CEC (1 << 23)
        #define XHCI_PORTSC_CAS (1 << 24)
        #define XHCI_PORTSC_WCE (1 << 25)
        #define XHCI_PORTSC_WDE (1 << 26)
        #define XHCI_PORTSC_WOE (1 << 27)
        #define XHCI_PORTSC_DR (1 << 30)
        #define XHCI_PORTSC_WPR (1 << 31)

        #define XHCI_PLS_U0              0   // 正常工作状态，USB 设备活跃，支持全速数据传输（USB 3.0 或 USB 2.0）
        #define XHCI_PLS_U1              1   // U1 低功耗状态，USB 设备进入轻度节能模式，快速恢复，适用于 USB 3.0
        #define XHCI_PLS_U2              2   // U2 低功耗状态，USB 设备进入更深节能模式，恢复时间稍长，适用于 USB 3.0
        #define XHCI_PLS_U3              3   // U3 挂起状态，USB 设备进入深度休眠，功耗最低，恢复时间较长，适用于 USB 3.0
        #define XHCI_PLS_DISABLED        4   // 禁用状态，USB 端口被禁用，无法通信
        #define XHCI_PLS_RX_DETECT       5   // 接收检测状态，USB 控制器正在检测是否有设备连接
        #define XHCI_PLS_INACTIVE        6   // 非活跃状态，USB 端口未连接设备或设备未响应
        #define XHCI_PLS_POLLING         7   // 轮询状态，USB 控制器正在初始化或尝试建立与设备的连接
        #define XHCI_PLS_RECOVERY        8   // 恢复状态，USB 端口从低功耗状态（如 U3）恢复到活跃状态
        #define XHCI_PLS_HOT_RESET       9   // 热重置状态，USB 端口正在执行热重置操作，重新初始化设备
        #define XHCI_PLS_COMPLIANCE_MODE 10  // 合规模式，用于 USB 设备或控制器的合规性测试
        #define XHCI_PLS_TEST_MODE       11  // 测试模式，USB 端口进入特定测试状态，用于硬件或协议测试
        #define XHCI_PLS_RESUME          15  // 恢复状态，USB 设备从挂起状态恢复，通常由主机发起

        // 端口电源管理状态和控制寄存器 (PORTPMSC),控制电源管理和U1/U2状态,具体字段依赖于协议（USB2或USB3）
        UINT32 portpmsc;

        // 端口链路信息寄存器 (PORTLI), 提供链路错误计数等信息
        UINT32 portli;

        // 主机控制器端口电源管理控制寄存器 (PORTHLPMC), 仅用于USB2协议端口，控制高级电源管理
        UINT32 porthlpmc;
    } portregs[256]; // 最大支持256个端口（根据HCSPARAMS1中的MaxPorts）
}  xhci_op_regs_t;

// ===== 3. 运行时寄存器 (Runtime Registers) =====
typedef struct {
    // 00h: 微帧索引寄存器 (MFINDEX)
    UINT32 mfindex;  // [13:0] 当前微帧索引（按125μs递增）

    // 04h: 保留
    UINT32 reserved0[7];

    // 中断管理数组 (IMAN) - 每个中断向量一个
    struct {
        // 中断管理寄存器 (IMAN), 偏移 0x00
        UINT32 iman;    // 中断管理 [0]：IP中断挂起（1=有中断待处理），[1]：中断使能（1=使能，0=禁用）

        //中断调节寄存器 (IMOD), 偏移 0x04,
        UINT32 imod;    // 中断调制器 (位 0-15): 中断调节间隔（以250ns为单位，(位 16-31): 中断调节计数器（只读）

        // 事件环段表大小寄存器 (ERSTSZ), 偏移 0x08, 32位
        UINT32 erstsz;  // - ERST Size (位 0-15): 事件环段表条目数（最大4096）
        UINT32 reserved1;

        // 事件环段表基地址寄存器 (ERSTBA), 偏移 0x10-0x17, 64位
        UINT64 erstba;  //指向事件环段表的64位基地址（对齐到64字节)

        // 事件环出队指针寄存器 (ERDP), 偏移 0x18-0x1F, 64位
        UINT64 erdp;    /*指向事件环的当前出队指针
                        - DESI (位 0-2): 出队事件环段索引
                        - EHB (位 3): 事件处理忙碌（1=忙碌，写1清除）
                        - Event Ring Dequeue Pointer (位 4-63): 出队指针地址*/
        #define XHCI_ERDP_EHB (1<<3)
    } intr_regs[1024]; // 最大支持1024个中断器（根据HCSPARAMS1中的MaxIntrs）
} xhci_rt_regs_t;

// ===== 4. 门铃寄存器 (Doorbell Registers) =====
// 门铃寄存器数组 (每个设备槽一个 + 主机控制器)
typedef unsigned int xhci_db_regs_t;    // 最大支持256个设备槽(由HCSPARAMS1的MaxSlots决定）
                                        // - DB Target (位 0-7): 门铃目标
                                        // - 值为0：触发命令环（Command Ring）
                                        // - 值为1-31：触发特定端点（Endpoint 0-31）的传输环
                                        // - DB Stream ID (位 16-31): 流ID（仅用于支持流的设备）

// ===== 5. 扩展寄存器 (HCCPARAMS2) =====
// 当HCCPARAMS1[0] (AC64) 设置为1时出现
typedef struct {
    // 00h: U1设备退出延迟 (U1DEL)
    UINT32 u1del;   // 默认U1退出延迟

    // 04h: U2设备退出延迟 (U2DEL)
    UINT32 u2del;   // 默认U2退出延迟

    // ... 更多扩展寄存器 ...
} xhci_ext_regs_t;

/* xHCI 扩展能力 (xCAP) 结构体 */
typedef struct {
    union {
        /* 通用头部：所有扩展能力的第一个 32 位寄存器 */
        UINT32  cap_id;  /* 能力头部，位7:0 为 Capability ID ,位15:8 为Next Capability Pointer*/
        UINT32  next_ptr;

        /* 0x01: USB Legacy Support (USB 传统支持) */
        struct {
            UINT32 usblegsup;     /* 位16=1 bios控制，位24=1 os控制 */
            UINT32 usblegctlsts;  /* 位0: USB SMI启用
                                     位4: 主机系统错误SMI启用
                                     位13: OS所有权变更SMI启用
                                     位14: PCI命令变更SMI启用
                                     位15: BAR变更SMI启用

                                     === 高16位：SMI 状态/事件区域 ===
                                     RO：只读
                                     位16: 事件中断SMI状态(RO)
                                     位19:17 保留 (RsvdP)
                                     位20: 主机系统错误SMI状态(RO)
                                     位28:21 保留 (RsvdZ)

                                     RW1C：写1清除
                                     位29: OS所有权变更SMI状态(RW1C)
                                     位30: PCI命令变更SMI状态(RW1C)
                                     位31: BAR变更SMI状态(RW1C)*/
        } legacy_support;

        /* 0x02: Supported Protocol Capability (支持的协议能力) */
        struct {
            UINT32 protocol_ver;   /* 位 23:16 小修订版本0x10 = x.10
                                      位 31:24 主修订版本0x03 = 3.x */
            UINT8  name[4];           /* 位 31:0 4个asci字符 */
            UINT32 port_info;      /* 位7:0 兼容端口偏移
                                      位15:8 兼容端口计数偏移
                                      位31:28 协议速度 ID 计数 - RO，PSI 字段数量 (0-15)*/
            UINT32 protocol_slot_type;/* 位4:0 协议插槽类型 */
            UINT32 protocol_speed[15]; /* 位3:0 协议速度 ID 值 - RO，Port Speed ID (1-15)
                                      位5:4 协议速度标识 0=b/s 1=Kb/s 2=Mb/s 3=Gb/s
                                      位7:6 SI 类型 - RO，对称性 (0=对称, 2/3=非对称)
                                      位8   全双工 - RO，1=全双工
                                      位15:14 链路协议 - RO，1=定义链路协议
                                      位31:16 协议速度 ID 尾数 - RO，速度尾数*/
        } supported_protocol;

        /* 0x03: Extended Power Management (扩展电源管理) */
        struct {
            UINT32 pwr_mgmt_cap;  /* 电源管理能力寄存器：描述电源管理功能 */
            UINT32 pwr_mgmt_ctrl; /* 电源管理控制寄存器：控制电源管理行为 */
        } ext_power_mgmt;

        /* 0x04: I/O Virtualization (I/O 虚拟化) */
        struct {
            UINT32 virt_cap;  /* 虚拟化能力寄存器：描述虚拟化支持特性 */
            UINT32 virt_ctrl; /* 虚拟化控制寄存器：控制虚拟化行为 */
        } io_virt;

        /* 0x05: Message Interrupts (消息中断) */
        struct {
            UINT32 msi_cap;  /* MSI/MSI-X 能力寄存器：描述消息中断支持 */
            UINT32 msi_ctrl; /* MSI/MSI-X 控制寄存器：控制消息中断行为 */
        } msg_interrupts;

        /* 0x06: Latency Tolerance Messaging (延迟容忍消息) */
        struct {
            UINT32 ltm_cap;  /* LTM 能力寄存器：描述延迟容忍消息支持 */
            UINT32 ltm_ctrl; /* LTM 控制寄存器：控制 LTM 行为 */
        } latency_tolerance;

        /* 0x07: USB Debug Capability (USB 调试能力) */
        struct {
            UINT32 dbc_cap;   /* 调试能力寄存器：描述 USB 调试功能参数 */
            UINT32 dbc_ctrl;  /* 调试控制寄存器：控制调试行为 */
            UINT32 dbc_port;  /* 调试端口寄存器：指定调试端口号 */
        } usb_debug;
    };
} xhci_cap_t;

/* ERST条目结构 (16字节) */
typedef struct {
    UINT64 ring_seg_base;       // 段的64位物理基地址 (位[63:6]有效，位[5:0]为0)
    UINT32 ring_seg_size;       // 段中TRB的数量 (16到4096)
    UINT32 reserved;            // 保留位，初始化为0
} xhci_erst_t;

typedef struct {
    UINT64 parameter;
    UINT32 status;
    UINT32 control;
} xhci_trb_t;

/* xHCI 设备上下文结构（64 字节版本，CSZ=1） */
typedef struct {
    /* Slot Context（64 字节） */
    struct {
        UINT32 reg0;    /* 位 19:0 Route String[19:8] - 路由字符串的高 12 位，描述设备在 USB 拓扑中的路径。
                         * 位 23:20 portsc速度
                         * 位 25 MTT 多重验证。
                         * 位 26 1=集线器 0=usb。
                         * 位 31:27 端点上下文条目数量1-31 */

        UINT32 reg1;      /* 位 15:0  最大退出延迟微妙
                           * 位 23:16 根集线器端口号。
                           * 位 31:24 端口数量 */

        UINT32 reg2;      /* 位 7:0 父集线器插槽id
                           * 位 15:8 父端口号
                           * 位 17:16 事务转换器端口号 */

        UINT32 reg3;
        UINT32 reserved[12];     /* 保留字段：填充至 64 字节 */
    } slot;

    /* Endpoint 0 Context（默认控制端点，64 字节） */
    struct {
        UINT32 reg0;    /* 位 31-24: Max Endpoint Service Time Interval Payload High (Max ESIT Payload Hi) - 如果 LEC = '1'，表示 Max ESIT Payload 值的较高 8 位；如果 LEC = '0'，保留 (RsvdZ)。
                             * 位 23-16: Interval - 请求发送或接收数据的周期，单位为 125 μs，值为 2^(n-1) * 125 μs，参考 Table 6-12。
                             * 位 15: Linear Stream Array (LSA) - 标识 Stream ID 的解释方式，'0' 表示线性索引，'1' 表示二级 Stream Array 索引。
                             * 位 14-10: 保留 (RsvdZ)。
                             * 位 9-8: Mult - 如果 LEC = '0'，表示突发数范围 (0-3)；如果 LEC = '1'，计算为 ROUNDUP(Max Exit Payload / (Max Packet Size * (Max Burst Size + 1)) - 1)。
                             * 位 7-3: RsvdZ - 保留，置 0。
                             * 位 2-0: Endpoint State (EP State) - 端点状态 (0=Disabled，1=Running，2=Halted，3=Stopped，4=Error)。 */

        UINT32 reg1;        /* 端点类型：包含端点方向、类型等信息 */
        UINT64 tr_dequeue_pointer; /* 位 0：DCS（Dequeue Cycle State）。当DCS=1时，主机控制器从传输环中获取的TRB需要其Cycle Bit为1才会被处理；当RCS=0时，则处理Cycle Bit为0的TRB。
                                    * 位 63:4：TR Dequeue Pointer（TR 出队指针）。64位传输环物理地址64字节对齐 */
        UINT32 reg4;
        UINT32 reserved[11];     /* 保留字段：填充至 64 字节 */
    } ep[31];
} xhci_device_context64_t;

/* 修改设备上下文数据结构 */
typedef struct {
    UINT32 drop_context;                    // 位图：指示要丢弃的上下文（通常为 0）
    UINT32 add_context;                     // 位图：指示要添加/更新的上下文
    UINT32 reserved[14];                    // 保留字段，填 0
    xhci_device_context64_t dev_ctx;        // 需要修改的设备上下文
} xhci_input_context64_t;

/* xHCI 设备上下文结构（32 字节版本，CSZ=0） */
typedef struct {
    /* Slot Context（32 字节） */
    struct {
        UINT32 reg0;    /* 位 19:0 Route String[19:8] - 路由字符串的高 12 位，描述设备在 USB 拓扑中的路径。
                         * 位 23:20 portsc速度
                         * 位 25 MTT 多重验证。
                         * 位 26 1=集线器 0=usb。
                         * 位 31:27 上下文条目 */

        UINT32 reg1;      /* 位 15:0  最大退出延迟微妙
                           * 位 23:16 根集线器端口号。
                           * 位 31:24 端口数量 */

        UINT32 reg2;      /* 位 7:0 父集线器插槽id
                           * 位 15:8 父端口号
                           * 位 17:16 事务转换器端口号 */

        UINT32 reg3;
        UINT32 reserved[4];     /* 保留字段：填充至 32 字节 */
    } slot;

    /* Endpoint 0 Context（默认控制端点，32 字节） */
    struct {
        UINT32 reg0;    /* 位 31-24: Max Endpoint Service Time Interval Payload High (Max ESIT Payload Hi) - 如果 LEC = '1'，表示 Max ESIT Payload 值的较高 8 位；如果 LEC = '0'，保留 (RsvdZ)。
                             * 位 23-16: Interval - 请求发送或接收数据的周期，单位为 125 μs，值为 2^(n-1) * 125 μs，参考 Table 6-12。
                             * 位 15: Linear Stream Array (LSA) - 标识 Stream ID 的解释方式，'0' 表示线性索引，'1' 表示二级 Stream Array 索引。
                             * 位 14-10: 保留 (RsvdZ)。
                             * 位 9-8: Mult - 如果 LEC = '0'，表示突发数范围 (0-3)；如果 LEC = '1'，计算为 ROUNDUP(Max Exit Payload / (Max Packet Size * (Max Burst Size + 1)) - 1)。
                             * 位 7-3: RsvdZ - 保留，置 0。
                             * 位 2-0: Endpoint State (EP State) - 端点状态 (0=Disabled，1=Running，2=Halted，3=Stopped，4=Error)。 */

        UINT32 reg1;        /* 端点类型：包含端点方向、类型等信息 */
        UINT64 tr_dequeue_ptr; /* 位 0：DCS（Dequeue Cycle State）。当DCS=1时，主机控制器从传输环中获取的TRB需要其Cycle Bit为1才会被处理；当RCS=0时，则处理Cycle Bit为0的TRB。
                                    * 位 63:4：TR Dequeue Pointer（TR 出队指针）。64位传输环物理地址64字节对齐 */
        UINT32 reg4;
        UINT32 reserved[3];     /* 保留字段：填充至 32 字节 */
    } ep[31];
} xhci_device_context32_t;

/* 修改设备上下文数据结构 */
typedef struct {
    UINT32 drop_context;                // 位图：指示要丢弃的上下文（通常为 0）
    UINT32 add_context;                 // 位图：指示要添加/更新的上下文
    UINT32 reserved[6];                 // 保留字段，填 0
    xhci_device_context32_t dev_ctx;
} xhci_input_context32_t;


// ===== 完整xHCI寄存器结构 =====
typedef struct {
    xhci_cap_regs_t *cap;        // 能力寄存器
    xhci_op_regs_t  *op;         // 操作寄存器
    xhci_rt_regs_t  *rt;         // 运行时寄存器
    xhci_db_regs_t  *db;         // 门铃寄存器
    xhci_ext_regs_t *ext;        // 扩展寄存器

    UINT64          *dcbaap;                //设备上下文
    xhci_trb_t      *cr_queue_ptr;          //命令环入队列指针
    xhci_trb_t      *evt_queue_ptr;         //事件环入队列指针
    xhci_trb_t      *ep0_tr_queue_ptr;      //端点0传输环虚拟地址
    UINT8           event_c;                //事件环C位标志
    UINT32          align_size;             //对齐边界
} xhci_regs_t;



#pragma pack(pop)

void init_xhci(void);