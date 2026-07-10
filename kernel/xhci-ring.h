#pragma once
#include "moslib.h" // 包含你的基本类型如 uint32, uint64 等

// ============================================================================
// 📖 TRB 类型字典 (TRB Types)
// 对应 DW3 [15:10]
// ============================================================================
// 传输环 TRB(Transfer Ring)
#define TRB_TYPE_NORMAL              1   // 普通数据传输
#define TRB_TYPE_SETUP_STAGE         2   // 控制传输：Setup 阶段
#define TRB_TYPE_DATA_STAGE          3   // 控制传输：Data 阶段
#define TRB_TYPE_STATUS_STAGE        4   // 控制传输：Status 阶段
#define TRB_TYPE_ISOCH               5   // 等时传输
#define TRB_TYPE_LINK                6   // 链接 TRB (环形闭环)
#define TRB_TYPE_EVENT_DATA          7   // 事件数据 (给虚拟化或者特殊同步用的)
#define TRB_TYPE_NO_OP               8   // 空操作 TRB (占坑用)

// 命令 TRB (Command Ring)
#define TRB_TYPE_ENABLE_SLOT         9   // 启用设备槽
#define TRB_TYPE_DISABLE_SLOT        10  // 禁用设备槽
#define TRB_TYPE_ADDRESS_DEVICE      11  // 分配设备地址
#define TRB_TYPE_CONFIGURE_EP        12  // 配置端点
#define TRB_TYPE_EVALUATE_CTX        13  // 评估上下文
#define TRB_TYPE_RESET_EP            14  // 复位端点 (解 STALL)
#define TRB_TYPE_STOP_EP             15  // 停止端点 (中止传输)
#define TRB_TYPE_SET_TR_DEQUEUE      16  // 设置出队指针
#define TRB_TYPE_RESET_DEVICE        17  // 复位设备
#define TRB_TYPE_FORCE_EVENT         18  // 强制事件 (SR-IOV 虚拟化常用)
#define TRB_TYPE_NEGOTIATE_BW        19  // 协商带宽
#define TRB_TYPE_SET_LATENCY_TOL     20  // 设置延迟容忍度
#define TRB_TYPE_GET_PORT_BW         21  // 获取端口带宽
#define TRB_TYPE_FORCE_HEADER        22  // 强制包头
#define TRB_TYPE_NO_OP_CMD           23  // 空操作命令 (测试命令环通不通时用)

// 事件 TRB (Event Ring)
#define TRB_TYPE_TRANSFER_EVENT      32  // 传输完成事件
#define TRB_TYPE_CMD_COMPLETION      33  // 命令完成事件
#define TRB_TYPE_PORT_STATUS_CHG     34  // 端口状态改变事件
#define TRB_TYPE_BANDWIDTH_REQ       35  // 带宽请求事件
#define TRB_TYPE_DOORBELL            36  // 门铃事件
#define TRB_TYPE_HOST_CTRL           37  // 主机控制器事件
#define TRB_TYPE_DEVICE_NOTIFY       38  // 设备通知事件
#define TRB_TYPE_MFINDEX_WRAP        39  // 微帧索引翻转事件

// ============================================================================
// 📖 TRB 完成码字典 (Completion Codes)
// 对应事件 TRB 的 DW2 [31:24]
// ============================================================================
// 【通用与系统级事件】 (Shared / System Level)
#define COMP_SUCCESS                     1  // [通用] 完美成功
#define COMP_TRB_ERROR                   5  // [通用] TRB 格式非法 (填错字段、Chain不对等)
#define COMP_RESOURCE_ERROR              7  // [通用] 主板 xHC 控制器内部资源/内存耗尽
#define COMP_VF_EVENT_RING_FULL_ERROR    16 // [通用] 虚拟功能事件环满爆 (SR-IOV)
#define COMP_EVENT_RING_FULL_ERROR       21 // [通用] 真实事件环满爆 (内核中断处理太慢)
#define COMP_EVENT_LOST_ERROR            32 // [通用] 事件丢失 (事件环溢出导致主板丢弃回执)
#define COMP_UNDEFINED_ERROR             33 // [通用] 未定义的致命硬件崩溃

// 【命令事件专属】 (Command Event Only)
#define COMP_BANDWIDTH_ERROR             8  // [命令] 配置端点时，USB 总线带宽不足
#define COMP_NO_SLOTS_AVAILABLE_ERROR    9  // [命令] Enable Slot 时，主板分配不出新槽位
#define COMP_INVALID_STREAM_TYPE_ERROR   10 // [命令] 配置流上下文时，Stream Type 非法
#define COMP_SLOT_NOT_ENABLED_ERROR      11 // [命令] 对未经 Enable 的槽位下发了命令
#define COMP_ENDPOINT_NOT_ENABLED_ERROR  12 // [命令] 对未经初始化的端点下发了命令
#define COMP_PARAMETER_ERROR             17 // [命令] Context 上下文结构体参数填错或未对齐
#define COMP_CONTEXT_STATE_ERROR         19 // [命令] 状态机时序错误 (如：乱发 Reset Endpoint)
#define COMP_COMMAND_RING_STOPPED        24 // [命令] 正常回执：命令环已成功停止
#define COMP_COMMAND_ABORTED             25 // [命令] 正常回执：命令已被成功中止
#define COMP_SECONDARY_BANDWIDTH_ERROR   35 // [命令] 配置端点辅助带宽时出错

// 【传输事件专属】 (Transfer Event Only)
#define COMP_DATA_BUFFER_ERROR           2  // [传输] 数据缓冲区错误 (主机内存 DMA 寻址失败)
#define COMP_BABBLE_ERROR                3  // [传输] 喋喋不休 (U盘发来的数据超出预期，端点 Halted)
#define COMP_USB_TRANSACTION_ERROR       4  // [传输] 物理链路车祸 (超时/CRC失败，端点 Halted)
#define COMP_STALL_ERROR                 6  // [传输] 逻辑卡死 (U盘主动拒绝服务，端点 Halted)
#define COMP_SHORT_PACKET                13 // [传输] 短包响应 (数据少于预期，BOT 中属正常)
#define COMP_RING_UNDERRUN               14 // [传输] 等时环下溢出 (发数据太慢)
#define COMP_RING_OVERRUN                15 // [传输] 等时环上溢出 (收数据太慢)
#define COMP_BANDWIDTH_OVERRUN_ERROR     18 // [传输] 带宽超载 (设备发送过量数据)
#define COMP_NO_PING_RESPONSE_ERROR      20 // [传输] USB 3.0 链路无 Ping 响应
#define COMP_INCOMPATIBLE_DEVICE_ERROR   22 // [传输] 试图与不兼容的设备通信
#define COMP_MISSED_SERVICE_ERROR        23 // [传输] 等时传输错过了时间微帧
#define COMP_STOPPED                     26 // [传输] 正常回执：传输流被主板强行刹车
#define COMP_STOPPED_LENGTH_INVALID      27 // [传输] 正常回执：刹车时残余长度无法计算
#define COMP_STOPPED_SHORT_PACKET        28 // [传输] 正常回执：刹车时刚好遇到短包
#define COMP_MAX_EXIT_LATENCY_TOO_LARGE  29 // [传输] 链路从休眠唤醒失败
#define COMP_ISOCH_BUFFER_OVERRUN        31 // [传输] 等时接收缓冲区上溢
#define COMP_INVALID_STREAM_ID_ERROR     34 // [传输] UAS 协议中发了非法的 Stream ID
#define COMP_SPLIT_TRANSACTION_ERROR     36 // [传输] USB 2.0 Hub 拆分事务失败

// ============================================================================
// ⚙️ DW3 (Control) 通用标志位宏
// ============================================================================
// 基础控制位 (按位或 | )
#define TRB_CYCLE                    (1 << 0)  // [0] Cycle Bit (所有 TRB 通用)
#define TRB_TOGGLE_CYCLE             (1 << 1)  // [1] Toggle Cycle (Link TRB 专用)
#define TRB_ENT                      (1 << 1)  // [1] Evaluate Next TRB
#define TRB_ISP                      (1 << 2)  // [2] Interrupt on Short Packet
#define TRB_NS                       (1 << 3)  // [3] No Snoop (PCIe 缓存一致性，通常设为 0)
#define TRB_CHAIN                    (1 << 4)  // [4] Chain Bit (串联多个 TRB)
#define TRB_IOC                      (1 << 5)  // [5] Interrupt On Completion (完成后发中断)
#define TRB_IDT                      (1 << 6)  // [6] Immediate Data (Setup阶段将命令放进指针区)
#define TRB_BEI                      (1 << 9)  // [9] Block Event Interrupt (Normal TRB专用：阻止事件中断，用于频繁传输的降本增效)

// ---------------------------------------------------------
// ★ DW3 装配宏 (Write/Set) - 用于填充发出 TRB
// ---------------------------------------------------------
#define TRB_SET_TYPE(t)              (((t) & 0x3F) << 10)  // [15:10] 设置 TRB 类型
#define TRB_SET_DIR_IN               (1 << 16)             // [16] 传输方向：IN (设备到主机)
#define TRB_SET_DIR_OUT              (0 << 16)             // [16] 传输方向：OUT (主机到设备)

// Setup TRB 专用的 TRT (Transfer Type) [17:16]
#define TRB_SET_TRT_NO_DATA          (0 << 16) // 0 = 无数据阶段 (No Data Stage)场景：命令发出去就完事了，不需要额外的数据负载。
#define TRB_SET_TRT_OUT_DATA         (2 << 16) // 2 = OUT 数据阶段 (OUT Data Stage)场景：主机不仅发命令，还要把一坨内存数据强塞给设备。
#define TRB_SET_TRT_IN_DATA          (3 << 16) // 3 = IN 数据阶段 (IN Data Stage)场景：主机发完命令，张开嘴等设备把数据喂回来。

// 各种 ID 的装配 [24:31] / [16:20]
#define TRB_SET_EP_ID(ep_id)         (((ep_id) & 0x1F) << 16)   // [20:16] Endpoint ID (DCI)
#define TRB_SET_SLOT_ID(slot_id)     (((slot_id) & 0xFF) << 24) // [31:24] Slot ID

// 特殊命令控制位
#define TRB_SET_BSR                  (1 << 9)  // Address Device 的 BSR 位 (Block Set Address Request)
#define TRB_SET_DC                   (1 << 9)  // Configure EP 的 DC 位 (Deconfigure)
#define TRB_SET_TSP                  (1 << 9)  // Reset EP / Set TR Dequeue 专用的 TSP 位 (Target State Preserved) ★ 补充
#define TRB_SET_SUSPEND              (1 << 23) // Stop EP 的 Suspend 位

// ---------------------------------------------------------
// ★ DW3 提取宏 (Read/Get) - 用于解析事件 TRB
// ---------------------------------------------------------
#define TRB_GET_CYCLE(dw3)           ((dw3) & 0x1)            // [0] 提取事件 TRB 的 Cycle 位 (用于判断事件环是否翻转) ★ 极其重要补充
#define TRB_GET_EVENT_DATA(dw3)      (((dw3) >> 2) & 0x1)     // [2] 提取传输事件的 ED 位 (判断是不是 Event Data TRB 产生的) ★ 补充
#define TRB_GET_TYPE(dw3)            (((dw3) >> 10) & 0x3F)   // [15:10] 提取 TRB 类型
#define TRB_GET_EP_ID(dw3)           (((dw3) >> 16) & 0x1F)   // [20:16] 提取发生事件的端点 ID
#define TRB_GET_VF_ID(dw3)           (((dw3) >> 16) & 0xFF)   // [23:16] 提取虚拟功能 ID (SR-IOV / 虚拟机直通场景) ★ 补充
#define TRB_GET_SLOT_ID(dw3)         (((dw3) >> 24) & 0xFF)   // [31:24] 提取设备槽位号

// ============================================================================
// 📊 DW2 (Status) 通用标志位宏
// ============================================================================

// ---------------------------------------------------------
// ★ DW2 装配宏 (Write/Set) - 用于填充发出 TRB
// ---------------------------------------------------------
#define TRB_SET_TR_LEN(len)          ((len) & 0x1FFFF)       // [16:0] 传输长度 (最大 128KB-1)
#define TRB_SET_STREAM_ID(sid)       ((sid) & 0xFFFF)        // [15:0] Stream ID (Set TR Dequeue Pointer 或 UAS 大容量存储流传输时必须配置) ★ UAS协议必备补充
#define TRB_SET_TD_SIZE(size)        (((size) & 0x1F) << 17) // [21:17] 剩余包数估计
#define TRB_SET_INTR_TARGET(irq)     (((irq) & 0x3FF) << 22) // [31:22] 目标中断器号 (Interrupter Target)

// ---------------------------------------------------------
// ★ DW2 提取宏 (Read/Get) - 用于解析事件 TRB
// ---------------------------------------------------------
#define TRB_GET_TR_LEN(dw2)          ((dw2) & 0xFFFFFF)      // [23:0] 传输事件的残余长度 (Short Packet 时必看)
#define TRB_GET_CMD_COMP_PARAM(dw2)  ((dw2) & 0xFFFFFF)      // [23:0] 命令完成事件的附加参数 (Command Completion Parameter) ★ 补充
#define TRB_GET_COMP_CODE(dw2)       (((dw2) >> 24) & 0xFF)  // [31:24] 事件的完成码 (成功/失败原因)

// ============================================================================
// 📦 DW0 & DW1 (Parameter) 特殊解析与装配宏
// 绝大多数情况下 DW0 & DW1 拼成一个 64 位物理地址，但在少数事件/命令中另有他用
// ============================================================================

// ---------------------------------------------------------
// ★ DW0 / DW1 装配宏
// ---------------------------------------------------------
// 解除 STALL 时：Set TR Dequeue Pointer 命令的指针最低位 (Bit 0) 必须携带出队周期状态 (DCS)
#define TRB_SET_DEQ_CYCLE_STATE(dcs) ((dcs) & 0x1)           // [0] DCS (Dequeue Cycle State) ★ 致命补充：解 STALL 必备！

// ---------------------------------------------------------
// ★ DW0 提取宏
// ---------------------------------------------------------
// 端口状态改变事件 (Port Status Change Event) 时，DW0 [31:24] 存放的是物理端口号！
#define TRB_GET_PORT_ID(dw0)         (((dw0) >> 24) & 0xFF)  // [31:24] 提取热插拔事件的 Root Hub 端口号 ★ 核心补充：Hub 枚举入口！



//响铃
static inline void xhci_ring_doorbell(xhci_hcd_t *xhcd, uint8 db_number, uint32 value) {
    xhcd->db_reg[db_number] = value;
}

// 计算步进后的索引，自动跨越 Link TRB
static inline uint32 xhci_submit_ring_next_idx(uint32 cur_idx,uint32 size) {
    // 如果走到倒数第一个位置 (Link TRB)，直接绕回 0
    return (++cur_idx == size - 1) ? 0 : cur_idx;
}

uint64 xhci_submit_ring_enq(xhci_submit_ring_t *ring, xhci_trb_t *trb_push);
int32 xhci_event_ring_deq(xhci_event_ring_t *ring, xhci_trb_t *out_evt);

int32 xhci_alloc_submit_ring(xhci_submit_ring_t *ring,uint32 size);  //分配发送环
int32 xhci_free_submit_ring(xhci_submit_ring_t *ring); //释放发送环
int32 xhci_alloc_event_ring(xhci_event_ring_t *ring,uint32 ring_size); //分配事件环
int32 xhci_free_event_ring(xhci_event_ring_t *ring); //释放事件环
int32 xhci_submit_cmd(xhci_hcd_t *xhcd,xhci_trb_t *cmd_trb,xhci_command_t *out_command);

