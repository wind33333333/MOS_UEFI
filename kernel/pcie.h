#ifndef _PCIE_H_
#define _PCIE_H_

#include "moslib.h"

typedef struct {
    /* PCI 通用配置头（前 64 字节）*/
    /* 设备标识区 (0x00 - 0x0F) */
    UINT16 vendor_id; // 厂商ID (0x00) - 由 PCI-SIG 分配
    UINT16 device_id; // 设备ID (0x02) - 厂商自定义型号
    UINT16 command; // 命令寄存器 (0x04)
    UINT16 status; // 状态寄存器 (0x06)
    UINT8 revision_id; // 修订ID (0x08) - 硬件版本号
    UINT8 class_code[3]; // 类代码 (0x09-0x0B)
    UINT8 cache_line_size; // 缓存行大小 (0x0C) - CPU 缓存对齐
    UINT8 latency_timer; // 延迟定时器 (0x0D) - PCI 总线延迟
    UINT8 header_type; // 头类型 (0x0E) - 0=端点设备,1=桥设备
    UINT8 bist; // BIST 寄存器 (0x0F) - 自检控制
    /* 设备/桥专用区 (0x10 - 0x3F) */
    union {
        // Type 0: 端点设备结构
        struct {
            UINT32 bar[6]; // BAR0-BAR5 (0x10-0x27) - 基地址寄存器
            UINT32 cardbus_cis; // CardBus CIS 指针 (0x28) - 向后兼容
            UINT16 subsystem_vendor; // 子系统厂商ID (0x2C)
            UINT16 subsystem_id; // 子系统设备ID (0x2E)
            UINT32 expansion_rom; // 扩展ROM BAR (0x30) - BIOS 固件地址
            UINT8 cap_ptr; // 能力链表指针 (0x34)
            UINT8 reserved[7]; // 保留区 (0x35-0x3B)
            UINT8 interrupt_line; // 中断线 (0x3C) - APIC/IOAPIC 路由
            UINT8 interrupt_pin; // 中断引脚 (0x3D) - INTA#-INTD#
            UINT8 min_grant; // 最小授权 (0x3E) - PCI 时序
            UINT8 max_latency; // 最大延迟 (0x3F) - PCI 时序
        } type0;

        // Type 1: PCI 桥设备结构
        struct {
            UINT32 bar[2]; // BAR0-BAR1 (0x10-0x17) - 桥专用
            UINT8 primary_bus; // 上游总线号 (0x18)
            UINT8 secondary_bus; // 下游总线号 (0x19)
            UINT8 subordinate_bus; // 子总线最大号 (0x1A)
            UINT8 secondary_latency; // 下游总线延迟 (0x1B)
            UINT8 io_base; // I/O 范围下限 (0x1C)
            UINT8 io_limit; // I/O 范围上限 (0x1D)
            UINT16 secondary_status; // 下游总线状态 (0x1E)
            UINT16 memory_base; // 内存范围下限 (0x20)
            UINT16 memory_limit; // 内存范围上限 (0x22)
            UINT16 prefetch_base; // 预取内存下限 (0x24)
            UINT16 prefetch_limit; // 预取内存上限 (0x26)
            UINT32 prefetch_upper; // 预取上限扩展 (0x28) - 64位地址高32位
            UINT16 io_upper_base; // I/O上限扩展 (0x2C)
            UINT16 io_upper_limit; // I/O上限扩展 (0x2E)
            UINT8 cap_ptr; // 能力链表指针 (0x34)
            UINT8 reserved[3]; // 保留区 (0x35-0x37)
            UINT32 rom_base; // 扩展ROM BAR (0x38)
            UINT8 interrupt_line; // 中断线 (0x3C)
            UINT8 interrupt_pin; // 中断引脚 (0x3D)
            UINT16 bridge_control; // 桥控制寄存器 (0x3E)
        } type1;
    };

    UINT8 device_specific[192]; // 0x40 - 0xFF
    UINT8 extended_config[4096 - 256]; // 0x100 - 0xFFF: 扩展配置空间
} __attribute__((packed)) pcie_config_space_t;

// 通用能力结构
typedef struct {
    UINT8 cap_id; // Capability ID
    UINT8 next_ptr; // Next Pointer
    union {
        // 能力特定字段
        // MSI-X能力结构（ID 0x11）
        struct {
            UINT16 control; // 控制字段（表大小、启用位）
            UINT32 table_offset; // MSI-X表偏移
            UINT32 pba_offset; // 挂起位阵列偏移
        } msi_x;

        // Power Management能力结构（ID 0x01）
        struct {
            UINT16 pmc; // 能力字段（支持的状态）
            UINT16 pmcsr; // 控制/状态寄存器
        } power_mgmt;

        // PCIe能力结构（ID 0x10）
        struct {
            UINT16 pcie_cap; // PCIe能力字段
            UINT32 dev_cap; // 设备能力
            UINT16 dev_ctrl; // 设备控制
        } pcie;

        // 通用数据（占位）
        UINT8 data[14]; // 最大能力结构长度（16字节-公共字段）
    };
} capability_t;

// MSI-X Table条目 (16字节)
typedef struct {
    UINT32 msg_addr_lo; // 消息地址低32位
    UINT32 msg_addr_hi; // 消息地址高32位 (如果64位)
    UINT32 msg_data; // 消息数据值
    UINT32 vector_control; // 向量控制 (通常Bit0=Per Vector Mask)
} msi_x_table_entry_t;

typedef struct {
    list_head_t list; /* 全局 PCI 设备链表节点 */
    UINT8 func;         /* 功能号 */
    UINT8 dev;          /* 设备号 */
    UINT8 bus;          /* 总线号 */
    UINT8 *name;        /* 设备名 */
    UINT32 class;       /* 设备类 */
    union {
        UINT32 *bar32[6];
        UINT64 *bar64[3];
    };
    UINT16 *msi_x_control;
    msi_x_table_entry_t *msi_x_table;
    UINT32 *msi_x_pad;
} pcie_dev_t;

void init_pcie(void);

#endif
