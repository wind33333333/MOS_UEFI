#pragma once
#include "moslib.h"

#pragma pack(push,1)

#define TRB_COUNT 256        //trb个数

// ============================================================================
// xHCI 规范 6.4.5: TRB 完成码 (按【事件归属类型】严格物理隔离版)
// 设计目的：完美适配 xhci_execute_command_sync 与 xhci_execute_transfer_sync
// ============================================================================
typedef enum : int8 {
    // ==========================================================
    // 【第 1 阵营：通用与系统级事件】 (Shared / System Level)
    // 归属：命令和传输都可能遇到，或者属于主板硬件级崩溃。
    // 处理建议：在 cmd 和 transfer 的函数中都需要做基础的拦截。
    // ==========================================================
    XHCI_COMP_TIMEOUT                    = -1, // [系统软件] 超时/未获取到事件
    XHCI_COMP_INVALID                    = 0,  // [通用] 非法状态
    XHCI_COMP_SUCCESS                    = 1,  // [通用] 完美成功
    XHCI_COMP_TRB_ERROR                  = 5,  // [通用] TRB 格式非法 (填错字段、Chain不对等)
    XHCI_COMP_RESOURCE_ERROR             = 7,  // [通用] 主板 xHC 控制器内部资源/内存耗尽
    XHCI_COMP_VF_EVENT_RING_FULL_ERROR   = 16, // [通用] 虚拟功能事件环满爆 (SR-IOV)
    XHCI_COMP_EVENT_RING_FULL_ERROR      = 21, // [通用] 真实事件环满爆 (内核中断处理太慢)
    XHCI_COMP_EVENT_LOST_ERROR           = 32, // [通用] 事件丢失 (事件环溢出导致主板丢弃回执)
    XHCI_COMP_UNDEFINED_ERROR            = 33, // [通用] 未定义的致命硬件崩溃

    // ==========================================================
    // 【第 2 阵营：命令事件专属】 (Command Event Only)
    // 归属：仅由 Command Ring 触发。
    // 处理建议：全部塞进 xhci_execute_command_sync 的 switch-case 中。
    // ==========================================================
    XHCI_COMP_BANDWIDTH_ERROR            = 8,  // [命令] 配置端点时，USB 总线带宽不足
    XHCI_COMP_NO_SLOTS_AVAILABLE_ERROR   = 9,  // [命令] Enable Slot 时，主板分配不出新槽位
    XHCI_COMP_INVALID_STREAM_TYPE_ERROR  = 10, // [命令] 配置流上下文时，Stream Type 非法
    XHCI_COMP_SLOT_NOT_ENABLED_ERROR     = 11, // [命令] 对未经 Enable 的槽位下发了命令
    XHCI_COMP_ENDPOINT_NOT_ENABLED_ERROR = 12, // [命令] 对未经初始化的端点下发了命令
    XHCI_COMP_PARAMETER_ERROR            = 17, // [命令] Context 上下文结构体参数填错或未对齐
    XHCI_COMP_CONTEXT_STATE_ERROR        = 19, // [命令] 状态机时序错误 (如：乱发 Reset Endpoint)
    XHCI_COMP_COMMAND_RING_STOPPED       = 24, // [命令] 正常回执：命令环已成功停止
    XHCI_COMP_COMMAND_ABORTED            = 25, // [命令] 正常回执：命令已被成功中止
    XHCI_COMP_SECONDARY_BANDWIDTH_ERROR  = 35, // [命令] 配置端点辅助带宽时出错

    // ==========================================================
    // 【第 3 阵营：传输事件专属】 (Transfer Event Only)
    // 归属：仅由 Transfer Ring (EP0-31) 通信触发。
    // 处理建议：全部塞进 xhci_execute_transfer_sync 的 switch-case 中。
    // ==========================================================
    XHCI_COMP_DATA_BUFFER_ERROR          = 2,  // [传输] 数据缓冲区错误 (主机内存 DMA 寻址失败)
    XHCI_COMP_BABBLE_ERROR               = 3,  // [传输] 喋喋不休 (U盘发来的数据超出预期，端点 Halted)
    XHCI_COMP_USB_TRANSACTION_ERROR      = 4,  // [传输] 物理链路车祸 (超时/CRC失败，端点 Halted)
    XHCI_COMP_STALL_ERROR                = 6,  // [传输] 逻辑卡死 (U盘主动拒绝服务，端点 Halted)
    XHCI_COMP_SHORT_PACKET               = 13, // [传输] 短包响应 (数据少于预期，BOT 中属正常)
    XHCI_COMP_RING_UNDERRUN              = 14, // [传输] 等时环下溢出 (发数据太慢)
    XHCI_COMP_RING_OVERRUN               = 15, // [传输] 等时环上溢出 (收数据太慢)
    XHCI_COMP_BANDWIDTH_OVERRUN_ERROR    = 18, // [传输] 带宽超载 (设备发送过量数据)
    XHCI_COMP_NO_PING_RESPONSE_ERROR     = 20, // [传输] USB 3.0 链路无 Ping 响应
    XHCI_COMP_INCOMPATIBLE_DEVICE_ERROR  = 22, // [传输] 试图与不兼容的设备通信
    XHCI_COMP_MISSED_SERVICE_ERROR       = 23, // [传输] 等时传输错过了时间微帧
    XHCI_COMP_STOPPED                    = 26, // [传输] 正常回执：传输流被主板强行刹车
    XHCI_COMP_STOPPED_LENGTH_INVALID     = 27, // [传输] 正常回执：刹车时残余长度无法计算
    XHCI_COMP_STOPPED_SHORT_PACKET       = 28, // [传输] 正常回执：刹车时刚好遇到短包
    XHCI_COMP_MAX_EXIT_LATENCY_TOO_LARGE = 29, // [传输] 链路从休眠唤醒失败
    XHCI_COMP_ISOCH_BUFFER_OVERRUN       = 31, // [传输] 等时接收缓冲区上溢
    XHCI_COMP_INVALID_STREAM_ID_ERROR    = 34, // [传输] UAS 协议中发了非法的 Stream ID
    XHCI_COMP_SPLIT_TRANSACTION_ERROR    = 36  // [传输] USB 2.0 Hub 拆分事务失败
} xhci_trb_comp_code_e;


// ============================================================================
// xHCI TRB 类型枚举 (对应所有 TRB Dword 3 的 Bits 10-15: type)
// 规范出处: xHCI Spec 1.2 - Table 132 (TRB Type Definitions)
// ============================================================================
typedef enum ：uint32 {
    XHCI_TRB_TYPE_RESERVED = 0,          // 0: 保留 (非法 TRB)

    XHCI_TRB_TYPE_LINK         = 6,      // 链接 TRB (传输环和命令环通用★ 物理内存环填满时，用它跳回环的开头)

    // ========================================================================
    // 【传输环 TRB】(Transfer Ring) - 塞入端点环，用于真实的数据搬运
    // ========================================================================
    XHCI_TRB_TYPE_NORMAL       = 1,      // 普通传输 (BOT 数据进出的绝对主力)
    XHCI_TRB_TYPE_SETUP_STAGE  = 2,      // 控制传输：Setup 阶段 (★ 你刚才写的 8 字节包就是它)
    XHCI_TRB_TYPE_DATA_STAGE   = 3,      // 控制传输：数据阶段
    XHCI_TRB_TYPE_STATUS_STAGE = 4,      // 控制传输：状态确认阶段
    XHCI_TRB_TYPE_ISOCH        = 5,      // 等时传输 (用于 USB 麦克风、摄像头等实时设备)
    XHCI_TRB_TYPE_EVENT_DATA   = 7,      // 事件数据 (给虚拟化或者特殊同步用的)
    XHCI_TRB_TYPE_NO_OP        = 8,      // 空操作 TRB (占坑用)

    // ========================================================================
    // 【命令环 TRB】(Command Ring) - 塞入全局命令环，用于控制 xHCI 主板硬件
    // ========================================================================
    XHCI_TRB_TYPE_ENABLE_SLOT       = 9,  // 启用设备槽 (设备刚插入时的第一步！)
    XHCI_TRB_TYPE_DISABLE_SLOT      = 10, // 禁用设备槽 (设备拔出时清理内存)
    XHCI_TRB_TYPE_ADDRESS_DEVICE    = 11, // 分配设备地址 (告诉 xHCI 给设备发 Set Address 命令)
    XHCI_TRB_TYPE_CONFIGURE_EP      = 12, // 配置端点 (激活 Bulk IN/OUT 管道全靠它)
    XHCI_TRB_TYPE_EVALUATE_CTX      = 13, // 评估上下文 (比如告诉硬件：这个 U 盘最大包长是 512)
    XHCI_TRB_TYPE_RESET_EP          = 14, // 复位端点 (★ 清除 STALL 卡死时必发的神兵利器)
    XHCI_TRB_TYPE_STOP_EP           = 15, // 停止端点 (强行踩刹车，中止正在进行的传输)
    XHCI_TRB_TYPE_SET_TR_DEQUEUE    = 16, // 设置出队指针 (★ STALL 恢复后，强行拨动硬件的出队指针)
    XHCI_TRB_TYPE_RESET_DEVICE      = 17, // 复位设备
    XHCI_TRB_TYPE_FORCE_EVENT       = 18, // 强制事件 (SR-IOV 虚拟化常用)
    XHCI_TRB_TYPE_NEGOTIATE_BW      = 19, // 协商带宽
    XHCI_TRB_TYPE_SET_LATENCY_TOL   = 20, // 设置延迟容忍度
    XHCI_TRB_TYPE_GET_PORT_BW       = 21, // 获取端口带宽
    XHCI_TRB_TYPE_FORCE_HEADER      = 22, // 强制包头
    XHCI_TRB_TYPE_NO_OP_CMD         = 23, // 空操作命令 (测试命令环通不通时用)

    // ========================================================================
    // 【事件环 TRB】(Event Ring) - xHCI 硬件主动写入内存，触发 CPU 中断的回执
    // ========================================================================
    XHCI_TRB_TYPE_TRANSFER_EVENT    = 32, // 传输完成事件 (★ 汇报 Normal/Setup 等传输的对错，如短包/STALL)
    XHCI_TRB_TYPE_CMD_COMPLETION    = 33, // 命令完成事件 (★ 汇报 Reset EP 等主板命令是否成功)
    XHCI_TRB_TYPE_PORT_STATUS_CHG   = 34, // 端口状态改变事件 (★ 最关键的中断：U盘插入或拔出的瞬间产生！)
    XHCI_TRB_TYPE_BANDWIDTH_REQ     = 35, // 带宽请求事件
    XHCI_TRB_TYPE_DOORBELL          = 36, // 门铃事件
    XHCI_TRB_TYPE_HOST_CTRL         = 37, // 主机控制器事件
    XHCI_TRB_TYPE_DEVICE_NOTIFY     = 38, // 设备通知事件
    XHCI_TRB_TYPE_MFINDEX_WRAP      = 39  // 微帧索引翻转事件

    // 48 到 63 是厂商自定义 (Vendor Defined)，通常不用管
} trb_type_e;

// ============================================================================
// xHCI 规范 6.4.4.1: 链接 TRB (Link TRB, Type = 6)
// 作用：放置在 Ring 的末尾，将硬件执行指针引回 Ring 的头部，并翻转硬件的周期期待值。
// ============================================================================
typedef struct trb_link_t{
    // Dword 0 & 1: 下一个 Ring Segment (环段) 的首地址。
    // 在我们这种单段环 (Single-Segment Ring) 的设计中，这里永远填 Ring 第 0 个 TRB 的物理地址！
    // 注意：地址必须是 16 字节对齐的 (也就是低 4 位必须为 0)。
    uint64 ring_segment_ptr;

    // Dword 2
    uint32 rsvd1:22;          // Bits [21:0]: 保留，填 0
    uint32 intr_target:10;    // Bits [31:22]: 目标中断器号 (通常填 0 即可)

    // Dword 3 (x86 小端序，从低位开始)
    uint32 cycle:1;           // Bit [0]: 硬件翻转位 (C)
    uint32 toggle_cycle:1;    // Bit [1]: ★ 极度关键！切换周期位 (TC)。
    uint32 rsvd2:2;           // Bits [3:2]: 保留，填 0
    uint32 chain:1;           // Bit [4]: 链位 (CH)。在普通的 Link TRB 中填 0
    uint32 ioc:1;             // Bit [5]: 完成时中断位 (IOC)。通常填 0，不让它产生多余中断
    uint32 rsvd3:4;           // Bits [9:6]: 保留，填 0
    uint32 trb_type:6;        // Bits [15:10]: 必须是 6 (XHCI_TRB_TYPE_LINK_TRB)
    uint32 rsvd4:16;          // Bits [31:16]: 保留，填 0
} trb_link_t;

//============================================传输trb=================================

// ============================================================================
// Setup Stage TRB (Type 2) - 控制传输的第一阶段
// ============================================================================
typedef enum : uint8 {
    USB_RECIP_DEVICE    = 0,  //设备
    USB_RECIP_INTERFACE = 1,  //接口
    USB_RECIP_ENDPOINT  = 2,  //端点
    USB_RECIP_OTHER     = 3   //其他
} usb_recipient_e;

// USB 请求类型枚举 (对应 bmRequestType 的 Bits 5-6:type)
typedef enum : uint8 {
    USB_REQ_TYPE_STANDARD = 0, // 0 = 标准请求 (Standard Request)场景：设备枚举、获取设备描述符 (Get Descriptor)、分配地址 (Set Address)、清除端点卡死 (Clear Feature) 等。
    USB_REQ_TYPE_CLASS    = 1, // 1 = 类特定请求 (Class Request)比如我们 U 盘 (Mass Storage Class) 专属的 BOT Mass Storage Reset (0xFF) 和 Get Max LUN (0xFE)。
    USB_REQ_TYPE_VENDOR   = 2,  // 2 = 厂商自定义请求 (Vendor Request)
    USB_REQ_TYPE_RESERVED = 3   // 3 = 保留 (Reserved)
} usb_req_type_e;

// USB 数据传输方向枚举 (对应 bmRequestType 的 Bit 7: dtd)
typedef enum :uint8 {
    USB_DIR_OUT = 0, // 0 = OUT (主机到设备 / Host to Device)作用：控制传输的数据阶段，数据是由主机发送给设备的。注意：如果这个控制请求根本【没有】数据阶段（比如只发一个没有参数的命令），按照规范，方向也必须填 OUT(0)。
    USB_DIR_IN  = 1  // 1 = IN (设备到主机 / Device to Host)作用：控制传输的数据阶段，主机期待设备把数据传回来，场景：比如你想读取 U 盘的最大 LUN (Get Max LUN)，或者读取设备的描述符 (Get Descriptor) 时。
} usb_data_dir_e;

// ============================================================================
// USB 具体请求指令枚举 (对应 bRequest)
// 注意：0x00~0x0F 通常为标准请求，0x20以上及 0xFE/0xFF 常用于类特定请求
// ============================================================================
typedef enum : uint8 {
    // ------------------------------------------------------------------------
    // 【USB 标准请求】(当 RequestType == USB_REQ_TYPE_STANDARD 时有效)
    // ------------------------------------------------------------------------
    USB_REQ_GET_STATUS        = 0x00, // 获取状态 (比如看看设备是自供电还是总线供电，端点有没有Halt)
    USB_REQ_CLEAR_FEATURE     = 0x01, // 清除特性 (★我们在撬开卡死端点时用的就是这个！)
    // 0x02 是保留的
    USB_REQ_SET_FEATURE       = 0x03, // 设置特性 (比如让设备进入测试模式，或挂起特定端点)
    // 0x04 是保留的
    USB_REQ_SET_ADDRESS       = 0x05, // 设置地址 (设备刚插入时地址为0，xHCI用这个给它分配一个 1~127 的新地址)
    USB_REQ_GET_DESCRIPTOR    = 0x06, // 获取描述符 (★枚举设备、获取厂商PID/VID、端点配置全靠它)
    USB_REQ_SET_DESCRIPTOR    = 0x07, // 设置描述符 (极少使用，通常用于固件更新)
    USB_REQ_GET_CONFIGURATION = 0x08, // 获取当前配置 (看看设备现在处于哪种工作模式)
    USB_REQ_SET_CONFIGURATION = 0x09, // 设置当前配置 (告诉设备：“你现在作为U盘模式启动吧”)
    USB_REQ_GET_INTERFACE     = 0x0A, // 获取当前接口备用设置
    USB_REQ_SET_INTERFACE     = 0x0B, // 设置接口备用设置 (常见于带麦克风和音响的复合 USB 耳机切换采样率)
    USB_REQ_SYNCH_FRAME       = 0x0C, // 同步帧 (仅用于等时传输，如音频/视频设备同步时间戳)
    // ------------------------------------------------------------------------
    // 【Mass Storage (BOT) 类专属请求】(当 qtype == USB_REQ_TYPE_CLASS 时有效)
    // ------------------------------------------------------------------------
    BOT_REQ_GET_MAX_LUN       = 0xFE, // 获取最大逻辑单元号 (问读卡器：“你身上插了几个SD卡？”)
    BOT_REQ_MASS_STORAGE_RESET= 0xFF  // 批量仅复位 (★核弹按钮：让U盘的协议状态机瞬间清零重启)

} usb_request_e;

// ============================================================================
// USB 标准特性选择器枚举 (对应 Clear Feature / Set Feature 的 wValue 字段)
// ============================================================================
typedef enum : uint16 {
    // ------------------------------------------------------------------------
    // 【发给 Endpoint (端点) 的特性】(当 recipient == ENDPOINT 时)
    // ------------------------------------------------------------------------
    // 作用：清除它，就能解开端点的 STALL 状态，让数据通道重新开放。
    // ★ 在 BOT 错误恢复中，你的 value 必须填这个！
    USB_FEATURE_ENDPOINT_HALT        = 0x00,

    // ------------------------------------------------------------------------
    // 【发给 Device (设备) 的特性】(当 recipient == DEVICE 时)
    // ------------------------------------------------------------------------
    USB_FEATURE_DEVICE_REMOTE_WAKEUP = 0x01, // 远程唤醒 (比如敲击休眠键盘唤醒电脑)
    USB_FEATURE_TEST_MODE            = 0x02, // 测试模式 (主要用于主板硬件出厂测试)

    // USB 3.0 新增设备特性
    USB_FEATURE_U1_ENABLE            = 0x30, // 允许进入 U1 节能状态
    USB_FEATURE_U2_ENABLE            = 0x31  // 允许进入 U2 节能状态
} usb_feature_selector_e;


/*
 * usb 8字节请求包
 */
typedef struct usb_req_pkg_t {
    //bmRequestType
    usb_recipient_e recipient : 5;
    usb_req_type_e  req_type  : 2;
    usb_data_dir_e  dtd       : 1;

    //bRequest
    usb_request_e   request; //请求代码

    //wValue
    uint16          value; //请求值，具体含义由 b_request 定义 例如：GET_DESCRIPTOR 中，w_value 高字节为描述符类型，低字节为索引

    //wIndex
    uint16          index; //索引或偏移，具体含义由 b_request 定义 例如：接口号、端点号或字符串描述符索引

    //wLength
    uint16          length; //数据阶段的传输长度（字节）主机到设备：发送的数据长度 设备到主机：请求的数据长度
}usb_req_pkg_t;

// ============================================================================
// xHCI 控制传输类型枚举 (对应 Setup TRB Dword 3 的 Bits 16-17: trt)
// 作用：告诉硬件这次控制传输是否包含数据阶段，以及数据的方向。
// ============================================================================
typedef enum : uint32 {
    TRB_TRT_NO_DATA   = 0, // 0 = 无数据阶段 (No Data Stage)场景：命令发出去就完事了，不需要额外的数据负载。
    TRB_TRT_RESERVED  = 1, // 1 = 保留 (Reserved)    // 绝对不要使用，硬件会报错。
    TRB_TRT_OUT_DATA  = 2, // 2 = OUT 数据阶段 (OUT Data Stage)场景：主机不仅发命令，还要把一坨内存数据强塞给设备。
    TRB_TRT_IN_DATA   = 3 // 3 = IN 数据阶段 (IN Data Stage)场景：主机发完命令，张开嘴等设备把数据喂回来。
} trb_trt_e;

typedef enum : uint32 {
    TRB_DIR_OUT = 0,
    TRB_DIR_IN  = 1
}trb_dir_e;

typedef enum : uint32 {
    TRB_IOC_DISABLE = 0,
    TRB_IOC_ENABLE  = 1
}trb_ioc_e;

typedef enum : uint32 {
    TRB_CHAIN_DISABLE = 0,
    TRB_CHAIN_ENABLE  = 1
}trb_chain_e;

typedef enum : uint32 {
    TRB_IDT_DISABLE = 0,
    TRB_IDT_ENABLE  = 1
}trb_idt_e;

typedef struct trb_setup_stage_t{
    // Dword 0-1:
    usb_req_pkg_t   usb_req_pkg;

    // Dword 2: 长度与中断目标
    uint32          trb_transfer_len : 17; // 规范强制要求：Setup TRB 的长度必须固定填 8！
    uint32          rsvd1            : 5;
    uint32          int_target       : 10;

    // Dword 3: 控制位
    uint32          cycle : 1;
    uint32          rsvd2 : 3;
    trb_chain_e     chain : 1;  // ★ 必须填0
    trb_ioc_e       ioc   : 1;  // 通常填 0，因为我们只关心最后一个 Status TRB 的完成中断
    trb_idt_e       idt   : 1;  // ★ 必须填 1！(Immediate Data: 告诉硬件前 8 字节是数据本身，不是指针)
    uint32          rsvd3 : 3;
    trb_type_e      type  : 6;  // Bits 10-15: TRB 类型 (固定为 2)
    trb_trt_e       trt   : 2;  // Bits 16-17: 传输类型 (见上方宏定义，极其重要)
    uint32          rsvd4 : 14;
} trb_setup_stage_t;

// ============================================================================
// Data Stage TRB (Type 3) - 控制传输的第二阶段 (可选)
// ============================================================================
typedef struct trb_data_stage_t {
    // Dword 0-1: 数据缓冲区的 64 位物理地址 (PA)
    uint64          data_buf_ptr;

    // Dword 2: 长度控制
    uint32          transfer_len : 17; // 你要传输的实际数据长度
    uint32          td_size      : 5;  // 剩余的包数 (简单起见常填 0)
    uint32          int_target   : 10;

    // Dword 3: 控制位
    uint32          cycle : 1;
    uint32          ent   : 1;  //评估下一个trb
    uint32          isp   : 1;  // 短包中断
    uint32          ns    : 1;  // No Snoop
    trb_chain_e     chain : 1;  //
    trb_ioc_e       ioc   : 1;  // 通常填 0
    trb_idt_e       idt   : 1;  // 必须填 0 (说明前面是个指针)
    uint32          rsvd1 : 3;
    trb_type_e      type  : 6;  // Bits 10-15: TRB 类型 (固定为 3)
    trb_dir_e       dir   : 1;  // ★ Bits 16: 数据方向 (0 = OUT 主机发给设备, 1 = IN 设备发给主机)
    uint32          rsvd2 : 15;
}trb_data_stage_t;

// ============================================================================
// Status Stage TRB (Type 4) - 控制传输的最终确认阶段
// ============================================================================
typedef struct trb_status_stage_t {
    // Dword 0-1: 规范强制要求保留全 0！(状态阶段没有真实的数据负载)
    uint64          rsvd0;

    // Dword 2
    uint32          rsvd1      : 22; // 必须全 0
    uint32          int_target : 10;

    // Dword 3: 控制位
    uint32          cycle : 1;
    uint32          ent   : 1;
    uint32          rsvd2 : 2;
    trb_chain_e     chain : 1;  // ★ 必须填 0！因为这是最后一节车厢了！
    trb_ioc_e       ioc   : 1;  // ★ 必须填 1！(Interrupt On Completion：硬件跑完这个 TRB，才向内核汇报)
    uint32          rsvd3 : 4;
    trb_type_e      type  : 6;  // Bits 10-15: TRB 类型 (固定为 4)
    trb_dir_e       dir   : 1;  // ★ Bits 16: 握手方向 (如果是 No Data 或 OUT，这里填 1； 如果 Data是 IN，这里填 0)
    uint32          rsvd4 : 15;
}trb_status_stage_t;

//===================================================================


//==================================命令trb==================================================

// ============================================================================
// xHCI 规范 6.4.3.8: 停止端点命令 TRB (Stop Endpoint Command, Type = 15)
// 作用：强制主板 xHC 芯片停止处理指定端点的传输环，并将端点状态机切入 "Stopped"。
// 核心用途：用于传输超时后的主动中止 (Abort Transfer) 和环指针的重新对齐。
// ============================================================================
typedef struct trb_stop_ep_cmd_t{
    uint32 rsvd1[3];          // Dword 0, 1, 2: 保留，必须全填 0

    // Dword 3
    uint32 cycle:1;           // Bit [0]: 硬件翻转位 (C)
    uint32 rsvd2:9;           // Bits [9:1]: 保留，填 0
    trb_type_e trb_type:6;        // Bits [15:10]: 必须是 15 (XHCI_TRB_TYPE_STOP_EP)
    // ★ 狙击目标：精准定位到具体的设备和具体的管道
    uint32 ep_id:5;           // Bits [20:16]: 目标 Endpoint ID (1~31)
    uint32 rsvd3:2;           // Bits [22:21]: 保留，填 0
    // ★ 挂起位：0 = 彻底停止并丢弃内部缓存; 1 = 只是挂起(Suspend)，以后还能原样恢复。
    // 在超时抢救场景中，我们永远填 0（彻底停止）！
    uint32 suspend:1;         // Bit [23]: SP (Suspend) 位
    uint32 slot_id:8;         // Bits [31:24]: 目标 Slot ID
}trb_stop_ep_cmd_t;

// ============================================================================
// xHCI 规范 6.4.3.4: 分配设备地址命令 TRB (Address Device Command, Type = 11)
// 作用：向新插入的 USB 设备分配总线地址，并初始化 Slot Context 和 EP0 Context。
// ============================================================================
typedef struct trb_address_device_cmd_t{
    // Dword 0 & 1: ★ 极度关键！指向 Input Context (输入上下文) 的物理地址。
    // 物理地址的最低 4 位必须为 0 (即 16 字节对齐)，
    // 但在实际的 x64 系统中，强烈建议直接进行 64 字节 (物理页的 Cache Line) 对齐！
    uint64 input_context_ptr;

    // Dword 2
    uint32 rsvd1;             // 保留，必须填 0

    // Dword 3
    uint32 cycle:1;           // Bit [0]: 硬件翻转位 (C)
    uint32 rsvd2:8;           // Bits [8:1]: 保留，填 0

    // ★ 架构师秘钥：BSR (Block Set Address Request)
    // 填 0：主板在底层自动帮你给 U 盘发 SET_ADDRESS 控制传输请求（最常用！）。
    // 填 1：主板只在内部配置上下文，但不向 U 盘发 SET_ADDRESS（用于某些高级 USB 3.0 设备的状态机跳跃）。
    uint32 bsr:1;             // Bit [9]: 阻止设置地址请求位

    trb_type_e  trb_type:6;        // Bits [15:10]: 必须是 11 (XHCI_TRB_TYPE_ADDRESS_DEVICE)
    uint32 rsvd3:8;           // Bits [23:16]: 保留，填 0

    // ★ 身份绑定：填入你刚才通过 Enable Slot 抢到的那个号码！
    uint32 slot_id:8;         // Bits [31:24]: 目标 Slot ID
}trb_address_device_cmd_t;

//复位端点trb
typedef struct trb_rest_ep_cmd_t{
    uint32          rsvd0[3];
    uint32          cycle:1;
    uint32          rsvd1:8;
    uint32          tsp:1;      // 常规填 0
    trb_type_e      type:6;
    uint32          ep_dci:5; // 端点索引 (DCI)，如 IN 是 3，OUT 是 4
    uint32          rsvd2:3;
    uint32          slot_id:8; // 设备槽位号
}trb_rest_ep_cmd_t;

// ============================================================================
// xHCI 规范：设置出队指针命令 (Set TR Dequeue Pointer Command TRB)类型码：16 (XHCI_TRB_TYPE_SET_TR_DEQ)
// ============================================================================
typedef struct trb_set_tr_deq_ptr_cmd_t{
    // Dword 0-1 (64位物理地址)
    uint64 tr_dequeue_ptr; // ★ 极其致命：最低位 Bit 0 是 DCS (Dequeue Cycle State)，必须包含新指针所在位置的 Cycle 位！

    // Dword 2
    uint16 rsvd0;
    uint16 stream_id;     // BOT 协议不支持 Stream，必须填 0

    // Dword 3
    uint32          cycle   : 1;
    uint32          rsvd1   : 8;
    uint32          tsp     : 1;
    trb_type_e      type    : 6;      // ★ 必须填 16
    uint32          ep_dci  : 5;      // 端点索引 (DCI)
    uint32          rsvd2   : 3;
    uint32          slot_id : 8;
}trb_set_tr_deq_ptr_cmd_t;

// ============================================================================
// xHCI 规范 6.4.3.9: Enable Slot Command TRB
// 作用：向主板 xHC 芯片申请一个空闲的设备槽位 (Slot)，主板会在完成事件中返回分配的 Slot ID
// ============================================================================
typedef struct trb_enable_slot_cmd_t {
    uint32          rsvd0[3];       // Dword 0, 1, 2: 规范要求全部保留，必须清零

    // Dword 3 (x86 小端序，从低位开始映射)
    uint32          cycle:1;        // Bit 0: 翻转位 (C)，交由底层的 enqueue 函数处理
    uint32          rsvd1:9;        // Bits 1-9: 保留，填 0
    trb_type_e      type:6;         // Bits 10-15: TRB 类型，这里必须是 9 (Enable Slot)
    uint32          slot_type:5;    // Bits 16-20: 槽位类型。对于常规的 USB 设备，直接填 0
    uint32          rsvd2:11;       // Bits 21-31: 保留，填 0
} trb_enable_slot_cmd_t;

// ============================================================================
// xHCI 规范 6.4.3.2: 禁用槽位命令 TRB (Disable Slot Command, Type = 10)
// 作用：释放主板为该 Slot ID 分配的内部资源，使该 Slot ID 可以被再次分配。
// ============================================================================
typedef struct trb_disable_slot_cmd_t{
    uint32 rsvd1[3];          // Dword 0, 1, 2: 保留，必须全填 0

    // Dword 3
    uint32 cycle:1;           // Bit [0]: 硬件翻转位 (C)
    uint32 rsvd2:9;           // Bits [9:1]: 保留，填 0
    trb_type_e trb_type:6;        // Bits [15:10]: 必须是 10 (XHCI_TRB_TYPE_DISABLE_SLOT)
    uint32 rsvd3:8;           // Bits [23:16]: 保留，填 0

    // ★ 绝杀目标：告诉主板你要超度哪个设备
    uint32 slot_id:8;         // Bits [31:24]: 目标 Slot ID
}trb_disable_slot_cmd_t;

// 在你的联合体中补充：
// trb_disable_slot_cmd_t  disable_slot_cmd;

//=================================================================================================


//============================ 事件trb ============================================================

// 1. 命令完成事件 (Command Completion Event, Type = 33)
// 发生场景：你发了 Enable Slot, Address Device 等主板命令后，主板的回执。
typedef struct trb_cmd_comp_event_t{
    uint64                    cmd_trb_ptr;       // Dword 0 & 1: 刚才引发该事件的 Command TRB 物理地址
    uint32                    cmd_comp_param:24; // Dword 2 [23:0]: 命令完成参数 (通常为 0，个别命令有用)
    xhci_trb_comp_code_e      comp_code:8;       // Dword 2 [31:24]: 完成码 (对应 xhci_comp_code_t)

    uint32                    cycle:1;           // Dword 3 [0]: 硬件翻转位
    uint32                    rsvd1:9;           // Dword 3 [9:1]: 保留
    trb_type_e                trb_type:6;        // Dword 3 [15:10]: 必须是 33 (XHCI_TRB_TYPE_CMD_COMP_EVENT)
    uint32                    vf_id:8;           // Dword 3 [23:16]: 虚拟功能 ID (SR-IOV 专用，常规填 0)
    uint32                    slot_id:8;         // Dword 3 [31:24]: ★ 极度重要！这里藏着主板分配的 Slot ID！
}trb_cmd_comp_event_t;


// 2. 传输事件 (Transfer Event, Type = 32)
// 发生场景：U盘数据读写完成、Setup 控制传输完成等，端点产生的中断回执。
typedef struct trb_transfer_event_t{
    uint64                    tr_trb_ptr;        // Dword 0 & 1: 引发中断的那条 Transfer/Setup TRB 物理地址
    uint32                    transfer_len:24;   // Dword 2 [23:0]: ★ 极度重要！残余字节数 (没传完的数据量，短包时必看)
    xhci_trb_comp_code_e      comp_code:8;       // Dword 2 [31:24]: 完成码 (如 SUCCESS, SHORT_PACKET, STALL)

    uint32                    cycle:1;           // Dword 3 [0]: 硬件翻转位
    uint32                    rsvd1:1;           // Dword 3 [1]: 保留
    uint32                    event_data:1;      // Dword 3 [2]: ED 位 (是否为纯事件数据)
    uint32                    rsvd2:7;           // Dword 3 [9:3]: 保留
    trb_type_e                trb_type:6;        // Dword 3 [15:10]: 必须是 32 (XHCI_TRB_TYPE_TRANSFER_EVENT)
    uint32                    ep_id:5;           // Dword 3 [20:16]: 发生事件的端点 DCI (1 是 EP0，等)
    uint32                    rsvd3:3;           // Dword 3 [23:21]: 保留
    uint32                    slot_id:8;         // Dword 3 [31:24]: 发生事件的设备槽位号
} trb_transfer_event_t;

// ============================================================================
// xHCI 规范 6.4.2.3: 端口状态改变事件 TRB (Port Status Change Event, Type = 34)
// 发生场景：物理线缆的插拔、端口复位完成、或者链路电源状态改变时硬件主动上报。
// ============================================================================
typedef struct trb_port_status_change_event_t{
    uint32      rsvd0:24;          // Dword 0 [23:0]: 保留，全 0
    uint32      port_id:8;         // Dword 0 [31:24]: ★ 核心机密！发生状态改变的物理端口号 (比如 1 号口)

    uint32      rsvd1;             // Dword 1: 保留，全 0
    uint32      rsvd2;             // Dword 2: 保留，全 0

    uint32      cycle:1;           // Dword 3 [0]: 硬件翻转位 (Cycle Bit)
    uint32      rsvd3:9;           // Dword 3 [9:1]: 保留
    trb_type_e  trb_type:6;        // Dword 3 [15:10]: 必须是 34 (XHCI_TRB_TYPE_PORT_STATUS_CHANGE_EVENT)
    uint32      rsvd4:16;          // Dword 3 [31:16]: 保留
}trb_port_status_change_event_t;

//================================================================================================

//trb集合
typedef union xhci_trb_t {
    // 【视角 1：内存搬运工视角】(用于底层 enqueue 拷贝和清零)
    uint64 raw[2];

    // 【视角 2：极简 64 位指针视角】(你刚才提议的优质写法)
    // struct {
    //     uint64 ptr;
    //     uint32 status;
    //     uint32 ctrl;
    // }generic;

    //命令或传输环连接trb
    trb_link_t link;

    // 【视角 3：业务定制视角】(包含了所有具体的 TRB 解析格式) ... 以后加什么 TRB，就往这里塞什么 struct ...
    //命令trb xhci命令环专用，用于发送启用插槽等
    trb_stop_ep_cmd_t        stop_ep_cmd;
    trb_address_device_cmd_t address_device_cmd;
    trb_disable_slot_cmd_t   disable_slot_cmd;
    trb_enable_slot_cmd_t    enable_slot_cmd;
    trb_set_tr_deq_ptr_cmd_t set_tr_deq_ptr_cmd;
    trb_rest_ep_cmd_t        rest_ep_cmd;

    //传输trb
    //控制传输端点1专用，用于发送usb命令如获取设备描述符/设备信息等
    trb_setup_stage_t        setup_stage;
    trb_data_stage_t         data_stage;
    trb_status_stage_t       status_stage;
    //bulk端点专用端点2-31专用，用于数据传输如read 10 write10等

    //事件trb
    trb_cmd_comp_event_t           cmd_comp_event;
    trb_transfer_event_t           transfer_event;
    trb_port_status_change_event_t prot_status_change_event;
}xhci_trb_t;

// ===== 1. 能力寄存器 (Capability Registers) =====
typedef struct {
    // 00h: 能力长度和版本 (CAPLENGTH/HCIVERSION)
    uint8 cap_length; // [7:0] 能力寄存器总长度 (字节)
    uint8 reserved0; // 保留
    uint16 hciversion; // 控制器版本 (0x100 = 1.0.0, 0x110 = 1.1.0, 0x120 = 1.2.0)

    // 04h: 硬件参数寄存器 (HCSPARAMS1)
    uint32 hcsparams1; /*[7:0]   MaxSlots: 支持的最大设备槽数（最大256）
                         [18:8]  MaxIntrs: 支持的中断向量数（最大2048）
                         [24:31] MaxPorts: 支持的根端口数（最大256）*/

    // 08h: 硬件参数寄存器 (HCSPARAMS2)
    uint32 hcsparams2; /*[3:0]	    IST 等时调度阈值，单位为微帧（125us）。Host Controller 在这个阈值之后的同一帧内不再调度新的等时传输。常见值：0~8。
                              [7:4]	    ERST Max 硬件支持的事件环段表最大条目数2^n ERST MAX = 8 则条目等于256。
                              [25:21]	Max Scratchpad Buffers (hi  5 bits)	最大暂存器缓冲区数量的高5位（范围 0~1023）。
                              [31:27]	Max Scratchpad Buffers (Lo 5 bits)	最大暂存器缓冲区数量的低5位*/

    // 0Ch: 硬件参数寄存器 (HCSPARAMS3)
    uint32 hcsparams3; /*[7:0]     U1DeviceExitLatency: U1设备退出延迟（以微秒为单位）
                              [15:8]    U2DeviceExitLatency: U2设备退出延迟（以微秒为单位）*/

    // 10h: 硬件参数寄存器 (HCCPARAMS1)
    uint32 hccparams1; /*- AC64 (位 0): 64位寻址能力（1=支持，0=不支持）
                              - BNC (位 1): 带宽协商能力
                              - CSZ (位 2): 上下文大小（0=32字节，1=64字节）
                              - PPC (位 3): 端口电源控制能力
                              - PIND (位 4): 端口指示器能力
                              - LHRC (位 5): 轻量级主机路由能力
                              - LTC (位 6): 延迟容忍能力
                              - NSS (位 7): 无嗅探能力
                              - MaxPSASize (位 12-15): 最大主控制器流数组大小
                              - ECPA(位16-31)：扩展能力链表偏移地址 = 偏移<<2 */
#define HCCP1_CSZ   (1<<2)

    uint32 dboff; // 0x14 门铃寄存器偏移

    uint32 rtsoff; // 0x18 运行时寄存器偏移

    uint32 hccparams2; /* - U3C (位 0): U3转换能力
                               - CMC (位 1): 配置最大能力
                               - FSC (位 2): 强制保存上下文能力
                               - CTC (位 3): 符合性测试能力
                               - LEC (位 4): 大型ESIT有效负载能力
                               - CIC (位 5): 配置信息能力*/
} xhci_cap_regs_t;

// ===== 2. 操作寄存器 (Operational Registers) =====
typedef struct {
    // 00h: 命令寄存器 (USBCMD)
    uint32 usbcmd; /*- R/S (位 0): 运行/停止（1=运行，0=停止）
                      - HCRST (位 1): 主机控制器复位（置1触发复位）
                      - INTE (位 2): 中断使能（1=使能，0=禁用）
                      - HSEE (位 3): 主机系统错误使能
                      - LHCRST (位 7): 轻量级主机控制器复位
                      - CSS (位 8): 控制器保存状态
                      - CRS (位 9): 控制器恢复状态
                      - EWE (位 10): 事件中断使能
                      - EU3S (位 11): 启用U3 MMI（电源管理相关）*/
#define XHCI_CMD_RS (1 << 0)
#define XHCI_CMD_HCRST (1 << 1)
#define XHCI_CMD_INTE (1 << 2)
#define XHCI_CMD_HSEE (1 << 3)
#define XHCI_CMD_LHCRST (1 << 7)
#define XHCI_CMD_CSS (1 << 8)
#define XHCI_CMD_CRS (1 << 9)
#define XHCI_CMD_EWE (1 << 10)
#define XHCI_CMD_EU3S (1 << 11)

    // 04h: 状态寄存器 (USBSTS)
    uint32 usbsts; /* - HCH (位 0): 主机控制器停止（1=已停止，0=运行中）
                      - HSE (位 2): 主机系统错误（1=错误发生）
                      - EINT (位 3): 事件中断（1=有事件中断待处理）
                      - PCD (位 4): 端口变化检测（1=端口状态变化）
                      - SSS (位 8): 保存状态状态
                      - RSS (位 9): 恢复状态状态
                      - SRE (位 10): 保存/恢复错误
                      - CNR (位 11): 控制器未就绪（1=未就绪）
                      - HCE (位 12): 主机控制器错误*/
#define XHCI_STS_HCH (1 << 0)
#define XHCI_STS_HSE (1 << 2)
#define XHCI_STS_EINT (1 << 3)
#define XHCI_STS_PCD (1 << 4)
#define XHCI_STS_SSS (1 << 8)
#define XHCI_STS_RSS (1 << 9)
#define XHCI_STS_SRE (1 << 10)
#define XHCI_STS_CNR (1 << 11)
#define XHCI_STS_HCE (1 << 12)

    // 08h: 页面大小寄存器 (PAGESIZE)
    uint32 pagesize; // 控制器支持的页面大小*0x1000

    // 0Ch: 保留 [RsvdZ]
    uint32 reserved0[2];

    //0x14: 设备通知控制寄存器 (DNCTRL)
    uint32 dnctrl; // - 每位对应一个设备槽的使能（0=禁用，1=使能）

    //0x18 命令环控制寄存器 (CRCR)
    uint64 crcr;
    /*- 位[0] - RCS（Ring Cycle State，环周期状态）：当RCS=1时，主机控制器从命令环中获取的TRB需要其Cycle Bit为1才会被处理；当RCS=0时，则处理Cycle Bit为0的TRB。
                      - 位[1] - CS（Command Stop，命令停止）：当置1时，命令环在完成当前命令后停止运行。
                      - 位[2] - CA（Command Abort，命令中止）：当置1时，命令环立即停止，当前正在执行的命令被中止。
                      - 位[3] - CRR（Command Ring Running，命令环运行状态）：为1时表示命令环正在运行，为0时表示命令环已停止。
                      - 位[6:63] - Command Ring Pointer（命令环指针）：指向命令环的64位基地址（物理地址）。低6位必须为0（即地址必须64字节对齐）*/

    //0x20: 保留字段
    uint64 reserved1[2];

    //0x30h: 设备上下文基础地址数组指针 (DCBAAP)
    uint64 dcbaap; // DCBAA的物理地址指针

    // 38h: 配置寄存器 (CONFIG)
    uint32 config; // [7:0] 启用的设备槽数 (值≤MaxSlots)

    // 保留字段 (Reserved), 偏移 0x3C-0x3FF, 填充到端口寄存器之前
    uint32 reserved2[241];

    // 端口寄存器数组 (PORTSC, PORTPMSC, PORTLI, PORTHLPMC), 偏移 0x400起,每个端口占用16字节，按端口数量动态分配
    struct {
        // 端口状态和控制寄存器 (PORTSC), 32位
        uint32 portsc; /*  - CCS (位 0): 当前连接状态（1=设备连接 0=设备未连接）
                               - PED (位 1): 端口已启用/禁用 （1=启用 0=禁用）
                               - TM  (位 2)：1=隧道模式 0=本地模式
                               - OCA (位 3)：过电流激活 （1=该端口处于过流状态 0=该端口不存在过流情况）
                               - PR (位 4): 端口复位 （1=端口复位信令已断言 0=端口未复位）
                               - PLS (位 5-8): 端口链路状态 0x0：U0 /0x1：U1 /0x2：U2 /0x3：U3 /0x4：Disabled /0x5：RxDetect /0x6：Inactive /0x7：Polling /0x8：Recovery /0x9：Hot Reset /0xA：Compliance Mode /0xB：Test Mode /0xF：Resume
                               - PP (位 9): 端口电源 默认=1
                               - PortSpeed (位 10-13): 端口速度 0：未定义（通常表示端口还没被 reset 初始化出有效速率）/1：Full Speed /2：Low Speed /3：High Speed /4：SuperSpeed /5：SuperSpeedPlus（SSP） /6–15：保留
                               - PIC (位 14-15): 端口指示器控制（0=端口指示灯关闭 1=琥珀色 2=绿色 3=未定义）
                               - LWS (位 16): 链路状态写入选通
                               - CSC (位 17): 连接状态变化
                               - PEC (位 18): 端口使能/禁用变化
                               - WRC (位 19): 热重置变化
                               - OCC (位 20): 过流变化
                               - PRC (位 21): 端口复位变化
                               - PLC (位 22): 端口链路状态变化
                               - CEC (位 23): 配置错误变化
                               - CAS (位 24): 冷连接状态
                               - WCE (位 25): 连接唤醒使能
                               - WDE (位 26): 断开唤醒使能
                               - WOE (位 27)：过流唤醒使能
                               - DR  (位 30)：1=设备不可拆卸 0=设备可移动
                               - WPR (位 31)：热端口复位 */
#define XHCI_PORTSC_CCS (1 << 0)
#define XHCI_PORTSC_PED (1 << 1)
#define XHCI_PORTSC_OCA (1 << 3)
#define XHCI_PORTSC_PR (1 << 4)
#define XHCI_PORTSC_PLS_SHIFT 5
#define XHCI_PORTSC_PLS_MASK 0xf
#define XHCI_PORTSC_PP (1 << 9)
#define XHCI_PORTSC_SPEED_SHIFT 10
#define XHCI_PORTSC_SPEED_MASK 0xf
#define XHCI_PORTSC_SPEED_FULL 1
#define XHCI_PORTSC_SPEED_LOW 2
#define XHCI_PORTSC_SPEED_HIGH 3
#define XHCI_PORTSC_SPEED_SUPER 4
#define XHCI_PORTSC_SPEED_SUPER_PLUS 5
#define XHCI_PORTSC_PIC_SHIFT 14
#define XHCI_PORTSC_PIC_MASK 0x3
#define XHCI_PORTSC_W1C_MASK 0xFE0000
#define XHCI_PORTSC_LWS (1 << 16)
#define XHCI_PORTSC_CSC (1 << 17)
#define XHCI_PORTSC_PEC (1 << 18)
#define XHCI_PORTSC_WRC (1 << 19)
#define XHCI_PORTSC_OCC (1 << 20)
#define XHCI_PORTSC_PRC (1 << 21)
#define XHCI_PORTSC_PLC (1 << 22)
#define XHCI_PORTSC_CEC (1 << 23)
#define XHCI_PORTSC_CAS (1 << 24)
#define XHCI_PORTSC_WCE (1 << 25)
#define XHCI_PORTSC_WDE (1 << 26)
#define XHCI_PORTSC_WOE (1 << 27)
#define XHCI_PORTSC_DR (1 << 30)
#define XHCI_PORTSC_WPR (1 << 31)

#define XHCI_PLS_U0              0   // 正常工作状态，USB 设备活跃，支持全速数据传输（USB 3.0 或 USB 2.0）
#define XHCI_PLS_U1              1   // U1 低功耗状态，USB 设备进入轻度节能模式，快速恢复，适用于 USB 3.0
#define XHCI_PLS_U2              2   // U2 低功耗状态，USB 设备进入更深节能模式，恢复时间稍长，适用于 USB 3.0
#define XHCI_PLS_U3              3   // U3 挂起状态，USB 设备进入深度休眠，功耗最低，恢复时间较长，适用于 USB 3.0
#define XHCI_PLS_DISABLED        4   // 禁用状态，USB 端口被禁用，无法通信
#define XHCI_PLS_RX_DETECT       5   // 接收检测状态，USB 控制器正在检测是否有设备连接
#define XHCI_PLS_INACTIVE        6   // 非活跃状态，USB 端口未连接设备或设备未响应
#define XHCI_PLS_POLLING         7   // 轮询状态，USB 控制器正在初始化或尝试建立与设备的连接
#define XHCI_PLS_RECOVERY        8   // 恢复状态，USB 端口从低功耗状态（如 U3）恢复到活跃状态
#define XHCI_PLS_HOT_RESET       9   // 热重置状态，USB 端口正在执行热重置操作，重新初始化设备
#define XHCI_PLS_COMPLIANCE_MODE 10  // 合规模式，用于 USB 设备或控制器的合规性测试
#define XHCI_PLS_TEST_MODE       11  // 测试模式，USB 端口进入特定测试状态，用于硬件或协议测试
#define XHCI_PLS_RESUME          15  // 恢复状态，USB 设备从挂起状态恢复，通常由主机发起

        // 端口电源管理状态和控制寄存器 (PORTPMSC),控制电源管理和U1/U2状态,具体字段依赖于协议（USB2或USB3）
        uint32 portpmsc;

        // 端口链路信息寄存器 (PORTLI), 提供链路错误计数等信息
        uint32 portli;

        // 主机控制器端口电源管理控制寄存器 (PORTHLPMC), 仅用于USB2协议端口，控制高级电源管理
        uint32 porthlpmc;
    } portregs[256]; // 最大支持256个端口（根据HCSPARAMS1中的MaxPorts）
} xhci_op_regs_t;

// ===== 3. 运行时寄存器 (Runtime Registers) =====
typedef struct {
    // 00h: 微帧索引寄存器 (MFINDEX)
    uint32 mfindex; // [13:0] 当前微帧索引（按125μs递增）

    // 04h: 保留
    uint32 reserved0[7];

    // 中断管理数组 (IMAN) - 每个中断向量一个
    struct {
        // 中断管理寄存器 (IMAN), 偏移 0x00
        uint32 iman; // 中断管理 [0]：IP中断挂起（1=有中断待处理），[1]：中断使能（1=使能，0=禁用）

        //中断调节寄存器 (IMOD), 偏移 0x04,
        uint32 imod; // 中断调制器 (位 0-15): 中断调节间隔（以250ns为单位，(位 16-31): 中断调节计数器（只读）

        // 事件环段表大小寄存器 (ERSTSZ), 偏移 0x08, 32位
        uint32 erstsz; // - ERST Size (位 0-15): 事件环段表条目数（最大4096）
        uint32 reserved1;

        // 事件环段表基地址寄存器 (ERSTBA), 偏移 0x10-0x17, 64位
        uint64 erstba; //指向事件环段表的64位基地址（对齐到64字节)

        // 事件环出队指针寄存器 (ERDP), 偏移 0x18-0x1F, 64位
        uint64 erdp; /*指向事件环的当前出队指针
                        - DESI (位 0-2): 出队事件环段索引
                        - EHB (位 3): 事件处理忙碌（1=忙碌，写1清除）
                        - Event Ring Dequeue Pointer (位 4-63): 出队指针地址*/
#define XHCI_ERDP_EHB (1<<3)
    } intr_regs[1024]; // 最大支持1024个中断器（根据HCSPARAMS1中的MaxIntrs）
} xhci_rt_regs_t;

// ===== 4. 门铃寄存器 (Doorbell Registers) =====
// 门铃寄存器数组 (每个设备槽一个 + 主机控制器)
typedef unsigned int xhci_db_regs_t;     /*最大支持256个设备槽(由HCSPARAMS1的MaxSlots决定）
                                         - DB Target (位 0-7): 门铃目标
                                         - 值为0：触发命令环（Command Ring）
                                         - 值为1-31：触发特定端点（Endpoint 0-31）的传输环
                                         - DB Stream ID (位 16-31): 流ID（仅用于支持流的设备）*/

// ===== 5. 扩展寄存器 (HCCPARAMS2) =====
// 当HCCPARAMS1[0] (AC64) 设置为1时出现
typedef struct {
    // 00h: U1设备退出延迟 (U1DEL)
    uint32 u1del; // 默认U1退出延迟

    // 04h: U2设备退出延迟 (U2DEL)
    uint32 u2del; // 默认U2退出延迟

    // ... 更多扩展寄存器 ...
} xhci_ext_regs_t;

/* 0x01: USB Legacy Support (USB 传统支持) */
typedef struct {
        uint32 usblegsup; /* 位16=1 bios控制，位24=1 os控制 */
        uint32 usblegctlsts; /* 位0: USB SMI启用
                                 位4: 主机系统错误SMI启用
                                 位13: OS所有权变更SMI启用
                                 位14: PCI命令变更SMI启用
                                 位15: BAR变更SMI启用

                                 === 高16位：SMI 状态/事件区域 ===
                                 RO：只读
                                 位16: 事件中断SMI状态(RO)
                                 位19:17 保留 (RsvdP)
                                 位20: 主机系统错误SMI状态(RO)
                                 位28:21 保留 (RsvdZ)

                                 RW1C：写1清除
                                 位29: OS所有权变更SMI状态(RW1C)
                                 位30: PCI命令变更SMI状态(RW1C)
                                 位31: BAR变更SMI状态(RW1C)*/
}xhci_ecap_legacy_support;

/* 0x02: Supported Protocol Capability (支持的协议能力) */
typedef struct {
    uint32 protocol_ver;    /* 位 23:16 小修订版本0x10 = x.10
                              位 31:24 主修订版本0x03 = 3.x */
    char8 name[4];            /* 位 31:0 4个asci字符 */
    uint32 port_info;         /* 位7:0 兼容端口偏移
                              位15:8 兼容端口计数偏移
                              位31:28 协议速度 ID 计数 - RO，PSI 字段数量 (0-15)*/
    uint32 protocol_slot_type; /* 位4:0 协议插槽类型 */
    uint32 protocol_speed[15]; /* PSIV = bits[3:0]：Protocol Speed ID Value（会出现在 PORTSC 的 Port Speed 字段里）
                                  PSIE = bits[5:4]：指数档，决定单位（0=bit/s，1=Kb/s，2=Mb/s，3=Gb/s）
                                  PLT = bits[7:6]：PSI 类型（0=对称，2=非对称Rx，3=非对称Tx；非对称必须成对）
                                  PFD = bit[8]：是否全双工（1=全双工，0=半双工）
                                  LP = bits[15:14]：Link Protocol。对 USB2（Major=02h）要求为 0，具体是 LS/FS/HS 由速率决定。
                                  PSIM = bits[31:16]：速率尾数（mantissa）*/
}xhci_ecap_supported_protocol;

/* ERST条目结构 (16字节) */
typedef struct {
    uint64 ring_seg_base; // 段的64位物理基地址 (位[63:6]有效，位[5:0]为0)
    uint32 ring_seg_size; // 段中TRB的数量 (16到4096)
    uint32 reserved; // 保留位，初始化为0
} xhci_erst_t;

typedef struct {
    uint64 member0;
    uint64 member1;
} trb_t;

//region 设备上下文结构

#pragma pack(push, 1) // 必须强制 1 字节对齐，绝不允许编译器随意塞 padding

// ============================================================================
// Slot Context (32 字节核心部分，外部按需套一层 32/64 字节外壳)
// ============================================================================
typedef struct slot_ctx_t{
    // Dword 0
    uint32 route_string:20;     // [19:0] 路由字符串
    uint32 port_speed:4;        // [23:20] 端口速度
    uint32 rsvd_dw0_24:1;       // [24] 保留位 (规范中隐蔽的一位)
    uint32 mtt:1;               // [25] 多重事务转换器
    uint32 is_hub:1;            // [26] 1=集线器, 0=USB设备
    uint32 context_entries:5;   // [31:27] 端点上下文条目数量 (1~31)

    // Dword 1
    uint16 max_exit_latency; // [15:0] 最大退出延迟 (us)
    uint8 root_hub_port_num; // [23:16] 根集线器端口号
    uint8 num_ports;         // [31:24] 端口数量 (仅Hub有效)

    // Dword 2
    uint8 parent_hub_slot_id;// [7:0] 父集线器插槽 ID
    uint8 parent_port_num;   // [15:8] 父端口号
    uint16 tt_think_time:2;     // [17:16] TT 思考时间
    uint16 rsvd_dw2_18:4;       // [21:18] 保留
    uint16 interrupter_target:10;// [31:22] 目标中断器编号

    // Dword 3
    uint32 usb_device_address:8;// [7:0] 硬件分配的 USB 地址 (只读)
    uint32 rsvd_dw3_8:19;       // [26:8] 保留
    uint32 slot_state:5;        // [31:27] 插槽状态 (0=禁用, 1=默认, 2=寻址, 3=已配置)

    uint32 reserved[4];         // 填充至 32 字节
} slot_ctx_t;

// ============================================================================
// Endpoint Context (32 字节核心部分)
// ============================================================================
typedef struct ep_ctx_t{
    // Dword 0
    uint16 ep_state:3;          // [2:0] 端点状态 (0=禁用, 1=运行, 2=暂停, 3=停止, 4=错误)
    uint16 rsvd_dw0_3:5;        // [7:3] 保留
    uint16 mult:2;              // [9:8] 突发乘数
    uint16 max_pstreams:5;      // [14:10] 最大主数据流数量
    uint16 lsa:1;               // [15] 线性流数组标志
    uint8 interval;          // [23:16] 轮询间隔
    uint8 max_esit_payload_hi;// [31:24] ESIT 有效载荷高 8 位

    // Dword 1
    uint8 rsvd_dw1_0:1;        // [0] 保留
    uint8 cerr:2;              // [2:1] 错误计数 (通常填 3)
    uint8 ep_type:3;           // [5:3] 端点类型 (4=控制传输, 6=Bulk In, 等)
    uint8 rsvd_dw1_6:1;        // [6] 保留
    uint8 hid:1;               // [7] 主机初始化的禁用流标志
    uint8 max_burst_size;    // [15:8] 最大突发大小
    uint16 max_packet_size;  // [31:16] 最大包长 (8, 64, 512)

    // ★ 你的神作：Dword 2 & 3 (Qword 1，完美映射 64 位指针)
    uint64 dcs:1;               // [0] 出队周期状态 (Cycle Bit)
    uint64 rsvd_dw2_1:3;        // [3:1] 保留 (或 SCT 流上下文类型)
    uint64 tr_dequeue_ptr:60;   // [63:4] 传输环物理出队指针

    // Dword 4
    uint16 average_trb_length;// [15:0] 平均 TRB 长度
    uint16 max_esit_payload_lo;// [31:16] ESIT 有效载荷低 16 位
    uint32 reserved[3];         // 填充至 32 字节
} ep_ctx_t;

// ============================================================================
// Input Control Context
// ============================================================================
typedef struct input_ctrl_ctx_t{
    uint32 drop_context_flags;  // Dword 0: 位 0 = Slot, 位 1 = EP0, 位 2 = EP1...
    uint32 add_context_flags;   // Dword 1: 同上
    uint32 reserved[6];         // 填充至 32 字节
} input_ctrl_ctx_t;

#define XHCI_DEVICE_CONTEXT_COUNT 32
#define XHCI_INPUT_CONTEXT_COUNT 33


typedef struct {
    uint64 tr_dequeue; // TR Dequeue Ptr+ DCS(位0)
    uint64 reserved;
} xhci_stream_ctx_t;

//endregion

typedef struct {
    xhci_trb_t   *ring_base; //环起始地址
    uint32       index; //trb索引
    uint8        cycle; //循环位
} xhci_ring_t;

#pragma pack(pop)


typedef enum {
    XHCI_PORT_EMPTY = 0,
    XHCI_PORT_DEV,
    XHCI_PORT_HUB,
} xhci_port_type_t;

//xhci端口
typedef struct {
    union {
        struct usb_hub_t *usb_hub;      // hub设备
        struct usb_dev_t *usb_dev;      // usb设备
    };
   xhci_port_type_t type;                     //1 = usb_dev , 2 = usb_hub;
}xhci_port;


typedef struct {
    uint8  major_bcd;           // 协议主版本（DW0[31:24]，常见 0x02=USB2，0x03=USB3.x）
    uint8  minor_bcd;           // 协议次版本（DW0[23:16]，如 0x10=USB3.1 等）
    char8  name[4];             // 协议名字符串（DW1，常见 "USB " = 0x20425355）
    uint16 proto_defined;       // 协议自定义字段（DW2[27:16]，USB2/USB3 各自有含义）
    uint8  port_first;          // 覆盖端口起始号（DW2[7:0]，1-based）
    uint8  port_count;          // 连续覆盖端口数量（DW2[15:8]）
    uint8  slot_type;           // Protocol Slot Type（DW3[4:0]）
    uint8  psi_count;           // PSI 条目数 PSIC（DW2[31:28]，0=默认映射，>0=显式 PSI 表）
    uint32 *psi;                // 按 PSIV 索引的 PSI 原始 dword（用于解释 PortSC speed）
} xhci_spc_t;


//xhci控制器
typedef struct {
    uint8               major_bcd;          // 主版本
    uint8               minor_bcd;          // 次版本
    xhci_cap_regs_t     *cap_reg;           // 能力寄存器
    xhci_op_regs_t      *op_reg;            // 操作寄存器
    xhci_rt_regs_t      *rt_reg;            // 运行时寄存器
    xhci_db_regs_t      *db_reg;            // 门铃寄存器
    xhci_ext_regs_t     *ext_reg;           // 扩展寄存器
    uint64              *dcbaap;            // 设备上下文插槽
    xhci_port           *ports;             // 端口
    xhci_ring_t         cmd_ring;           // 命令环
    xhci_ring_t         event_ring;         // 事件环
    uint32              align_size;         // xhci内存分配对齐边界
    uint8               ctx_size;           // 设备上下文字节数（32或64字节）
    uint8               max_ports;          // 最大端口数量
    uint8               max_slots;          // 最大插槽数量
    uint16              max_intrs;          // 最大中断数量
    uint8               spc_count;          // spc数量
    xhci_spc_t          *spc;               // 支持的协议功能
    uint8               *port_to_spc;       // 端口找spc号
} xhci_controller_t;


uint64 xhci_ring_enqueue(xhci_ring_t *ring, xhci_trb_t *trb_push);
xhci_trb_comp_code_e xhci_wait_for_event(xhci_controller_t *xhci_controller, uint64 wait_trb_pa, uint64 timeout_ms,xhci_trb_t *out_event_trb);
static inline int32 xhci_ring_init(xhci_ring_t *ring, uint32 align_size);
static inline void xhci_ring_doorbell(xhci_controller_t *xhci_controller, uint8 db_number, uint32 value);
int8 xhci_enable_slot(xhci_controller_t *xhci_controller, uint8 *out_slot_id);
int8 xhci_address_device(xhci_controller_t *xhci_controller, uint8 slot_id,xhci_input_context_t *input_ctx);



/////////////////////////////////////// 准备作废 /////////////////////////////////////////////
#define TRB_RESERVED                (0 << 10)   // 保留
#define TRB_NORMAL                  (1 << 10)   // 普通传输
#define TRB_SETUP_STAGE             (2 << 10)   // 设置阶段
#define TRB_DATA_STAGE              (3 << 10)   // 数据阶段
#define TRB_STATUS_STAGE            (4 << 10)   // 状态阶段
#define TRB_ISOCH                   (5 << 10)   // 等时传输
#define TRB_LINK                    (6 << 10)   // 链接
#define TRB_EVDATA                  (7 << 10)   // 事件数据
#define TRB_NOOP                    (8 << 10)   // 空操作
#define TRB_ENABLE_SLOT             (9 << 10)   // 启用插槽
#define TRB_DISABLE_SLOT            (10 << 10)  // 禁用插槽
#define TRB_ADDRESS_DEVICE          (11 << 10)  // 设备寻址
#define TRB_CONFIGURE_ENDPOINT      (12 << 10)  // 配置端点
#define TRB_EVALUATE_CONTEXT        (13 << 10)  // 评估上下文
#define TRB_RESET_ENDPOINT          (14 << 10)  // 重置端点
#define TRB_STOP_ENDPOINT           (15 << 10)  // 停止端点
#define TRB_SET_TR_DEQUEUE          (16 << 10)  // 设置传输环出队
#define TRB_RESET_DEVICE            (17 << 10)  // 重置设备
#define TRB_FORCE_EVENT             (18 << 10)  // 强制事件
#define TRB_NEGOTIATE_BW            (19 << 10)  // 协商带宽
#define TRB_SET_LATENCY_TOLERANCE   (20 << 10)  // 设置延迟容忍
#define TRB_GET_PORT_BANDWIDTH      (21 << 10)  // 获取端口带宽
#define TRB_FORCE_HEADER            (22 << 10)  // 强制头部
#define TRB_NOOP_COMMAND            (23 << 10)  // 空操作命令
#define TRB_TRANSFER                (32 << 10)  // 传输
#define TRB_COMMAND_COMPLETE        (33 << 10)  // 命令完成
#define TRB_PORT_STATUS_CHANGE      (34 << 10)  // 端口状态改变
#define TRB_BANDWIDTH_REQUEST       (35 << 10)  // 带宽请求
#define TRB_DOORBELL                (36 << 10)  // 门铃
#define TRB_HOST_CONTROLLER         (37 << 10)  // 主机控制器
#define TRB_DEVICE_NOTIFICATION     (38 << 10)  // 设备通知
#define TRB_MFINDEX_WRAP            (39 << 10)  // 主框架索引回绕

//定时
static inline void timing(void) {
    // uint64 count = 20000000;
    // while (count--) asm_pause();
}

//region 命令环trb
#define TRB_TYPE_ENABLE_SLOT             (9UL << 42)   // 启用插槽
#define TRB_TYPE_ADDRESS_DEVICE          (11UL << 42)  // 设备寻址
#define TRB_TYPE_CONFIGURE_ENDPOINT      (12UL << 42)  // 配置端点
#define TRB_TYPE_EVALUATE_CONTEXT        (13UL << 42)  // 评估上下文

/*
 * 启用插槽命令trb
 * uint64 member0 位0-63 = 0
 *
 * uint64 member1 位32    cycle
 *                位42-47 TRB Type 类型
 */
static inline void enable_slot_com_trb(trb_t *trb) {
    trb->member0 = 0;
    trb->member1 = TRB_TYPE_ENABLE_SLOT;
}

/*
 * 设置设备地址命令trb
 * uint64 member0 位0-63 = input context pointer 物理地址
 *
 * uint64 member1 位32    cycle
 *                位41    bsr 块地址请求命令 0=发送usb_set_address请求，1=不发送（一般设置0）
 *                位42-47 TRB Type 类型
 *                位56-63 slot id
 */
static inline void addr_dev_com_trb(trb_t *trb, uint64 input_ctx_ptr, uint64 slot_id) {
    trb->member0 = input_ctx_ptr;
    trb->member1 = TRB_TYPE_ADDRESS_DEVICE | (slot_id << 56);
}

/*
 * 配置端点trb
 * uint64 member0 位0-63 = input context pointer 物理地址
 *
 * uint64 member1 位32    cycle
 *                位41    dc    接触配置 1= xhci将忽略输入上下文指针字段，一般设置0
 *                位42-47 TRB Type 类型
 *                位56-63 slot id
 */
static inline void config_endpoint_com_trb(trb_t *trb, uint64 input_ctx_ptr, uint64 slot_id) {
    trb->member0 = input_ctx_ptr;
    trb->member1 = TRB_TYPE_CONFIGURE_ENDPOINT | (slot_id << 56);
}

/*
 * 评估上下文trb
 * uint64 member0 位0-63 = input context pointer 物理地址
 *
 * uint64 member1 位32    cycle
 *                位41    bsr 块地址请求命令 0=发送usb_set_address请求，1=不发送（一般设置0）
 *                位42-47 TRB Type 类型
 *                位56-63 slot id
 */
static inline void evaluate_context_com_trb(trb_t *trb, uint64 input_ctx_ptr, uint64 slot_id) {
    trb->member0 = input_ctx_ptr;
    trb->member1 = TRB_TYPE_EVALUATE_CONTEXT | (slot_id << 56);
}

//endregion

//region 端点控制环trb

#define TRB_TYPE_SETUP_STAGE             (2UL << 42)   // 设置阶段
#define TRB_TYPE_DATA_STAGE              (3UL << 42)   // 数据阶段
#define TRB_TYPE_STATUS_STAGE            (4UL << 42)   // 状态阶段

typedef enum {
    DISABLE_IOC = 0UL << 37,
    ENABLE_IOC = 1UL << 37
} config_ioc_e;

typedef enum {
    trb_out = 0UL << 48,
    trb_in = 1UL << 48,
} trb_dir_e;

typedef enum {
    disable_ch = (0UL << 33),
    enable_ch = (1UL << 33)
} config_ch_e;

typedef enum {
    usb_req_get_status = 0x00 << 8, /* 获取状态
                                               - 接收者：设备、接口、端点
                                               - 返回：设备/接口/端点的状态（如挂起、遥控唤醒）
                                               - w_value: 0
                                               - w_index: 设备=0，接口=接口号，端点=端点号
                                               - w_length: 2（返回 2 字节状态） */
    usb_req_clear_feature = 0x01UL << 8, /* 清除特性
                                               - 接收者：设备、接口、端点
                                               - 用途：清除特定状态（如取消遥控唤醒或端点暂停）
                                               - w_value: 特性选择（如 0=设备遥控唤醒，1=端点暂停）
                                               - w_index: 设备=0，接口=接口号，端点=端点号
                                               - w_length: 0 */
    usb_req_set_feature = 0x03UL << 8, /* 设置特性
                                               - 接收者：设备、接口、端点
                                               - 用途：启用特定特性（如遥控唤醒、测试模式）
                                               - w_value: 特性选择（如 0=设备遥控唤醒，1=端点暂停）
                                               - w_index: 设备=0，接口=接口号，端点=端点号
                                               - w_length: 0 */
    usb_req_set_address = 0x05UL << 8, /* 设置设备地址
                                               - 接收者：设备
                                               - 用途：在枚举过程中分配设备地址（1-127）
                                               - w_value: 新地址（低字节）
                                               - w_index: 0
                                               - w_length: 0 */
    usb_req_get_descriptor = 0x06UL << 8, /* 获取描述符
                                               - 接收者：设备、接口
                                               - 用途：获取设备、配置、接口、字符串等描述符
                                               - w_value: 高字节=描述符类型（如 0x01=设备，0x02=配置），低字节=索引
                                               - w_index: 0（设备/配置描述符）或语言 ID（字符串描述符）
                                               - w_length: 请求的字节数 */
    usb_req_set_descriptor = 0x07UL << 8, /* 设置描述符
                                               - 接收者：设备、接口
                                               - 用途：更新设备描述符（较少使用）
                                               - w_value: 高字节=描述符类型，低字节=索引
                                               - w_index: 0 或语言 ID
                                               - w_length: 数据长度 */
    usb_req_get_config = 0x08UL << 8, /* 获取当前配置
                                               - 接收者：设备
                                               - 用途：返回当前激活的配置值
                                               - w_value: 0
                                               - w_index: 0
                                               - w_length: 1（返回 1 字节配置值） */
    usb_req_set_config = 0x09UL << 8, /* 设置配置
                                               - 接收者：设备
                                               - 用途：激活指定配置
                                               - w_value: 配置值（来自配置描述符的 b_configuration_value）
                                               - w_index: 0
                                               - w_length: 0 */
    usb_req_get_interface = 0x0AUL << 8, /* 获取接口的备用设置
                                               - 接收者：接口
                                               - 用途：返回当前接口的备用设置编号
                                               - w_value: 0
                                               - w_index: 接口号
                                               - w_length: 1（返回 1 字节备用设置值） */
    usb_req_set_interface = 0x0BUL << 8, /* 设置接口的备用设置
                                               - 接收者：接口
                                               - 用途：选择接口的备用设置
                                               - w_value: 备用设置编号
                                               - w_index: 接口号
                                               - w_length: 0 */
    usb_req_synch_frame = 0x0CUL << 8, /* 同步帧
                                               - 接收者：端点
                                               - 用途：为同步端点（如音频设备）提供帧编号
                                               - w_value: 0
                                               - w_index: 端点号
                                               - w_length: 2（返回 2 字节帧号） */

    usb_req_get_max_lun = 0xFEUL << 8, /* Mass Storage 类请求 (BOT)
                                      * - bRequestType=0xFE (Host→Interface)
                                      * - wValue=0, wIndex=接口号
                                      * - wLength=1
                                      * - 获取最大逻辑单元 返回最大 LUN 编号（0 = 1个LUN） */

    usb_req_mass_storage_reset = 0xFFUL << 8, /* Mass Storage 类请求 (BOT)
                                      * - bRequestType=0x21 (Host→Interface)
                                      * - wValue=0, wIndex=接口号
                                      * - wLength=0
                                      * - 用于复位 U 盘状态机 */
} setup_stage_req_e;

/*设置阶段trb
    uint64 member0;  *位4-0：接收者（0=设备，1=接口，2=端点，3=其他）
                     *位6-5：类型（0=标准，1=类，2=厂商，3=保留）
                     *位7：方向（0=主机到设备，1=设备到主机）
                     *位8-15  Request    请求代码，指定具体请求（如 GET_DESCRIPTOR、SET_ADDRESS）标准请求示例：0x06（GET_DESCRIPTOR）、0x05（SET_ADDRESS）
                     *位16-31 Value      请求值，具体含义由 b_request 定义 例如：GET_DESCRIPTOR 中，w_value 高字节为描述符类型，低字节为索引
                     *位32-47 Index      索引或偏移，具体含义由 b_request 定义 例如：接口号、端点号或字符串描述符索引
                     *位48-63 Length     数据阶段的传输长度（字节）主机到设备：发送的数据长度 设备到主机：请求的数据长度

    uint64 member1;  *位0-15  TRB Transfer Length  传输长度
                     *位22-31 Interrupter Target 中断目标
                     *位32    cycle
                     *位37    1=IOC 完成时中断
                     *位38    1=IDT 数据包含在trb
                     *位42-47 TRB Type 类型
                     *位48-49 TRT 传输类型 0=无数据阶段 1=保留 2=out(主机到设备) 3=in(设备到主机)
*/
typedef enum {
    setup_stage_norm = 0 << 5,
    setup_stage_calss = 1UL << 5,
    setup_stage_firm = 2UL << 5,
    setup_stage_reserve = 3UL << 5
} setup_stage_type_e;

typedef enum {
    setup_stage_out = 0 << 7,
    setup_stage_in = 1UL << 7
} setup_stage_dir_e;

typedef enum {
    no_data_stage = 0 << 48,
    out_data_stage = 2UL << 48,
    in_data_stage = 3UL << 48
} trb_trt_e;

typedef enum {
    setup_stage_device = 0 << 0,
    setup_stage_interface = 1UL << 0,
    setup_stage_endpoint = 2UL << 0
} setup_stage_receiver_e;

#define TRB_FLAG_IDT    (1UL<<38)
#define TRB_TRAN_LEN 8

static inline void setup_stage_trb(trb_t *trb, setup_stage_receiver_e receiver,setup_stage_type_e type, setup_stage_dir_e dir,setup_stage_req_e req, uint64 value, uint64 index, uint64 length, trb_trt_e trt) {
    trb->member0 = receiver | type | dir | req | (value<<16) | (index<<32) | (length<<48);
    trb->member1 = (TRB_TRAN_LEN << 0) | TRB_FLAG_IDT | TRB_TYPE_SETUP_STAGE | trt;
}

/*
 * 数据阶段trb
 * uint64 member0 位0-63 data buffer pointer 数据区缓冲区物理地址指针
 *
 *  uint64 member1 位0-16   trb transfer length 传输长度
 *                 位17-21 td size             剩余数据包
 *                 位22-31 Interrupter Target 中断目标
 *                 位32    cycle
 *                 位33    ent     1=评估下一个trb
 *                 位34    isp     1=短数据包中断
 *                 位35    ns      1=禁止窥探
 *                 位36    ch      1=链接 多个trb关联
 *                 位37    IOC     1=完成时中断
 *                 位38    IDT     1=数据包含在trb
 *                 位41    bei     1=块事件中端，ioc=1 则传输事件在下一个中断阀值时，ioc产生的中断不应向主机发送中断。
 *                 位42-47 TRB Type 类型
 *                 位48    dir     0=out(主机到设备) 1=in(设备到主机)
 */
#define TRB_FLAG_ENT    (1UL<<33)

static inline void data_stage_trb(trb_t *trb, uint64 data_buff_ptr, uint64 trb_tran_length, trb_dir_e dir) {
    trb->member0 = data_buff_ptr;
    trb->member1 = (trb_tran_length << 0) | TRB_TYPE_DATA_STAGE | TRB_FLAG_ENT | enable_ch | dir;
}


/*
 * 状态阶段trb
 * uint64 member0 位0-63 =0
 *
 * uint64 member1  位22-31 Interrupter Target 中断目标
 *                 位32    cycle
 *                 位33    ent     1=评估下一个trb
 *                 位36    ch      1=链接 多个trb关联
 *                 位37    IOC     1=完成时中断
 *                 位42-47 TRB Type 类型
 *                 位48    dir     0=out(主机到设备) 1=in(设备到主机)
 */
static inline void status_stage_trb(trb_t *trb, config_ioc_e ioc, trb_dir_e dir) {
    trb->member0 = 0;
    trb->member1 = ioc | TRB_TYPE_STATUS_STAGE | dir;
}

//endregion

//region 传输环trb
#define TRB_TYPE_NORMAL                  (1UL << 42)   // 普通传输
/*  普通传输trb
 *  uint64 member0 位0-63 data buffer pointer 数据区缓冲区物理地址指针
 *
 *  uint64 member1 位0-16  trb transfer length 传输长度
 *                 位17-21 td size             剩余数据包
 *                 位22-31 Interrupter Target 中断目标
 *                 位32    cycle
 *                 位33    ent     1=“评估下一个 TRB”，提示控制器：立即继续执行下一个 TRB，不必等事件或中断触发。
 *                 位34    isp     1=短数据包中断
 *                 位35    ns      1=禁止窥探
 *                 位36    ch      1=链接 多个trb关联
 *                 位37    IOC     1=完成时中断
 *                 位38    IDT     1=数据包含在trb
 *                 位41    bei     1=块事件中端，ioc=1 则传输事件在下一个中断阀值时，ioc产生的中断不应向主机发送中断。
 *                 位42-47 TRB Type 类型
 */
static inline void normal_transfer_trb(trb_t *trb, uint64 data_buff_ptr, config_ch_e ent_ch, uint32 trb_tran_length,
                                       config_ioc_e ioc) {
    trb->member0 = data_buff_ptr;
    trb->member1 = ent_ch | (trb_tran_length << 0) | ioc | TRB_TYPE_NORMAL;
}

//endregion

//region 其他trb
#define TRB_TYPE_LINK                    (6UL << 42)   // 链接
#define TRB_FLAG_TC                      (1UL << 33)
#define TRB_FLAG_CYCLE                   (1UL << 32)
/*  link trb
 *  uint64 member0 位0-63  ring segment pointer 环起始物理地址指针
 *
 *  uint64 member1 位22-31 Interrupter Target 中断目标
 *                 位32    cycle
 *                 位33    tc      1=下个环周期切换 0=不切换
 *                 位36    ch      1=链接 多个trb关联
 *                 位37    IOC     1=完成时中断
 *                 位42-47 TRB Type 类型
 */
static inline void link_trb(trb_t *trb, uint64 ring_base_ptr, uint64 cycle) {
    trb->member0 = ring_base_ptr;
    trb->member1 = cycle | TRB_FLAG_TC | TRB_TYPE_LINK;
}

//endregion





