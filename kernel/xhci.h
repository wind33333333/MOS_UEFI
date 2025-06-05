#include <stdint.h>
#ifndef _XHCI_H
#define _XHCI_H

#include "moslib.h"

/* xHCI BAR 内存映射 I/O (MMIO) 空间整体结构 */
typedef struct {
    /* 
     * ========== Capability Registers (功能寄存器) ==========
     * 从 BAR 基地址 + 0x00 开始，只读区域
     */
    struct {
        UINT8  caplength;      // [0x00] 功能寄存器长度 (单位：字节)
        UINT8  reserved0;      // [0x01] 保留
        UINT16 hciversion;     // [0x02] HCI 版本 (BCD格式，如 0x100 表示 1.00)
        UINT32 hcsparams1;     // [0x04] 结构参数1:
                                 //   - Bits 0-7: 最大插槽数 (MaxSlots)
                                 //   - Bits 8-15: 最大中断数 (MaxIntrs)
                                 //   - Bits 16-23: 最大端口数 (MaxPorts)
        UINT32 hcsparams2;     // [0x08] 结构参数2:
                                 //   - Bit 0: 事件中断支持 (IST)
                                 //   - Bit 1: 事件环分段支持 (ERST Max)
                                 //   - Bits 2-6: 保留
                                 //   - Bits 7-31: 支持的协议速度
        UINT32 hcsparams3;     // [0x0C] 结构参数3:
                                 //   - Bits 0-4: U1设备延迟
                                 //   - Bits 8-14: U2设备延迟
        UINT32 hccparams1;     // [0x10] 能力参数1:
                                 //   - Bit 0: 64位寻址支持 (AC64)
                                 //   - Bit 1: 带宽协商支持 (BNC)
                                 //   - Bit 2: 上下文数据结构类型 (CSZ)
                                 //   - Bit 3: 端口功率控制支持 (PPC)
                                 //   - Bit 4: 端口指示器支持 (PIC)
                                 //   - Bit 5: 端口状态改变事件支持 (PCE)
                                 //   - Bit 7: 门铃寄存器偏移 (DBOFF)
        UINT32 dboff;          // [0x14] 门铃寄存器阵列偏移量 (仅当 HCCPARAMS1[7]==1)
        UINT32 rtsoff;         // [0x18] 运行时寄存器偏移量 (仅当 HCCPARAMS1[7]==1)
        UINT32 hccparams2;     // [0x1C] 能力参数2 (扩展特性，如调试能力)
        UINT32 reserved1[9];   // [0x20-0x3F] 保留
    } cap;
    
    /*
     * 注意：操作寄存器位置不是固定偏移！
     * 驱动需根据 cap.caplength 计算实际基址:
     * oper_base = bar_base + cap.caplength;
     */
    
}__attribute__((packed)) xhci_regs_t;

/* xHCI 操作寄存器定义 */
typedef struct {
    /* [0x00] USB Command Register (USB命令寄存器) */
    UINT32 usbcmd;
        #define USB_CMD_RUNSTOP     (1 << 0)   // 控制器运行/停止
        #define USB_CMD_HCRESET     (1 << 1)   // 主机控制器重置
        #define USB_CMD_INTE        (1 << 2)   // 中断启用
        #define USB_CMD_HSEE        (1 << 3)   // 主机系统错误启用
    
    /* [0x04] USB Status Register (USB状态寄存器) */
    UINT32 usbsts;
        #define USB_STS_HCHALTED    (1 << 0)   // 主机控制器停止
        #define USB_STS_HSE         (1 << 2)   // 主机系统错误
        #define USB_STS_EINT        (1 << 3)   // 事件中断待处理
        #define USB_STS_PCD         (1 << 4)   // 端口变更检测
    
    /* [0x08] Page Size Register (页大小寄存器) */
    UINT32 pagesize;            // 控制器支持的页大小 (通常为4KB)
    
    /* 保留区 [0x0C-0x0F] */
    UINT32 reserved0;
    
    /* [0x10] Device Notification Control Register (设备通知控制寄存器) */
    UINT32 dnctrl;
    
    /* [0x14] Command Ring Control Register (命令环控制寄存器) */
    UINT64 crcr;                // 命令环当前段地址（高32位在0x18）
    
    /* [0x1C] Device Context Base Address Array Pointer (设备上下文基地址数组指针) */
    UINT64 dcbaap;              // 指向设备上下文数组物理地址
    
    /* [0x24] Configure Register (配置寄存器) */
    UINT32 config;
        #define CONFIG_MAX_SLOTS_EN (0xFF << 0) // 启用的最大插槽数
    
    /* 端口控制寄存器集 (从0x400开始) */
    struct {
        UINT32 portsc;          // 端口状态和控制
            #define PORTSC_CCS      (1 << 0)   // 当前连接状态
            #define PORTSC_PED      (1 << 1)   // 端口启用/禁用
            #define PORTSC_OCA      (1 << 3)   // 过流激活
            #define PORTSC_RESET    (1 << 4)   // 端口重置中
            #define PORTSC_PLC      (1 << 5)   // 端口链路状态改变
            #define PORTSC_CSC      (1 << 16)  // 连接状态改变
            #define PORTSC_WRC      (1 << 18)  // 热复位完成
        UINT32 portpmsc;        // 端口电源管理状态和控制
        UINT32 portli;           // 端口链路信息
        UINT32 porthlpmc;        // 主机控制端口LPM配置
    } ports[];                  // 最大端口数从 hcsparams1 获取

    // ... 更多操作寄存器 ...

}__attribute__((packed)) xhci_oper_regs_t;

/* xHCI 运行时寄存器定义 */
typedef struct {
    /* 微帧索引寄存器 [0x00] */
    UINT32 mfindex;             // 当前微帧索引
    
    /* 中断寄存器集 (每个中断器一个) */
    struct xhci_intr_regs {
        UINT32 iman;            // 中断管理寄存器
            #define IMAN_INTR_EN   (1 << 0)    // 中断启用
            #define IMAN_INTR_PEND (1 << 1)    // 中断待处理
        UINT32 imod;            // 中断调制寄存器（频率）
        UINT32 ersts;            // 事件环段表大小
        UINT64 erstba;           // 事件环段表基址
        UINT64 erdp;             // 事件环出队指针
    } intrs[];   // 最大中断数从 hcsparams1 获取

    // ... 更多运行时寄存器 ...
    
}__attribute__((packed)) xhci_runtime_regs_t;

/* xHCI 门铃寄存器阵列定义 */
typedef struct {
    UINT32 db[256];             // 256个门铃寄存器 (DWORD大小)
    #define DB_TARGET_CMD_RING 0   // 索引0：命令环通知
    #define DB_TARGET_EP(ep)   (ep+1) // 端点门铃索引计算
    
}__attribute__((packed)) xhci_doorbell_regs_t;

#endif