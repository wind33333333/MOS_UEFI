#ifndef _PCIE_H_
#define _PCIE_H_

#include "moslib.h"

typedef struct {
    UINT16 vendor_id;         // 0x00: 供应商 ID
    UINT16 device_id;         // 0x02: 设备 ID
    UINT16 command;           // 0x04: 命令寄存器
    UINT16 status;            // 0x06: 状态寄存器
    UINT8 revision_id;        // 0x08: 修订号
    UINT8 prog_if;            // 0x09: 编程接口
    UINT8 subclass;           // 0x0A: 子类
    UINT8 class_code;         // 0x0B: 类别代码
    UINT8 cache_line_size;    // 0x0C
    UINT8 latency_timer;      // 0x0D
    UINT8 header_type;        // 0x0E: 头部类型
    UINT8 bist;               // 0x0F: 自测试
    UINT32 bar[6];            // 0x10 - 0x27: Base Address Registers (BAR0 - BAR5)
    UINT32 cardbus_cis_ptr;   // 0x28: CardBus CIS 指针
    UINT16 subsystem_vendor_id; // 0x2C
    UINT16 subsystem_id;        // 0x2E
    UINT32 expansion_rom_base; // 0x30: 扩展 ROM 地址
    UINT8 capabilities_ptr;    // 0x34: 能力列表指针
    UINT8 reserved1[3];        // 0x35 - 0x37
    UINT32 reserved2;          // 0x38 - 0x3B
    UINT8 interrupt_line;      // 0x3C
    UINT8 interrupt_pin;       // 0x3D
    UINT8 min_grant;           // 0x3E
    UINT8 max_latency;         // 0x3F
} __attribute__((packed)) pci_config_space_header_t;

typedef struct {
    pci_config_space_header_t header;
    UINT8 device_specific[192]; // 0x40 - 0xFF: 设备私有数据
} __attribute__((packed)) pci_config_space_t;

typedef struct {
    pci_config_space_header_t header;
    UINT8 device_specific[192];     // 0x40 - 0xFF
    UINT8 extended_config[4096 - 256]; // 0x100 - 0xFFF: 扩展配置空间
} __attribute__((packed)) pcie_config_space_t;

// 通用能力结构
struct capability {
    UINT8 cap_id;      // Capability ID
    UINT8 next_ptr;    // Next Pointer
    union {              // 能力特定字段
        // MSI-X能力结构（ID 0x11）
        struct {
            UINT16 control;     // 控制字段（表大小、启用位）
            UINT32 table_offset; // MSI-X表偏移
            UINT32 pba_offset;   // 挂起位阵列偏移
        } msi_x;
        // Power Management能力结构（ID 0x01）
        struct {
            UINT16 pmc;         // 能力字段（支持的状态）
            UINT16 pmcsr;       // 控制/状态寄存器
        } power_mgmt;
        // PCIe能力结构（ID 0x10）
        struct {
            UINT16 pcie_cap;    // PCIe能力字段
            UINT32 dev_cap;     // 设备能力
            UINT16 dev_ctrl;    // 设备控制
        } pcie;
        // 通用数据（占位）
        UINT8 data[14];         // 最大能力结构长度（16字节-公共字段）
    } specific;
};

#endif
