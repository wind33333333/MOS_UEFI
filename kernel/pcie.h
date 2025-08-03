#pragma once
#include "moslib.h"

#pragma pack(push,1)
typedef struct {
    /* PCI 通用配置头（前 64 字节）*/
    /* 设备标识区 (0x00 - 0x0F) */
    // 偏移 0x00
    UINT16 vendor_id; // 厂商ID (0x00) - 由 PCI-SIG 分配
    UINT16 device_id; // 设备ID (0x02) - 厂商自定义型号
    // 偏移 0x04
    UINT16 command;     // 命令寄存器 (0x04)
    UINT16 status;      // 状态寄存器 (0x06)
    // 偏移 0x08
    UINT8 revision_id;  // 修订ID (0x08) - 硬件版本号
    UINT8 prog_if;      // 编程接口，定义设备特定功能接口
    UINT8 subclass;     // 子类别代码，标识设备子类型
    UINT8 class_code;   // 类别代码，标识设备类型（如显示控制器、网络控制器等）
    // 偏移 0x0C
    UINT8 cache_line_size; // 缓存行大小 (0x0C) - CPU 缓存对齐
    UINT8 latency_timer; // 延迟定时器 (0x0D) - PCI 总线延迟
    UINT8 header_type; // 头类型 (0x0E) - bit0 0=端点设备 1=桥设备,bit7 0=单功能设备 1=多功能设备
    UINT8 bist; // BIST 寄存器 (0x0F) - 自检控制
    /* 设备/桥专用区 (0x10 - 0x3F) */
    union {
        // Type 0: 端点设备结构
        struct {
            /*+--------------------+---------+
            | Bit 0              | 1 bit   | 0 = 内存 BAR
            | Bit 1-2            | 2 bits  | 地址类型 (00 = 32-bit, 10 = 64-bit)
            | Bit 3              | 1 bit   | 预取 (1 = Prefetchable, 0 = Non-Prefetchable)
            | Bit 4-31           | 28 bits | 基地址 (4 字节对齐)
            +--------------------+---------+*/
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
} pcie_config_space_t;

// 通用能力结构
typedef struct {
    UINT8 cap_id; // Capability ID
    UINT8 next_ptr; // Next Pointer
    union {
        //MSI 能力结构（ID 0x5）
        struct {
            UINT16 msg_control;/*- 位0：MSI Enable（1=启用，0=禁用）。
                             - 位1-3：Multiple Message Capable（支持的向量数：0=1，1=2，2=4，3=8，4=16，5=32）。
                             - 位4-6：Multiple Message Enable（启用的向量数）。
                             - 位7：64-bit Address Capable（1=支持64位地址）。
                             - 位8-15：保留。*/
            UINT32 msg_addr_lo;  //32位消息地址（MSI中断写入的内存地址）。
            UINT32 msg_addr_hi;  //64位地址的高32位（仅当64-bit Address Capable=1时有效）。
            UINT16 msg_data;     //中断消息数据（写入Message Address的值，用于触发中断）。
        }msi;

        // MSI-X能力结构（ID 0x11）
        struct {
            UINT16 msg_control; // 位 0-10：MSI-X 表大小（N-1 编码，实际向量数 = vector_count + 1）
                                // 位 14：全局掩码（1 = 禁用所有 MSI-X 中断，0 = 启用）
                                // 位 15：MSI-X 启用（1 = 启用 MSI-X，0 = 禁用）
            UINT32 table_offset; // 位 0-2：BAR 指示器（Base Address Register Index）
                                 // 位 3-31：MSI-X 表偏移地址（相对于 BAR 的基地址）
            UINT32 pba_offset;   // 位 0-2：BAR 指示器
                                 // 位 3-31：PBA 偏移地址（相对于 BAR 的基地址）
        } msi_x;

        // Power Management能力结构（ID 0x01）
        struct {
            UINT16 pmc; // 能力字段（支持的状态）
            UINT16 pmcsr; // 控制/状态寄存器
        } power_mgmt;

        // PCIe能力结构（ID 0x10）
        struct {
            UINT16 pcie_capability;        // PCIe能力寄存器，包含版本和设备类型等信息
            UINT32 device_capability;      // 设备能力寄存器，描述设备支持的功能
            UINT16 device_control;         // 设备控制寄存器，用于配置设备行为
            UINT16 device_status;          // 设备状态寄存器，反映设备当前状态
            UINT32 link_capability;        // 链路能力寄存器，描述链路特性如带宽和速度
            UINT16 link_control;           // 链路控制寄存器，用于配置链路行为
            UINT16 link_status;            // 链路状态寄存器，反映链路当前状态
            UINT32 slot_capability;        // 插槽能力寄存器（仅对Root Port或Switch有效）
            UINT16 slot_control;           // 插槽控制寄存器，配置插槽行为
            UINT16 slot_status;            // 插槽状态寄存器，反映插槽状态
            UINT16 root_control;           // 根控制寄存器（仅对Root Complex有效）
            UINT16 root_capability;        // 根能力寄存器，描述Root Complex支持的功能
            UINT32 root_status;            // 根状态寄存器，反映Root Complex状态
            UINT32 device_capability2;     // 设备能力寄存器2，支持扩展功能
            UINT16 device_control2;        // 设备控制寄存器2，配置扩展功能
            UINT16 device_status2;         // 设备状态寄存器2，反映扩展功能状态
            UINT32 link_capability2;       // 链路能力寄存器2，支持更新的链路特性
            UINT16 link_control2;          // 链路控制寄存器2，配置更新链路行为
            UINT16 link_status2;           // 链路状态寄存器2，反映更新链路状态
        } pcie_cap;

        // 通用数据（占位）
        UINT8 data[14]; // 最大能力结构长度（16字节-公共字段）
    };
} cap_t;

// MSI-X Table条目 (16字节)
typedef struct {
    UINT32 msg_addr_lo;    // 消息地址低32位
    UINT32 msg_addr_hi;    // 消息地址高32位 (如果64位)
    UINT32 msg_data;       // 消息数据值
    UINT32 vector_control; // 向量控制 (通常Bit0=Per Vector Mask)
} msi_x_table_t;

#pragma pack(pop)

typedef struct {
    list_head_t list; /* 全局 PCI 设备链表节点 */
    UINT8 func; /* 功能号 */
    UINT8 dev; /* 设备号 */
    UINT8 bus; /* 总线号 */
    UINT8 *name; /* 设备名 */
    pcie_config_space_t *pcie_config_space; /* pcie配置空间 */
    void *bar[6]; /*bar*/
    UINT8 msi_x_flags;    // 1 = 支持msi_x 0 = 不支持msi_x
    union {
        struct {
            UINT16 *msg_control;/*- 位0：MSI Enable（1=启用，0=禁用）。
                                  - 位1-3：Multiple Message Capable（支持的向量数：0=1，1=2，2=4，3=8，4=16，5=32）。
                                  - 位4-6：Multiple Message Enable（启用的向量数）。
                                  - 位7：64-bit Address Capable（1=支持64位地址）。
                                  - 位8-15：保留。*/
            UINT32 *msg_addr_lo;  //32位消息地址（MSI中断写入的内存地址）。
            UINT32 *msg_addr_hi;  //64位地址的高32位（仅当64-bit Address Capable=1时有效）。
            UINT16 *msg_data;     //中断消息数据（写入Message Address的值，用于触发中断）。
        } msi;

        struct {
            UINT16        *msg_control;   // 位 0-10：MSI-X 表大小（N-1 编码，实际向量数 = vector_count + 1）
                                          // 位 14：全局掩码（1 = 禁用所有 MSI-X 中断，0 = 启用）
                                          // 位 15：MSI-X 启用（1 = 启用 MSI-X，0 = 禁用）
            msi_x_table_t *msi_x_table;   //msi-x中断配置表
            UINT64        *pba_table;     //中断挂起表
        } msi_x;
    };
} pcie_dev_t;

typedef enum {
    power_mgmt_e = 1,
    msi_e        = 5,
    pcie_cap_e   = 0x10,
    msi_x_e      = 0x11
} cap_id_e;

//获取pice设备的class_code
static inline UINT32 get_pcie_classcode(pcie_dev_t *pcie_dev) {
    UINT32 *class_code = (UINT32*)&pcie_dev->pcie_config_space->revision_id;
    return *class_code >> 8;
}

/*pcie设备class_code*/
#define UNCLASSIFIED_CLASS_CODE        0x000000  // 未分类设备
#define SCSI_CLASS_CODE                0x010000  // SCSI 控制器
#define AHCI_CLASS_CODE                0x010601  // SATA 控制器（AHCI 模式）
#define NVME_CLASS_CODE                0x010802  // NVMe 控制器
#define ETHERNET_CLASS_CODE            0x020000  // 以太网控制器
#define VGA_CLASS_CODE                 0x030000  // VGA 控制器
#define XGA_CLASS_CODE                 0x030100  // XGA 控制器
#define DISPLAY_3D_CLASS_CODE          0x030200  // 3D 控制器（非 VGA 兼容）
#define DISPLAY_OTHER_CLASS_CODE       0x038000  // 其他显示控制器
#define MULTIMEDIA_VIDEO_CLASS_CODE    0x040000  // 多媒体视频控制器
#define HOST_BRIDGE_CLASS_CODE         0x060000  // 主机桥
#define ISA_BRIDGE_CLASS_CODE          0x060100  // ISA 桥
#define EISA_BRIDGE_CLASS_CODE         0x060200  // EISA 桥
#define MCA_BRIDGE_CLASS_CODE          0x060300  // MCA 桥
#define PCI_TO_PCI_BRIDGE_CLASS_CODE   0x060400  // PCI 到 PCI 桥
#define PCMCIA_BRIDGE_CLASS_CODE       0x060500  // PCMCIA 桥
#define NUBUS_BRIDGE_CLASS_CODE        0x060600  // NuBus 桥
#define CARDBUS_BRIDGE_CLASS_CODE      0x060700  // CardBus 桥
#define RACEWAY_BRIDGE_CLASS_CODE      0x060800  // RACEway 桥
#define PCI_TO_PCI_ALT_CLASS_CODE      0x060900  // PCI 到 PCI 桥（备用）
#define INFINIBAND_TO_PCI_CLASS_CODE   0x060A00  // InfiniBand 到 PCI 主机桥
#define BRIDGE_OTHER_CLASS_CODE        0x068000  // 其他桥接设备
#define UHCI_CLASS_CODE                0x0C0300  // USB 1.1 控制器（UHCI）
#define OHCI_CLASS_CODE                0x0C0310  // USB 1.1 控制器（OHCI）
#define EHCI_CLASS_CODE                0x0C0320  // USB 2.0 控制器（EHCI）
#define XHCI_CLASS_CODE                0x0C0330  // USB 3.0 控制器（XHCI）
#define FIBRE_CHANNEL_CLASS_CODE       0x0C0400  // 光纤通道
#define SMBUS_CLASS_CODE               0x0C0500  // SMBus 控制器
#define INFINIBAND_CLASS_CODE          0x0C0600  // InfiniBand 控制器
#define IPMI_CLASS_CODE                0x0C0700  // IPMI 接口
#define SERCOS_CLASS_CODE              0x0C0800  // SERCOS 接口（IEC 61491）
#define CANBUS_CLASS_CODE              0x0C0900  // CANbus 控制器
#define SERIAL_BUS_OTHER_CLASS_CODE    0x0C8000  // 其他串行总线控制器

void init_pcie(void);
pcie_dev_t *find_pcie_dev(UINT32 class_code);
cap_t *find_pcie_cap(pcie_dev_t *pcie_dev, cap_id_e cap_id);
void *init_pcie_bar(pcie_dev_t *pcie_dev,UINT8 bar_number);
void init_pcie_msi_intrpt(pcie_dev_t *pcie_dev) ;
void enable_msi_intrpt(pcie_dev_t *pcie_dev);
void disable_msi_intrpt(pcie_dev_t *pcie_dev);

