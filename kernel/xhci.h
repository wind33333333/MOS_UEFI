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
                              - MaxPSASize (位 12-15): 最大主控制器流数组大小 2^(N+1)
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
#define XHCI_HCCPARAMS2_CIC (1u << 5)
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
    /*
 * bits[7:0]  MaxSlotsEn：启用的设备槽数量，必须 <= HCSPARAMS1.MaxSlots
 * bit 8      U3 Entry Enable：是否允许设备进入 U3，具体依规范版本/实现
 * bit 9      CIE：Configuration Information Enable
 *             1 = 启用 Input Control Context offset 0x1C 的
 *                 Configuration Value / Interface Number / Alternate Setting 字段
 *             0 = 不启用，这些字段必须清 0
 * bits[31:10] Reserved
 */
#define XHCI_CONFIG_MAX_SLOTS_MASK  0xFF
#define XHCI_CONFIG_U3E             (1u << 8)
#define XHCI_CONFIG_CIE             (1u << 9)

    // 保留字段 (Reserved), 偏移 0x3C-0x3FF, 填充到端口寄存器之前
    uint32 reserved2[241];

    // 端口寄存器数组 (PORTSC, PORTPMSC, PORTLI, PORTHLPMC), 偏移 0x400起,每个端口占用16字节，按端口数量动态分配
    struct {
        // 端口状态和控制寄存器 (PORTSC), 32位
        uint32 portsc; /*      - CCS (位 0): 当前连接状态（1=设备连接 0=设备未连接）
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
                               - CAS (位 24): 冷连接状态设备在系统冷启动时就已经插在上面了
                               - WCE (位 25): 连接唤醒使能
                               - WDE (位 26): 断开唤醒使能
                               - WOE (位 27)：过流唤醒使能
                               - DR  (位 30)：1=设备不可拆卸 0=设备可移动
                               - WPR (位 31)：热端口复位 */
#define XHCI_PORTSC_CCS (1 << 0)
#define XHCI_PORTSC_PED (1 << 1)
#define XHCI_PORTSC_OCA (1 << 3)
#define XHCI_PORTSC_PR (1 << 4)
#define XHCI_PORTSC_PP (1 << 9)
#define XHCI_PORTSC_PIC (3<<14)
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

#define XHCI_PORTSC_PLS_MASK     (0xf<<5)
#define XHCI_PORTSC_PLS_U0              (0<<5)   // 正常工作状态，USB 设备活跃，支持全速数据传输（USB 3.0 或 USB 2.0）
#define XHCI_PORTSC_PLS_U1              (1<<5)   // U1 低功耗状态，USB 设备进入轻度节能模式，快速恢复，适用于 USB 3.0
#define XHCI_PORTSC_PLS_U2              (2<<5)   // U2 低功耗状态，USB 设备进入更深节能模式，恢复时间稍长，适用于 USB 3.0
#define XHCI_PORTSC_PLS_U3              (3<<5)   // U3 挂起状态，USB 设备进入深度休眠，功耗最低，恢复时间较长，适用于 USB 3.0
#define XHCI_PORTSC_PLS_DISABLED        (4<<5)   // 禁用状态，USB 端口被禁用，无法通信
#define XHCI_PORTSC_PLS_RX_DETECT       (5<<5)   // 接收检测状态，USB 控制器正在检测是否有设备连接
#define XHCI_PORTSC_PLS_INACTIVE        (6<<5)   // 非活跃状态，USB 端口未连接设备或设备未响应
#define XHCI_PORTSC_PLS_POLLING         (7<<5)   // 轮询状态，USB 控制器正在初始化或尝试建立与设备的连接
#define XHCI_PORTSC_PLS_RECOVERY        (8<<5)   // 恢复状态，USB 端口从低功耗状态（如 U3）恢复到活跃状态
#define XHCI_PORTSC_PLS_HOT_RESET       (9<<5)   // 热重置状态，USB 端口正在执行热重置操作，重新初始化设备
#define XHCI_PORTSC_PLS_COMPLIANCE_MODE (10<<5)  // 合规模式，用于 USB 设备或控制器的合规性测试
#define XHCI_PORTSC_PLS_TEST_MODE       (11<<5)  // 测试模式，USB 端口进入特定测试状态，用于硬件或协议测试
#define XHCI_PORTSC_PLS_RESUME          (12<<5)  // 恢复状态，USB 设备从挂起状态恢复，通常由主机发起

// 🛡️ xHCI 端口寄存器安全保留掩码 (只保留 RWS 和 R/W 属性的安全位)
#define XHCI_PORTSC_PRESERVE_MASK ( XHCI_PORTSC_PP  | XHCI_PORTSC_PIC | \
                                    XHCI_PORTSC_WCE |  XHCI_PORTSC_WDE |  XHCI_PORTSC_WOE)


// 提取出快照中所有变成了 1 的“突变标志 (RW1C)” (17~24位)
#define XHCI_PORTSC_CHANGE_MASK (XHCI_PORTSC_CSC | XHCI_PORTSC_PEC | \
                                 XHCI_PORTSC_WRC | XHCI_PORTSC_OCC | \
                                 XHCI_PORTSC_PRC | XHCI_PORTSC_PLC | \
                                 XHCI_PORTSC_CEC | XHCI_PORTSC_CAS)

        // 端口电源管理状态和控制寄存器 (PORTPMSC),控制电源管理和U1/U2状态,具体字段依赖于协议（USB2或USB3）
        uint32 portpmsc;

        // 端口链路信息寄存器 (PORTLI), 提供链路错误计数等信息
        uint32 portli;

        // 主机控制器端口电源管理控制寄存器 (PORTHLPMC), 仅用于USB2协议端口，控制高级电源管理
        uint32 porthlpmc;
    } portregs[256]; // 最大支持256个端口（根据HCSPARAMS1中的MaxPorts）
} xhci_op_regs_t;


// 中断管理数组 (IMAN) - 每个中断向量一个
typedef struct {
    // 中断管理寄存器 (IMAN), 偏移 0x00
    uint32 iman; // 中断管理 [0]：IP中断挂起（1=有中断待处理），[1]：中断使能（1=使能，0=禁用）
    // 定义宏，防止写错魔法数字
#define XHCI_IMAN_IP (1 << 0) // Bit 0 传统的int中断模式根据该位是否置位判断是不是自己的中断，msi和msix不需要。
#define XHCI_IMAN_IE (1 << 1);

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
}xhci_intr_regs_t; // 最大支持1024个中断器（根据HCSPARAMS1中的MaxIntrs）

// ===== 3. 运行时寄存器 (Runtime Registers) =====
typedef struct {
    // 00h: 微帧索引寄存器 (MFINDEX)
    uint32 mfindex; // [13:0] 当前微帧索引（按125μs递增）

    // 04h: 保留
    uint32 reserved0[7];

    xhci_intr_regs_t intr_regs[1024]; // 最大支持1024个中断器（根据HCSPARAMS1中的MaxIntrs）
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


//======================== 设备上下文结构 ==========================================

// ============================================================================
// 🔌 Slot Context (插槽上下文)
// 规范出处: xHCI Spec 1.2 - 6.2.2 Slot Context
// 作用：描述整个 USB 设备的全局属性 (速度、挂在哪个 Hub 端口、激活了几个端点)
// ============================================================================

typedef struct xhci_slot_ctx_t {
    uint32 dw0;          // 路由字符串、速度、Hub标志、端点上下文数量
    uint32 dw1;          // 退出延迟、根集线器端口、子端口数量
    uint32 dw2;          // TT (事务转换器) 相关信息、中断器目标
    uint32 dw3;          // 设备地址 (硬件只读)、插槽状态
    uint32 reserved[4];  // 填充至 32 字节核心大小 (x64 下外部容器需对齐至 64 字节)
} xhci_slot_ctx_t;

// ---------------------------------------------------------
// ★ Slot Context DW0 装配宏
// ---------------------------------------------------------
#define SLOT_CTX_DW0_ROUTE(r)       ((r) & 0xFFFFF)         // [19:0] 路由字符串 (描述设备在 Hub 拓扑树中的路径，直连设备填 0)
#define SLOT_CTX_DW0_SPEED(s)       (((s) & 0xF) << 20)     // [23:20] 端口速度 (1=全速, 2=低速, 3=高速, 4=超高速)
#define SLOT_CTX_DW0_MTT            (1 << 25)               // [25] 多重事务转换器 (Multi-TT 标志，仅对高速 Hub 有效)
#define SLOT_CTX_DW0_HUB            (1 << 26)               // [26] 集线器标志 (1=这是一个 Hub, 0=普通设备)
#define SLOT_CTX_DW0_CTX_ENTRIES(c) (((c) & 0x1F) << 27)    // [31:27] ★极其重要：激活的端点上下文索引 (DCI)。比如只激活了 EP0，这里填 1；激活了 EP1 IN (DCI=3)，这里填 3。

// ---------------------------------------------------------
// ★ Slot Context DW1 装配宏
// ---------------------------------------------------------
#define SLOT_CTX_DW1_MAX_EXIT_LAT(l) ((l) & 0xFFFF)         // [15:0] 最大退出延迟 (us，超高速休眠唤醒用，常规填 0)
#define SLOT_CTX_DW1_ROOT_PORT(p)    (((p) & 0xFF) << 16)   // [23:16] 根集线器端口号 (设备最终连接在主板的哪个物理端口上)
#define SLOT_CTX_DW1_NUM_PORTS(p)    (((p) & 0xFF) << 24)   // [31:24] 端口数量 (只有当 DW0 的 Hub 标志为 1 时才需要填)

// ---------------------------------------------------------
// ★ Slot Context DW2 装配宏 (处理 USB 2.0 事务转换器)
// ---------------------------------------------------------
#define SLOT_CTX_DW2_TT_HUB_SLOT(s)  ((s) & 0xFF)           // [7:0] TT Hub 插槽 ID (当低速/全速设备挂在高速 Hub 下时必填，指向上级 Hub 的 Slot ID)
#define SLOT_CTX_DW2_TT_PORT_NUM(p)  (((p) & 0xFF) << 8)    // [15:8] TT Hub 端口号 (指向上级 Hub 的物理端口)
#define SLOT_CTX_DW2_TT_THINK_TIME(t)(((t) & 0x3) << 16)    // [17:16] TT 思考时间 (通常从 Hub 描述符中直接抄过来)
#define SLOT_CTX_DW2_INTR_TARGET(i)  (((i) & 0x3FF) << 22)  // [31:22] 目标中断器编号 (指引该插槽的事件打向哪个硬件中断，通常填 0)

// ---------------------------------------------------------
// ★ Slot Context DW3 提取宏 (多数属于硬件回写的状态)
// ---------------------------------------------------------
#define SLOT_CTX_DW3_DEV_ADDR(dw3)   ((dw3) & 0xFF)         // [7:0] 获取硬件分配的 USB 总线地址 (Address Device 成功后硬件自动写入)
#define SLOT_CTX_DW3_SLOT_STATE(dw3) (((dw3) >> 27) & 0x1F) // [31:27] 插槽当前状态 (0=Disabled, 1=Default, 2=Addressed, 3=Configured)


// ============================================================================
// 🎯 Endpoint Context (端点上下文)
// 规范出处: xHCI Spec 1.2 - 6.2.3 Endpoint Context
// 作用：描述单一数据管道的传输特性 (如 EP0 控制管道、EP1 批量管道)
// ============================================================================

typedef struct xhci_ep_ctx_t {
    uint32 dw0;          // 状态、突发乘数、流支持、轮询间隔
    uint32 dw1;          // 错误计数(CErr)、端点类型、最大包长
    uint64 tr_dequeue_ptr; // [63:4] 传输环出队指针 (物理地址)，[0] Cycle 状态
    uint32 dw4;          // 平均 TRB 长度、最大 ESIT 负载
    uint32 reserved[3];  // 填充至 32 字节核心大小
} xhci_ep_ctx_t;

// ---------------------------------------------------------
// ★ Endpoint Context DW0 装配宏
// ---------------------------------------------------------
// EP State [2:0] (0=Disabled, 1=Running, 2=Halted, 3=Stopped, 4=Error) 通常只用于读取
#define EP_CTX_DW0_MULT(m)           (((m) & 0x3) << 8)     // [9:8] 突发乘数 (SuperSpeed 等时传输专用，常规填 0)
#define EP_CTX_DW0_MAX_PSTREAMS(p)   (((p) & 0x1F) << 10)   // [14:10] 最大流数量 (支持 UAS 协议时填入，值为 2 的幂指数。不支持填 0)
#define EP_CTX_DW0_LSA               (1 << 15)              // [15] 线性流数组标志
#define EP_CTX_DW0_INTERVAL(i)       (((i) & 0xFF) << 16)   // [23:16] 轮询间隔 (中断/等时端点必填，根据描述符的 bInterval 换算；批量/控制填 0)
#define EP_CTX_DW0_MAX_ESIT_HI(e)    (((e) & 0xFF) << 24)   // [31:24] ESIT 有效载荷高 8 位 (周期性传输专用)

// ---------------------------------------------------------
// ★ Endpoint Context DW1 装配宏
// ---------------------------------------------------------
#define EP_CTX_DW1_CERR(c)           (((c) & 0x3) << 1)     // [2:1] ★错误计数 (Error Count)。规范建议直接填 3！表示硬件在重试 3 次失败后才上报 Error 放弃。
#define EP_CTX_DW1_EP_TYPE(t)        (((t) & 0x7) << 3)     // [5:3] 端点类型 (极其重要，见下方字典)
#define EP_CTX_DW1_HID               (1 << 7)               // [7] 主机初始化的禁用流标志 (Host Initiate Disable)
#define EP_CTX_DW1_MAX_BURST(b)      (((b) & 0xFF) << 8)    // [15:8] 最大突发大小 (SuperSpeed 必填，从端点伴随描述符中获取，常规填 0)
#define EP_CTX_DW1_MAX_PACKET(p)     (((p) & 0xFFFF) << 16) // [31:16] ★最大包长 (Max Packet Size)。全速 EP0 填 64，高速 Bulk 填 512，必须精准！

// 端点类型字典 (用于 EP_CTX_DW1_EP_TYPE)
#define EP_TYPE_NOT_VALID            0
#define EP_TYPE_ISOCH_OUT            1
#define EP_TYPE_BULK_OUT             2
#define EP_TYPE_INTR_OUT             3
#define EP_TYPE_CONTROL              4  // EP0 专属！双向控制端点
#define EP_TYPE_ISOCH_IN             5
#define EP_TYPE_BULK_IN              6
#define EP_TYPE_INTR_IN              7

// ---------------------------------------------------------
// ★ Endpoint Context DW2 & DW3 装配宏 (tr_dequeue_ptr)
// 此处直接使用 64 位整数，与 TRB parameter 处理方式一致
// ---------------------------------------------------------

// ---------------------------------------------------------
// ★ Endpoint Context DW4 装配宏
// ---------------------------------------------------------
#define EP_CTX_DW4_AVG_TRB_LEN(l)    ((l) & 0xFFFF)         // [15:0] ★平均 TRB 长度。必须填！(Bulk 通常填 3072，Control 填 8，如果全填 0 xHC 可能会在调度时降低带宽优先级甚至罢工)
#define EP_CTX_DW4_MAX_ESIT_LO(e)    (((e) & 0xFFFF) << 16) // [31:16] ESIT 有效载荷低 16 位

// ============================================================================
// 🎛️ Input Control Context (输入控制上下文)
// 规范出处: xHCI Spec 1.2 - 6.2.5.1 Input Control Context
// 物理位置：永远位于 Input Context 内存块的最开头的 32 字节！
// ============================================================================

typedef struct xhci_input_ctrl_ctx_t {
    uint32 drop_context_flags;  // DW0: 降级/删除标志 (要砍掉的端点)
    uint32 add_context_flags;   // DW1: 新增/更新标志 (要激活或修改的端点及 Slot)
    uint32 reserved[5];         // DW2-DW6: 保留区域 (必须严格清 0)
    uint32 configuration;       // DW7: 全局配置指令 (Configuration / Interface)
} xhci_input_ctrl_ctx_t;

// ---------------------------------------------------------
// ★ DW0 & DW1: Drop / Add Context Flags 标志位装配宏
// xHCI 的端点索引 (DCI) 刚好完美映射到标志位的 Bit [1:31]。
// Bit 0 专属留给 Slot Context。
// ---------------------------------------------------------

// 🌟 Slot Context 标志 (固定在 Bit 0)
#define INPUT_CTRL_ADD_SLOT          (1 << 0)  // 告诉硬件：本次施工图里包含了 Slot Context 的更新 (Evaluate Context 命令常用)
// 注意：规范明确指出 Drop Context Flag 的 Bit 0 是保留的 (必须为 0)，因为插槽一旦建立就不能单独 Drop，只能 Disable Slot。

// 🌟 端点 Context 标志 (DCI: 1~31)
// 例如：控制端点 EP0 的 DCI 是 1，对应 Bit 1。
#define INPUT_CTRL_DROP_EP(dci)      (1 << (dci)) // 在 DW0 中标记：让硬件停止该端点并丢弃其状态！
#define INPUT_CTRL_ADD_EP(dci)       (1 << (dci)) // 在 DW1 中标记：告诉硬件去读取下面的 Endpoint Context 并建立通道！

// ---------------------------------------------------------
// ★ DW7: 全局配置指令装配宏 (Configuration / Interface)
// ---------------------------------------------------------
#define INPUT_CTRL_DW7_CONFIG(c)     ((c) & 0xFF)           // [7:0]   🌟 核心大闸！配置描述符的值 (bConfigurationValue，通常为 1)
#define INPUT_CTRL_DW7_INTERFACE(i)  (((i) & 0xFF) << 8)    // [15:8]  正在配置的接口号
#define INPUT_CTRL_DW7_ALT_SETTING(a)(((a) & 0xFF) << 16)   // [23:16] 正在配置的备用设置 (Alternate Setting)

#define XHCI_DEVICE_CONTEXT_COUNT 32
#define XHCI_INPUT_CONTEXT_COUNT 33

typedef struct {
    uint64 tr_dequeue; // TR Dequeue Ptr+ DCS(位0)
    uint64 reserved;
} xhci_stream_ctx_t;

//================================================================================


// ============================================================================
// 🌟 终极 TRB 结构体 (混合视角：64位物理地址 + 32位状态 + 32位控制)
// 规范出处：xHCI Spec 1.2
// 绝对保证 16 字节对齐，免疫一切编译器大小端及 Padding 陷阱！
// ============================================================================
typedef struct xhci_trb_t {
    uint64 parameter;  // DW0 & DW1: 物理地址、事件指针、或 Setup 包的前 8 字节
    uint32 status;     // DW2: 传输长度、中断目标、完成码等
    uint32 control;    // DW3: Cycle位、TRB类型、IOC、端点号、槽位号等
}xhci_trb_t;

#pragma pack(pop)


//============================ 软件抽象 ==========================================

typedef struct xhci_submit_ring_t{
    // === [物理/内存层] ==================
    xhci_trb_t   *ring_base;        // 虚拟起始地址
    uint32       size;              // 容量

    // === [逻辑游标层] ==================
    uint32       enq_idx;           // 写游标
    uint32       deq_idx;           // 读游标
    uint8        cycle;             // 生产 Cycle 状态

    // === [并发与调度层] ===
    uint32   ring_lock;             // 保护当前环的唯一自旋锁
    list_head_t  pending_list;      // 在此环上排队等待硬件完成的面单 (URB 或 Command)

} xhci_submit_ring_t;


// 硬件是生产者，软件是消费者
typedef struct xhci_event_ring_t{
    xhci_trb_t   *ring_base;        // 虚拟起始地址
    uint32       ring_size;              // 事件环通常极大 (例如 1024)
    uint32       deq_idx;           // 🌟 只有出队游标！干净利落！
    uint8        cycle;             // 软件期望硬件写入的 Cycle 状态

    // 🌟 事件环独有的物理结构
    xhci_erst_t *erst_base;   // 指向 ERST 段表内存的虚拟地址
    uint32       erst_size;

    uint32      ring_lock;
} xhci_event_ring_t;


typedef struct xhci_command_t {
    // 1. 链表锚点
    list_head_t     node;

    // 2. 身份识别凭证
    uint64       cmd_trb_pa;

    int32        status;

    // 4. 战利品 (硬件回执包裹)
    uint8        slot_id;
    uint32       comp_code;
    uint32       comp_param;

    // 5. 同步原语
    volatile boolean is_done;    // 🌟 单任务环境的终极同步神器
} xhci_command_t;

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
}xhci_port_t;


//端口速率
typedef enum : uint8 {
    USB_SPEED_UNKNOWN    = 0, // 未知/未连接/出错
    USB_SPEED_LOW        = 1, // 1.5 Mbps (USB 1.1)
    USB_SPEED_FULL       = 2, // 12 Mbps (USB 1.1)
    USB_SPEED_HIGH       = 3, // 480 Mbps (USB 2.0)
    USB_SPEED_SUPER_5G   = 4, // 5 Gbps (USB 3.2)
    USB_SPEED_SUPER_10G  = 5, // 10 Gbps (USB 3.2 Gen 2x1)
    USB_SPEED_SUPER_20G  = 6, // 20 Gbps (USB 3.2 Gen 2x2)
}usb_port_speed_e;

// ==========================================
// xHCI 速率翻译字典条目 (纯软件解析版)
// ==========================================
typedef struct {
    uint8               psiv;           // 速度 ID (Port Speed ID Value, 1~15) 这个是实际需要写入 slot context中的数值

    // 🌟 核心：直接在初始化时算出绝对速率，运行时 O(1) 直接拿！
    uint32              speed_kbps;     // 绝对物理速率 (如 12, 480, 5000, 10000 Mbps)

    // 预解析好的硬件属性
    uint8               is_full_duplex; // 是否全双工 (PFD)
    uint8               is_symmetric;   // 是否对称链路 (PLT)

    // 🌟 终极映射：直接绑定到 USB Core 的标准速率枚举！
    usb_port_speed_e    mapped_speed;
} xhci_psi_t;

typedef struct {
    uint8  major_bcd;           // 协议主版本（DW0[31:24]，常见 0x02=USB2，0x03=USB3.x）
    uint8  minor_bcd;           // 协议次版本（DW0[23:16]，如 0x10=USB3.1 等）
    char8  name[4];             // 协议名字符串（DW1，常见 "USB " = 0x20425355）
    uint16 proto_defined;       // 协议自定义字段（DW2[27:16]，USB2/USB3 各自有含义）
    uint8  port_first;          // 覆盖端口起始号（DW2[7:0]，1-based）
    uint8  port_count;          // 连续覆盖端口数量（DW2[15:8]）
    uint8  slot_type;           // Protocol Slot Type（DW3[4:0]）
    xhci_psi_t psi_dict[16];    // psi字典
} xhci_spc_t;


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
    uint8               max_streams_exp;    //  最大支持流指数2^(n+1)

    // ==========================================
    // 2. 协议支持扩展与拓扑路由 (Topology Routing)
    // ==========================================
    uint8               spc_count;
    xhci_spc_t          spc[8];
    uint8               port_to_spc[256];         // O(1): 物理口 -> SPC 索引

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
    xhci_submit_ring_t  cmd_ring;           // 全局单例：命令环 (Command Ring)

    // ==========================================
    // 5. 软硬件映射与并发控制 (Software State)
    // ==========================================
    struct usb_dev_t    **udevs;           // 插槽到设备的逻辑映射 (通过 Slot ID 查找 usb_dev_t)
    struct usb_hub_port_t *ports;          // xhci原生端口

    // 注意：事件环不是一个，它是和中断器绑定的！这里根据 max_intrs 动态分配！
    xhci_event_ring_t*  event_ring_arr;
    uint16              enable_num_event_ring;  // 启用中断器数量，取cpu核心数量和max_intrs最小值

    pcie_dev_t          *xdev;
} xhci_hcd_t;


//读端口
static inline uint32 xhci_read_portsc(xhci_hcd_t *xhcd,uint8 port_num) {
    return xhcd->op_reg->portregs[port_num-1].portsc;
}

//写端口
static inline void  xhci_write_portsc(xhci_hcd_t *xhcd,uint8 port_num,uint32 protsc) {
    xhcd->op_reg->portregs[port_num-1].portsc = protsc;
}

//获取端口速率id
static inline uint8 xhci_get_psi (xhci_hcd_t *xhcd,uint8 port_num) {
    uint32 portsc = xhci_read_portsc(xhcd,port_num);
    return  (portsc >> 10) & 0xF;
}

//xhci原生端口操作命令
//==========================================================================================
/**
 * @brief 发起端口热复位 (Hot Reset - 适用于 USB 2.0 & 3.0 常规设备)
 */
void xhci_port_reset_hot(xhci_hcd_t *xhcd, uint8 port_num) {
    uint32 portsc = xhci_read_portsc(xhcd, port_num);

    // 构造安全回写值：保留安全位 | 设置热复位位(RW1S)
    portsc = (portsc & XHCI_PORTSC_PRESERVE_MASK) | XHCI_PORTSC_PR;
    xhci_write_portsc(xhcd, port_num, portsc);
}

/**
 * @brief 发起端口暖复位 (Warm Reset - 仅适用于 USB 3.0 链路死锁救援)
 */
void xhci_port_reset_warm(xhci_hcd_t *xhcd, uint8 port_num) {
    uint32 portsc = xhci_read_portsc(xhcd, port_num);

    // 构造安全回写值：保留安全位 | 设置暖复位位(RW1S)
    portsc = (portsc & XHCI_PORTSC_PRESERVE_MASK) | XHCI_PORTSC_WPR;
    xhci_write_portsc(xhcd, port_num, portsc);
}

/**
 * @brief 强制禁用端口 (Disable Port)
 * 物理不断电，但切断数据链路通信。
 */
void xhci_port_disable(xhci_hcd_t *xhcd, uint8 port_num) {
    uint32 portsc = xhci_read_portsc(xhcd, port_num);

    // 构造安全回写值：保留安全位 | 故意给 PED 写 1 (触发 RW1CS 禁用效果)
    uint32 val = (portsc & XHCI_PORTSC_PRESERVE_MASK) | XHCI_PORTSC_PED;
    xhci_write_portsc(xhcd, port_num, val);
}

//xhci端口上电
static void xhci_port_power_on(xhci_hcd_t *xhcd,uint8 port_num) {
    uint32 portsc = xhci_read_portsc(xhcd, port_num);
    portsc |= XHCI_PORTSC_PP;
    xhci_write_portsc(xhcd, port_num, portsc);
    //等待20ms
}

//xhci端口断电
static void xhci_port_power_off(xhci_hcd_t *xhcd, uint8 port_num) {
    uint32 portsc = xhci_read_portsc(xhcd, port_num);
    portsc &= ~XHCI_PORTSC_PP;
    xhci_write_portsc(xhcd, port_num, portsc);
    //等待20ms
}
//=======================================================================================



