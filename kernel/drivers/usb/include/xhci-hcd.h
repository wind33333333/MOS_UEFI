#pragma once
#include "xhci-hw.h"

typedef struct xhci_submit_ring_t{
    // === [物理/内存层] ==================
    struct xhci_trb_t   *ring_base;        // 虚拟起始地址
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
    struct xhci_trb_t   *ring_base;        // 虚拟起始地址
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
    uint8               mapped_speed;
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

struct usb_dev_t;
struct usb_hub_port_t;
struct pcie_dev_t;

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

    struct pcie_dev_t          *xdev;
} xhci_hcd_t;

//响铃
static inline void xhci_ring_doorbell(xhci_hcd_t *xhcd, uint8 db_number, uint32 value) {
    xhcd->db_reg[db_number] = value;
}


//xhci原生端口操作命令
//==========================================================================================

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

/**
 * @brief 发起端口热复位 (Hot Reset - 适用于 USB 2.0 & 3.0 常规设备)
 */
static inline void xhci_port_reset_hot(xhci_hcd_t *xhcd, uint8 port_num) {
    uint32 portsc = xhci_read_portsc(xhcd, port_num);

    // 构造安全回写值：保留安全位 | 设置热复位位(RW1S)
    portsc = (portsc & XHCI_PORTSC_PRESERVE_MASK) | XHCI_PORTSC_PR;
    xhci_write_portsc(xhcd, port_num, portsc);
}

/**
 * @brief 发起端口暖复位 (Warm Reset - 仅适用于 USB 3.0 链路死锁救援)
 */
static inline void xhci_port_reset_warm(xhci_hcd_t *xhcd, uint8 port_num) {
    uint32 portsc = xhci_read_portsc(xhcd, port_num);

    // 构造安全回写值：保留安全位 | 设置暖复位位(RW1S)
    portsc = (portsc & XHCI_PORTSC_PRESERVE_MASK) | XHCI_PORTSC_WPR;
    xhci_write_portsc(xhcd, port_num, portsc);
}

/**
 * @brief 强制禁用端口 (Disable Port)
 * 物理不断电，但切断数据链路通信。
 */
static inline void xhci_port_disable(xhci_hcd_t *xhcd, uint8 port_num) {
    uint32 portsc = xhci_read_portsc(xhcd, port_num);

    // 构造安全回写值：保留安全位 | 故意给 PED 写 1 (触发 RW1CS 禁用效果)
    uint32 val = (portsc & XHCI_PORTSC_PRESERVE_MASK) | XHCI_PORTSC_PED;
    xhci_write_portsc(xhcd, port_num, val);
}

//xhci端口上电
static inline  void xhci_port_power_on(xhci_hcd_t *xhcd,uint8 port_num) {
    uint32 portsc = xhci_read_portsc(xhcd, port_num);
    portsc |= XHCI_PORTSC_PP;
    xhci_write_portsc(xhcd, port_num, portsc);
    //等待20ms
}

//xhci端口断电
static inline  void xhci_port_power_off(xhci_hcd_t *xhcd, uint8 port_num) {
    uint32 portsc = xhci_read_portsc(xhcd, port_num);
    portsc &= ~XHCI_PORTSC_PP;
    xhci_write_portsc(xhcd, port_num, portsc);
    //等待20ms
}
//=======================================================================================
