#pragma once
#include "../core/usb-def.h"
#include "xhci-hw.h"

#define XHCI_CMD_RING_LEN  256                   // 命令环深度 (必须是 2 的幂次)
#define XHCI_CMD_RING_MASK (NOVA_CMD_RING_LEN - 1)

/* 3. 命令环影子上下文表项 (O(1) 按位对应槽位) */
typedef struct xhci_cmd_io_tracker_t{
    void     *async_waker;     // 指向协程/进程/异步 Task 的 Waker 句柄
    uint32   out_slot_id;     // [输出直放指针] 用于接收 Enable Slot 成功返回的 Slot ID
    int32    out_hw_status;   // [输出状态码] 用于回填硬件 Completion Code
    boolean  is_completed;     // 任务完成原子标示
}xhci_cmd_io_tracker_t;

#define XHCI_CONTROL_TRANSFER_RING_LEN  256                   // 控制传输环深度 (必须是 2 的幂次)
#define XHCI_CONTROL_TRANSFER_RING_MASK (NOVA_CMD_RING_LEN - 1)
/* =========================================================================
 * 🌟 统一传输环影子槽位节点 (EP0 控制传输 与 EP1~31 普通传输 完全归一同构！)
 * 不再有任何 usb_urb_t 历史包袱，纯粹以 task_id / user_data 驱动
 * ========================================================================= */
typedef struct xhci_transfer_io_tracker_t {
    void     *async_waker;       // 挂起的协程 / 异步任务 Waker 句柄
    uint64  task_id;           // 上层用户态/eBPF驱动下发的任务唯一标号 (user_data)
    uint32  actual_bytes;      // [输出] DMA 实际收发成功的真实字节数
    int32   hw_status;         // [输出] xHCI 硬件状态码 (1 = SUCCESS)
    boolean   is_completed;      // 任务完成原子标示
    void (*cb)(void *context);
} xhci_transfer_io_tracker_t;


/* 传输控制掩码 */
#define TX_IOC          (1 << 0)  // 终点 TRB 强制点亮 IOC=1 中断位
#define TX_ZERO_PACKET  (1 << 1)  // 整数倍长度时自动追加一格 0 字节 ZLP
#define TX_BLOCKED      (1 << 2)  // 阻塞

/**
 * @brief [USB 控制平面] EP0 控制传输请求
 * @note 专供 xhci_submit_control 使用。严格匹配 SETUP 阶段需求。
 */
typedef struct xhci_ctrl_req_t {
    // 1. 数据阶段缓冲 (如果没有数据阶段，buf 为 NULL, length 为 0)
    void               *buf;           // [0~7B]
    uint32             length;         // [8~11B]

    // 2. 传输控制
    uint32             flags;          // [12~15B] TX_IOC 等标志
    uint64             task_id;        // [16~23B] 控制请求流水号
    void               *waker;         // [24~31B] 异步唤醒句柄

    // 3. 🌟 绝对独占：控制传输的心脏 —— 8 字节 SETUP 报文
    union {
        uint64             setup_packet_raw;
        usb_setup_packet_t setup_packet;
    };                                 // [32~39B]

    // (结构体总大小: 40 Bytes，可按需 padding 到 64 Bytes 对齐 Cache Line)
} xhci_ctrl_req_t;

/**
 * @brief [数据平面] 批量/中断/流端点 纯数据传输请求
 * @note 专供 xhci_submit_normal 和 xhci_submit_stream 使用。
 */
typedef struct xhci_data_req_t {
    // 1. 核心数据缓冲
    void               *buf;           // [0~7B] 数据缓冲区 DMA 基址
    uint32             length;         // [8~11B] 有效负载长度

    // 2. 传输控制
    uint32             flags;          // [12~15B] TX_IOC, TX_ZERO_PACKET 等
    uint64             task_id;        // [16~23B] SCSI Task Tag 等业务凭证
    void               *waker;         // [24~31B] 异步唤醒句柄

    // 3. 🌟 数据面特有属性：流通道 ID
    uint16             stream_id;      // [32~33B] UAS 目标硬件流通道 (普通传输置 0)

    void (*cb)(void *context);
    // (结构体总大小: 34 Bytes，远小于 64 Bytes，内存密度极高)
} xhci_data_req_t;


typedef struct  xhci_submit_ring_t {
    xhci_trb_t *ring_base;     // 原生硬件 TRB 虚拟基址
    int32     enq_idx;       // 生产者游标
    int32     deq_idx;       // 消费者游标
    int32     size;     // 环深度 (32 / 64 / 256 / 1024)
    uint8     cycle;         // 当前硬件 Cycle Toggle Bit (0/1)
} xhci_submit_ring_t;


// 硬件是生产者，软件是消费者
typedef struct xhci_event_ring_t{
    xhci_trb_t   *ring_base;        // 虚拟起始地址
    uint32       ring_size;         // 事件环通常极大 (例如 1024)
    uint32       deq_idx;           // 🌟 只有出队游标！干净利落！
    uint8        cycle;             // 软件期望硬件写入的 Cycle 状态

    // 🌟 事件环独有的物理结构
    xhci_erst_t *erst_base;   // 指向 ERST 段表内存的虚拟地址
    uint32       erst_size;

    uint32      ring_lock;
} xhci_event_ring_t;



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

typedef struct usb_dev_t usb_dev_t;
typedef struct usb_if_alt_t usb_if_alt_t;
typedef struct usb_ep_t usb_ep_t;
typedef struct usb_hub_port_t usb_hub_port_t;
typedef struct pcie_dev_t pcie_dev_t;


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
    uint64                *dcbaap;            // 设备上下文基址数组 (物理地址数组)
    xhci_submit_ring_t    cmd_ring;           // 全局单例：命令环 (Command Ring)
    xhci_cmd_io_tracker_t *cmd_io_tracker;    //I/O 追踪器 / 发送追踪表

    // ==========================================
    // 5. 软硬件映射与并发控制 (Software State)
    // ==========================================
    usb_dev_t      **udevs;         // 插槽到设备的逻辑映射 (通过 Slot ID 查找 usb_dev_t)
    usb_hub_port_t *ports;          // xhci原生端口

    // 注意：事件环不是一个，它是和中断器绑定的！这里根据 max_intrs 动态分配！
    xhci_event_ring_t*  event_ring_arr;
    uint16              enable_num_event_ring;  // 启用中断器数量，取cpu核心数量和max_intrs最小值

    pcie_dev_t          *xdev;
} xhci_hcd_t;


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



//====================================ring 接口函数=======================================
int32 xhci_submit_ring_enq(xhci_submit_ring_t *ring, xhci_trb_t *trb_push);
int32 xhci_event_ring_deq(xhci_event_ring_t *ring, xhci_trb_t *out_evt);
int32 xhci_alloc_submit_ring(xhci_submit_ring_t *ring,uint32 size);  //分配发送环
int32 xhci_free_submit_ring(xhci_submit_ring_t *ring); //释放发送环
int32 xhci_alloc_event_ring(xhci_event_ring_t *ring,uint32 ring_size); //分配事件环
int32 xhci_free_event_ring(xhci_event_ring_t *ring); //释放事件环
int32 xhci_submit_cmd(xhci_hcd_t *xhcd,xhci_trb_t *cmd_trb,xhci_cmd_io_tracker_t *out_tracker);
int32 xhci_submit_control(struct usb_dev_t *udev,const xhci_ctrl_req_t *req);
int32 xhci_submit_normal(usb_ep_t *ep, const xhci_data_req_t *req);
int32 xhci_submit_stream(usb_ep_t *ep, const xhci_data_req_t *req);
int32 xhci_alloc_ep_resource(usb_ep_t *ep,uint32 ring_max_trbs);
int32 xhci_free_ep_resource(usb_ep_t *ep);

//响铃
static inline void xhci_ring_doorbell(xhci_hcd_t *xhcd, uint8 db_number, uint32 value) {
    xhcd->db_reg[db_number] = value;
}


//================================= ctx接口函数===============================================
int32 xhci_ctx_addr_dev(usb_dev_t *udev);
int32 xhci_ctx_slot_cfg(usb_dev_t *udev);
int32 xhci_ctx_slot_ep0_eval(usb_dev_t *udev);
int32 xhci_ctx_eps_cfg(usb_if_alt_t *drop_uif_alt,usb_if_alt_t *add_uif_alt);
int32 xhci_ctx_deconfigure_all(usb_dev_t *udev );
//============================================================================================

//================================= cmd命令 =================================================
int32 xhci_cmd_enable_slot(xhci_hcd_t *xhcd, uint8 port_num);
int32 xhci_cmd_disable_slot(xhci_hcd_t *xhcd, uint8 slot_id);
int32 xhci_cmd_addr_dev(xhci_hcd_t *xhcd, uint8 slot_id, xhci_input_ctrl_ctx_t *in_ctx);
int32 xhci_cmd_cfg_ep(xhci_hcd_t *xhcd, uint8 slot_id, xhci_input_ctrl_ctx_t *in_ctx, uint8 dc);
int32 xhci_cmd_eval_ctx(xhci_hcd_t *xhcd, uint8 slot_id, xhci_input_ctrl_ctx_t *in_ctx);
int32 xhci_cmd_stop_ep(xhci_hcd_t *xhcd, uint8 slot_id, uint8 ep_dci);
int32 xhci_cmd_reset_ep(xhci_hcd_t *xhcd, uint8 slot_id, uint8 ep_dci);
int32 xhci_cmd_set_tr_deq_ptr(xhci_hcd_t *xhcd, uint8 slot_id, uint8 ep_dci,xhci_submit_ring_t *transfer_ring);
int32 xhci_cmd_reset_dev(xhci_hcd_t *xhcd, uint8 slot_id);
//==================================================================================================



