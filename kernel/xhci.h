#pragma once
#include "moslib.h"
#include "slub.h"
#include "vmm.h"

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

#pragma pack(push,1)

// ===== 1. 能力寄存器 (Capability Registers) =====
typedef struct {
    // 00h: 能力长度和版本 (CAPLENGTH/HCIVERSION)
    uint8 cap_length; // [7:0] 能力寄存器总长度 (字节)
    uint8 reserved0; // 保留
    uint16 hciversion; // 控制器版本 (0x100 = 1.0.0, 0x110 = 1.1.0, 0x120 = 1.2.0)

    // 04h: 硬件参数寄存器 (HCSPARAMS1)
    uint32 hcsparams1; /*[7:0]   MaxSlots: 支持的最大设备槽数（最大256）
                         [18:8]  MaxIntrs: 支持的中断向量数（最大2048）
                         [24:31] MaxPorts: 支持的根端口数（最大256）*/

    // 08h: 硬件参数寄存器 (HCSPARAMS2)
    uint32 hcsparams2; /*[3:0]	    IST 等时调度阈值，单位为微帧（125us）。Host Controller 在这个阈值之后的同一帧内不再调度新的等时传输。常见值：0~8。
                              [7:4]	    ERST Max 硬件支持的事件环段表最大条目数2^n ERST MAX = 8 则条目等于256。
                              [25:21]	Max Scratchpad Buffers (hi  5 bits)	最大暂存器缓冲区数量的高5位（范围 0~1023）。
                              [31:27]	Max Scratchpad Buffers (Lo 5 bits)	最大暂存器缓冲区数量的低5位*/

    // 0Ch: 硬件参数寄存器 (HCSPARAMS3)
    uint32 hcsparams3; /*[7:0]     U1DeviceExitLatency: U1设备退出延迟（以微秒为单位）
                              [15:8]    U2DeviceExitLatency: U2设备退出延迟（以微秒为单位）*/

    // 10h: 硬件参数寄存器 (HCCPARAMS1)
    uint32 hccparams1; /*- AC64 (位 0): 64位寻址能力（1=支持，0=不支持）
                              - BNC (位 1): 带宽协商能力
                              - CSZ (位 2): 上下文大小（0=32字节，1=64字节）
                              - PPC (位 3): 端口电源控制能力
                              - PIND (位 4): 端口指示器能力
                              - LHRC (位 5): 轻量级主机路由能力
                              - LTC (位 6): 延迟容忍能力
                              - NSS (位 7): 无嗅探能力
                              - MaxPSASize (位 12-15): 最大主控制器流数组大小
                              - ECPA(位16-31)：扩展能力链表偏移地址 = 偏移<<2 */
#define HCCP1_CSZ   (1<<2)

    uint32 dboff; // 0x14 门铃寄存器偏移

    uint32 rtsoff; // 0x18 运行时寄存器偏移

    uint32 hccparams2; /* - U3C (位 0): U3转换能力
                               - CMC (位 1): 配置最大能力
                               - FSC (位 2): 强制保存上下文能力
                               - CTC (位 3): 符合性测试能力
                               - LEC (位 4): 大型ESIT有效负载能力
                               - CIC (位 5): 配置信息能力*/
} xhci_cap_regs_t;

// ===== 2. 操作寄存器 (Operational Registers) =====
typedef struct {
    // 00h: 命令寄存器 (USBCMD)
    uint32 usbcmd; /*- R/S (位 0): 运行/停止（1=运行，0=停止）
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
    uint32 usbsts; /* - HCH (位 0): 主机控制器停止（1=已停止，0=运行中）
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
    uint32 pagesize; // 控制器支持的页面大小*0x1000

    // 0Ch: 保留 [RsvdZ]
    uint32 reserved0[2];

    //0x14: 设备通知控制寄存器 (DNCTRL)
    uint32 dnctrl; // - 每位对应一个设备槽的使能（0=禁用，1=使能）

    //0x18 命令环控制寄存器 (CRCR)
    uint64 crcr;
    /*- 位[0] - RCS（Ring Cycle State，环周期状态）：当RCS=1时，主机控制器从命令环中获取的TRB需要其Cycle Bit为1才会被处理；当RCS=0时，则处理Cycle Bit为0的TRB。
                      - 位[1] - CS（Command Stop，命令停止）：当置1时，命令环在完成当前命令后停止运行。
                      - 位[2] - CA（Command Abort，命令中止）：当置1时，命令环立即停止，当前正在执行的命令被中止。
                      - 位[3] - CRR（Command Ring Running，命令环运行状态）：为1时表示命令环正在运行，为0时表示命令环已停止。
                      - 位[6:63] - Command Ring Pointer（命令环指针）：指向命令环的64位基地址（物理地址）。低6位必须为0（即地址必须64字节对齐）*/

    //0x20: 保留字段
    uint64 reserved1[2];

    //0x30h: 设备上下文基础地址数组指针 (DCBAAP)
    uint64 dcbaap; // DCBAA的物理地址指针

    // 38h: 配置寄存器 (CONFIG)
    uint32 config; // [7:0] 启用的设备槽数 (值≤MaxSlots)

    // 保留字段 (Reserved), 偏移 0x3C-0x3FF, 填充到端口寄存器之前
    uint32 reserved2[241];

    // 端口寄存器数组 (PORTSC, PORTPMSC, PORTLI, PORTHLPMC), 偏移 0x400起,每个端口占用16字节，按端口数量动态分配
    struct {
        // 端口状态和控制寄存器 (PORTSC), 32位
        uint32 portsc; /*  - CCS (位 0): 当前连接状态（1=设备连接 0=设备未连接）
                               - PED (位 1): 端口已启用/禁用 （1=启用 0=禁用）
                               - TM  (位 2)：1=隧道模式 0=本地模式
                               - OCA (位 3)：过电流激活 （1=该端口处于过流状态 0=该端口不存在过流情况）
                               - PR (位 4): 端口复位 （1=端口复位信令已断言 0=端口未复位）
                               - PLS (位 5-8): 端口链路状态 0x0：U0 /0x1：U1 /0x2：U2 /0x3：U3 /0x4：Disabled /0x5：RxDetect /0x6：Inactive /0x7：Polling /0x8：Recovery /0x9：Hot Reset /0xA：Compliance Mode /0xB：Test Mode /0xF：Resume
                               - PP (位 9): 端口电源 默认=1
                               - PortSpeed (位 10-13): 端口速度 0：未定义（通常表示端口还没被 reset 初始化出有效速率）/1：Full Speed /2：Low Speed /3：High Speed /4：SuperSpeed /5：SuperSpeedPlus（SSP） /6–15：保留
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
                               - WCE (位 25): 连接唤醒使能
                               - WDE (位 26): 断开唤醒使能
                               - WOE (位 27)：过流唤醒使能
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
#define XHCI_PORTSC_SPEED_FULL 1
#define XHCI_PORTSC_SPEED_LOW 2
#define XHCI_PORTSC_SPEED_HIGH 3
#define XHCI_PORTSC_SPEED_SUPER 4
#define XHCI_PORTSC_SPEED_SUPER_PLUS 5
#define XHCI_PORTSC_PIC_SHIFT 14
#define XHCI_PORTSC_PIC_MASK 0x3
#define XHCI_PORTSC_W1C_MASK 0xFE0000
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
        uint32 portpmsc;

        // 端口链路信息寄存器 (PORTLI), 提供链路错误计数等信息
        uint32 portli;

        // 主机控制器端口电源管理控制寄存器 (PORTHLPMC), 仅用于USB2协议端口，控制高级电源管理
        uint32 porthlpmc;
    } portregs[256]; // 最大支持256个端口（根据HCSPARAMS1中的MaxPorts）
} xhci_op_regs_t;

// ===== 3. 运行时寄存器 (Runtime Registers) =====
typedef struct {
    // 00h: 微帧索引寄存器 (MFINDEX)
    uint32 mfindex; // [13:0] 当前微帧索引（按125μs递增）

    // 04h: 保留
    uint32 reserved0[7];

    // 中断管理数组 (IMAN) - 每个中断向量一个
    struct {
        // 中断管理寄存器 (IMAN), 偏移 0x00
        uint32 iman; // 中断管理 [0]：IP中断挂起（1=有中断待处理），[1]：中断使能（1=使能，0=禁用）

        //中断调节寄存器 (IMOD), 偏移 0x04,
        uint32 imod; // 中断调制器 (位 0-15): 中断调节间隔（以250ns为单位，(位 16-31): 中断调节计数器（只读）

        // 事件环段表大小寄存器 (ERSTSZ), 偏移 0x08, 32位
        uint32 erstsz; // - ERST Size (位 0-15): 事件环段表条目数（最大4096）
        uint32 reserved1;

        // 事件环段表基地址寄存器 (ERSTBA), 偏移 0x10-0x17, 64位
        uint64 erstba; //指向事件环段表的64位基地址（对齐到64字节)

        // 事件环出队指针寄存器 (ERDP), 偏移 0x18-0x1F, 64位
        uint64 erdp; /*指向事件环的当前出队指针
                        - DESI (位 0-2): 出队事件环段索引
                        - EHB (位 3): 事件处理忙碌（1=忙碌，写1清除）
                        - Event Ring Dequeue Pointer (位 4-63): 出队指针地址*/
#define XHCI_ERDP_EHB (1<<3)
    } intr_regs[1024]; // 最大支持1024个中断器（根据HCSPARAMS1中的MaxIntrs）
} xhci_rt_regs_t;

// ===== 4. 门铃寄存器 (Doorbell Registers) =====
// 门铃寄存器数组 (每个设备槽一个 + 主机控制器)
typedef unsigned int xhci_db_regs_t;     /*最大支持256个设备槽(由HCSPARAMS1的MaxSlots决定）
                                         - DB Target (位 0-7): 门铃目标
                                         - 值为0：触发命令环（Command Ring）
                                         - 值为1-31：触发特定端点（Endpoint 0-31）的传输环
                                         - DB Stream ID (位 16-31): 流ID（仅用于支持流的设备）*/

// ===== 5. 扩展寄存器 (HCCPARAMS2) =====
// 当HCCPARAMS1[0] (AC64) 设置为1时出现
typedef struct {
    // 00h: U1设备退出延迟 (U1DEL)
    uint32 u1del; // 默认U1退出延迟

    // 04h: U2设备退出延迟 (U2DEL)
    uint32 u2del; // 默认U2退出延迟

    // ... 更多扩展寄存器 ...
} xhci_ext_regs_t;

/* 0x01: USB Legacy Support (USB 传统支持) */
typedef struct {
        uint32 usblegsup; /* 位16=1 bios控制，位24=1 os控制 */
        uint32 usblegctlsts; /* 位0: USB SMI启用
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
}xhci_ecap_legacy_support;

/* 0x02: Supported Protocol Capability (支持的协议能力) */
typedef struct {
    uint32 protocol_ver;    /* 位 23:16 小修订版本0x10 = x.10
                              位 31:24 主修订版本0x03 = 3.x */
    char8 name[4];            /* 位 31:0 4个asci字符 */
    uint32 port_info;         /* 位7:0 兼容端口偏移
                              位15:8 兼容端口计数偏移
                              位31:28 协议速度 ID 计数 - RO，PSI 字段数量 (0-15)*/
    uint32 protocol_slot_type; /* 位4:0 协议插槽类型 */
    uint32 protocol_speed[15]; /* PSIV = bits[3:0]：Protocol Speed ID Value（会出现在 PORTSC 的 Port Speed 字段里）
                                  PSIE = bits[5:4]：指数档，决定单位（0=bit/s，1=Kb/s，2=Mb/s，3=Gb/s）
                                  PLT = bits[7:6]：PSI 类型（0=对称，2=非对称Rx，3=非对称Tx；非对称必须成对）
                                  PFD = bit[8]：是否全双工（1=全双工，0=半双工）
                                  LP = bits[15:14]：Link Protocol。对 USB2（Major=02h）要求为 0，具体是 LS/FS/HS 由速率决定。
                                  PSIM = bits[31:16]：速率尾数（mantissa）*/
}xhci_ecap_supported_protocol;

/* ERST条目结构 (16字节) */
typedef struct {
    uint64 ring_seg_base; // 段的64位物理基地址 (位[63:6]有效，位[5:0]为0)
    uint32 ring_seg_size; // 段中TRB的数量 (16到4096)
    uint32 reserved; // 保留位，初始化为0
} xhci_erst_t;

typedef struct {
    uint64 member0;
    uint64 member1;
} trb_t;

//region 设备上下文结构

/* Slot Context（64 字节） */
typedef struct {
    uint32 route_speed; /* 位 19:0 Route String[19:8] - 路由字符串的高 12 位，描述设备在 USB 拓扑中的路径。
                         * 位 23:20 portsc速度
                         * 位 25 MTT 多重验证。
                         * 位 26 1=集线器 0=usb。
                         * 位 31:27 端点上下文条目数量1-31 */

    uint32 latency_hub; /* 位 15:0  最大退出延迟微妙
                               * 位 23:16 根集线器端口号 1 - MaxPorts。
                               * 位 31:24 端口数量 */

    uint32 parent_info; /* 位 7:0 父集线器插槽id
                               * 位 15:8 父端口号
                               * 位 17:16 事务转换器端口号 */

    uint32 addr_status; /* 位 7:0 usb地址
                            * 位 31:27 插槽状态 0=禁用/启用 1=默认值 2=地址 3=配置*/
    uint32 reserved[12]; /* 保留字段：填充至 64 字节 */
    } slot64_t;

    /* Endpoint 0 Context（默认控制端点，64 字节） */
typedef struct {
        uint32 ep_config;
        /* 位 31-24: Max Endpoint Service Time Interval Payload High (Max ESIT Payload Hi) - 如果 LEC = '1'，表示 Max ESIT Payload 值的较高 8 位；如果 LEC = '0'，保留 (RsvdZ)。
         * 位 23-16: Interval - 请求发送或接收数据的周期，单位为 125 μs，值为 2^(n-1) * 125 μs，参考 Table 6-12。
         * 位 15: Linear Stream Array (LSA) - 标识 Stream ID 的解释方式，'0' 表示线性索引，'1' 表示二级 Stream Array 索引。
         * 位 14-10：MAXPStreams 主数据流最大值
         * 位 9-8: Mult - 如果 LEC = '0'，表示突发数范围 (0-3)；如果 LEC = '1'，计算为 ROUNDUP(Max Exit Payload / (Max Packet Size * (Max Burst Size + 1)) - 1)。
         * 位 2-0: Endpoint State (EP State) - 端点状态 (0=已禁用，1=运行中，2=暂停，3=停止，4=错误)。 */

        uint32 ep_type_size; /* 位 2:1 错误计数
                                 * 位 5:3 端点类型
                                                1=Isoch Out (主机→设备)同步传输（实时数据流）
                                                2=Bulk Out批量传输（大容量非实时数据）
                                                3=Interrupt Out中断传输（低延迟小数据）
                                                4=Control Bidirectional控制传输（双向，设备配置/命令）
                                                5=Isoch In (设备→主机)同步传输
                                                6=Bulk In 批量传输
                                                7=Interrupt In 中断传输
                                 * 位 7 流传输（Streams）开关控制 0=启用流传输（默认）1=禁用主机发起的流选择（需手动管理流ID）
                                 * 位 15:8 最大突发大小
                                 * 位 31:16 最大包大小（Max Packet Size）*/
        // xHCI 端点类型 (Endpoint Type)
        #define EP_TYPE_ISOCH_OUT       (1 << 3)   // 同步传输 OUT（主机→设备）
        #define EP_TYPE_BULK_OUT        (2 << 3)   // 批量传输 OUT（主机→设备）
        #define EP_TYPE_INTERRUPT_OUT   (3 << 3)   // 中断传输 OUT（主机→设备）
        #define EP_TYPE_CONTROL         (4 << 3)   // 控制传输 双向（EP0）
        #define EP_TYPE_ISOCH_IN        (5 << 3)   // 同步传输 IN（设备→主机）
        #define EP_TYPE_BULK_IN         (6 << 3)   // 批量传输 IN（设备→主机）
        #define EP_TYPE_INTERRUPT_IN    (7 << 3)   // 中断传输 IN（设备→主机）
        uint64 tr_dequeue_ptr;
        /* 位 0：DCS（Dequeue Cycle State）。当DCS=1时，主机控制器从传输环中获取的TRB需要其Cycle Bit为1才会被处理；当RCS=0时，则处理Cycle Bit为0的TRB。
                                            * 位 63:4：TR Dequeue Pointer（TR 出队指针）。64位传输环物理地址64字节对齐 */
        uint32 trb_payload; /*
                                     * 位 15:0 trb平均长度
                                     * 位 31:16  最大ESIT有效载荷低16位（Max ESIT Payload Lo)*/
        uint32 reserved[11]; /* 保留字段：填充至 64 字节 */
    }ep64_t;

/* xHCI 设备上下文结构（64 字节版本，CSZ=1） */
typedef struct {
    slot64_t slot;
    ep64_t ep[31];
} xhci_device_context64_t;

typedef struct {
    uint32 drop_context; // Context Drop Flag 位图
    uint32 add_context; // Context Add Flag 位图
    uint32 reserved[14];
} input_control_context64_t;

/* 修改设备上下文数据结构 */
typedef struct {
    input_control_context64_t control;
    xhci_device_context64_t dev_ctx; // 需要修改的设备上下文
} xhci_input_context64_t;


/* Slot Context（32 字节） */
typedef struct {
    uint32 route_speed; /* 位 19:0 Route String[19:8] - 路由字符串的高 12 位，描述设备在 USB 拓扑中的路径。
                         * 位 23:20 portsc速度
                         * 位 25 MTT 多重验证。
                         * 位 26 1=集线器 0=usb。
                         * 位 31:27 端点上下文条目数量1-31 */

    uint32 latency_hub; /* 位 15:0  最大退出延迟微妙
                               * 位 23:16 根集线器端口号 1 - MaxPorts。
                               * 位 31:24 端口数量 */

    uint32 parent_info; /* 位 7:0 父集线器插槽id
                               * 位 15:8 父端口号
                               * 位 17:16 事务转换器端口号 */

    uint32 addr_status; /* 位 7:0 usb地址
                            * 位 31:27 插槽状态 0=禁用/启用 1=默认值 2=地址 3=配置*/
    uint32 reserved[4]; /* 保留字段：填充至 32 字节 */
} slot32_t;

/* Endpoint 0 Context（默认控制端点，32 字节） */
typedef struct {
        uint32 ep_config;
        /* 位 31-24: Max Endpoint Service Time Interval Payload High (Max ESIT Payload Hi) - 如果 LEC = '1'，表示 Max ESIT Payload 值的较高 8 位；如果 LEC = '0'，保留 (RsvdZ)。
         * 位 23-16: Interval - 请求发送或接收数据的周期，单位为 125 μs，值为 2^(n-1) * 125 μs，参考 Table 6-12。
         * 位 15: Linear Stream Array (LSA) - 标识 Stream ID 的解释方式，'0' 表示线性索引，'1' 表示二级 Stream Array 索引。
         * 位 14-10：MAXPStreams 主数据流最大值
         * 位 9-8: Mult - 如果 LEC = '0'，表示突发数范围 (0-3)；如果 LEC = '1'，计算为 ROUNDUP(Max Exit Payload / (Max Packet Size * (Max Burst Size + 1)) - 1)。
         * 位 2-0: Endpoint State (EP State) - 端点状态 (0=已禁用，1=运行中，2=暂停，3=停止，4=错误)。 */

        uint32 ep_type_size; /* 位 2:1 错误计数
                                 * 位 5:3 端点类型
                                                1=Isoch Out (主机→设备)同步传输（实时数据流）
                                                2=Bulk Out批量传输（大容量非实时数据）
                                                3=Interrupt Out中断传输（低延迟小数据）
                                                4=Control Bidirectional控制传输（双向，设备配置/命令）
                                                5=Isoch In (设备→主机)同步传输
                                                6=Bulk In 批量传输
                                                7=Interrupt In 中断传输
                                 * 位 7 流传输（Streams）开关控制 0=启用流传输（默认）1=禁用主机发起的流选择（需手动管理流ID）
                                 * 位 15:8 Max Packet Size 最大突发大小(0-15)
                                 * 位 31:16 最大包大小（Max Packet Size）*/

        uint64 tr_dequeue_ptr;
        /* 位 0：DCS（Dequeue Cycle State）。当DCS=1时，主机控制器从传输环中获取的TRB需要其Cycle Bit为1才会被处理；当RCS=0时，则处理Cycle Bit为0的TRB。
                                            * 位 63:4：TR Dequeue Pointer（TR 出队指针）。64位传输环物理地址64字节对齐 */
        uint32 trb_payload; /*
                                     * 位 15:0 trb平均长度
                                     * 位 31:16  最大ESIT有效载荷低16位（Max ESIT Payload Lo)*/
        uint32 reserved[3]; /* 保留字段：填充至 32 字节 */
    }ep32_t;

/* xHCI 设备上下文结构（32 字节版本，CSZ=0） */
typedef struct {
    slot32_t slot;
    ep32_t ep[31];
} xhci_device_context32_t;

typedef struct {
    uint32 drop_context; // Context Drop Flag 位图
    uint32 add_context; // Context Add Flag 位图
    uint32 reserved[6]; // 保留字段，填 0
} xhci_input_control_context32_t;

/* 修改设备上下文数据结构32字节 */
typedef struct {
    xhci_input_control_context32_t control;
    xhci_device_context32_t dev_ctx;
} xhci_input_context32_t;

typedef struct {
    union {
        xhci_device_context32_t dev_ctx32;
        xhci_device_context64_t dev_ctx64;
    };
} xhci_device_context_t;

typedef struct {
    union {
        xhci_input_context32_t input_ctx32;
        xhci_input_context64_t input_ctx64;
    };
} xhci_input_context_t;

typedef struct {
    uint64 tr_dequeue; // TR Dequeue Ptr+ DCS(位0)
    uint64 reserved;
} xhci_stream_ctx_t;

//endregion

typedef struct {
    trb_t   *ring_base; //环起始地址
    uint64  index; //trb索引
    uint64  status_c; //循环位
} xhci_ring_t;

#pragma pack(pop)


struct usb_dev_t;
struct usb_hub_t;

typedef enum {
    XHCI_PORT_EMPTY = 0,
    XHCI_PORT_DEV,
    XHCI_PORT_HUB,
} xhci_port_type_t;

//xhci端口
typedef struct {
    union {
        struct usb_hub_t *usb_hub;      // hub设备
        struct usb_dev_t *usb_dev;      // usb设备
    };
   xhci_port_type_t type;                     //1 = usb_dev , 2 = usb_hub;
}xhci_port;


typedef struct {
    uint8  major_bcd;           // 协议主版本（DW0[31:24]，常见 0x02=USB2，0x03=USB3.x）
    uint8  minor_bcd;           // 协议次版本（DW0[23:16]，如 0x10=USB3.1 等）
    char8  name[4];             // 协议名字符串（DW1，常见 "USB " = 0x20425355）
    uint16 proto_defined;       // 协议自定义字段（DW2[27:16]，USB2/USB3 各自有含义）
    uint8  port_first;          // 覆盖端口起始号（DW2[7:0]，1-based）
    uint8  port_count;          // 连续覆盖端口数量（DW2[15:8]）
    uint8  slot_type;           // Protocol Slot Type（DW3[4:0]）
    uint8  psi_count;           // PSI 条目数 PSIC（DW2[31:28]，0=默认映射，>0=显式 PSI 表）
    uint32 *psi;                // 按 PSIV 索引的 PSI 原始 dword（用于解释 PortSC speed）
} xhci_spc_t;


//xhci控制器
typedef struct {
    uint8               major_bcd;          // 主版本
    uint8               minor_bcd;          // 次版本
    xhci_cap_regs_t     *cap_reg;           // 能力寄存器
    xhci_op_regs_t      *op_reg;            // 操作寄存器
    xhci_rt_regs_t      *rt_reg;            // 运行时寄存器
    xhci_db_regs_t      *db_reg;            // 门铃寄存器
    xhci_ext_regs_t     *ext_reg;           // 扩展寄存器
    uint64              *dcbaap;            // 设备上下文插槽
    xhci_port           *ports;             // 端口
    xhci_ring_t         cmd_ring;           // 命令环
    xhci_ring_t         event_ring;         // 事件环
    uint32              align_size;         // xhci内存分配对齐边界
    uint8               dev_ctx_size;       // 设备上下文字节数（32或64字节）
    uint8               max_ports;          // 最大端口数量
    uint8               max_slots;          // 最大插槽数量
    uint16              max_intrs;          // 最大中断数量
    uint8               spc_count;          // spc数量
    xhci_spc_t          *spc;               // 支持的协议功能
    uint8               *port_to_spc;       // 端口找spc号
} xhci_controller_t;

//定时
static inline void timing(void) {
    // uint64 count = 20000000;
    // while (count--) pause();
}

//region 命令环trb
#define TRB_TYPE_ENABLE_SLOT             (9UL << 42)   // 启用插槽
#define TRB_TYPE_ADDRESS_DEVICE          (11UL << 42)  // 设备寻址
#define TRB_TYPE_CONFIGURE_ENDPOINT      (12UL << 42)  // 配置端点
#define TRB_TYPE_EVALUATE_CONTEXT        (13UL << 42)  // 评估上下文

/*
 * 启用插槽命令trb
 * uint64 member0 位0-63 = 0
 *
 * uint64 member1 位32    cycle
 *                位42-47 TRB Type 类型
 */
static inline void enable_slot_com_trb(trb_t *trb) {
    trb->member0 = 0;
    trb->member1 = TRB_TYPE_ENABLE_SLOT;
}

/*
 * 设置设备地址命令trb
 * uint64 member0 位0-63 = input context pointer 物理地址
 *
 * uint64 member1 位32    cycle
 *                位41    bsr 块地址请求命令 0=发送usb_set_address请求，1=不发送（一般设置0）
 *                位42-47 TRB Type 类型
 *                位56-63 slot id
 */
static inline void addr_dev_com_trb(trb_t *trb, uint64 input_ctx_ptr, uint64 slot_id) {
    trb->member0 = input_ctx_ptr;
    trb->member1 = TRB_TYPE_ADDRESS_DEVICE | (slot_id << 56);
}

/*
 * 配置端点trb
 * uint64 member0 位0-63 = input context pointer 物理地址
 *
 * uint64 member1 位32    cycle
 *                位41    dc    接触配置 1= xhci将忽略输入上下文指针字段，一般设置0
 *                位42-47 TRB Type 类型
 *                位56-63 slot id
 */
static inline void config_endpoint_com_trb(trb_t *trb, uint64 input_ctx_ptr, uint64 slot_id) {
    trb->member0 = input_ctx_ptr;
    trb->member1 = TRB_TYPE_CONFIGURE_ENDPOINT | (slot_id << 56);
}

/*
 * 评估上下文trb
 * uint64 member0 位0-63 = input context pointer 物理地址
 *
 * uint64 member1 位32    cycle
 *                位41    bsr 块地址请求命令 0=发送usb_set_address请求，1=不发送（一般设置0）
 *                位42-47 TRB Type 类型
 *                位56-63 slot id
 */
static inline void evaluate_context_com_trb(trb_t *trb, uint64 input_ctx_ptr, uint64 slot_id) {
    trb->member0 = input_ctx_ptr;
    trb->member1 = TRB_TYPE_EVALUATE_CONTEXT | (slot_id << 56);
}

//endregion

//region 端点控制环trb

#define TRB_TYPE_SETUP_STAGE             (2UL << 42)   // 设置阶段
#define TRB_TYPE_DATA_STAGE              (3UL << 42)   // 数据阶段
#define TRB_TYPE_STATUS_STAGE            (4UL << 42)   // 状态阶段

typedef enum {
    disable_ioc = 0UL << 37,
    enable_ioc = 1UL << 37,
} config_ioc_e;

typedef enum {
    trb_out = 0UL << 48,
    trb_in = 1UL << 48,
} trb_dir_e;

typedef enum {
    disable_ch = (0UL << 33),
    enable_ch = (1UL << 33)
} config_ch_e;

typedef enum {
    usb_req_get_status = 0x00 << 8, /* 获取状态
                                               - 接收者：设备、接口、端点
                                               - 返回：设备/接口/端点的状态（如挂起、遥控唤醒）
                                               - w_value: 0
                                               - w_index: 设备=0，接口=接口号，端点=端点号
                                               - w_length: 2（返回 2 字节状态） */
    usb_req_clear_feature = 0x01UL << 8, /* 清除特性
                                               - 接收者：设备、接口、端点
                                               - 用途：清除特定状态（如取消遥控唤醒或端点暂停）
                                               - w_value: 特性选择（如 0=设备遥控唤醒，1=端点暂停）
                                               - w_index: 设备=0，接口=接口号，端点=端点号
                                               - w_length: 0 */
    usb_req_set_feature = 0x03UL << 8, /* 设置特性
                                               - 接收者：设备、接口、端点
                                               - 用途：启用特定特性（如遥控唤醒、测试模式）
                                               - w_value: 特性选择（如 0=设备遥控唤醒，1=端点暂停）
                                               - w_index: 设备=0，接口=接口号，端点=端点号
                                               - w_length: 0 */
    usb_req_set_address = 0x05UL << 8, /* 设置设备地址
                                               - 接收者：设备
                                               - 用途：在枚举过程中分配设备地址（1-127）
                                               - w_value: 新地址（低字节）
                                               - w_index: 0
                                               - w_length: 0 */
    usb_req_get_descriptor = 0x06UL << 8, /* 获取描述符
                                               - 接收者：设备、接口
                                               - 用途：获取设备、配置、接口、字符串等描述符
                                               - w_value: 高字节=描述符类型（如 0x01=设备，0x02=配置），低字节=索引
                                               - w_index: 0（设备/配置描述符）或语言 ID（字符串描述符）
                                               - w_length: 请求的字节数 */
    usb_req_set_descriptor = 0x07UL << 8, /* 设置描述符
                                               - 接收者：设备、接口
                                               - 用途：更新设备描述符（较少使用）
                                               - w_value: 高字节=描述符类型，低字节=索引
                                               - w_index: 0 或语言 ID
                                               - w_length: 数据长度 */
    usb_req_get_config = 0x08UL << 8, /* 获取当前配置
                                               - 接收者：设备
                                               - 用途：返回当前激活的配置值
                                               - w_value: 0
                                               - w_index: 0
                                               - w_length: 1（返回 1 字节配置值） */
    usb_req_set_config = 0x09UL << 8, /* 设置配置
                                               - 接收者：设备
                                               - 用途：激活指定配置
                                               - w_value: 配置值（来自配置描述符的 b_configuration_value）
                                               - w_index: 0
                                               - w_length: 0 */
    usb_req_get_interface = 0x0AUL << 8, /* 获取接口的备用设置
                                               - 接收者：接口
                                               - 用途：返回当前接口的备用设置编号
                                               - w_value: 0
                                               - w_index: 接口号
                                               - w_length: 1（返回 1 字节备用设置值） */
    usb_req_set_interface = 0x0BUL << 8, /* 设置接口的备用设置
                                               - 接收者：接口
                                               - 用途：选择接口的备用设置
                                               - w_value: 备用设置编号
                                               - w_index: 接口号
                                               - w_length: 0 */
    usb_req_synch_frame = 0x0CUL << 8, /* 同步帧
                                               - 接收者：端点
                                               - 用途：为同步端点（如音频设备）提供帧编号
                                               - w_value: 0
                                               - w_index: 端点号
                                               - w_length: 2（返回 2 字节帧号） */

    usb_req_get_max_lun = 0xFEUL << 8, /* Mass Storage 类请求 (BOT)
                                      * - bRequestType=0xFE (Host→Interface)
                                      * - wValue=0, wIndex=接口号
                                      * - wLength=1
                                      * - 获取最大逻辑单元 返回最大 LUN 编号（0 = 1个LUN） */

    usb_req_mass_storage_reset = 0xFFUL << 8, /* Mass Storage 类请求 (BOT)
                                      * - bRequestType=0x21 (Host→Interface)
                                      * - wValue=0, wIndex=接口号
                                      * - wLength=0
                                      * - 用于复位 U 盘状态机 */
} setup_stage_req_e;

/*设置阶段trb
    uint64 member0;  *位4-0：接收者（0=设备，1=接口，2=端点，3=其他）
                     *位6-5：类型（0=标准，1=类，2=厂商，3=保留）
                     *位7：方向（0=主机到设备，1=设备到主机）
                     *位8-15  Request    请求代码，指定具体请求（如 GET_DESCRIPTOR、SET_ADDRESS）标准请求示例：0x06（GET_DESCRIPTOR）、0x05（SET_ADDRESS）
                     *位16-31 Value      请求值，具体含义由 b_request 定义 例如：GET_DESCRIPTOR 中，w_value 高字节为描述符类型，低字节为索引
                     *位32-47 Index      索引或偏移，具体含义由 b_request 定义 例如：接口号、端点号或字符串描述符索引
                     *位48-63 Length     数据阶段的传输长度（字节）主机到设备：发送的数据长度 设备到主机：请求的数据长度

    uint64 member1;  *位0-15  TRB Transfer Length  传输长度
                     *位22-31 Interrupter Target 中断目标
                     *位32    cycle
                     *位37    1=IOC 完成时中断
                     *位38    1=IDT 数据包含在trb
                     *位42-47 TRB Type 类型
                     *位48-49 TRT 传输类型 0=无数据阶段 1=保留 2=out(主机到设备) 3=in(设备到主机)
*/
typedef enum {
    setup_stage_norm = 0 << 5,
    setup_stage_calss = 1UL << 5,
    setup_stage_firm = 2UL << 5,
    setup_stage_reserve = 3UL << 5
} setup_stage_type_e;

typedef enum {
    setup_stage_out = 0 << 7,
    setup_stage_in = 1UL << 7
} setup_stage_dir_e;

typedef enum {
    no_data_stage = 0 << 48,
    out_data_stage = 2UL << 48,
    in_data_stage = 3UL << 48
} trb_trt_e;

typedef enum {
    setup_stage_device = 0 << 0,
    setup_stage_interface = 1UL << 0,
    setup_stage_endpoint = 2UL << 0
} setup_stage_receiver_e;

#define TRB_FLAG_IDT    (1UL<<38)

static inline void setup_stage_trb(trb_t *trb, setup_stage_receiver_e receiver,setup_stage_type_e type, setup_stage_dir_e dir,setup_stage_req_e req, uint64 value, uint64 index, uint64 length,uint64 trb_tran_length, trb_trt_e trt) {
    trb->member0 = receiver | type | dir | req | (value<<16) | (index<<32) | (length<<48);
    trb->member1 = (trb_tran_length << 0) | TRB_FLAG_IDT | TRB_TYPE_SETUP_STAGE | trt;
}

/*
 * 数据阶段trb
 * uint64 member0 位0-63 data buffer pointer 数据区缓冲区物理地址指针
 *
 *  uint64 member1 位0-16   trb transfer length 传输长度
 *                 位17-21 td size             剩余数据包
 *                 位22-31 Interrupter Target 中断目标
 *                 位32    cycle
 *                 位33    ent     1=评估下一个trb
 *                 位34    isp     1=短数据包中断
 *                 位35    ns      1=禁止窥探
 *                 位36    ch      1=链接 多个trb关联
 *                 位37    IOC     1=完成时中断
 *                 位38    IDT     1=数据包含在trb
 *                 位41    bei     1=块事件中端，ioc=1 则传输事件在下一个中断阀值时，ioc产生的中断不应向主机发送中断。
 *                 位42-47 TRB Type 类型
 *                 位48    dir     0=out(主机到设备) 1=in(设备到主机)
 */
#define TRB_FLAG_ENT    (1UL<<33)

static inline void data_stage_trb(trb_t *trb, uint64 data_buff_ptr, uint64 trb_tran_length, trb_dir_e dir) {
    trb->member0 = data_buff_ptr;
    trb->member1 = (trb_tran_length << 0) | TRB_TYPE_DATA_STAGE | TRB_FLAG_ENT | enable_ch | dir;
}


/*
 * 状态阶段trb
 * uint64 member0 位0-63 =0
 *
 * uint64 member1  位22-31 Interrupter Target 中断目标
 *                 位32    cycle
 *                 位33    ent     1=评估下一个trb
 *                 位36    ch      1=链接 多个trb关联
 *                 位37    IOC     1=完成时中断
 *                 位42-47 TRB Type 类型
 *                 位48    dir     0=out(主机到设备) 1=in(设备到主机)
 */
static inline void status_stage_trb(trb_t *trb, config_ioc_e ioc, trb_dir_e dir) {
    trb->member0 = 0;
    trb->member1 = ioc | TRB_TYPE_STATUS_STAGE | dir;
}

//endregion

//region 传输环trb
#define TRB_TYPE_NORMAL                  (1UL << 42)   // 普通传输
/*  普通传输trb
 *  uint64 member0 位0-63 data buffer pointer 数据区缓冲区物理地址指针
 *
 *  uint64 member1 位0-16  trb transfer length 传输长度
 *                 位17-21 td size             剩余数据包
 *                 位22-31 Interrupter Target 中断目标
 *                 位32    cycle
 *                 位33    ent     1=“评估下一个 TRB”，提示控制器：立即继续执行下一个 TRB，不必等事件或中断触发。
 *                 位34    isp     1=短数据包中断
 *                 位35    ns      1=禁止窥探
 *                 位36    ch      1=链接 多个trb关联
 *                 位37    IOC     1=完成时中断
 *                 位38    IDT     1=数据包含在trb
 *                 位41    bei     1=块事件中端，ioc=1 则传输事件在下一个中断阀值时，ioc产生的中断不应向主机发送中断。
 *                 位42-47 TRB Type 类型
 */
static inline void normal_transfer_trb(trb_t *trb, uint64 data_buff_ptr, config_ch_e ent_ch, uint64 trb_tran_length,
                                       config_ioc_e ioc) {
    trb->member0 = data_buff_ptr;
    trb->member1 = ent_ch | (trb_tran_length << 0) | ioc | TRB_TYPE_NORMAL;
}

//endregion

//region 其他trb
#define TRB_TYPE_LINK                    (6UL << 42)   // 链接
#define TRB_FLAG_TC                      (1UL << 33)
#define TRB_FLAG_CYCLE                   (1UL << 32)
/*  link trb
 *  uint64 member0 位0-63  ring segment pointer 环起始物理地址指针
 *
 *  uint64 member1 位22-31 Interrupter Target 中断目标
 *                 位32    cycle
 *                 位33    tc      1=下个环周期切换 0=不切换
 *                 位36    ch      1=链接 多个trb关联
 *                 位37    IOC     1=完成时中断
 *                 位42-47 TRB Type 类型
 */
static inline void link_trb(trb_t *trb, uint64 ring_base_ptr, uint64 cycle) {
    trb->member0 = ring_base_ptr;
    trb->member1 = cycle | TRB_FLAG_TC | TRB_TYPE_LINK;
}

//endregion


//初始化环
static inline int xhci_ring_init(xhci_ring_t *ring, uint32 align_size) {
    ring->ring_base = kzalloc(align_up(TRB_COUNT * sizeof(trb_t), align_size));
    ring->index = 0;
    ring->status_c = TRB_FLAG_CYCLE;
}

//响铃
static inline void xhci_ring_doorbell(xhci_controller_t *xhci_controller, uint8 db_number, uint32 value) {
    xhci_controller->db_reg[db_number] = value;
}

uint8 xhci_enable_slot(struct usb_dev_t *usb_dev);
void xhci_address_device(struct usb_dev_t *usb_dev);
int xhci_ring_enqueue(xhci_ring_t *ring, trb_t *trb);
int xhci_ering_dequeue(xhci_controller_t *xhci_controller, trb_t *evt_trb);
void xhci_input_context_add(xhci_input_context_t *input_ctx,void *from_ctx, uint32 ctx_size, uint32 ep_num);
void xhci_context_read(xhci_device_context_t *dev_context,void* to_ctx,uint32 ctx_size, uint32 ep_num);
uint8 xhci_ecap_find(xhci_controller_t *xhci_controller,void **ecap_arr,uint8 cap_id);




