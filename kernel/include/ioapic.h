#pragma once

#include "moslib.h"

// -------------------------------------------------------------
// 1. IOAPIC 重定向表项 (Redirection Table Entry, RTE)
// 这是 IOAPIC 最核心的结构，每个外部引脚对应一个 64 位的 RTE
// -------------------------------------------------------------
typedef union {
    struct {
        uint32 vector          : 8;  // [7:0]   中断向量号 (通常映射为 0x20~0xFF)
        uint32 delivery_mode   : 3;  // [10:8]  投递模式 (000: Fixed, 001: Lowest Priority等)
        uint32 dest_mode       : 1;  // [11]    目标模式 (0: 物理模式, 1: 逻辑模式)
        uint32 delivery_status : 1;  // [12]    投递状态 (只读：0=Idle, 1=Send Pending)
        uint32 polarity        : 1;  // [13]    引脚极性 (0: 高电平/上升沿触发, 1: 低电平/下降沿触发)
        uint32 remote_irr      : 1;  // [14]    远程 IRR (只读，Level触发时有效)
        uint32 trigger_mode    : 1;  // [15]    触发模式 (0: 边沿触发 Edge, 1: 电平触发 Level)
        uint32 mask            : 1;  // [16]    中断屏蔽 (0: 允许投递, 1: 屏蔽该中断)
        uint32 reserved1       : 15; // [31:17] 保留位

        uint32 reserved2       : 24; // [55:32] 保留位
        uint32 destination     : 8;  // [63:56] 目标 CPU 的 Local APIC ID
    } __attribute__((packed)) bits;

    struct {
        uint32 low_dword;            // 低 32 位
        uint32 high_dword;           // 高 32 位
    } dwords;

    uint64 value;                    // 64 位整体访问
} ioapic_rte_t;

typedef struct {
    uint32 reg_sel;  //虚拟地址：索引寄存器 (IOREGSEL, 偏移 0x00)
    uint32 reserved1[3];
    uint32 reg_win;  // 虚拟地址：数据寄存器 (IOWIN, 偏移 0x10)
    uint32 reserved2[11];
    uint32 reg_eoi;  // 基地址 + 0x40 (EOIR)
}__attribute__((packed)) ioapic_hw_res_t;

// -------------------------------------------------------------
// 2. IOAPIC 硬件设备抽象 (软件描述符)
// -------------------------------------------------------------
typedef struct ioapic_devive_t {
    // 物理与虚拟地址
    uint64 phys_addr;          // IOAPIC 在主板上的物理基址 (通常是 0xFEC00000)
    ioapic_hw_res_t *ioapic_hw_res;

    // 硬件基础信息
    uint8  id;                 // IOAPIC 的硬件 ID
    uint8  version;            // 硬件版本号
    uint8  max_intr_entries;   // 该 IOAPIC 支持的最大中断引脚数 (通常为 24)

    // 全局系统中断 (GSI) 映射
    // 现代多路服务器可能有多个 IOAPIC，比如 IOAPIC0 负责引脚 0~23，IOAPIC1 负责 24~47
    uint32 gsi_base;           // 本 IOAPIC 负责的起始全局中断号

    // 软件状态维护
    ioapic_rte_t *shadow_rtes;   // (可选) 在内存中备份一份 RTE，用于快速读取和恢复
}ioapic_devive_t;


void init_ioapic(void);

typedef struct{
    uint8 *ioregsel;    //索引寄存器 8位
    uint32 *iowin;      //数据寄存器 32位
    uint32 *eoi;        //中断结束寄存器   32位
}ioapic_address_t;


