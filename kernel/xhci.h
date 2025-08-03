#pragma once

#include "moslib.h"

#pragma pack(push,1)

// ===== 1. 能力寄存器 (Capability Registers) =====
typedef struct {
    // 00h: 能力长度和版本 (CAPLENGTH/HCIVERSION)
    UINT8   cap_length;        // [7:0] 能力寄存器总长度 (字节)
    UINT8   reserved0;         // 保留
    UINT16  hciversion;        // [31:16] 控制器版本 (0x100 = 1.0, 0x110 = 1.1, 0x120 = 1.2)

    // 04h: 硬件参数寄存器 (HCSPARAMS1)
    UINT32 hcsparams1;      /*[7:0]   MaxSlots: 支持的最大设备槽数
                              [15:8]  MaxIntrs: 支持的中断向量数
                              [24:31] MaxPorts: 支持的根端口数*/

    // 08h: 硬件参数寄存器 (HCSPARAMS2)
    UINT32 hcsparams2;      /*[3:0]IsochSchedThreshold: 等时调度阈值
                              [7:4]EventRingSegmentTableMax: 事件环段表最大条目数（2^(n+3)）
                              [位 21:25: 高位, 27:31: 低位]MaxScratchpadBufs: 最大Scratchpad缓冲区数量*/

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

    // 08h: 页面大小寄存器 (PAGESIZE)
    UINT32 pagesize; // [15:0] 控制器支持的页面大小*0x1000

    // 0Ch: 保留 [RsvdZ]
    UINT32 reserved0[2];

    //0x14: 设备通知控制寄存器 (DNCTRL)
    UINT32 dnctrl;  // - 每位对应一个设备槽的使能（0=禁用，1=使能）

    //0x18 命令环控制寄存器 (CRCR)
    UINT64 crcr; /*- RCP (位 0-5): 环周期状态
                   - CRR (位 3): 命令环运行（1=运行，0=停止）
                   - CA (位 4): 命令终止
                   - CCE (位 5): 命令完成使能
                   - Command Ring Pointer (位 6-63): 命令环的64位基地址（对齐到64字节）*/
    //0x20: 保留字段
    UINT64 reserved1[2];

    //0x30h: 设备上下文基础地址数组指针 (DCBAAP)
    UINT64 dcbaap;  // DCBAA的物理地址指针 (低32位+高32位)

    // 38h: 配置寄存器 (CONFIG)
    UINT32 config;   // [7:0] 启用的设备槽数 (值≤MaxSlots)

    // 保留字段 (Reserved), 偏移 0x3C-0x3FF, 填充到端口寄存器之前
    UINT32 reserved2[241];

    // 端口寄存器数组 (PORTSC, PORTPMSC, PORTLI, PORTHLPMC), 偏移 0x400起,每个端口占用16字节，按端口数量动态分配
    struct {
        // 端口状态和控制寄存器 (PORTSC), 32位
        UINT32 portsc;     /*  - CCS (位 0): 当前连接状态（1=设备连接）
                               - PED (位 1): 端口使能/禁用
                               - PR (位 4): 端口复位
                               - PLS (位 5-8): 端口链路状态
                               - PP (位 9): 端口电源
                               - PortSpeed (位 10-13): 端口速度
                               - PIC (位 14-15): 端口指示器控制
                               - LWS (位 16): 链路状态写入选通
                               - CSC (位 17): 连接状态变化
                               - PEC (位 18): 端口使能/禁用变化
                               - WRC (位 19): 热重置变化
                               - ORC (位 20): 过流变化
                               - PRC (位 21): 端口复位变化
                               - PLC (位 22): 端口链路状态变化
                               - CEC (位 23): 配置错误变化
                               - WCE (位 24): 热启动使能
                               - WDE (位 25): 热禁用使能
                               - WOE (位 26): 热过流使能*/

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
    UINT32 mfindex;  // [13:0] 当前微帧索引

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
        UINT64 erdp;   // 指向事件环的当前出队指针
                       // - DESI (位 0-2): 出队事件环段索引
                       // - EHB (位 3): 事件处理忙碌（1=忙碌，写1清除）
                       // - Event Ring Dequeue Pointer (位 4-63): 出队指针地址
    } intr_regs[1024]; // 最大支持1024个中断器（根据HCSPARAMS1中的MaxIntrs）
} xhci_rt_regs_t;

// ===== 4. 门铃寄存器 (Doorbell Registers) =====
typedef struct {
    // 门铃寄存器数组 (每个设备槽一个 + 主机控制器)
    UINT32 doorbell[256]; // 最大支持256个设备槽

    // 寄存器定义:
    // 写门铃寄存器会通知主机控制器处理命令
    // 格式: [7:0] - 目标端点ID (0=控制端点)
    //       [15:8] - 设备槽ID
} xhci_db_regs_t;

// ===== 5. 扩展寄存器 (HCCPARAMS2) =====
// 当HCCPARAMS1[0] (AC64) 设置为1时出现
typedef struct {
    // 00h: U1设备退出延迟 (U1DEL)
    UINT32 u1del;   // 默认U1退出延迟

    // 04h: U2设备退出延迟 (U2DEL)
    UINT32 u2del;   // 默认U2退出延迟

    // ... 更多扩展寄存器 ...
} xhci_ext_regs_t;

// ===== 完整xHCI寄存器结构 =====
typedef struct {
    xhci_cap_regs_t *cap;        // 能力寄存器 (只读)
    xhci_op_regs_t  *op;          // 操作寄存器 (读写)
    xhci_rt_regs_t  *runtime;     // 运行时寄存器 (通常是op_regs + cap.cap_length)
    xhci_db_regs_t  *doorbells;   // 门铃寄存器 (通常是runtime + runtime_offset)
    xhci_ext_regs_t *ext;        // 扩展寄存器 (可选的)
} xhci_regs_t;

#pragma pack(pop)

void init_xhci(void);
