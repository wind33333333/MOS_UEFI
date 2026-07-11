#pragma once
#include "moslib.h"
#include "device.h"
#include "driver.h"
#include "usb-def.h"

// ============================================================================
// 🚄 USB 端口与设备逻辑运行速率 (USB Port/Device Speeds)
// 统一宏定义版：提供绝对安全的无符号常量，消除跨模块类型歧义
// ============================================================================
#define USB_SPEED_UNKNOWN    0  // 未知/未连接/出错
#define USB_SPEED_LOW        1  // 低速 1.5 Mbps (USB 1.1)
#define USB_SPEED_FULL       2  // 全速 12 Mbps (USB 1.1)
#define USB_SPEED_HIGH       3  // 高速 480 Mbps (USB 2.0)
#define USB_SPEED_SUPER_5G   4  // 超高速 5 Gbps (USB 3.2 Gen 1)
#define USB_SPEED_SUPER_10G  5  // 超高速 10 Gbps (USB 3.2 Gen 2x1)
#define USB_SPEED_SUPER_20G  6  // 超高速 20 Gbps (USB 3.2 Gen 2x2)


// 定义匹配标志位 (位掩码)
#define USB_MATCH_ANY (-1)
#define USB_MATCH_VENDOR       0x0001  // 要求匹配 VID
#define USB_MATCH_PRODUCT      0x0002  // 要求匹配 PID
#define USB_MATCH_INT_CLASS    0x0080  // 要求匹配接口大类
#define USB_MATCH_INT_SUBCLASS 0x0100  // 要求匹配接口子类
#define USB_MATCH_INT_PROTOCOL 0x0200  // 要求匹配接口协议

typedef struct usb_id_t {
    uint16 match_flags; // 🌟 灵魂字段：告诉 match 函数怎么做比较

    // 设备级匹配
    uint16 vendor_id;
    uint16 product_id;

    // 接口级匹配
    uint8  if_class;
    uint8  if_subclass;
    uint8  if_protocol;
} usb_id_t;


//usb驱动
typedef struct usb_drv_t{
    driver_t drv;
    int  (*probe)(struct usb_if_t *usb_if, usb_id_t *id);
    void (*remove)(struct usb_if_t *usb_if);
} usb_drv_t;

//usb端点
typedef struct usb_ep_t {
    // 1. 通用 USB 逻辑特性 (完全独立于硬件，直接来源于描述符)
    uint8       ep_type;           // 端点：控制/批量/中断/等时
    uint16      max_packet_size;   //wMaxPacketSize：解码后的单次最大包长
    uint8       interval;          //bInterval：轮询间隔（中断/等时用；bulk 通常可忽略但保留）

    // USB 3.0 超高速伴随特性 (SuperSpeed Companion)
    uint8       max_burst;          // USB3 bMaxBurst（0=1 burst；仅 SS/SSP 有意义）
    uint16      max_streams_exp;    // bulk 端点支持的最大 stream 数（由 ss_comp->bmAttributes 解码，0 表示不支持 streams（BOT 一般用不到，UAS 可能需要）
    uint16      bytes_per_interval; // wBytesPerInterval：周期性端点每 ESIT 传输字节数
    uint8       mult;               // USB 2.0 High-Speed 高带宽事务 (Mult) 处理 0=1 transaction, 1=2 trans, 2=3 trans

    // 动态数组：紧随端点后的 class-specific/未知描述符块，枚举层不解释语义，交给类驱动（例如 UAS）按需解析
    void        *extras_desc;
    uint16      extras_len;

    // 2. 仅为 xHCI 定制的强绑定硬件特性 (Endpoint Context 推导值与 DMA 资源)
    uint8       ep_dci;            // xHCI 专属的设备上下文索引 (Device Context Index, 1~31)
    uint8       cerr;              // xHCI 错误重试计数 (通常设为 3)
    uint8       lsa;               // xHCI 线性流数组标志 (Linear Stream Array)
    uint8       hid;               // xHCI 主机发起禁用标志 (Host Initiate Disable)
    uint32      max_esit_payload;  // xHCI 周期端点有效载荷 (基于通用参数推导计算)
    uint16      average_trb_length;// xHCI 专用的 DMA 预取启发值 (Average TRB Length)
    uint64      trq_phys_addr;     // xHCI 硬件出队指针 (TR Dequeue Pointer) 物理地址

    // ★ 统一传输环数组：
    // 情况 A (非流模式): 分配大小为 1 的数组。rings[0] 就是普通的 transfer_ring。
    // 情况 B (流模式)  : 分配大小为 num_streams + 1 的数组。rings[1...N] 是流环。
    uint8              enable_streams_exp;// xHCI 实际向主板申请并启用的流指数
    uint32             ring_max_trbs; // 上层驱动期望的环大小 (0 表示使用 Core 默认值)
    struct xhci_submit_ring_t *ring_arr;            // xHCI 传输环数组 (普通模式大小为1，流模式大小为 N+1)
    void               *streams_ctx_array;   // xHCI 流上下文数组的 DMA 内存基地址

} usb_ep_t;


//usb备用接口
typedef struct usb_if_alt_t {
    struct usb_if_t *uif;
    usb_if_desc_t *if_desc;  // 指向 cfg_raw 内
    void          *extras_desc; //接口似有描述符
    uint16        extras_len;
    usb_ep_t      *eps;      // 可选：解析后的端点数组
} usb_if_alt_t;

//usb接口
typedef struct usb_if_t {
    struct usb_dev_t *udev;
    uint8 num_if_alts;          // 备用接口数量
    usb_if_alt_t *if_alts;      // 备用接口数组
    usb_if_alt_t *activity_if_alt;   // 当前激活的备用接口
    device_t dev;
    void    *drv_data;
} usb_if_t;


struct xhci_input_ctrl_ctx_t;
struct xhci_hcd_t;

//USB设备
typedef struct usb_dev_t{
    // 1. 通用总线拓扑与设备模型
    device_t                        dev;               // 继承系统基础设备对象
    struct usb_dev_t                *parent_hub;       // 亲爹指针 (直连主板则为 NULL)
    uint8                           root_hub_port_num;     // 🌟 新增：认祖归宗，主板上的物理根端口号
    uint8                           tt_hub_slot_id;
    uint8                           tt_port_num;   // 替代原来的 port_num：插在亲爹的第几个口上？
    uint8                           psiv;             // xHCI 专属的底层 DMA 挡位 (用于填 Slot Context)
    uint8                           port_speed;       // 🌟 1. 保留全局标准枚举 (供状态机流转和描述符解析使用)
    uint32                          speed_kbps;       // 🌟 2. 新增：扁平化的绝对物理带宽 (供高级驱动精确计算资源)
    uint32                          route_string;
    uint8                           hub_depth;      // 🌟 新增：记录当前设备处于第几层 (0=直连主板, 1=第一层Hub...)

    // Hub 专属特性 (非 Hub 时忽略)
    boolean                         is_hub;        //1=hub 0=普通设备
    uint8                           hub_num_ports; //hub端口数量
    uint8                           hub_mtt; //Multiple Transaction Translators - 多事务翻译器
    uint8                           hub_ttt;
    uint16                          max_exit_latency;

    // 2. 纯 USB 协议概念 (描述符与配置)
    usb_dev_desc_t                  *dev_desc;             //设备描述符
    usb_cfg_desc_t                  *config_desc;          //配置描述符
    usb_string_desc_t               *language_desc;        //语言描述符
    usb_string_desc_t               *manufacturer_desc;    //制造商描述符
    usb_string_desc_t               *product_desc;         //产品型号名描述符
    usb_string_desc_t               *serial_number_desc;   //序列号描述符

    uint8                           *manufacturer;     // 制造商ascii字符
    uint8                           *product;          // 产品型号ascii字符
    uint8                           *serial_number;    // 序列号ascii字符

    // 3. 逻辑端点与接口路由 (暴露给业务层驱动的资源)
    usb_if_t                        *ifs;              // 接口指针根据接口数量动态分配
    usb_ep_t                        *eps[32];          // eps[0]仅占位，eps[1]-eps[31]=端点1-31

    // 4. 仅为xhci定制强绑定
    uint8                           slot_id;
    uint16                          interrupter_target;
    void                            *out_ctx;    // 硬件状态
    struct xhci_input_ctrl_ctx_t    *in_ctx;     // 软件状态
    uint32                          active_ep_map;       //当前活跃的端点图
    struct xhci_hcd_t               *xhcd;              // xhci控制器
    void                            *drv_data;
} usb_dev_t;

/* ========================================================================
 * URB 传输控制标志位 (Transfer Flags) - 完美复刻 Linux 内核语义
 * ======================================================================== */

// 1. 数据完整性控制
#define URB_SHORT_NOT_OK        0x0001  // ★ 严格模式：发生短包 (Short Packet) 时直接视为错误。仅适用于 IN 传输。

// 2. 内存与 DMA 管理
#define URB_NO_TRANSFER_DMA_MAP 0x0004  // 驱动已自行完成物理内存映射，底层请直接使用 urb->transfer_dma，不要再调 IOMMU/映射函数。
#define URB_NO_SETUP_DMA_MAP    0x0008  // 驱动已自行完成 Setup 包的物理映射，底层请直接使用 urb->setup_dma。
#define URB_FREE_BUFFER         0x0100  // 内核托管：当这个 URB 被销毁时，请帮我自动 kfree 掉 transfer_buffer。

// 3. 协议边缘情况处理
#define URB_ZERO_PACKET         0x0040  // ★ Bulk OUT 极客专属：如果传输长度刚好是 MaxPacketSize 的整数倍，强制在末尾追加一个 0 字节包 (ZLP)，防止设备死等。

// 4. 硬件中断调度控制 (替换原先的 ioc)
#define URB_NO_INTERRUPT        0x0080  // ★ 幽灵模式：静默传输，传输完成时【不触发】硬件中断 (即清零最后一个 TRB 的 IOC 位)。通常用于大批量 URB 连续提交的场景，只在最后一个 URB 开启中断。

// 5. 传输方向强制覆盖 (可选，通常优先以端点描述符方向为准)
#define URB_DIR_IN              0x0200  // 强制标明方向为：设备 -> 主机
#define URB_DIR_OUT             0x0000  // 强制标明方向为：主机 -> 设备
#define URB_DIR_MASK            0x0200  // 用于提取方向的掩码


/**
 * @brief USB 请求块 (USB Request Block) - 纯逻辑版
 */
typedef struct usb_urb_t {
    // === 1. 路由寻址区 ===
    usb_dev_t   *udev;        // 目标设备上下文
    usb_ep_t    *ep;          // 目标端点
    uint16      stream_id;    // UAS 协议专用的 Stream ID

    // === 2. 业务载荷区 ===
    usb_setup_packet_t *setup_packet;// EP0 控制包指针
    void        *transfer_buf;  // 数据缓冲区虚拟地址
    uint32      transfer_len;   // 期望传输总长度
    uint32      interval;

    // 👑 核心换血：用标志位取代具体的硬件配置
    uint32      transfer_flags; // 传输控制组合掩码 (如 URB_SHORT_NOT_OK | URB_NO_INTERRUPT)

    // === 3. 状态与回调区 ===
    uint32      actual_length;  // [新增] 实际成功传输的字节数 (硬件回填)
    uint64      last_trb_pa;    // 最后一个 TRB 的物理地址 (仅做底层同步过渡用)
    int         status;         // URB 状态码

    // void (*complete)(struct usb_urb *urb); // 未来做全异步驱动时，这里放回调函数

    list_head_t node;         // 挂载到端点 pending_urbs 的链表节点

    // 🌟 单任务环境的终极同步神器
    volatile boolean is_done;
} usb_urb_t;


//端点转Dci
static inline uint8 epaddr_to_epdci(uint8 ep) {
    asm volatile(
        "rolb $1,%0"
        :"+q"(ep)
        :
        :"cc");
    return ep;
}

//Dci转端点
static inline uint8 epdci_to_epaddr(uint8 dci) {
    asm volatile(
        "rorb $1,%0"
        :"+q"(dci)
        :
        :"cc");
    return dci;

}


//获取下一个描述符
static inline void *usb_get_next_desc(usb_desc_head_t *head) {
    return (uint8*)head + head->length;
}


extern struct bus_type_t usb_bus_type;

void usb_dev_init(usb_dev_t *udev);

//注册usb设备
static inline void usb_dev_register(usb_dev_t *usb_dev) {
    device_register(&usb_dev->dev);
}

int32 usb_if_create(usb_dev_t *udev);

void usb_if_register(usb_dev_t *udev);


void usb_drv_register(usb_drv_t *usb_drv);//注册usb驱动
int usb_bus_match(device_t* dev,driver_t* drv);
int usb_bus_probe(device_t* dev);
void usb_bus_remove(device_t* dev);

usb_urb_t *usb_alloc_urb(void);
void usb_free_urb(usb_urb_t *urb);
void usb_fill_control_urb(usb_urb_t *urb,usb_dev_t *udev,usb_ep_t *ep,usb_setup_packet_t *setup_packet,void *transfer_buf,uint32 transfer_len);
void usb_fill_bulk_urb(usb_urb_t *urb,usb_dev_t *udev,usb_ep_t *ep,void *transfer_buf,uint32 transfer_len);
void usb_fill_int_urb(usb_urb_t *urb,usb_dev_t *udev,usb_ep_t *ep,void *transfer_buf,uint32 transfer_len,uint32 interval);

usb_if_alt_t* usb_find_alt_if(usb_if_t *uif, int16 class, int16 subclass, int16 protocol);
int32 usb_enable_alt_if(usb_if_alt_t *new_alt);
int32 usb_cfg_alt_streams(usb_if_alt_t *alt, uint8 want_streams_exp);

int32 usb_control_msg(usb_dev_t *udev, void *data_buf,
                      uint8 request_type,uint8 request, uint16 value, uint16 index, uint16 length);


// ============================================================================
// 🚦 端点状态控制与接口配置 API (极简封装版)
// ============================================================================
/**
 * @brief 端点解锁 (清除端点 Halt / 清除 Stall 状态)
 * @param udev   目标设备
 * @param ep_dci xHCI 端点上下文索引 (DCI)
 * @note 用于抢救出现 STALL 错误的端点。无数据阶段，length = 0。
 */
static inline int32 usb_clear_ep_halt(usb_dev_t *udev, uint8 ep_dci) {
    // 🌟 发往 Endpoint 的标准控制请求
    uint8 req_type = USB_BM_REQ_TYPE(USB_REQ_DIR_OUT, USB_REQ_TYPE_STANDARD, USB_REQ_REC_ENDPOINT);

    return usb_control_msg(udev, NULL,
                           req_type,
                           USB_REQ_CLEAR_FEATURE,
                           USB_FEATURE_ENDPOINT_HALT, // wValue: 清除 Halt 特性
                           epdci_to_epaddr(ep_dci),   // wIndex: 目标端点物理地址
                           0);                        // wLength: 0
}

/**
 * @brief 端点上锁 (强制端点进入 Halt/Stall 状态)
 * @param udev   目标设备
 * @param ep_dci xHCI 端点上下文索引 (DCI)
 * @note 通常用于模拟错误或调试。无数据阶段，length = 0。
 */
static inline int32 usb_set_ep_halt(usb_dev_t *udev, uint8 ep_dci) {
    uint8 req_type = USB_BM_REQ_TYPE(USB_REQ_DIR_OUT, USB_REQ_TYPE_STANDARD, USB_REQ_REC_ENDPOINT);

    return usb_control_msg(udev, NULL,
                           req_type,
                           USB_REQ_SET_FEATURE,
                           USB_FEATURE_ENDPOINT_HALT, // wValue: 设置 Halt 特性
                           epdci_to_epaddr(ep_dci),   // wIndex: 目标端点物理地址
                           0);                        // wLength: 0
}

/**
 * @brief 激活配置 (Set Configuration)
 * @note 这是一个纯命令传输，没有后续的数据包，因此 buffer 为 NULL，length 为 0。
 */
static inline int32 usb_set_cfg(usb_dev_t *udev) {
    // 🌟 发往整个 Device 的标准控制请求
    uint8 req_type = USB_BM_REQ_TYPE(USB_REQ_DIR_OUT, USB_REQ_TYPE_STANDARD, USB_REQ_REC_DEVICE);

    return usb_control_msg(udev, NULL,
                           req_type,
                           USB_REQ_SET_CONFIGURATION,
                           udev->config_desc->configuration_value, // wValue: 要激活的配置号
                           0,                                      // wIndex: 0
                           0);                                     // wLength: 0
}

/**
 * @brief 激活接口 (Set Interface)
 * @note 用于在复合设备 (如带有多个备用设置的摄像头或声卡) 中切换接口的 Alternate Setting。
 */
static inline int32 usb_set_if(usb_dev_t *udev, uint8 if_num, uint8 alt_num) {
    // 🌟 发往指定 Interface 的标准控制请求
    uint8 req_type = USB_BM_REQ_TYPE(USB_REQ_DIR_OUT, USB_REQ_TYPE_STANDARD, USB_REQ_REC_INTERFACE);

    return usb_control_msg(udev, NULL,
                           req_type,
                           USB_REQ_SET_INTERFACE,
                           alt_num, // wValue: 备用设置号 (Alternate Setting)
                           if_num,  // wIndex: 接口号 (Interface Number)
                           0);      // wLength: 0
}
