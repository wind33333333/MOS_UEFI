#include "xhci.h"

#include <time.h>

#include "moslib.h"
#include "pcie.h"
#include "printk.h"
#include "slub.h"
#include "vmm.h"

//usb设备全局链
list_head_t usb_dev_list;

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
    disable_ioc = 0UL << 37,
    enable_ioc = 1UL << 37,
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

static inline void setup_stage_trb(trb_t *trb, setup_stage_receiver_e setup_stage_receiver,
                                   setup_stage_type_e setup_stage_type, setup_stage_dir_e setup_stage_dir,\
                                   setup_stage_req_e req, uint64 value, uint64 index, uint64 length,
                                   uint64 trb_tran_length, trb_trt_e trt) {
    trb->member0 = setup_stage_receiver | setup_stage_type | setup_stage_dir | req | (value << 16) | (index << 32) | (
                       length << 48);
    trb->member1 = (trb_tran_length << 0) | TRB_FLAG_IDT | TRB_TYPE_SETUP_STAGE | trt;
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
static inline void normal_transfer_trb(trb_t *trb, uint64 data_buff_ptr, config_ch_e ent_ch, uint64 trb_tran_length,
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


xhci_cap_t *xhci_cap_find(xhci_controller_t *xhci_reg, uint8 cap_id) {
    uint32 offset = xhci_reg->cap_reg->hccparams1 >> 16;
    while (offset) {
        xhci_cap_t *xhci_cap = (void *) xhci_reg->cap_reg + (offset << 2);
        if ((xhci_cap->cap_id & 0xFF) == cap_id) return xhci_cap;
        offset = (xhci_cap->next_ptr >> 8) & 0xFF;
    }
    return NULL;
}

//响铃
static inline void xhci_ring_doorbell(xhci_controller_t *xhci_controller, uint8 db_number, uint32 value) {
    xhci_controller->db_reg[db_number] = value;
}

//命令环/传输环入队列
int xhci_ring_enqueue(xhci_ring_t *ring, trb_t *trb) {
    if (ring->index >= TRB_COUNT - 1) {
        link_trb(&ring->ring_base[TRB_COUNT - 1], va_to_pa(ring->ring_base), ring->status_c);
        ring->index = 0;
        ring->status_c ^= TRB_FLAG_CYCLE;
    }

    ring->ring_base[ring->index].member0 = trb->member0;
    ring->ring_base[ring->index].member1 = trb->member1 | ring->status_c;
    ring->index++;
    return 0;
}

//事件环出队列
int xhci_ering_dequeue(xhci_controller_t *xhci_controller, trb_t *evt_trb) {
    xhci_ring_t *event_ring = &xhci_controller->event_ring;
    while ((event_ring->ring_base[event_ring->index].member1 & TRB_FLAG_CYCLE) == event_ring->status_c) {
        evt_trb->member0 = event_ring->ring_base[event_ring->index].member0;
        evt_trb->member1 = event_ring->ring_base[event_ring->index].member1;
        event_ring->index++;
        if (event_ring->index >= TRB_COUNT) {
            event_ring->index = 0;
            event_ring->status_c ^= TRB_FLAG_CYCLE;
        }
        xhci_controller->rt_reg->intr_regs[0].erdp =
                va_to_pa(&event_ring->ring_base[event_ring->index]) | XHCI_ERDP_EHB;
    }
    return 0;
}

//分配插槽
uint8 xhci_enable_slot(xhci_controller_t *xhci_controller) {
    trb_t trb;
    enable_slot_com_trb(&trb);
    xhci_ring_enqueue(&xhci_controller->cmd_ring, &trb);
    xhci_ring_doorbell(xhci_controller, 0, 0);
    timing();
    xhci_ering_dequeue(xhci_controller, &trb);
    if ((trb.member1 >> 42 & 0x3F) == 33 && trb.member1 >> 56) {
        return trb.member1 >> 56;
    }
    return -1;
}

//初始化环
static inline int xhci_ring_init(xhci_ring_t *ring, uint32 align_size) {
    ring->ring_base = kzalloc(align_up(TRB_COUNT * sizeof(trb_t), align_size));
    ring->index = 0;
    ring->status_c = TRB_FLAG_CYCLE;
}

typedef struct {
    uint32 reg0;
    uint32 reg1;
    uint32 reg2;
    uint32 reg3;
} xhci_slot_context_t;

//增加输入插槽上下文
void xhci_input_slot_context_add(xhci_input_context_t *input_ctx, uint32 ctx_size, xhci_slot_context_t *from_slot_ctx) {
    xhci_slot_context_t *to_slot_ctx = (xhci_slot_context_t *) ((uint64) input_ctx + ctx_size);
    to_slot_ctx->reg0 = from_slot_ctx->reg0;
    to_slot_ctx->reg1 = from_slot_ctx->reg1;
    to_slot_ctx->reg2 = from_slot_ctx->reg2;
    to_slot_ctx->reg3 = from_slot_ctx->reg3;
    input_ctx->input_ctx32.control.add_context |= 1;
}

typedef struct {
    uint32 reg0;
    uint32 reg1;
    uint64 reg2;
    uint32 reg3;
} xhci_endpoint_context_t;

//增加输入端点上下文
void xhci_input_endpoint_context_add(xhci_input_context_t *input_ctx, uint32 ctx_size, uint32 ep_num,
                                     xhci_endpoint_context_t *from_ep_ctx) {
    xhci_endpoint_context_t *to_ep_ctx = (xhci_endpoint_context_t *) ((uint64) input_ctx + ctx_size * (ep_num + 1));
    to_ep_ctx->reg0 = from_ep_ctx->reg0;
    to_ep_ctx->reg1 = from_ep_ctx->reg1;
    to_ep_ctx->reg2 = from_ep_ctx->reg2;
    to_ep_ctx->reg3 = from_ep_ctx->reg3;
    input_ctx->input_ctx32.control.add_context |= 1 << ep_num;
}

//读取插槽上下文
void xhci_slot_context_read(xhci_device_context_t *dev_context, xhci_slot_context_t *to_slot_ctx) {
    to_slot_ctx->reg0 = dev_context->dev_ctx32.slot.route_speed;
    to_slot_ctx->reg1 = dev_context->dev_ctx32.slot.latency_hub;
    to_slot_ctx->reg2 = dev_context->dev_ctx32.slot.parent_info;
    to_slot_ctx->reg3 = dev_context->dev_ctx32.slot.addr_status;
}

//读取端点上下文
void xhci_endpoint_context_read(xhci_device_context_t *dev_context, uint32 ctx_size, uint32 ep_num,
                                xhci_endpoint_context_t *to_ep_ctx) {
    xhci_endpoint_context_t *from_ep_ctx = (xhci_endpoint_context_t *) ((uint64) dev_context + ctx_size * (ep_num + 1));
    to_ep_ctx->reg0 = from_ep_ctx->reg0;
    to_ep_ctx->reg1 = from_ep_ctx->reg1;
    to_ep_ctx->reg2 = from_ep_ctx->reg2;
    to_ep_ctx->reg3 = from_ep_ctx->reg3;
}

//读取端点上下文

//设置设备地址
static inline void xhci_address_device(usb_dev_t *usb_dev) {
    xhci_controller_t *xhci_controller = usb_dev->xhci_controller;
    //分配设备插槽上下文内存
    usb_dev->dev_context = kzalloc(align_up(sizeof(xhci_device_context_t), xhci_controller->align_size));
    xhci_controller->dcbaap[usb_dev->slot_id] = va_to_pa(usb_dev->dev_context);
    //初始化控制
    xhci_ring_init(&usb_dev->control_ring, xhci_controller->align_size);
    //配置设备上下文
    xhci_input_context_t *input_ctx = kzalloc(align_up(sizeof(xhci_input_context_t), xhci_controller->align_size));
    xhci_slot_context_t slot_ctx;
    slot_ctx.reg0 = 1 << 27 | (xhci_controller->op_reg->portregs[usb_dev->port_id - 1].portsc & 0x3C00) << 10;
    slot_ctx.reg1 = usb_dev->port_id << 16;
    slot_ctx.reg2 = 0;
    slot_ctx.reg3 = 0;
    xhci_input_slot_context_add(input_ctx, xhci_controller->context_size, &slot_ctx); // 启用 Slot Context

    xhci_endpoint_context_t ep_ctx;
    ep_ctx.reg0 = 0;
    ep_ctx.reg1 = EP_TYPE_CONTROL | 8 << 16 | 3 << 1;
    ep_ctx.reg2 = va_to_pa(usb_dev->control_ring.ring_base) | 1;
    ep_ctx.reg3 = 0;
    xhci_input_endpoint_context_add(input_ctx, xhci_controller->context_size, 1, &ep_ctx); //Endpoint 0 Context

    trb_t trb;
    addr_dev_com_trb(&trb, va_to_pa(input_ctx), usb_dev->slot_id);
    xhci_ring_enqueue(&xhci_controller->cmd_ring, &trb);
    xhci_ring_doorbell(xhci_controller, 0, 0);
    timing();
    xhci_ering_dequeue(xhci_controller, &trb);
    kfree(input_ctx);
}

static inline uint32 get_endpoint_transfer_type(usb_endpoint_descriptor_t *endpoint_desc) {
    uint32 ep_type = 0;
    if (endpoint_desc->endpoint_address & 0x80) {
        switch (endpoint_desc->attributes) {
            case USB_EP_ISOCH:
                ep_type = EP_TYPE_ISOCH_IN;
                break;
            case USB_EP_BULK:
                ep_type = EP_TYPE_BULK_IN;
                break;
            case USB_EP_INTERRUPT:
                ep_type = EP_TYPE_INTERRUPT_IN;
        }
    } else {
        switch (endpoint_desc->attributes) {
            case USB_EP_ISOCH:
                ep_type = EP_TYPE_ISOCH_OUT;
                break;
            case USB_EP_BULK:
                ep_type = EP_TYPE_BULK_OUT;
                break;
            case USB_EP_INTERRUPT:
                ep_type = EP_TYPE_INTERRUPT_OUT;
        }
    }
    return ep_type;
}

//获取usb设备描述符
static inline int32 usb_get_device_descriptor(usb_dev_t *usb_dev) {
    xhci_controller_t *xhci_controller = usb_dev->xhci_controller;
    usb_device_descriptor_t *dev_desc = kzalloc(align_up(sizeof(usb_device_descriptor_t), 64));

    //第一次先获取设备描述符前8字节，拿到max_pack_size后更新端点1，再重新获取描述符。
    trb_t trb;
    // Setup TRB
    setup_stage_trb(&trb, setup_stage_device, setup_stage_norm, setup_stage_in, usb_req_get_descriptor, 0x100, 0, 8, 8,
                    in_data_stage);
    xhci_ring_enqueue(&usb_dev->control_ring, &trb);
    // Data TRB
    data_stage_trb(&trb, va_to_pa(dev_desc), 8, trb_in);
    xhci_ring_enqueue(&usb_dev->control_ring, &trb);
    // Status TRB
    status_stage_trb(&trb, enable_ioc, trb_out);
    xhci_ring_enqueue(&usb_dev->control_ring, &trb);

    xhci_ring_doorbell(xhci_controller, usb_dev->slot_id, 1);
    timing();
    xhci_ering_dequeue(xhci_controller, &trb);

    //更新端点0的最大包
    uint32 max_packe_size = dev_desc->usb_version >= 0x300
                                ? 1 << dev_desc->max_packet_size0
                                : dev_desc->max_packet_size0;
    xhci_input_context_t *input_ctx = kzalloc(align_up(sizeof(xhci_input_context_t), xhci_controller->align_size));
    xhci_endpoint_context_t ep_ctx;
    xhci_endpoint_context_read(usb_dev->dev_context, xhci_controller->context_size, 1, &ep_ctx);
    ep_ctx.reg1 = EP_TYPE_CONTROL | max_packe_size << 16;
    xhci_input_endpoint_context_add(input_ctx, xhci_controller->context_size, 1, &ep_ctx);
    evaluate_context_com_trb(&trb, va_to_pa(input_ctx), usb_dev->slot_id);
    xhci_ring_enqueue(&xhci_controller->cmd_ring, &trb);
    xhci_ring_doorbell(xhci_controller, 0, 0);
    timing();
    xhci_ering_dequeue(xhci_controller, &trb);
    kfree(input_ctx);

    //第二次获取整个设备描述符
    setup_stage_trb(&trb, setup_stage_device, setup_stage_norm, setup_stage_in, usb_req_get_descriptor, 0x100, 0, 18, 8,
                    in_data_stage);
    xhci_ring_enqueue(&usb_dev->control_ring, &trb);
    // Data TRB
    data_stage_trb(&trb, va_to_pa(dev_desc), 18, trb_in);
    xhci_ring_enqueue(&usb_dev->control_ring, &trb);
    // Status TRB
    status_stage_trb(&trb, enable_ioc, trb_out);
    xhci_ring_enqueue(&usb_dev->control_ring, &trb);

    xhci_ring_doorbell(xhci_controller, usb_dev->slot_id, 1);
    timing();
    xhci_ering_dequeue(xhci_controller, &trb);

    usb_dev->usb_ver = dev_desc->usb_version;
    usb_dev->vid = dev_desc->vendor_id;
    usb_dev->pid = dev_desc->product_id;
    usb_dev->dev_ver = dev_desc->device_version;

    kfree(dev_desc);
    return 0;
}

//获取usb配置描述符
static inline usb_config_descriptor_t *usb_get_config_descriptor(usb_dev_t *usb_dev) {
    xhci_controller_t *xhci_controller = usb_dev->xhci_controller;
    usb_config_descriptor_t *config_desc = kzalloc(align_up(sizeof(usb_config_descriptor_t), 64));

    //第一次先获取配置描述符前9字节
    trb_t trb;
    setup_stage_trb(&trb, setup_stage_device, setup_stage_norm, setup_stage_in, usb_req_get_descriptor, 0x200, 0, 9, 8,
                    in_data_stage);
    xhci_ring_enqueue(&usb_dev->control_ring, &trb);
    // Data TRB
    data_stage_trb(&trb, va_to_pa(config_desc), 9, trb_in);
    xhci_ring_enqueue(&usb_dev->control_ring, &trb);
    // Status TRB
    status_stage_trb(&trb, enable_ioc, trb_out);
    xhci_ring_enqueue(&usb_dev->control_ring, &trb);

    // 响铃
    xhci_ring_doorbell(xhci_controller, usb_dev->slot_id, 1);
    timing();
    xhci_ering_dequeue(xhci_controller, &trb);

    //第二次从配置描述符中得到总长度获取整个配置描述符
    uint16 config_desc_length = config_desc->total_length;
    kfree(config_desc);
    config_desc = kzalloc(align_up(config_desc_length, 64));

    setup_stage_trb(&trb, setup_stage_device, setup_stage_norm, setup_stage_in, usb_req_get_descriptor, 0x200, 0,
                    config_desc_length, 8, in_data_stage);
    xhci_ring_enqueue(&usb_dev->control_ring, &trb);
    // Data TRB
    data_stage_trb(&trb, va_to_pa(config_desc), config_desc_length, trb_in);
    xhci_ring_enqueue(&usb_dev->control_ring, &trb);
    // Status TRB
    status_stage_trb(&trb, enable_ioc, trb_out);
    xhci_ring_enqueue(&usb_dev->control_ring, &trb);

    // 响铃
    xhci_ring_doorbell(xhci_controller, usb_dev->slot_id, 1);
    timing();
    xhci_ering_dequeue(xhci_controller, &trb);

    return config_desc;
}

//激活usb配置
int usb_set_config(usb_dev_t *usb_dev, uint8 config_value) {
    xhci_controller_t *xhci_controller = usb_dev->xhci_controller;
    trb_t trb;

    setup_stage_trb(&trb, setup_stage_device, setup_stage_norm, setup_stage_out, usb_req_set_config,
                    config_value, 0, 0, 8, no_data_stage);
    xhci_ring_enqueue(&usb_dev->control_ring, &trb);

    status_stage_trb(&trb, enable_ioc, trb_in);
    xhci_ring_enqueue(&usb_dev->control_ring, &trb);

    xhci_ring_doorbell(xhci_controller, usb_dev->slot_id, 1);
    timing();
    xhci_ering_dequeue(xhci_controller, &trb);
    return 0;
}

//设置备用设置
int usb_set_interface(usb_dev_t *usb_dev, int64 if_num, int64 alt_num) {
    xhci_controller_t *xhci_controller = usb_dev->xhci_controller;
    trb_t trb;

    setup_stage_trb(&trb, setup_stage_interface, setup_stage_norm, setup_stage_out, usb_req_set_interface,
                    alt_num, if_num, 0, 8, no_data_stage);
    xhci_ring_enqueue(&usb_dev->control_ring, &trb);

    status_stage_trb(&trb, enable_ioc, trb_in);
    xhci_ring_enqueue(&usb_dev->control_ring, &trb);

    xhci_ring_doorbell(xhci_controller, usb_dev->slot_id, 1);
    timing();
    xhci_ering_dequeue(xhci_controller, &trb);
    return 0;
}

//获取下一个描述符
static inline void *get_next_desc(usb_config_descriptor_t *config_desc) {
    return (void *)((uint64)config_desc + config_desc->length);
}

//usb驱动链表
list_head_t usb_driver_list;

//usb驱动结构
typedef struct {
    const char *name;
    uint8 class;
    uint8 subclass;
    int32 (*usb_init)(usb_dev_t *usb_dev, usb_interface_descriptor_t *interface_desc, void *desc_end);
    list_head_t list;
} usb_driver_t;

//注册usb驱动
static inline int32 usb_driver_register(usb_driver_t *usb_driver) {
    list_add_head(&usb_driver_list, &usb_driver->list);
}

//加载usb驱动
static inline int32 adaptation_driver(usb_dev_t *usb_dev, usb_config_descriptor_t *config_desc) {
    if (config_desc->descriptor_type != USB_DESC_TYPE_CONFIGURATION) return -1;

    if (usb_dev->interfaces_count == 0 || usb_dev->interfaces == NULL) {
        usb_dev->interfaces_count = config_desc->num_interfaces;
        usb_dev->interfaces = kzalloc(usb_dev->interfaces_count * 8);
    }

    usb_interface_descriptor_t *interface_desc = (usb_interface_descriptor_t *) config_desc;
    void *desc_end = (usb_config_descriptor_t *) ((uint64) config_desc + config_desc->total_length);
    while (interface_desc < desc_end) {
        interface_desc = get_next_desc(interface_desc);
        if (interface_desc->descriptor_type == USB_DESC_TYPE_INTERFACE && interface_desc->alternate_setting == 0) {
            list_head_t *next = usb_driver_list.next;
            while (next != &usb_driver_list) {
                usb_driver_t *usb_driver = CONTAINER_OF(next, usb_driver_t, list);
                if (interface_desc->interface_class == usb_driver->class && interface_desc->interface_subclass ==
                    usb_driver->subclass) {
                    usb_driver->usb_init(usb_dev, interface_desc, desc_end); //调用驱动初始化usb设备接口
                }
                next = next->next;
            }
        }
        interface_desc = (usb_interface_descriptor_t *) ((uint64) interface_desc + interface_desc->length);
    }
}

//测试逻辑单元是否有效
static inline boolean bot_msc_test_lun(xhci_controller_t *xhci_controller, usb_dev_t *usb_dev, usb_bot_msc_t *bot_msc,
                                       uint8 lun_id) {
    //测试状态检测3次不成功则视为无效逻辑单元
    boolean flags = FALSE;
    usb_cbw_t *cbw = kzalloc(align_up(sizeof(usb_cbw_t), 64));
    usb_csw_t *csw = kzalloc(align_up(sizeof(usb_csw_t), 64));
    trb_t trb;
    for (uint8 j = 0; j < 3; j++) {
        mem_set(csw, 0, sizeof(usb_csw_t));
        mem_set(cbw, 0, sizeof(usb_cbw_t));
        cbw->cbw_signature = 0x43425355; // 'USBC'
        cbw->cbw_tag = ++bot_msc->tag; // 唯一标签（示例）
        cbw->cbw_data_transfer_length = 0;
        cbw->cbw_flags = 0;
        cbw->cbw_lun = lun_id;
        cbw->cbw_cb_length = 6;

        // 1. 发送 CBW（批量 OUT 端点）
        normal_transfer_trb(&trb, va_to_pa(cbw), disable_ch, sizeof(usb_cbw_t), disable_ioc);
        xhci_ring_enqueue(&bot_msc->out_ep.transfer_ring, &trb);
        // 3. 接收 CSW（批量 IN 端点）
        normal_transfer_trb(&trb, va_to_pa(csw), disable_ch, sizeof(usb_csw_t), enable_ioc);
        xhci_ring_enqueue(&bot_msc->in_ep.transfer_ring, &trb);

        xhci_ring_doorbell(xhci_controller, usb_dev->slot_id, bot_msc->out_ep.ep_num);
        xhci_ring_doorbell(xhci_controller, usb_dev->slot_id, bot_msc->in_ep.ep_num);
        timing();
        xhci_ering_dequeue(xhci_controller, &trb);

        if (!csw->csw_status) {
            flags = TRUE;
            break;
        }
    }
    kfree(cbw);
    kfree(csw);
    return flags;
}

//获取最大逻辑单元
static inline uint8 bot_msc_read_max_lun(xhci_controller_t *xhci_controller, usb_dev_t *usb_dev,
                                         usb_bot_msc_t *bot_msc) {
    trb_t trb;
    setup_stage_trb(&trb, setup_stage_interface, setup_stage_calss, setup_stage_in, usb_req_get_max_lun, 0, 0,
                    bot_msc->interface_num, 8,
                    in_data_stage);
    xhci_ring_enqueue(&usb_dev->control_ring, &trb);

    uint8 *max_lun = kzalloc(64);
    data_stage_trb(&trb, va_to_pa(max_lun), 1, trb_in);
    xhci_ring_enqueue(&usb_dev->control_ring, &trb);

    status_stage_trb(&trb, enable_ioc, trb_out);
    xhci_ring_enqueue(&usb_dev->control_ring, &trb);

    xhci_ring_doorbell(xhci_controller, usb_dev->slot_id, 1);
    timing();
    xhci_ering_dequeue(xhci_controller, &trb);

    uint8 lun_count = ++*max_lun;
    kfree(max_lun);
    return lun_count;
}

//获取u盘厂商信息
static inline uint8 bot_msc_read_vid(xhci_controller_t *xhci_controller, usb_dev_t *usb_dev, usb_bot_msc_t *bot_msc,
                                     uint8 lun_id) {
    usb_lun_t *lun = &bot_msc->lun[lun_id];
    usb_cbw_t *cbw = kzalloc(align_up(sizeof(usb_cbw_t), 64));
    usb_csw_t *csw = kzalloc(align_up(sizeof(usb_csw_t), 64));
    trb_t trb;
    cbw->cbw_signature = 0x43425355; // 'USBC'
    cbw->cbw_tag = ++bot_msc->tag; // 唯一标签（示例）
    cbw->cbw_data_transfer_length = sizeof(inquiry_data_t);
    cbw->cbw_flags = 0x80; // IN 方向
    cbw->cbw_lun = lun->lun_id; // 逻辑单元号
    cbw->cbw_cb_length = 6; //
    cbw->cbw_cb[0] = 0x12;
    cbw->cbw_cb[4] = sizeof(inquiry_data_t);

    // 1. 发送 CBW（批量 OUT 端点)
    normal_transfer_trb(&trb, va_to_pa(cbw), disable_ch, sizeof(usb_cbw_t), disable_ioc);
    xhci_ring_enqueue(&bot_msc->out_ep.transfer_ring, &trb);
    //2. 接收数据（批量 IN 端点）
    inquiry_data_t *inquiry_data = kzalloc(align_up(sizeof(inquiry_data_t), 64));
    normal_transfer_trb(&trb, va_to_pa(inquiry_data), enable_ch, sizeof(inquiry_data_t), disable_ioc);
    xhci_ring_enqueue(&bot_msc->in_ep.transfer_ring, &trb);
    // 3. 接收 CSW（批量 IN 端点）
    normal_transfer_trb(&trb, va_to_pa(csw), disable_ch, sizeof(usb_csw_t), enable_ioc);
    xhci_ring_enqueue(&bot_msc->in_ep.transfer_ring, &trb);

    xhci_ring_doorbell(xhci_controller, usb_dev->slot_id, bot_msc->out_ep.ep_num);
    xhci_ring_doorbell(xhci_controller, usb_dev->slot_id, bot_msc->in_ep.ep_num);
    timing();
    xhci_ering_dequeue(xhci_controller, &trb);

    mem_cpy(&inquiry_data->vendor_id, &lun->vid, 24);
    lun->vid[24] = 0;

    color_printk(GREEN,BLACK, "scsi-version:%d    \n", inquiry_data->version);

    kfree(cbw);
    kfree(csw);
    kfree(inquiry_data);
    return 0;
}

//获取u盘容量信息
static inline uint8 bot_msc_read_capacity(xhci_controller_t *xhci_controller, usb_dev_t *usb_dev,
                                          usb_bot_msc_t *bot_msc, uint8 lun_id) {
    usb_lun_t *lun = &bot_msc->lun[lun_id];
    usb_cbw_t *cbw = kzalloc(align_up(sizeof(usb_cbw_t), 64));
    usb_csw_t *csw = kzalloc(align_up(sizeof(usb_csw_t), 64));
    trb_t trb;

    cbw->cbw_signature = 0x43425355; // 'USBC'
    cbw->cbw_tag = ++bot_msc->tag; // 唯一标签（示例）
    cbw->cbw_data_transfer_length = 32; // READ CAPACITY (16) 返回32 字节
    cbw->cbw_flags = 0x80; // IN 方向
    cbw->cbw_lun = lun->lun_id; // 逻辑单元号
    cbw->cbw_cb_length = 16; // READ CAPACITY (16) 命令长度

    //填充 SCSI READ CAPACITY (16) 命令
    cbw->cbw_cb[0] = 0x9E; // 操作码：READ CAPACITY (16)
    cbw->cbw_cb[1] = 0x10; // 服务动作：0x10
    cbw->cbw_cb[13] = 32; // 分配长度低字节（32 字节）

    // 1. 发送 CBW（批量 OUT 端点）
    normal_transfer_trb(&trb, va_to_pa(cbw), disable_ch, sizeof(usb_cbw_t), disable_ioc);
    xhci_ring_enqueue(&bot_msc->out_ep.transfer_ring, &trb);
    //2. 接收数据（批量 IN 端点
    read_capacity_16_t *capacity_data = kzalloc(align_up(sizeof(read_capacity_16_t), 64));
    normal_transfer_trb(&trb, va_to_pa(capacity_data), enable_ch, sizeof(read_capacity_16_t), disable_ioc);
    xhci_ring_enqueue(&bot_msc->in_ep.transfer_ring, &trb);
    // 3. 接收 CSW（批量 IN 端点
    normal_transfer_trb(&trb, va_to_pa(csw), disable_ch, sizeof(usb_csw_t), enable_ioc);
    xhci_ring_enqueue(&bot_msc->in_ep.transfer_ring, &trb);

    xhci_ring_doorbell(xhci_controller, usb_dev->slot_id, bot_msc->out_ep.ep_num);
    xhci_ring_doorbell(xhci_controller, usb_dev->slot_id, bot_msc->in_ep.ep_num);
    timing();
    xhci_ering_dequeue(xhci_controller, &trb);

    lun->block_count = bswap64(capacity_data->last_lba) + 1;
    lun->block_size = bswap32(capacity_data->block_size);

    kfree(cbw);
    kfree(csw);
    return 0;
}

//读u盘
uint8 bot_scsi_read16(xhci_controller_t *xhci_controller, usb_dev_t *usb_dev, usb_bot_msc_t *bot_msc, uint8 lun_id,
                      uint64 lba, uint32 block_count, uint32 block_size, void *buf) {
    usb_lun_t *lun = &bot_msc->lun[lun_id];
    usb_cbw_t *cbw = kzalloc(align_up(sizeof(usb_cbw_t), 64));
    usb_csw_t *csw = kzalloc(align_up(sizeof(usb_csw_t), 64));
    trb_t trb;

    cbw->cbw_signature = 0x43425355; // 'USBC'
    cbw->cbw_tag = ++bot_msc->tag; // 唯一标签（示例）
    cbw->cbw_data_transfer_length = block_count * block_size;
    cbw->cbw_flags = 0x80; // IN 方向
    cbw->cbw_lun = lun->lun_id; // 逻辑单元号
    cbw->cbw_cb_length = 16; // READ CAPACITY (16) 命令长度
    // 构造 READ(16) 命令块
    cbw->cbw_cb[0] = 0x88; //READ(16)
    *(uint64 *) &cbw->cbw_cb[2] = bswap64(lba);
    *(uint32 *) &cbw->cbw_cb[10] = bswap32(block_count);

    // 1. 发送 CBW（批量 OUT 端点）
    normal_transfer_trb(&trb, va_to_pa(cbw), disable_ch, sizeof(usb_cbw_t), disable_ioc);
    xhci_ring_enqueue(&bot_msc->out_ep.transfer_ring, &trb);

    //2. 接收数据（批量 IN 端点
    normal_transfer_trb(&trb, va_to_pa(buf), enable_ch, block_count * block_size, disable_ioc);
    xhci_ring_enqueue(&bot_msc->in_ep.transfer_ring, &trb);

    // 3. 接收 CSW（批量 IN 端点
    normal_transfer_trb(&trb, va_to_pa(csw), disable_ch, sizeof(usb_csw_t), enable_ioc);
    xhci_ring_enqueue(&bot_msc->in_ep.transfer_ring, &trb);

    xhci_ring_doorbell(xhci_controller, usb_dev->slot_id, bot_msc->out_ep.ep_num);
    xhci_ring_doorbell(xhci_controller, usb_dev->slot_id, bot_msc->in_ep.ep_num);
    timing();
    xhci_ering_dequeue(xhci_controller, &trb);
    color_printk(GREEN,BLACK, "read16 m1:%#lx m2:%#lx   \n", trb.member0, trb.member1);

    kfree(cbw);
    kfree(csw);
    return 0;
}

uint8 bot_scsi_write16(xhci_controller_t *xhci_controller, usb_dev_t *usb_dev, usb_bot_msc_t *bot_msc, uint8 lun_id,
                       uint64 lba, uint32 block_count, uint32 block_size, void *buf) {
    usb_lun_t *lun = &bot_msc->lun[lun_id];
    usb_cbw_t *cbw = kzalloc(align_up(sizeof(usb_cbw_t), 64));
    usb_csw_t *csw = kzalloc(align_up(sizeof(usb_csw_t), 64));
    trb_t trb;

    cbw->cbw_signature = 0x43425355; // 'USBC'
    cbw->cbw_tag = ++bot_msc->tag; // 唯一标签（示例）
    cbw->cbw_data_transfer_length = block_count * block_size; // READ CAPACITY (16) 返回32 字节
    cbw->cbw_flags = 0x00; // OUT方向（主机->设备）
    cbw->cbw_lun = lun->lun_id; // 逻辑单元号
    cbw->cbw_cb_length = 16; // READ CAPACITY (16) 命令长度
    // 构造 write(16) 命令块
    cbw->cbw_cb[0] = 0x8A; //write(16)
    *(uint64 *) &cbw->cbw_cb[2] = bswap64(lba);
    *(uint32 *) &cbw->cbw_cb[10] = bswap32(block_count);

    // 1. 发送 CBW（批量 OUT 端点）
    normal_transfer_trb(&trb, va_to_pa(cbw), disable_ch, sizeof(usb_cbw_t), disable_ioc);
    xhci_ring_enqueue(&bot_msc->out_ep.transfer_ring, &trb);

    //2. 发送数据（批量 OUT 端点）
    normal_transfer_trb(&trb, va_to_pa(buf), enable_ch, block_count * block_size, disable_ioc);
    xhci_ring_enqueue(&bot_msc->out_ep.transfer_ring, &trb);

    // 3. 接收 CSW（批量 IN 端点
    normal_transfer_trb(&trb, va_to_pa(csw), disable_ch, sizeof(usb_csw_t), enable_ioc);
    xhci_ring_enqueue(&bot_msc->in_ep.transfer_ring, &trb);

    xhci_ring_doorbell(xhci_controller, usb_dev->slot_id, bot_msc->out_ep.ep_num);
    xhci_ring_doorbell(xhci_controller, usb_dev->slot_id, bot_msc->in_ep.ep_num);
    timing();
    xhci_ering_dequeue(xhci_controller, &trb);
    color_printk(GREEN,BLACK, "wirte16 m1:%#lx m2:%#lx   \n", trb.member0, trb.member1);

    kfree(cbw);
    kfree(csw);
    return 0;
}

uint8 bot_scsi_read10(xhci_controller_t *xhci_controller,
                      usb_dev_t *usb_dev,
                      usb_bot_msc_t *bot_msc,
                      uint8 lun_id,
                      uint32 lba,
                      uint16 block_count,
                      uint32 block_size,
                      void *buf) {
    usb_lun_t *lun = &bot_msc->lun[lun_id];

    usb_cbw_t *cbw = kzalloc(align_up(sizeof(usb_cbw_t), 64));
    usb_csw_t *csw = kzalloc(align_up(sizeof(usb_csw_t), 64));
    trb_t trb;

    cbw->cbw_signature = 0x43425355; // "USBC"
    cbw->cbw_tag = ++bot_msc->tag;
    cbw->cbw_data_transfer_length = block_count * block_size;
    cbw->cbw_flags = 0x80; // IN 方向
    cbw->cbw_lun = lun->lun_id;
    cbw->cbw_cb_length = 10; // READ(10) 长度

    // READ(10) 命令格式
    cbw->cbw_cb[0] = 0x28; // 操作码：READ(10)
    *(uint32 *) &cbw->cbw_cb[2] = bswap32(lba);
    *(uint16 *) &cbw->cbw_cb[7] = bswap16(block_count); // 要读的块数

    // 1. 发送 CBW（Bulk OUT）
    normal_transfer_trb(&trb, va_to_pa(cbw), disable_ch, sizeof(usb_cbw_t), disable_ioc);
    xhci_ring_enqueue(&bot_msc->out_ep.transfer_ring, &trb);

    // 2. 接收数据（Bulk IN）
    normal_transfer_trb(&trb, va_to_pa(buf), enable_ch, block_count * block_size, disable_ioc);
    xhci_ring_enqueue(&bot_msc->in_ep.transfer_ring, &trb);

    // 3. 接收 CSW（Bulk IN）
    normal_transfer_trb(&trb, va_to_pa(csw), disable_ch, sizeof(usb_csw_t), enable_ioc);
    xhci_ring_enqueue(&bot_msc->in_ep.transfer_ring, &trb);

    // Doorbell
    xhci_ring_doorbell(xhci_controller, usb_dev->slot_id, bot_msc->out_ep.ep_num);
    xhci_ring_doorbell(xhci_controller, usb_dev->slot_id, bot_msc->in_ep.ep_num);

    timing();
    xhci_ering_dequeue(xhci_controller, &trb);
    color_printk(GREEN,BLACK, "read10 m1:%#lx m2:%#lx   \n", trb.member0, trb.member1);

    kfree(cbw);
    kfree(csw);
    return 0;
}

uint8 bot_scsi_write10(xhci_controller_t *xhci_controller,
                       usb_dev_t *usb_dev,
                       usb_bot_msc_t *bot_msc,
                       uint8 lun_id,
                       uint32 lba,
                       uint16 block_count,
                       uint32 block_size,
                       void *buf) {
    usb_lun_t *lun = &bot_msc->lun[lun_id];

    usb_cbw_t *cbw = kzalloc(align_up(sizeof(usb_cbw_t), 64));
    usb_csw_t *csw = kzalloc(align_up(sizeof(usb_csw_t), 64));
    trb_t trb;

    cbw->cbw_signature = 0x43425355; // "USBC"
    cbw->cbw_tag = ++bot_msc->tag;
    cbw->cbw_data_transfer_length = block_count * block_size;
    cbw->cbw_flags = 0x00; // OUT 方向
    cbw->cbw_lun = lun->lun_id;
    cbw->cbw_cb_length = 10; // WRITE(10)

    // === 构造 WRITE(10) 命令块 ===
    cbw->cbw_cb[0] = 0x2A; // 操作码：READ(10)
    *(uint32 *) &cbw->cbw_cb[2] = bswap32(lba);
    *(uint16 *) &cbw->cbw_cb[7] = bswap16(block_count); // 要读的块数

    // 1. 发送 CBW（Bulk OUT）
    normal_transfer_trb(&trb, va_to_pa(cbw), disable_ch, sizeof(usb_cbw_t), disable_ioc);
    xhci_ring_enqueue(&bot_msc->out_ep.transfer_ring, &trb);

    // 2. 发送数据（Bulk OUT）
    normal_transfer_trb(&trb, va_to_pa(buf), enable_ch, block_count * block_size, disable_ioc);
    xhci_ring_enqueue(&bot_msc->out_ep.transfer_ring, &trb);

    // 3. 接收 CSW（Bulk IN）
    normal_transfer_trb(&trb, va_to_pa(csw), disable_ch, sizeof(usb_csw_t), enable_ioc);
    xhci_ring_enqueue(&bot_msc->in_ep.transfer_ring, &trb);

    // Doorbell
    xhci_ring_doorbell(xhci_controller, usb_dev->slot_id, bot_msc->out_ep.ep_num);
    xhci_ring_doorbell(xhci_controller, usb_dev->slot_id, bot_msc->in_ep.ep_num);

    timing();
    xhci_ering_dequeue(xhci_controller, &trb);
    color_printk(GREEN,BLACK, "wirte10 m1:%#lx m2:%#lx   \n", trb.member0, trb.member1);

    kfree(cbw);
    kfree(csw);
    return 0;
}

//获取u盘信息（u盘品牌,容量等）bot 协议u盘
void bot_get_msc_info(usb_dev_t *usb_dev, usb_bot_msc_t *bot_msc) {
    xhci_controller_t *xhci_controller = usb_dev->xhci_controller;
    //获取最大逻辑单元
    bot_msc->lun_count = bot_msc_read_max_lun(xhci_controller, usb_dev,bot_msc);
    bot_msc->lun = kzalloc(bot_msc->lun_count*8);
    //枚举逻辑单元
    for (uint8 i = 0; i < bot_msc->lun_count; i++) {
        bot_msc->lun[i].lun_id = i;
        if (bot_msc_test_lun(xhci_controller, usb_dev,bot_msc, i) == FALSE) break; //测试逻辑单元是否有效
        bot_msc_read_vid(xhci_controller, usb_dev,bot_msc, i); //获取u盘厂商信息
        bot_msc_read_capacity(xhci_controller, usb_dev,bot_msc, i); //获取u盘容量

        uint64 *write = kzalloc(4096);
        mem_set(write, 0x23, 4096);

        uint64 *buf = kzalloc(4096);
        bot_scsi_read10(xhci_controller, usb_dev,bot_msc, i, 0, 2, bot_msc->lun[i].block_size, buf);

        color_printk(BLUE,BLACK, "buf:");
        for (uint32 i = 0; i < 100; i++) {
            color_printk(BLUE,BLACK, "%#lx", buf[i]);
        }
        color_printk(BLUE,BLACK, "\n");
        color_printk(GREEN,BLACK, "vid:%#x pid:%#x mode:%s block_num:%#lx block_size:%#x    \n", usb_dev->vid,
                     usb_dev->pid,
                     bot_msc->lun[i].vid, bot_msc->lun[i].block_count, bot_msc->lun[i].block_size);
    }
}

//u盘驱动程序
int32 mass_storage_probe(usb_dev_t *usb_dev, usb_interface_descriptor_t *interface_desc, void *desc_end) {
    xhci_controller_t *xhci_controller = usb_dev->xhci_controller;
    xhci_input_context_t *input_ctx = kzalloc(align_up(sizeof(xhci_input_context_t), xhci_controller->align_size));
    xhci_slot_context_t slot_ctx;
    xhci_endpoint_context_t ep_ctx;
    trb_t trb;

    //检测接口是否支持uasp协议
    usb_interface_descriptor_t *uas_if_desc = 0;
    usb_interface_descriptor_t *next_if_desc = interface_desc;
    while (next_if_desc < desc_end) {
        if (next_if_desc->descriptor_type == USB_DESC_TYPE_INTERFACE && next_if_desc->interface_class == 0x8 &&
            next_if_desc->interface_subclass == 0x6 && next_if_desc->interface_protocol == 0x62) {
            uas_if_desc = next_if_desc;
        }
        next_if_desc = get_next_desc(next_if_desc);
    }

    if (uas_if_desc) {
        //uas协议初始化流程
        usb_uas_msc_t *uas_msc = kzalloc(sizeof(usb_uas_msc_t));
        usb_dev->interfaces = uas_msc;
        usb_dev->interfaces_count = 1;
    } else {
        //bot协议初始化流程
        usb_set_interface(usb_dev,interface_desc->interface_number,interface_desc->alternate_setting);
        usb_bot_msc_t *bot_msc = kzalloc(sizeof(usb_bot_msc_t));
        bot_msc->usb_dev = usb_dev;
        bot_msc->interface_num = interface_desc->interface_number;
        usb_dev->interfaces = bot_msc;
        usb_dev->interfaces_count = 1;
        usb_endpoint_descriptor_t *endpoint_desc = (usb_endpoint_descriptor_t *) interface_desc;
        uint32 context_entries = 0;
        for (uint8 i = 0; i < 2; i++) {
            endpoint_desc = get_next_desc(endpoint_desc);
            usb_endpoint_t *endpoint = endpoint_desc->endpoint_address & 0x80 ? &bot_msc->in_ep : &bot_msc->out_ep;
            endpoint->ep_num = (endpoint_desc->endpoint_address & 0xF) << 1 | endpoint_desc->endpoint_address >> 7;
            context_entries = endpoint->ep_num;
            xhci_ring_init(&endpoint->transfer_ring, xhci_controller->align_size); //初始化端点传输环
            uint32 ep_transfer_type = get_endpoint_transfer_type(endpoint_desc);
            uint32 max_burst = 0;
            if (usb_dev->usb_ver >= 0x300) {
                usb_ss_ep_comp_descriptor_t *ss_ep_comp_desc = get_next_desc(endpoint_desc);
                endpoint_desc = (usb_endpoint_descriptor_t *) ss_ep_comp_desc;
                max_burst = ss_ep_comp_desc->max_burst;
            }
            //获取端点类型
            //增加端点
            ep_ctx.reg0 = 0;
            ep_ctx.reg1 = ep_transfer_type | endpoint_desc->max_packet_size << 16 | max_burst << 8 | 3 << 1;
            ep_ctx.reg2 = va_to_pa(endpoint->transfer_ring.ring_base) | 1;
            ep_ctx.reg3 = 0;
            xhci_input_endpoint_context_add(input_ctx, xhci_controller->context_size, endpoint->ep_num, &ep_ctx);
        }
        //更新slot
        slot_ctx.reg0 = context_entries << 27 | (
                            (usb_dev->xhci_controller->op_reg->portregs[usb_dev->port_id - 1].portsc &
                             0x3C00) << 10);
        slot_ctx.reg1 = usb_dev->port_id << 16;
        slot_ctx.reg2 = 0;
        slot_ctx.reg3 = 0;
        xhci_input_slot_context_add(input_ctx, xhci_controller->context_size, &slot_ctx);

        config_endpoint_com_trb(&trb, va_to_pa(input_ctx), usb_dev->slot_id);
        xhci_ring_enqueue(&xhci_controller->cmd_ring, &trb);
        xhci_ring_doorbell(xhci_controller, 0, 0);
        timing();
        xhci_ering_dequeue(xhci_controller, &trb);



        //获取u盘基本信息
        bot_get_msc_info(usb_dev,bot_msc);
    }
    kfree(input_ctx);
}


//创建usb设备
usb_dev_t *create_usb_dev(xhci_controller_t *xhci_controller, uint32 port_id) {

    list_head_init(&usb_driver_list);
    usb_driver_t* msc_driver = kzalloc(sizeof(usb_driver_t));
    msc_driver->name = "msc-driver";
    msc_driver->class = 0x8;
    msc_driver->subclass = 0x6;
    msc_driver->usb_init = mass_storage_probe;
    usb_driver_register(msc_driver);

    usb_dev_t *usb_dev = kzalloc(sizeof(usb_dev_t));
    usb_dev->xhci_controller = xhci_controller;
    usb_dev->port_id = port_id + 1;
    usb_dev->slot_id = xhci_enable_slot(xhci_controller); //启用插槽
    xhci_address_device(usb_dev); //设置设备地址
    usb_get_device_descriptor(usb_dev); //获取设备描述符
    usb_config_descriptor_t *config_desc = usb_get_config_descriptor(usb_dev); //获取配置描述符
    usb_set_config(usb_dev, config_desc->configuration_value); //激活配置
    adaptation_driver(usb_dev, config_desc); //适配驱动
    list_add_head(&usb_dev_list, &usb_dev->list);
    kfree(config_desc);
}

//枚举usb设备
void usb_dev_enum(xhci_controller_t *xhci_controller) {
    trb_t trb;
    for (uint32 i = 0; i < xhci_controller->cap_reg->hcsparams1 >> 24; i++) {
        if (xhci_controller->op_reg->portregs[i].portsc & XHCI_PORTSC_CCS) {
            if ((xhci_controller->op_reg->portregs[i].portsc >> XHCI_PORTSC_PLS_SHIFT & XHCI_PORTSC_PLS_MASK) ==
                XHCI_PLS_POLLING) {
                //usb2.0协议版本
                xhci_controller->op_reg->portregs[i].portsc |= XHCI_PORTSC_PR;
                timing();
                xhci_ering_dequeue(xhci_controller, &trb);
            }
            //usb3.x以上协议版本
            while (!(xhci_controller->op_reg->portregs[i].portsc & XHCI_PORTSC_PED)) pause();
            create_usb_dev(xhci_controller, i);
        }
    }
}

INIT_TEXT void init_xhci(void) {
    pcie_dev_t *xhci_dev = pcie_dev_find(XHCI_CLASS_CODE); //查找xhci设备
    pcie_bar_set(xhci_dev, 0); //初始化bar0寄存器
    pcie_msi_intrpt_set(xhci_dev); //初始化xhci msi中断
    xhci_dev->private = kzalloc(sizeof(xhci_controller_t)); //设备私有数据空间申请一块内存，存放xhci相关信息

    xhci_controller_t *xhci_controller = xhci_dev->private;
    xhci_controller->pcie_dev = xhci_dev;

    /*初始化xhci寄存器*/
    xhci_controller->cap_reg = xhci_dev->bar[0]; //xhci能力寄存器基地址
    xhci_controller->op_reg = xhci_dev->bar[0] + xhci_controller->cap_reg->cap_length; //xhci操作寄存器基地址
    xhci_controller->rt_reg = xhci_dev->bar[0] + xhci_controller->cap_reg->rtsoff; //xhci运行时寄存器基地址
    xhci_controller->db_reg = xhci_dev->bar[0] + xhci_controller->cap_reg->dboff; //xhci门铃寄存器基地址

    /*停止复位xhci*/
    xhci_controller->op_reg->usbcmd &= ~XHCI_CMD_RS; //停止xhci
    while (!(xhci_controller->op_reg->usbsts & XHCI_STS_HCH)) pause();
    xhci_controller->op_reg->usbcmd |= XHCI_CMD_HCRST; //复位xhci
    while (xhci_controller->op_reg->usbcmd & XHCI_CMD_HCRST) pause();
    while (xhci_controller->op_reg->usbsts & XHCI_STS_CNR) pause();

    /*计算xhci内存对齐边界*/
    xhci_controller->align_size = PAGE_4K_SIZE << bsf(xhci_controller->op_reg->pagesize);

    /*设备上下文字节数*/
    xhci_controller->context_size = 32 << ((xhci_controller->cap_reg->hccparams1 & HCCP1_CSZ) >> 2);

    /*初始化设备上下文*/
    uint32 max_slots = xhci_controller->cap_reg->hcsparams1 & 0xff;
    xhci_controller->dcbaap = kzalloc(align_up((max_slots + 1) << 3, xhci_controller->align_size));
    //分配设备上下文插槽内存,最大插槽数量(插槽从1开始需要+1)*8字节内存
    xhci_controller->op_reg->dcbaap = va_to_pa(xhci_controller->dcbaap); //把设备上下文基地址数组表的物理地址写入寄存器
    xhci_controller->op_reg->config = max_slots; //把最大插槽数量写入寄存器

    /*初始化命令环*/
    xhci_ring_init(&xhci_controller->cmd_ring, xhci_controller->align_size);
    xhci_controller->op_reg->crcr = va_to_pa(xhci_controller->cmd_ring.ring_base) | 1; //命令环物理地址写入crcr寄存器，置位rcs

    /*初始化事件环*/
    xhci_ring_init(&xhci_controller->event_ring, xhci_controller->align_size);
    xhci_erst_t *erstba = kmalloc(align_up(sizeof(xhci_erst_t), xhci_controller->align_size)); //分配单事件环段表内存64字节
    erstba->ring_seg_base = va_to_pa(xhci_controller->event_ring.ring_base); //段表中写入事件环物理地址
    erstba->ring_seg_size = TRB_COUNT; //事件环最大trb个数
    erstba->reserved = 0;
    xhci_controller->rt_reg->intr_regs[0].erstsz = 1; //设置单事件环段
    xhci_controller->rt_reg->intr_regs[0].erstba = va_to_pa(erstba); //事件环段表物理地址写入寄存器
    xhci_controller->rt_reg->intr_regs[0].erdp = va_to_pa(xhci_controller->event_ring.ring_base); //事件环物理地址写入寄存器

    /*初始化暂存器缓冲区*/
    uint32 spb_number = (xhci_controller->cap_reg->hcsparams2 & 0x1f << 21) >> 16 | xhci_controller->cap_reg->hcsparams2
                        >> 27;
    if (spb_number) {
        uint64 *spb_array = kzalloc(align_up(spb_number << 3, xhci_controller->align_size)); //分配暂存器缓冲区指针数组
        for (uint32 i = 0; i < spb_number; i++) spb_array[i] = va_to_pa(kzalloc(xhci_controller->align_size));
        //分配暂存器缓存区
        xhci_controller->dcbaap[0] = va_to_pa(spb_array); //暂存器缓存去数组指针写入设备上下写文数组0
    }

    /*启动xhci*/
    xhci_controller->op_reg->usbcmd |= XHCI_CMD_RS;

    /*获取协议支持能力*/
    xhci_cap_t *sp_cap = xhci_cap_find(xhci_controller, 2);

    color_printk(
        GREEN,BLACK,
        "Xhci Version:%x.%x USB%x.%x BAR0 MMIO:%#lx MSI-X:%d MaxSlots:%d MaxIntrs:%d MaxPorts:%d Context_Size:%d AC64:%d SPB:%d USBcmd:%#x USBsts:%#x AlignSize:%d iman:%#x imod:%#x crcr:%#lx dcbaap:%#lx erstba:%#lx erdp0:%#lx\n",
        xhci_controller->cap_reg->hciversion >> 8, xhci_controller->cap_reg->hciversion & 0xFF,
        sp_cap->supported_protocol.protocol_ver >> 24, sp_cap->supported_protocol.protocol_ver >> 16 & 0xFF,
        va_to_pa(xhci_dev->bar[0]), xhci_dev->msi_x_flags, xhci_controller->cap_reg->hcsparams1 & 0xFF,
        xhci_controller->cap_reg->hcsparams1 >> 8 & 0x7FF,
        xhci_controller->cap_reg->hcsparams1 >> 24, xhci_controller->cap_reg->hccparams1 >> 2 & 1,
        xhci_controller->context_size, spb_number,
        xhci_controller->op_reg->usbcmd, xhci_controller->op_reg->usbsts, xhci_controller->align_size,
        xhci_controller->rt_reg->intr_regs[0].iman,
        xhci_controller->rt_reg->intr_regs[0].imod, va_to_pa(xhci_controller->cmd_ring.ring_base),
        xhci_controller->op_reg->dcbaap, xhci_controller->rt_reg->intr_regs[0].erstba,
        xhci_controller->rt_reg->intr_regs[0].erdp);

    timing();

    list_head_init(&usb_dev_list);

    usb_dev_enum(xhci_controller);

    color_printk(GREEN,BLACK, "\nUSBcmd:%#x  USBsts:%#x", xhci_controller->op_reg->usbcmd,
                 xhci_controller->op_reg->usbsts);
    while (1);
}
