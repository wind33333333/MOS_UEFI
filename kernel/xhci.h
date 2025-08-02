#pragma once

#include "moslib.h"

#pragma pack(push,1)

// ===== 1. 能力寄存器 (Capability Registers) =====
typedef struct {
    // 00h: 能力长度和版本 (CAPLENGTH/HCIVERSION)
    UINT8 cap_length;      // [7:0] 能力寄存器总长度 (字节)
    UINT8 reserved0;        // 保留
    UINT16 xhci_version;     // [31:16] 控制器版本 (0x100 = 1.0, 0x110 = 1.1)

    // 04h: 硬件参数寄存器 (HCSPARAMS1)
    UINT32 hcsparams1;      //
        // [7:0]   MaxSlots: 支持的最大设备槽数
        // [15:8]  MaxIntrs: 支持的中断向量数
        // [24:31] MaxPorts: 支持的根端口数

    // 08h: 硬件参数寄存器 (HCSPARAMS2)
    UINT32 hcsparams2;      //
        // [0]     IsochSchedThreshold: 等时调度阈值
        // [1]     EventInterrupterMax: 事件中断器最大索引
        // [3:2]   ERSTMax: 事件环段表最大大小 (2^ERSTMax)
        // [7:4]   MaxScratchpadBufs: 最大暂存缓冲区数

    // 0Ch: 硬件参数寄存器 (HCSPARAMS3)
    UINT32 hcsparams3;      //
        // [0]     U1DeviceExitLatency: U1延迟支持
        // [1]     U2DeviceExitLatency: U2延迟支持

    // 10h: 硬件参数寄存器 (HCCPARAMS1)
    UINT32 hccparams1;      //
        // [0]     AC64: 64位寻址能力
        // [1]     BNC: 带宽协商能力
        // [2]     CSZ: 上下文大小 (0=64字节, 1=32字节)
        // [4]     PPC: 端口功率控制支持
        // [5]     PIND: 端口指示器支持

    UINT32 dboff;           // 0x14 门铃寄存器偏移
    UINT32 rtsoff;          // 0x18 运行时寄存器偏移
    UINT32 hccparams2;      // 0x1C 主机控制器能力参数2（1.1引入）
}  xhci_cap_regs_t;

// ===== 2. 操作寄存器 (Operational Registers) =====
typedef struct {
    // 00h: 命令寄存器 (USBCMD)
    UINT32 usbcmd; // 控制器命令寄存器
        #define XHCI_CMD_RS    (1 << 0)  // 运行/停止 (1=运行)
        #define XHCI_CMD_HCRST (1 << 1)  // 主机控制器复位
        #define XHCI_CMD_INTE   (1 << 2)  // 中断使能
        #define XHCI_CMD_HSEE  (1 << 3)  // 主机系统错误使能

    // 04h: 状态寄存器 (USBSTS)
    UINT32 usbsts; // 控制器状态寄存器
        #define XHCI_STS_HCH   (1 << 0)  // 控制器停止状态
        #define XHCI_STS_CNR   (1 << 1)  // 控制器未准备好
        #define XHCI_STS_EINT  (1 << 3)  // 事件中断挂起
        #define XHCI_STS_PCD   (1 << 4)  // 端口变更检测

    // 08h: 页面大小寄存器 (PAGESIZE)
    UINT32 pagesize; // [15:0] 控制器支持的页面大小

    // 0Ch: 保留 [RsvdZ]
    UINT32 reserved0;

    // 10h: 设备上下文基础地址数组指针 (DCBAAP)
    UINT64 dcbaap;  // DCBAA的物理地址指针 (低32位+高32位)

    // 18h: 配置寄存器 (CONFIG)
    UINT32 config;   // [7:0] 启用的设备槽数 (值≤MaxSlots)

    // ... 更多寄存器 ...

    // 20h: 端口状态控制寄存器 (PORTSC[n]) - 每个端口一个
    UINT32 portsc[64]; // 最大支持64个端口
        #define PORTSC_CCS   (1 << 0)   // 当前连接状态 (1=连接设备)
        #define PORTSC_PED   (1 << 1)   // 端口启用/禁用 (1=启用)
        #define PORTSC_OCA   (1 << 3)   // 过流激活
        #define PORTSC_PR    (1 << 4)   // 端口复位 (写1启动复位)
        #define PORTSC_PP    (1 << 9)   // 端口电源 (1=开启)
        #define PORTSC_PLS   (0xF << 5) // 端口链路状态
        #define PORTSC_SPEED (0xF << 10) // 端口速度 (SS=0x03, HS=0x02)
}  xhci_op_regs_t;

// ===== 3. 运行时寄存器 (Runtime Registers) =====
typedef struct {
    // 00h: 微帧索引寄存器 (MFINDEX)
    UINT32 mfindex;  // [13:0] 当前微帧索引

    // 04h: 保留
    UINT32 reserved0[7];

    // 中断管理数组 (IMAN) - 每个中断向量一个
    struct {
        UINT32 iman; // 中断管理
        #define IMAN_IE  (1 << 0)   // 中断使能
        #define IMAN_IP  (1 << 1)   // 中断挂起 (写1清除)
        UINT32 imod; // 中断调制器
    } intr_regs[256]; // 最多支持256个中断向量

    // 事件环段表基址寄存器 (ERSTBA) - 每个中断向量一个
    UINT64 erstba[256]; // 事件环段表的物理地址

    // 事件环段表大小寄存器 (ERSTSZ) - 每个中断向量一个
    UINT32 erstsz[256]; // [15:0] 事件环段表中的条目数

    // ... 更多运行时寄存器 ...
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
