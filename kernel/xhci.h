#pragma once
#include "moslib.h"
#include "pcie.h"

#pragma pack(push,1)

//======================== XHCI硬件mimo寄存器 ========================================

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

//==================================================================================




//============================= TRB集合 ===============================================

#define TRB_COUNT 256        //trb个数

// ============================================================================
// xHCI 规范 6.4.5: TRB 完成码 (按【事件归属类型】严格物理隔离版)
// 设计目的：完美适配 xhci_execute_command_sync 与 xhci_execute_transfer_sync
// ============================================================================
typedef enum : int8 {
    // ==========================================================
    // 【第 1 阵营：通用与系统级事件】 (Shared / System Level)
    // 归属：命令和传输都可能遇到，或者属于主板硬件级崩溃。
    // 处理建议：在 cmd 和 transfer 的函数中都需要做基础的拦截。
    // ==========================================================
    XHCI_COMP_TIMEOUT                    = -1, // [系统软件] 超时/未获取到事件
    XHCI_COMP_INVALID                    = 0,  // [通用] 非法状态
    XHCI_COMP_SUCCESS                    = 1,  // [通用] 完美成功
    XHCI_COMP_TRB_ERROR                  = 5,  // [通用] TRB 格式非法 (填错字段、Chain不对等)
    XHCI_COMP_RESOURCE_ERROR             = 7,  // [通用] 主板 xHC 控制器内部资源/内存耗尽
    XHCI_COMP_VF_EVENT_RING_FULL_ERROR   = 16, // [通用] 虚拟功能事件环满爆 (SR-IOV)
    XHCI_COMP_EVENT_RING_FULL_ERROR      = 21, // [通用] 真实事件环满爆 (内核中断处理太慢)
    XHCI_COMP_EVENT_LOST_ERROR           = 32, // [通用] 事件丢失 (事件环溢出导致主板丢弃回执)
    XHCI_COMP_UNDEFINED_ERROR            = 33, // [通用] 未定义的致命硬件崩溃

    // ==========================================================
    // 【第 2 阵营：命令事件专属】 (Command Event Only)
    // 归属：仅由 Command Ring 触发。
    // 处理建议：全部塞进 xhci_execute_command_sync 的 switch-case 中。
    // ==========================================================
    XHCI_COMP_BANDWIDTH_ERROR            = 8,  // [命令] 配置端点时，USB 总线带宽不足
    XHCI_COMP_NO_SLOTS_AVAILABLE_ERROR   = 9,  // [命令] Enable Slot 时，主板分配不出新槽位
    XHCI_COMP_INVALID_STREAM_TYPE_ERROR  = 10, // [命令] 配置流上下文时，Stream Type 非法
    XHCI_COMP_SLOT_NOT_ENABLED_ERROR     = 11, // [命令] 对未经 Enable 的槽位下发了命令
    XHCI_COMP_ENDPOINT_NOT_ENABLED_ERROR = 12, // [命令] 对未经初始化的端点下发了命令
    XHCI_COMP_PARAMETER_ERROR            = 17, // [命令] Context 上下文结构体参数填错或未对齐
    XHCI_COMP_CONTEXT_STATE_ERROR        = 19, // [命令] 状态机时序错误 (如：乱发 Reset Endpoint)
    XHCI_COMP_COMMAND_RING_STOPPED       = 24, // [命令] 正常回执：命令环已成功停止
    XHCI_COMP_COMMAND_ABORTED            = 25, // [命令] 正常回执：命令已被成功中止
    XHCI_COMP_SECONDARY_BANDWIDTH_ERROR  = 35, // [命令] 配置端点辅助带宽时出错

    // ==========================================================
    // 【第 3 阵营：传输事件专属】 (Transfer Event Only)
    // 归属：仅由 Transfer Ring (EP0-31) 通信触发。
    // 处理建议：全部塞进 xhci_execute_transfer_sync 的 switch-case 中。
    // ==========================================================
    XHCI_COMP_DATA_BUFFER_ERROR          = 2,  // [传输] 数据缓冲区错误 (主机内存 DMA 寻址失败)
    XHCI_COMP_BABBLE_ERROR               = 3,  // [传输] 喋喋不休 (U盘发来的数据超出预期，端点 Halted)
    XHCI_COMP_USB_TRANSACTION_ERROR      = 4,  // [传输] 物理链路车祸 (超时/CRC失败，端点 Halted)
    XHCI_COMP_STALL_ERROR                = 6,  // [传输] 逻辑卡死 (U盘主动拒绝服务，端点 Halted)
    XHCI_COMP_SHORT_PACKET               = 13, // [传输] 短包响应 (数据少于预期，BOT 中属正常)
    XHCI_COMP_RING_UNDERRUN              = 14, // [传输] 等时环下溢出 (发数据太慢)
    XHCI_COMP_RING_OVERRUN               = 15, // [传输] 等时环上溢出 (收数据太慢)
    XHCI_COMP_BANDWIDTH_OVERRUN_ERROR    = 18, // [传输] 带宽超载 (设备发送过量数据)
    XHCI_COMP_NO_PING_RESPONSE_ERROR     = 20, // [传输] USB 3.0 链路无 Ping 响应
    XHCI_COMP_INCOMPATIBLE_DEVICE_ERROR  = 22, // [传输] 试图与不兼容的设备通信
    XHCI_COMP_MISSED_SERVICE_ERROR       = 23, // [传输] 等时传输错过了时间微帧
    XHCI_COMP_STOPPED                    = 26, // [传输] 正常回执：传输流被主板强行刹车
    XHCI_COMP_STOPPED_LENGTH_INVALID     = 27, // [传输] 正常回执：刹车时残余长度无法计算
    XHCI_COMP_STOPPED_SHORT_PACKET       = 28, // [传输] 正常回执：刹车时刚好遇到短包
    XHCI_COMP_MAX_EXIT_LATENCY_TOO_LARGE = 29, // [传输] 链路从休眠唤醒失败
    XHCI_COMP_ISOCH_BUFFER_OVERRUN       = 31, // [传输] 等时接收缓冲区上溢
    XHCI_COMP_INVALID_STREAM_ID_ERROR    = 34, // [传输] UAS 协议中发了非法的 Stream ID
    XHCI_COMP_SPLIT_TRANSACTION_ERROR    = 36  // [传输] USB 2.0 Hub 拆分事务失败
} xhci_trb_comp_code_e;


// ============================================================================
// xHCI TRB 类型枚举 (对应所有 TRB Dword 3 的 Bits 10-15: type)
// 规范出处: xHCI Spec 1.2 - Table 132 (TRB Type Definitions)
// ============================================================================
typedef enum ：uint32 {
    XHCI_TRB_TYPE_RESERVED = 0,          // 0: 保留 (非法 TRB)

    XHCI_TRB_TYPE_LINK         = 6,      // 链接 TRB (传输环和命令环通用★ 物理内存环填满时，用它跳回环的开头)

    // ========================================================================
    // 【传输环 TRB】(Transfer Ring) - 塞入端点环，用于真实的数据搬运
    // ========================================================================
    XHCI_TRB_TYPE_NORMAL       = 1,      // 普通传输 (BOT 数据进出的绝对主力)
    XHCI_TRB_TYPE_SETUP_STAGE  = 2,      // 控制传输：Setup 阶段 (★ 你刚才写的 8 字节包就是它)
    XHCI_TRB_TYPE_DATA_STAGE   = 3,      // 控制传输：数据阶段
    XHCI_TRB_TYPE_STATUS_STAGE = 4,      // 控制传输：状态确认阶段
    XHCI_TRB_TYPE_ISOCH        = 5,      // 等时传输 (用于 USB 麦克风、摄像头等实时设备)
    XHCI_TRB_TYPE_EVENT_DATA   = 7,      // 事件数据 (给虚拟化或者特殊同步用的)
    XHCI_TRB_TYPE_NO_OP        = 8,      // 空操作 TRB (占坑用)

    // ========================================================================
    // 【命令环 TRB】(Command Ring) - 塞入全局命令环，用于控制 xHCI 主板硬件
    // ========================================================================
    XHCI_TRB_TYPE_ENABLE_SLOT       = 9,  // 启用设备槽 (设备刚插入时的第一步！)
    XHCI_TRB_TYPE_DISABLE_SLOT      = 10, // 禁用设备槽 (设备拔出时清理内存)
    XHCI_TRB_TYPE_ADDRESS_DEVICE    = 11, // 分配设备地址 (告诉 xHCI 给设备发 Set Address 命令)
    XHCI_TRB_TYPE_CONFIGURE_EP      = 12, // 配置端点 (激活 Bulk IN/OUT 管道全靠它)
    XHCI_TRB_TYPE_EVALUATE_CTX      = 13, // 评估上下文 (比如告诉硬件：这个 U 盘最大包长是 512)
    XHCI_TRB_TYPE_RESET_EP          = 14, // 复位端点 (★ 清除 STALL 卡死时必发的神兵利器)
    XHCI_TRB_TYPE_STOP_EP           = 15, // 停止端点 (强行踩刹车，中止正在进行的传输)
    XHCI_TRB_TYPE_SET_TR_DEQUEUE    = 16, // 设置出队指针 (★ STALL 恢复后，强行拨动硬件的出队指针)
    XHCI_TRB_TYPE_RESET_DEVICE      = 17, // 复位设备
    XHCI_TRB_TYPE_FORCE_EVENT       = 18, // 强制事件 (SR-IOV 虚拟化常用)
    XHCI_TRB_TYPE_NEGOTIATE_BW      = 19, // 协商带宽
    XHCI_TRB_TYPE_SET_LATENCY_TOL   = 20, // 设置延迟容忍度
    XHCI_TRB_TYPE_GET_PORT_BW       = 21, // 获取端口带宽
    XHCI_TRB_TYPE_FORCE_HEADER      = 22, // 强制包头
    XHCI_TRB_TYPE_NO_OP_CMD         = 23, // 空操作命令 (测试命令环通不通时用)

    // ========================================================================
    // 【事件环 TRB】(Event Ring) - xHCI 硬件主动写入内存，触发 CPU 中断的回执
    // ========================================================================
    XHCI_TRB_TYPE_TRANSFER_EVENT    = 32, // 传输完成事件 (★ 汇报 Normal/Setup 等传输的对错，如短包/STALL)
    XHCI_TRB_TYPE_CMD_COMPLETION    = 33, // 命令完成事件 (★ 汇报 Reset EP 等主板命令是否成功)
    XHCI_TRB_TYPE_PORT_STATUS_CHG   = 34, // 端口状态改变事件 (★ 最关键的中断：U盘插入或拔出的瞬间产生！)
    XHCI_TRB_TYPE_BANDWIDTH_REQ     = 35, // 带宽请求事件
    XHCI_TRB_TYPE_DOORBELL          = 36, // 门铃事件
    XHCI_TRB_TYPE_HOST_CTRL         = 37, // 主机控制器事件
    XHCI_TRB_TYPE_DEVICE_NOTIFY     = 38, // 设备通知事件
    XHCI_TRB_TYPE_MFINDEX_WRAP      = 39  // 微帧索引翻转事件

    // 48 到 63 是厂商自定义 (Vendor Defined)，通常不用管
} trb_type_e;

// ============================================================================
// xHCI 规范 6.4.4.1: 链接 TRB (Link TRB, Type = 6)
// 作用：放置在 Ring 的末尾，将硬件执行指针引回 Ring 的头部，并翻转硬件的周期期待值。
// ============================================================================
typedef struct trb_link_t{
    // Dword 0 & 1: 下一个 Ring Segment (环段) 的首地址。
    // 在我们这种单段环 (Single-Segment Ring) 的设计中，这里永远填 Ring 第 0 个 TRB 的物理地址！
    // 注意：地址必须是 16 字节对齐的 (也就是低 4 位必须为 0)。
    uint64 ring_segment_ptr;

    // Dword 2
    uint32 rsvd1:22;          // Bits [21:0]: 保留，填 0
    uint32 intr_target:10;    // Bits [31:22]: 目标中断器号 (通常填 0 即可)

    // Dword 3 (x86 小端序，从低位开始)
    uint32 cycle:1;           // Bit [0]: 硬件翻转位 (C)
    uint32 toggle_cycle:1;    // Bit [1]: ★ 极度关键！切换周期位 (TC)。
    uint32 rsvd2:2;           // Bits [3:2]: 保留，填 0
    uint32 chain:1;           // Bit [4]: 链位 (CH)。在普通的 Link TRB 中填 0
    uint32 ioc:1;             // Bit [5]: 完成时中断位 (IOC)。通常填 0，不让它产生多余中断
    uint32 rsvd3:4;           // Bits [9:6]: 保留，填 0
    uint32 trb_type:6;        // Bits [15:10]: 必须是 6 (XHCI_TRB_TYPE_LINK_TRB)
    uint32 rsvd4:16;          // Bits [31:16]: 保留，填 0
} trb_link_t;

//============================================传输trb=================================

// ============================================================================
// Setup Stage TRB (Type 2) - 控制传输的第一阶段
// ============================================================================
typedef enum : uint8 {
    USB_RECIP_DEVICE    = 0,  //设备
    USB_RECIP_INTERFACE = 1,  //接口
    USB_RECIP_ENDPOINT  = 2,  //端点
    USB_RECIP_OTHER     = 3   //其他
} usb_recipient_e;

// USB 请求类型枚举 (对应 bmRequestType 的 Bits 5-6:type)
typedef enum : uint8 {
    USB_REQ_TYPE_STANDARD = 0, // 0 = 标准请求 (Standard Request)场景：设备枚举、获取设备描述符 (Get Descriptor)、分配地址 (Set Address)、清除端点卡死 (Clear Feature) 等。
    USB_REQ_TYPE_CLASS    = 1, // 1 = 类特定请求 (Class Request)比如我们 U 盘 (Mass Storage Class) 专属的 BOT Mass Storage Reset (0xFF) 和 Get Max LUN (0xFE)。
    USB_REQ_TYPE_VENDOR   = 2,  // 2 = 厂商自定义请求 (Vendor Request)
    USB_REQ_TYPE_RESERVED = 3   // 3 = 保留 (Reserved)
} usb_req_type_e;

// USB 数据传输方向枚举 (对应 bmRequestType 的 Bit 7: dtd)
typedef enum :uint8 {
    USB_DIR_OUT = 0, // 0 = OUT (主机到设备 / Host to Device)作用：控制传输的数据阶段，数据是由主机发送给设备的。注意：如果这个控制请求根本【没有】数据阶段（比如只发一个没有参数的命令），按照规范，方向也必须填 OUT(0)。
    USB_DIR_IN  = 1  // 1 = IN (设备到主机 / Device to Host)作用：控制传输的数据阶段，主机期待设备把数据传回来，场景：比如你想读取 U 盘的最大 LUN (Get Max LUN)，或者读取设备的描述符 (Get Descriptor) 时。
} usb_data_dir_e;

// ============================================================================
// USB 具体请求指令枚举 (对应 bRequest)
// 注意：0x00~0x0F 通常为标准请求，0x20以上及 0xFE/0xFF 常用于类特定请求
// ============================================================================
typedef enum : uint8 {
    // ------------------------------------------------------------------------
    // 【USB 标准请求】(当 RequestType == USB_REQ_TYPE_STANDARD 时有效)
    // ------------------------------------------------------------------------
    USB_REQ_GET_STATUS        = 0x00, // 获取状态 (比如看看设备是自供电还是总线供电，端点有没有Halt)
    USB_REQ_CLEAR_FEATURE     = 0x01, // 清除特性 (★我们在撬开卡死端点时用的就是这个！)
    // 0x02 是保留的
    USB_REQ_SET_FEATURE       = 0x03, // 设置特性 (比如让设备进入测试模式，或挂起特定端点)
    // 0x04 是保留的
    USB_REQ_SET_ADDRESS       = 0x05, // 设置地址 (设备刚插入时地址为0，xHCI用这个给它分配一个 1~127 的新地址)
    USB_REQ_GET_DESCRIPTOR    = 0x06, // 获取描述符 (★枚举设备、获取厂商PID/VID、端点配置全靠它)
    USB_REQ_SET_DESCRIPTOR    = 0x07, // 设置描述符 (极少使用，通常用于固件更新)
    USB_REQ_GET_CONFIGURATION = 0x08, // 获取当前配置 (看看设备现在处于哪种工作模式)
    USB_REQ_SET_CONFIGURATION = 0x09, // 设置当前配置 (告诉设备：“你现在作为U盘模式启动吧”)
    USB_REQ_GET_INTERFACE     = 0x0A, // 获取当前接口备用设置
    USB_REQ_SET_INTERFACE     = 0x0B, // 设置接口备用设置 (常见于带麦克风和音响的复合 USB 耳机切换采样率)
    USB_REQ_SYNCH_FRAME       = 0x0C, // 同步帧 (仅用于等时传输，如音频/视频设备同步时间戳)
    // ------------------------------------------------------------------------
    // 【Mass Storage (BOT) 类专属请求】(当 qtype == USB_REQ_TYPE_CLASS 时有效)
    // ------------------------------------------------------------------------
    BOT_REQ_GET_MAX_LUN       = 0xFE, // 获取最大逻辑单元号 (问读卡器：“你身上插了几个SD卡？”)
    BOT_REQ_MASS_STORAGE_RESET= 0xFF  // 批量仅复位 (★核弹按钮：让U盘的协议状态机瞬间清零重启)

} usb_request_e;

// ============================================================================
// USB 标准特性选择器枚举 (对应 Clear Feature / Set Feature 的 wValue 字段)
// ============================================================================
typedef enum : uint16 {
    // ------------------------------------------------------------------------
    // 【发给 Endpoint (端点) 的特性】(当 recipient == ENDPOINT 时)
    // ------------------------------------------------------------------------
    // 作用：清除它，就能解开端点的 STALL 状态，让数据通道重新开放。
    // ★ 在 BOT 错误恢复中，你的 value 必须填这个！
    USB_FEATURE_ENDPOINT_HALT        = 0x00,

    // ------------------------------------------------------------------------
    // 【发给 Device (设备) 的特性】(当 recipient == DEVICE 时)
    // ------------------------------------------------------------------------
    USB_FEATURE_DEVICE_REMOTE_WAKEUP = 0x01, // 远程唤醒 (比如敲击休眠键盘唤醒电脑)
    USB_FEATURE_TEST_MODE            = 0x02, // 测试模式 (主要用于主板硬件出厂测试)

    // USB 3.0 新增设备特性
    USB_FEATURE_U1_ENABLE            = 0x30, // 允许进入 U1 节能状态
    USB_FEATURE_U2_ENABLE            = 0x31  // 允许进入 U2 节能状态
} usb_feature_selector_e;


/*
 * usb 8字节请求包
 */
typedef struct usb_req_pkg_t {
    //bmRequestType
    usb_recipient_e recipient : 5;
    usb_req_type_e  req_type  : 2;
    usb_data_dir_e  dtd       : 1;

    //bRequest
    usb_request_e   request; //请求代码

    //wValue
    uint16          value; //请求值，具体含义由 b_request 定义 例如：GET_DESCRIPTOR 中，w_value 高字节为描述符类型，低字节为索引

    //wIndex
    uint16          index; //索引或偏移，具体含义由 b_request 定义 例如：接口号、端点号或字符串描述符索引

    //wLength
    uint16          length; //数据阶段的传输长度（字节）主机到设备：发送的数据长度 设备到主机：请求的数据长度
}usb_req_pkg_t;

// ============================================================================
// xHCI 控制传输类型枚举 (对应 Setup TRB Dword 3 的 Bits 16-17: trt)
// 作用：告诉硬件这次控制传输是否包含数据阶段，以及数据的方向。
// ============================================================================
typedef enum : uint32 {
    TRB_TRT_NO_DATA   = 0, // 0 = 无数据阶段 (No Data Stage)场景：命令发出去就完事了，不需要额外的数据负载。
    TRB_TRT_RESERVED  = 1, // 1 = 保留 (Reserved)    // 绝对不要使用，硬件会报错。
    TRB_TRT_OUT_DATA  = 2, // 2 = OUT 数据阶段 (OUT Data Stage)场景：主机不仅发命令，还要把一坨内存数据强塞给设备。
    TRB_TRT_IN_DATA   = 3 // 3 = IN 数据阶段 (IN Data Stage)场景：主机发完命令，张开嘴等设备把数据喂回来。
} trb_trt_e;

typedef enum : uint32 {
    TRB_DIR_OUT = 0,
    TRB_DIR_IN  = 1
}trb_dir_e;

typedef enum : uint32 {
    TRB_IOC_DISABLE = 0,
    TRB_IOC_ENABLE  = 1
}trb_ioc_e;

typedef enum : uint32 {
    TRB_CHAIN_DISABLE = 0,
    TRB_CHAIN_ENABLE  = 1
}trb_chain_e;

typedef enum : uint32 {
    TRB_IDT_DISABLE = 0,
    TRB_IDT_ENABLE  = 1
}trb_idt_e;

typedef struct trb_setup_stage_t{
    // Dword 0-1:
    usb_req_pkg_t   usb_req_pkg;

    // Dword 2: 长度与中断目标
    uint32          trb_tr_len : 17; // 规范强制要求：Setup TRB 的长度必须固定填 8！
    uint32          rsvd1            : 5;
    uint32          int_target       : 10;

    // Dword 3: 控制位
    uint32          cycle : 1;
    uint32          rsvd2 : 3;
    trb_chain_e     chain : 1;  // ★ 必须填0
    trb_ioc_e       ioc   : 1;  // 通常填 0，因为我们只关心最后一个 Status TRB 的完成中断
    trb_idt_e       idt   : 1;  // ★ 必须填 1！(Immediate Data: 告诉硬件前 8 字节是数据本身，不是指针)
    uint32          rsvd3 : 3;
    trb_type_e      type  : 6;  // Bits 10-15: TRB 类型 (固定为 2)
    trb_trt_e       trt   : 2;  // Bits 16-17: 传输类型 (见上方宏定义，极其重要)
    uint32          rsvd4 : 14;
} trb_setup_stage_t;

// ============================================================================
// Data Stage TRB (Type 3) - 控制传输的第二阶段 (可选)
// ============================================================================
typedef struct trb_data_stage_t {
    // Dword 0-1: 数据缓冲区的 64 位物理地址 (PA)
    uint64          data_buf_ptr;

    // Dword 2: 长度控制
    uint32          tr_len : 17; // 你要传输的实际数据长度
    uint32          td_size      : 5;  // 剩余的包数 (简单起见常填 0)
    uint32          int_target   : 10;

    // Dword 3: 控制位
    uint32          cycle : 1;
    uint32          ent   : 1;  //评估下一个trb
    uint32          isp   : 1;  // 短包中断
    uint32          ns    : 1;  // No Snoop
    trb_chain_e     chain : 1;  //
    trb_ioc_e       ioc   : 1;  // 通常填 0
    trb_idt_e       idt   : 1;  // 必须填 0 (说明前面是个指针)
    uint32          rsvd1 : 3;
    trb_type_e      type  : 6;  // Bits 10-15: TRB 类型 (固定为 3)
    trb_dir_e       dir   : 1;  // ★ Bits 16: 数据方向 (0 = OUT 主机发给设备, 1 = IN 设备发给主机)
    uint32          rsvd2 : 15;
}trb_data_stage_t;

// ============================================================================
// Status Stage TRB (Type 4) - 控制传输的最终确认阶段
// ============================================================================
typedef struct trb_status_stage_t {
    // Dword 0-1: 规范强制要求保留全 0！(状态阶段没有真实的数据负载)
    uint64          rsvd0;

    // Dword 2
    uint32          rsvd1      : 22; // 必须全 0
    uint32          int_target : 10;

    // Dword 3: 控制位
    uint32          cycle : 1;
    uint32          ent   : 1;
    uint32          rsvd2 : 2;
    trb_chain_e     chain : 1;  // ★ 必须填 0！因为这是最后一节车厢了！
    trb_ioc_e       ioc   : 1;  // ★ 必须填 1！(Interrupt On Completion：硬件跑完这个 TRB，才向内核汇报)
    uint32          rsvd3 : 4;
    trb_type_e      type  : 6;  // Bits 10-15: TRB 类型 (固定为 4)
    trb_dir_e       dir   : 1;  // ★ Bits 16: 握手方向 (如果是 No Data 或 OUT，这里填 1； 如果 Data是 IN，这里填 0)
    uint32          rsvd4 : 15;
}trb_status_stage_t;

//===================================================================


//==================================命令trb==================================================

// ============================================================================
// xHCI 规范 6.4.3.3: 复位设备命令 TRB (Reset Device Command, Type = 11)
// 作用：当设备发生灾难性故障且已完成物理端口复位后，通知控制器清空该设备的
//       内部所有传输状态，将其强行拉回 Default 状态。
// ============================================================================
typedef struct trb_reset_device_cmd_t{
    uint32 rsvd1[3];          // Dword 0, 1, 2: 保留，必须全填 0

    // Dword 3
    uint32 cycle:1;           // [0] 硬件翻转位 (C)
    uint32 rsvd2:9;           // [9:1] 保留，填 0
    trb_type_e trb_type:6;        // [15:10] 必须是 11 (XHCI_TRB_TYPE_RESET_DEVICE)
    uint32 rsvd3:8;           // [23:16] 保留，填 0

    // ★ 狙击目标：你要抢救哪个坑位？
    uint32 slot_id:8;         // [31:24] 目标 Slot ID
} trb_reset_dev_t;

// ============================================================================
// xHCI 规范 6.4.3.5: 配置端点命令 TRB (Configure Endpoint Command, Type = 12)
// 作用：评估 Input Context，增加/删除端点，或者修改已存在端点的参数。
// ============================================================================
typedef struct trb_cfg_ep_t{
    uint64 input_ctx_ptr; // [63:0] 输入上下文物理基址 (必须64字节对齐)
    uint32 rsvd1;             // [31:0] 保留

    // Dword 3
    uint32 cycle:1;           // [0] 硬件翻转位 (C)
    uint32 rsvd2:8;           // [8:1] 保留

    // ★ 架构师武器：一键清空标志！
    // 填 0 = 正常配置/修改端点。
    // 填 1 = Deconfigure！直接砍掉除了 EP0 之外的所有端点，退回 Address 状态！
    uint32 dc:1;              // [9] 取消配置标志 (Deconfigure)

    trb_type_e trb_type:6;        // [15:10] 必须是 12 (XHCI_TRB_TYPE_CONFIGURE_ENDPOINT)
    uint32 rsvd3:8;           // [23:16] 保留
    uint32 slot_id:8;         // [31:24] 目标 Slot ID
} trb_cfg_ep_t;

// ============================================================================
// xHCI 规范 6.4.3.8: 停止端点命令 TRB (Stop Endpoint Command, Type = 15)
// 作用：强制主板 xHC 芯片停止处理指定端点的传输环，并将端点状态机切入 "Stopped"。
// 核心用途：用于传输超时后的主动中止 (Abort Transfer) 和环指针的重新对齐。
// ============================================================================
typedef struct trb_stop_ep_t{
    uint32 rsvd1[3];          // Dword 0, 1, 2: 保留，必须全填 0

    // Dword 3
    uint32 cycle:1;           // Bit [0]: 硬件翻转位 (C)
    uint32 rsvd2:9;           // Bits [9:1]: 保留，填 0
    trb_type_e trb_type:6;        // Bits [15:10]: 必须是 15 (XHCI_TRB_TYPE_STOP_EP)
    // ★ 狙击目标：精准定位到具体的设备和具体的管道
    uint32 ep_id:5;           // Bits [20:16]: 目标 Endpoint ID (1~31)
    uint32 rsvd3:2;           // Bits [22:21]: 保留，填 0
    // ★ 挂起位：0 = 彻底停止并丢弃内部缓存; 1 = 只是挂起(Suspend)，以后还能原样恢复。
    // 在超时抢救场景中，我们永远填 0（彻底停止）！
    uint32 suspend:1;         // Bit [23]: SP (Suspend) 位
    uint32 slot_id:8;         // Bits [31:24]: 目标 Slot ID
}trb_stop_ep_t;

// ============================================================================
// xHCI 规范 6.4.3.4: 分配设备地址命令 TRB (Address Device Command, Type = 11)
// 作用：向新插入的 USB 设备分配总线地址，并初始化 Slot Context 和 EP0 Context。
// ============================================================================
typedef struct trb_addr_dev_t{
    // Dword 0 & 1: ★ 极度关键！指向 Input Context (输入上下文) 的物理地址。
    // 物理地址的最低 4 位必须为 0 (即 16 字节对齐)，
    // 但在实际的 x64 系统中，强烈建议直接进行 64 字节 (物理页的 Cache Line) 对齐！
    uint64 input_ctx_ptr;

    // Dword 2
    uint32 rsvd1;             // 保留，必须填 0

    // Dword 3
    uint32 cycle:1;           // Bit [0]: 硬件翻转位 (C)
    uint32 rsvd2:8;           // Bits [8:1]: 保留，填 0

    // ★ 架构师秘钥：BSR (Block Set Address Request)
    // 填 0：主板在底层自动帮你给 U 盘发 SET_ADDRESS 控制传输请求（最常用！）。
    // 填 1：主板只在内部配置上下文，但不向 U 盘发 SET_ADDRESS（用于某些高级 USB 3.0 设备的状态机跳跃）。
    uint32 bsr:1;             // Bit [9]: 阻止设置地址请求位

    trb_type_e  trb_type:6;        // Bits [15:10]: 必须是 11 (XHCI_TRB_TYPE_ADDRESS_DEVICE)
    uint32 rsvd3:8;           // Bits [23:16]: 保留，填 0

    // ★ 身份绑定：填入你刚才通过 Enable Slot 抢到的那个号码！
    uint32 slot_id:8;         // Bits [31:24]: 目标 Slot ID
}trb_addr_dev_t;

//复位端点trb
typedef struct trb_rest_ep_t{
    uint32          rsvd0[3];
    uint32          cycle:1;
    uint32          rsvd1:8;
    uint32          tsp:1;      // 常规填 0
    trb_type_e      type:6;
    uint32          ep_dci:5; // 端点索引 (DCI)，如 IN 是 3，OUT 是 4
    uint32          rsvd2:3;
    uint32          slot_id:8; // 设备槽位号
}trb_rest_ep_t;

// ============================================================================
// xHCI 规范：设置出队指针命令 (Set TR Dequeue Pointer Command TRB)类型码：16 (XHCI_TRB_TYPE_SET_TR_DEQ)
// ============================================================================
typedef struct trb_set_tr_deq_ptr_t{
    // Dword 0-1 (64位物理地址)
    uint64 tr_deq_ptr; // ★ 极其致命：最低位 Bit 0 是 DCS (Dequeue Cycle State)，必须包含新指针所在位置的 Cycle 位！

    // Dword 2
    uint16 rsvd0;
    uint16 stream_id;     // BOT 协议不支持 Stream，必须填 0

    // Dword 3
    uint32          cycle   : 1;
    uint32          rsvd1   : 8;
    uint32          tsp     : 1;
    trb_type_e      type    : 6;      // ★ 必须填 16
    uint32          ep_dci  : 5;      // 端点索引 (DCI)
    uint32          rsvd2   : 3;
    uint32          slot_id : 8;
}trb_set_tr_deq_ptr_t;

// ============================================================================
// xHCI 规范 6.4.3.9: Enable Slot Command TRB
// 作用：向主板 xHC 芯片申请一个空闲的设备槽位 (Slot)，主板会在完成事件中返回分配的 Slot ID
// ============================================================================
typedef struct trb_enable_slot_t {
    uint32          rsvd0[3];       // Dword 0, 1, 2: 规范要求全部保留，必须清零

    // Dword 3 (x86 小端序，从低位开始映射)
    uint32          cycle:1;        // Bit 0: 翻转位 (C)，交由底层的 enqueue 函数处理
    uint32          rsvd1:9;        // Bits 1-9: 保留，填 0
    trb_type_e      type:6;         // Bits 10-15: TRB 类型，这里必须是 9 (Enable Slot)
    uint32          slot_type:5;    // Bits 16-20: 槽位类型。对于常规的 USB 设备，直接填 0
    uint32          rsvd2:11;       // Bits 21-31: 保留，填 0
} trb_enable_slot_t;

// ============================================================================
// xHCI 规范 6.4.3.2: 禁用槽位命令 TRB (Disable Slot Command, Type = 10)
// 作用：释放主板为该 Slot ID 分配的内部资源，使该 Slot ID 可以被再次分配。
// ============================================================================
typedef struct trb_disable_slot_t{
    uint32 rsvd1[3];          // Dword 0, 1, 2: 保留，必须全填 0

    // Dword 3
    uint32 cycle:1;           // Bit [0]: 硬件翻转位 (C)
    uint32 rsvd2:9;           // Bits [9:1]: 保留，填 0
    trb_type_e trb_type:6;        // Bits [15:10]: 必须是 10 (XHCI_TRB_TYPE_DISABLE_SLOT)
    uint32 rsvd3:8;           // Bits [23:16]: 保留，填 0

    // ★ 绝杀目标：告诉主板你要超度哪个设备
    uint32 slot_id:8;         // Bits [31:24]: 目标 Slot ID
}trb_disable_slot_t;

// ============================================================================
// xHCI 规范 6.4.3.6: 评估上下文命令 TRB (Evaluate Context Command, Type = 13)
// 作用：通知主板更新设备上下文中某些字段 (最常用：更新全速设备的 EP0 最大包长)
// ============================================================================
typedef struct trb_eval_ctx_t{
    uint64 input_ctx_ptr; // [63:0] 输入上下文物理基址 (要求对齐)
    uint32 rsvd1;             // [31:0] 保留

    // Dword 3
    uint32 cycle:1;           // [0] 硬件翻转位 (C)
    uint32 rsvd2:9;           // [9:1] 保留，全填 0
    trb_type_e trb_type:6;        // [15:10] 必须是 13 (XHCI_TRB_TYPE_EVALUATE_CONTEXT)
    uint32 rsvd3:8;           // [23:16] 保留，全填 0
    uint32 slot_id:8;         // [31:24] 目标 Slot ID
} trb_eval_ctx_t;

//=================================================================================================


//============================ 事件trb ============================================================

// 1. 命令完成事件 (Command Completion Event, Type = 33)
// 发生场景：你发了 Enable Slot, Address Device 等主板命令后，主板的回执。
typedef struct trb_cmd_comp_event_t{
    uint64                    cmd_trb_ptr;       // Dword 0 & 1: 刚才引发该事件的 Command TRB 物理地址
    uint32                    cmd_comp_param:24; // Dword 2 [23:0]: 命令完成参数 (通常为 0，个别命令有用)
    xhci_trb_comp_code_e      comp_code:8;       // Dword 2 [31:24]: 完成码 (对应 xhci_comp_code_t)

    uint32                    cycle:1;           // Dword 3 [0]: 硬件翻转位
    uint32                    rsvd1:9;           // Dword 3 [9:1]: 保留
    trb_type_e                trb_type:6;        // Dword 3 [15:10]: 必须是 33 (XHCI_TRB_TYPE_CMD_COMP_EVENT)
    uint32                    vf_id:8;           // Dword 3 [23:16]: 虚拟功能 ID (SR-IOV 专用，常规填 0)
    uint32                    slot_id:8;         // Dword 3 [31:24]: ★ 极度重要！这里藏着主板分配的 Slot ID！
}trb_cmd_comp_event_t;


// 2. 传输事件 (Transfer Event, Type = 32)
// 发生场景：U盘数据读写完成、Setup 控制传输完成等，端点产生的中断回执。
typedef struct trb_transfer_event_t{
    uint64                    tr_trb_ptr;        // Dword 0 & 1: 引发中断的那条 Transfer/Setup TRB 物理地址
    uint32                    tr_len:24;   // Dword 2 [23:0]: ★ 极度重要！残余字节数 (没传完的数据量，短包时必看)
    xhci_trb_comp_code_e      comp_code:8;       // Dword 2 [31:24]: 完成码 (如 SUCCESS, SHORT_PACKET, STALL)

    uint32                    cycle:1;           // Dword 3 [0]: 硬件翻转位
    uint32                    rsvd1:1;           // Dword 3 [1]: 保留
    uint32                    event_data:1;      // Dword 3 [2]: ED 位 (是否为纯事件数据)
    uint32                    rsvd2:7;           // Dword 3 [9:3]: 保留
    trb_type_e                trb_type:6;        // Dword 3 [15:10]: 必须是 32 (XHCI_TRB_TYPE_TRANSFER_EVENT)
    uint32                    ep_id:5;           // Dword 3 [20:16]: 发生事件的端点 DCI (1 是 EP0，等)
    uint32                    rsvd3:3;           // Dword 3 [23:21]: 保留
    uint32                    slot_id:8;         // Dword 3 [31:24]: 发生事件的设备槽位号
} trb_transfer_event_t;

// ============================================================================
// xHCI 规范 6.4.2.3: 端口状态改变事件 TRB (Port Status Change Event, Type = 34)
// 发生场景：物理线缆的插拔、端口复位完成、或者链路电源状态改变时硬件主动上报。
// ============================================================================
typedef struct trb_port_status_change_event_t{
    uint32      rsvd0:24;          // Dword 0 [23:0]: 保留，全 0
    uint32      port_id:8;         // Dword 0 [31:24]: ★ 核心机密！发生状态改变的物理端口号 (比如 1 号口)

    uint32      rsvd1;             // Dword 1: 保留，全 0
    uint32      rsvd2;             // Dword 2: 保留，全 0

    uint32      cycle:1;           // Dword 3 [0]: 硬件翻转位 (Cycle Bit)
    uint32      rsvd3:9;           // Dword 3 [9:1]: 保留
    trb_type_e  trb_type:6;        // Dword 3 [15:10]: 必须是 34 (XHCI_TRB_TYPE_PORT_STATUS_CHANGE_EVENT)
    uint32      rsvd4:16;          // Dword 3 [31:16]: 保留
}trb_port_status_change_event_t;

//================================================================================================

//trb集合
typedef union xhci_trb_t {
    // 【视角 1：内存搬运工视角】(用于底层 enqueue 拷贝和清零)
    uint64 raw[2];

    //命令或传输环连接trb
    trb_link_t link;

    // 【视角 3：业务定制视角】(包含了所有具体的 TRB 解析格式) ... 以后加什么 TRB，就往这里塞什么 struct ...
    //命令trb xhci命令环专用，用于发送启用插槽等
    trb_cfg_ep_t         cfg_ep;
    trb_stop_ep_t        stop_ep;
    trb_addr_dev_t       addr_dev;
    trb_disable_slot_t   disable_slot;
    trb_enable_slot_t    enable_slot;
    trb_set_tr_deq_ptr_t set_tr_deq_ptr;
    trb_rest_ep_t        rest_ep;
    trb_eval_ctx_t       eval_ctx;
    trb_reset_dev_t      reset_dev;

    //传输trb
    //控制传输端点1专用，用于发送usb命令如获取设备描述符/设备信息等
    trb_setup_stage_t        setup_stage;
    trb_data_stage_t         data_stage;
    trb_status_stage_t       status_stage;
    //bulk端点专用端点2-31专用，用于数据传输如read 10 write10等

    //事件trb
    trb_cmd_comp_event_t           cmd_comp_event;
    trb_transfer_event_t           transfer_event;
    trb_port_status_change_event_t prot_status_change_event;
}xhci_trb_t;

//=========================================================================================


//========================== USB描述符 =========================

/**
 * @brief USB 描述符类型 (naos 完美多态版)
 * 警告：0x20 及以上的宏存在值域重叠，解析时必须结合 Interface Class 上下文！
 */
typedef enum : uint8 {
    // ==========================================
    // 第一阵营：标准描述符 (Standard) - 绝对领域，无多态歧义
    // 适用范围：所有 USB 设备的通用枚举阶段
    // ==========================================
    USB_DESC_TYPE_DEVICE                = 0x01, // 设备描述符
    USB_DESC_TYPE_CONFIG                = 0x02, // 配置描述符
    USB_DESC_TYPE_STRING                = 0x03, // 字符串描述符
    USB_DESC_TYPE_INTERFACE             = 0x04, // 接口描述符
    USB_DESC_TYPE_ENDPOINT              = 0x05, // 端点描述符
    USB_DESC_TYPE_DEVICE_QUALIFIER      = 0x06, // 设备限定描述符 (USB 2.0 专属)
    USB_DESC_TYPE_OTHER_SPEED_CONFIG    = 0x07, // 其他速度配置描述符
    USB_DESC_TYPE_INTERFACE_POWER       = 0x08, // 接口电源描述符
    USB_DESC_TYPE_OTG                   = 0x09, // OTG 描述符
    USB_DESC_TYPE_DEBUG                 = 0x0A, // 调试描述符
    USB_DESC_TYPE_INTERFACE_ASSOCIATION = 0x0B, // 接口关联描述符 (IAD)
    USB_DESC_TYPE_BOS                   = 0x0F, // 二进制对象存储描述符 (USB 3.0+)
    USB_DESC_TYPE_DEVICE_CAPABILITY     = 0x10, // 设备能力描述符
    USB_DESC_TYPE_SS_ENDPOINT_COMPANION = 0x30, // 超高速端点伴随描述符 (USB 3.0 极度关键)
    USB_DESC_TYPE_SSP_ISOCH_COMPANION   = 0x31, // 超高速+ 等时端点伴随描述符

    // ==========================================
    // 第二阵营：类特定描述符 (Class-Specific) - 多态重叠区
    // 解析条件：必须在遇到 0x04 (接口描述符) 之后，根据 bInterfaceClass 决定含义！
    // ==========================================

    // --- [多态分支 A] 音频 (UAC) / 视频 (UVC) / 通信 (CDC) 等类 ---
    // 前提: bInterfaceClass == 0x01(Audio) 或 0x0E(Video) 或 0x02(CDC)
    USB_DESC_TYPE_CS_DEVICE             = 0x21,
    USB_DESC_TYPE_CS_CONFIG             = 0x22,
    USB_DESC_TYPE_CS_STRING             = 0x23,
    USB_DESC_TYPE_CS_INTERFACE          = 0x24, // 音视频处理单元、格式声明等
    USB_DESC_TYPE_CS_ENDPOINT           = 0x25,

    // --- [多态分支 B] 人机交互设备 (HID，如鼠标/键盘) ---
    // 前提: bInterfaceClass == 0x03 (HID)
    USB_DESC_TYPE_HID                   = 0x21, // ★ 与 CS_DEVICE 撞车！描述 HID 版本和报文数
    USB_DESC_TYPE_HID_REPORT            = 0x22, // ★ 与 CS_CONFIG 撞车！极其复杂的报文定义树
    USB_DESC_TYPE_HID_PHYSICAL          = 0x23, // ★ 与 CS_STRING 撞车！物理设备部件描述

    // --- [多态分支 C] 大容量存储 (Mass Storage - UAS 协议) ---
    // 前提: bInterfaceClass == 0x08 且 bInterfaceProtocol == 0x62 (UAS)
    USB_DESC_TYPE_UAS_PIPE_USAGE        = 0x24, // ★ 与 CS_INTERFACE 撞车！(你抓出来的 U 盘就是它)

    // ==========================================
    // 第三阵营：集线器专用 (Hub Specific) - 事实上的独立类
    // 前提: bDeviceClass == 0x09 (Hub)
    // ==========================================
    USB_DESC_TYPE_HUB                   = 0x29, // USB 2.0 集线器描述符
    USB_DESC_TYPE_SS_HUB                = 0x2A  // USB 3.0 超高速集线器描述符

} usb_desc_type_e;

typedef struct {
    uint8 length; // 描述符长度
    usb_desc_type_e desc_type; // 描述符类型
}usb_desc_head;

/*usb设备描述符
描述符长度，固定为 18 字节（0x12）
描述符类型，固定为 0x01（设备描述符）*/
typedef struct {
    usb_desc_head head;
    uint16 usb_version;         // USB 协议版本，BCD 编码（如 0x0200 表示 USB 2.0，0x0300 表示 USB 3.0）
    uint8 device_class;         // 设备类代码，定义设备类别（如 0x00 表示类在接口描述符定义，0x03 表示 HID）
    uint8 device_subclass;      // 设备子类代码，进一步细化设备类（如 HID 的子类）
    uint8 device_protocol;      // 设备协议代码，定义类内协议（如 HID 的 0x01 表示键盘）
    uint8 max_packet_size0;     // 端点 0 的最大数据包大小（字节），USB 2.0 为 8/16/32/64，USB 3.x 为 9（表示 2^9=512 字节）
    uint16 vendor_id;           // 供应商 ID（VID），由 USB-IF 分配，标识制造商
    uint16 product_id;          // 产品 ID（PID），由厂商分配，标识具体产品
    uint16 device_version;      // 设备发布版本，BCD 编码（如 0x0100 表示版本 1.00）
    uint8 manufacturer_index;   // 制造商字符串描述符索引（0 表示无）
    uint8 product_index;        // 产品字符串描述符索引（0 表示无）
    uint8 serial_number_index;  // 序列号字符串描述符索引（0 表示无，建议提供唯一序列号）
    uint8 num_configurations;   // 支持的配置描述符数量（通常为 1）
} usb_dev_desc_t;

/*usb配置描述符
描述符长度，固定为 9 字节（0x09）
描述符类型，固定为 0x02（配置描述符）*/
typedef struct {
    usb_desc_head head;
    uint16 total_length; // 配置描述符总长度（包括所有子描述符，如接口、端点等），单位为字节
    uint8 num_interfaces; // 该配置支持的接口数量
    uint8 configuration_value; // 配置值，用于 SET_CONFIGURATION 请求（通常从 1 开始）
    uint8 configuration_index; // 配置字符串描述符索引（0 表示无）
    uint8 attributes; /*配置属性
                                位7：固定为 1（保留）
                                位6：1=自供电，0=总线供电
                                位5：1=支持远程唤醒，0=不支持
                                位4-0：保留，置 0*/
    uint8 max_power; // 最大功耗，单位为 2mA（USB 2.0）或 8mA（USB 3.x）例如：50 表示 USB 2.0 的 100mA 或 USB 3.x 的 400mA
} usb_cfg_desc_t;

/*USB 字符串描述符
描述符长度（含头部和字符串）
描述符类型 = 0x03*/
typedef struct {
    usb_desc_head head;
    uint16 string[]; // UTF-16LE 编码的字符串内容（变长数组）
} usb_string_desc_t;

/*接口描述符
描述符长度，固定为 9 字节（0x09）
描述符类型，固定为 0x04（接口描述符）*/
typedef struct {
    usb_desc_head head;
    uint8 interface_number; // 接口编号，从 0 开始，标识该接口
    uint8 alternate_setting; // 备用设置编号，同一接口的不同配置（通常为 0）
    uint8 num_endpoints; // 该接口使用的端点数量（不包括端点 0）
    uint8 interface_class; // 接口类代码，定义接口功能（如 0x03 表示 HID，0x08 表示 Mass Storage）
    uint8 interface_subclass; // 接口子类代码，进一步细化接口类（如 HID 的子类）
    uint8 interface_protocol; // 接口协议代码，定义类内协议（如 HID 的 0x01 表示键盘）
    uint8 interface_index; // 接口字符串描述符索引（0 表示无）
} usb_if_desc_t;

/*端点描述符
描述符长度（固定7字节）
描述符类型：0x05 = 端点描述符*/
typedef struct {
    usb_desc_head head;
    uint8 endpoint_address; // 端点地址：位7方向(0=OUT,主机→设备 1=IN，设备→主机)，位3-0端点号
    uint8 attributes; // 传输类型：0x00=控制，0x01=Isochronous，0x02=Bulk，0x03=Interrupt
    uint16 max_packet_size; // 该端点的最大包长（不同速度有不同限制）
    uint8 interval; // 轮询间隔（仅中断/同步传输有意义）
} usb_ep_desc_t;

/*  超高速端点伴随描述符
 *  uint8  bLength;            // 固定 6
    uint8  bDescriptorType;    // 0x30 表示 SuperSpeed Endpoint Companion Descriptor*/
typedef struct {
    usb_desc_head head;
    uint8 max_burst; // 每次突发包数（0-15），实际表示突发数+1
    uint8 attributes; // 位 4:0 Streams 支持数 (Bulk)，或多事务机会 (Isoch)
    uint16 bytes_per_interval; // 对于 Isoch/Interrupt，最大字节数
} usb_ss_comp_desc_t;

/**
 * @brief UAS (USB Attached SCSI) 管道用途标识符 (Pipe ID)
 */
typedef enum : uint8 {
    // ==========================================
    // 1. 控制平面 (Control Plane) - 负责下发指令和接收执行结果
    // ==========================================
    USB_UAS_PIPE_COMMAND_OUT = 1,  // [指令下发] 主机 -> U盘。用于发送 SCSI 命令块 (CDB)
    USB_UAS_PIPE_STATUS_IN   = 2,  // [状态回执] U盘 -> 主机。用于接收命令执行成功与否的 Sense Data

    // ==========================================
    // 2. 数据平面 (Data Plane) - 负责极限速度的底层扇区搬运
    // ==========================================
    USB_UAS_PIPE_BULK_IN     = 3,  // [数据读取] U盘 -> 主机。你的 "Data-In Pipe" (你刚才抓包抓到的就是它！)
    USB_UAS_PIPE_BULK_OUT    = 4   // [数据写入] 主机 -> U盘。你的 "Data-Out Pipe"
} usb_uas_pipe_id_e;

/* USA管道描述符
 * uint8  bLength;            // 固定 4
 * uint8  bDescriptorType;    // 0x24
 */
typedef struct {
    usb_desc_head head;
   usb_uas_pipe_id_e  pipe_id;              // 端点用途标识 1=command_out 2=status_in 3=bulk_in 4=bulk_out
    uint8  reserved;
} usb_uas_pipe_usage_desc_t;

/*HID 类描述符（可选
描述符长度
描述符类型：0x21 = HID 描述符*/
typedef struct {
    usb_desc_head head;
    uint16 hid; // HID 版本号
    uint8 country_code; // 国家代码（0=无）
    uint8 num_descriptors; // 后面跟随的子描述符数量
    // 后面通常跟 HID 报告描述符（类型0x22）等
} usb_hid_desc_t;

/**
 * @brief USB 2.0 集线器描述符 (Type: 0x29)
 * 警告：这是一个变长结构体！最后两个数组的长度取决于 bNbrPorts。
 */
typedef struct {
    usb_desc_head head;
    uint8  num_ports;           // ★ 下游端口总数 (极其关键，决定了你要轮询几次)

    // wHubCharacteristics 包含了极其重要的位域：
    // Bit 1:0 - 电源切换模式 (00=所有端口联动上电, 01=各端口独立上电, 11=无电源切换)
    // Bit 4:3 - 过流保护模式 (00=全局过流保护, 01=单端口独立过流保护, 11=无保护)
    uint16 hub_characteristics;

    uint8  power_on_to_power_good;      // 端口上电后，需要等多久电源才能稳定？(单位是 2ms，比如填 50 就是要等 100ms)
    uint8  hub_control_current;    // Hub 芯片自身工作需要的最大电流 (mA)

    // ==========================================
    // ⚠️ 变长警告：下面这两个字段在内存中紧挨着，但长度是不固定的！
    // 它们的字节数 = (bNbrPorts / 8) + 1
    // 比如 4 口 Hub，这里就是 1 个字节；10 口 Hub，这里就是 2 个字节。
    // ==========================================

    // uint8 DeviceRemovable[]; // 位图：指示每个端口上的设备是不是焊死的（比如笔记本内置摄像头）
    // uint8 PortPwrCtrlMask[]; // 历史遗留字段，USB 1.1 的产物，USB 2.0 规定全填 0xFF

} usb_hub2_desc_t;

/**
 * @brief USB 3.0 超高速集线器描述符 (Type: 0x2A)
 * 优势：长度固定为 12 字节，无变长数组陷阱。
 */
typedef struct {
    usb_desc_head head;
    uint8  num_ports;           // 下游端口总数 (由于规范限制，绝不会超过 15)
    uint16 hub_characteristics; // 特性掩码 (与 USB 2.0 类似，但去掉了废弃位)
    uint8  power_on_to_power_good;      // 单位依然是 2ms
    uint8  hub_control_current;    // ★ 注意：USB 3.0 规范里，这里的单位变成了 2mA！

    // ==========================================
    // SuperSpeed 专属的新字段 (用于内核调度器评估总线延迟)
    // ==========================================
    uint8  hub_hdr_hecLat;       // Hub 数据包头解码延迟 (Hub Header Decode Latency)
    uint16 hub_delay;           // Hub 转发数据块的纳秒级平均延迟 (单位: ns)

    // ==========================================
    // 曾经的变长数组，现在变成了固定的 16-bit 整数
    // ==========================================
    uint16 device_removable;     // 16位位图。Bit 1~15 代表对应的端口是否可移除。(Bit 0 保留)

} usb_hub3_desc_t;

//=============================================================


//======================== 设备上下文结构 ==========================================

// ============================================================================
// Slot Context (32 字节核心部分，外部按需套一层 32/64 字节外壳)
// ============================================================================
typedef struct slot_ctx_t{
    // Dword 0
    uint32 route_string:20;     // [19:0] 路由字符串
    uint32 port_speed:4;        // [23:20] 端口速度
    uint32 rsvd_dw0_24:1;       // [24] 保留位 (规范中隐蔽的一位)
    uint32 mtt:1;               // [25] 多重事务转换器
    uint32 is_hub:1;            // [26] 1=集线器, 0=USB设备
    uint32 context_entries:5;   // [31:27] 端点上下文条目数量 (1~31)

    // Dword 1
    uint16 max_exit_latency; // [15:0] 最大退出延迟 (us)
    uint8 root_hub_port_num; // [23:16] 根集线器端口号
    uint8 num_ports;         // [31:24] 端口数量 (仅Hub有效)

    // Dword 2
    uint8 parent_hub_slot_id;// [7:0] 父集线器插槽 ID
    uint8 parent_port_num;   // [15:8] 父端口号
    uint16 tt_think_time:2;     // [17:16] TT 思考时间
    uint16 rsvd_dw2_18:4;       // [21:18] 保留
    uint16 interrupter_target:10;// [31:22] 目标中断器编号

    // Dword 3
    uint32 usb_device_address:8;// [7:0] 硬件分配的 USB 地址 (只读)
    uint32 rsvd_dw3_8:19;       // [26:8] 保留
    uint32 slot_state:5;        // [31:27] 插槽状态 (0=禁用, 1=默认, 2=寻址, 3=已配置)

    uint32 reserved[4];         // 填充至 32 字节
} xhci_slot_ctx_t;

// ============================================================================
// Endpoint Context (32 字节核心部分)
// ============================================================================
typedef struct ep_ctx_t{
    // Dword 0
    uint16 ep_state:3;          // [2:0] 端点状态 (0=禁用, 1=运行, 2=暂停, 3=停止, 4=错误)
    uint16 rsvd_dw0_3:5;        // [7:3] 保留
    uint16 mult:2;              // [9:8] 突发乘数
    uint16 max_pstreams:5;      // [14:10] 最大主数据流数量
    uint16 lsa:1;               // [15] 线性流数组标志
    uint8 interval;          // [23:16] 轮询间隔
    uint8 max_esit_payload_hi;// [31:24] ESIT 有效载荷高 8 位

    // Dword 1
    uint8 rsvd_dw1_0:1;        // [0] 保留
    uint8 cerr:2;              // [2:1] 错误计数 (通常填 3)
    uint8 ep_type:3;           // [5:3] 端点类型 (4=控制传输, 6=Bulk In, 等)
    uint8 rsvd_dw1_6:1;        // [6] 保留
    uint8 hid:1;               // [7] 主机初始化的禁用流标志
    uint8 max_burst_size;    // [15:8] 最大突发大小
    uint16 max_packet_size;  // [31:16] 最大包长 (8, 64, 512)

    //Dword 2 & 3
    uint64 tr_dequeue_ptr;   //[0] 出队周期状态 (Cycle Bit) [3:1] 保留 (或 SCT 流上下文类型) [63:4] 传输环物理出队指针

    // Dword 4
    uint16 average_trb_length;// [15:0] 平均 TRB 长度
    uint16 max_esit_payload_lo;// [31:16] ESIT 有效载荷低 16 位
    uint32 reserved[3];         // 填充至 32 字节
} xhci_ep_ctx_t;

// ============================================================================
// Input Control Context
// ============================================================================
typedef struct input_ctrl_ctx_t{
    uint32 drop_context_flags;  // Dword 0: 位 0 = Slot, 位 1 = EP0, 位 2 = EP1...
    uint32 add_context_flags;   // Dword 1: 同上
    uint32 reserved[6];         // 填充至 32 字节
} xhci_input_ctrl_ctx_t;

#define XHCI_DEVICE_CONTEXT_COUNT 32
#define XHCI_INPUT_CONTEXT_COUNT 33

typedef struct {
    uint64 tr_dequeue; // TR Dequeue Ptr+ DCS(位0)
    uint64 reserved;
} xhci_stream_ctx_t;

//================================================================================

#pragma pack(pop)


//============================ 软件抽象 ==========================================

typedef struct {
    xhci_trb_t   *ring_base; //环起始地址
    uint32       index; //trb索引
    uint8        cycle; //循环位
} xhci_ring_t;

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

//抽象xhci中断器结构
typedef struct {
    xhci_erst_t *erstba;
    xhci_ring_t event_rings;
}xhci_intr;

//xhci控制器
typedef struct xhci_hcd_t{
    // ==========================================
    // 1. 硬件属性 (Hardware Capabilities)
    // ==========================================
    uint8               major_bcd;          // 主版本号
    uint8               minor_bcd;          // 次版本号
    uint8               ctx_size;           // 设备上下文字节数 (32 还是 64 字节)
    uint8               max_ports;          // 最大物理端口数量 (MaxPorts)
    uint8               max_slots;          // 最大逻辑插槽数量 (MaxSlots)
    uint16              max_intrs;          // 最大中断器数量 (MaxIntrs)

    // ==========================================
    // 2. 协议支持扩展 (Supported Protocol Capability)
    // ==========================================
    uint8               spc_count;          // SPC 块数量
    xhci_spc_t          *spc;               // 动态分配的 SPC 结构体数组
    uint8               *port_to_spc;       // [核心映射]: 物理 Port ID 对应的 SPC 索引

    // ==========================================
    // 3. MMIO 硬件寄存器指针 (Registers Mapping)
    // ==========================================
    xhci_cap_regs_t     *cap_reg;           // 能力寄存器 (只读)
    xhci_op_regs_t      *op_reg;            // 操作寄存器 (控制全局状态)
    xhci_rt_regs_t      *rt_reg;            // 运行时寄存器 (中断管理)
    xhci_db_regs_t      *db_reg;            // 门铃寄存器 (敲门砖)
    xhci_ext_regs_t     *ext_reg;           // 扩展寄存器链表起始地址

    // ==========================================
    // 4. DMA 核心共享内存 (Host <-> Device)
    // ==========================================
    uint64              *dcbaap;            // 设备上下文基址数组 (物理地址数组)
    xhci_ring_t         cmd_ring;           // 全局单例：命令环 (Command Ring)

    // ==========================================
    // 5. 软硬件映射与并发控制 (Software State)
    // ==========================================
    // xhci_port_t         *ports;             // 端口逻辑对象数组
    // struct usb_dev_t    *udevs;             // 插槽到设备的逻辑映射 (通过 Slot ID 查找 usb_dev_t)

    // 注意：事件环不是一个，它是和中断器绑定的！这里根据 max_intrs 动态分配！
    xhci_intr*          intr;
    uint16              enable_intr_count;  // 启用中断器数量，取cpu核心数量和max_intrs最小值

    pcie_dev_t          *xdev;
    //spinlock_t          lock;               // 保护整个 xHCI 状态机的全局自旋锁
} xhci_hcd_t;


uint64 xhci_ring_enqueue(xhci_ring_t *ring, xhci_trb_t *trb_push);
uint8 xhci_handle_common_error(xhci_trb_comp_code_e comp_code, uint64 trb_pa);
xhci_trb_comp_code_e xhci_wait_for_event(xhci_hcd_t *xhcd,uint16 intr_number, uint64 wait_trb_pa, uint64 timeout_ms,xhci_trb_t *out_event_trb) ;
static inline int32 xhci_ring_init(xhci_ring_t *ring);
static inline void xhci_ring_doorbell(xhci_hcd_t *xhcd, uint8 db_number, uint32 value);
int32 xhci_cmd_enable_slot(xhci_hcd_t *xhcd, uint8 *out_slot_id);
int32 xhci_cmd_disable_slot(xhci_hcd_t *xhcd, uint8 slot_id);
int32 xhci_cmd_addr_dev(xhci_hcd_t *xhcd, uint8 slot_id,xhci_input_ctrl_ctx_t *input_ctx);
int32 xhci_cmd_cfg_ep(xhci_hcd_t *xhcd, xhci_input_ctrl_ctx_t *input_ctx, uint8 slot_id, uint8 dc);
int32 xhci_cmd_stop_ep(xhci_hcd_t *xhcd, uint8 slot_id, uint8 ep_id);
uint32 xhci_cmd_reset_ep(xhci_hcd_t *xhcd, uint8 slot_id, uint8 ep_dci);
int32 xhci_cmd_eval_ctx(xhci_hcd_t *xhcd, xhci_input_ctrl_ctx_t *input_ctx, uint8 slot_id);
int32 xhci_cmd_set_tr_deq_ptr(xhci_hcd_t *xhcd, uint8 slot_id, uint8 ep_dci,xhci_ring_t *transfer_ring);
int32 xhci_cmd_reset_dev(xhci_hcd_t *xhcd, uint8 slot_id);




