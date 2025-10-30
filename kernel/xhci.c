#include "xhci.h"
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

//增加输入上下文
void xhci_input_context_add(xhci_input_context_t *input_ctx, uint32 ctx_size, uint32 ctx_number,
                            xhci_context_t *from_ctx) {
    xhci_context_t *to_dev_ctx = (xhci_context_t *) ((uint64) input_ctx + ctx_size * (ctx_number + 1));
    to_dev_ctx->reg0 = from_ctx->reg0;
    to_dev_ctx->reg1 = from_ctx->reg1;
    to_dev_ctx->reg2 = from_ctx->reg2;
    to_dev_ctx->reg3 = from_ctx->reg3;
    input_ctx->input_ctx32.control.add_context |= 1 << ctx_number;
}

//读取设备上下文
void xhci_devctx_read(xhci_controller_t *xhci_controller, uint32 slot_id, uint32 ctx_number, xhci_context_t *to_ctx) {
    xhci_context_t *from_ctx = pa_to_va(xhci_controller->dcbaap[slot_id]);
    from_ctx = (xhci_context_t *) ((uint64) from_ctx + xhci_controller->context_size * ctx_number);
    to_ctx->reg0 = from_ctx->reg0;
    to_ctx->reg1 = from_ctx->reg1;
    to_ctx->reg2 = from_ctx->reg2;
    to_ctx->reg3 = from_ctx->reg3;
}

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
    xhci_context_t ctx;
    ctx.reg0 = 1 << 27 | (xhci_controller->op_reg->portregs[usb_dev->port_id - 1].portsc & 0x3C00) << 10;
    ctx.reg1 = usb_dev->port_id << 16;
    ctx.reg2 = 0;
    ctx.reg3 = 0;
    xhci_input_context_add(input_ctx, xhci_controller->context_size, 0, &ctx); // 启用 Slot Context

    ctx.reg0 = 0;
    ctx.reg1 = EP_TYPE_CONTROL | 8 << 16 | 3 << 1;
    ctx.reg2 = va_to_pa(usb_dev->control_ring.ring_base) | 1;
    ctx.reg3 = 0;
    xhci_input_context_add(input_ctx, xhci_controller->context_size, 1, &ctx); //Endpoint 0 Context

    trb_t trb;
    addr_dev_com_trb(&trb, va_to_pa(input_ctx), usb_dev->slot_id);
    xhci_ring_enqueue(&xhci_controller->cmd_ring, &trb);
    xhci_ring_doorbell(xhci_controller, 0, 0);
    timing();
    xhci_ering_dequeue(xhci_controller, &trb);
    kfree(input_ctx);
}

static inline uint32 get_ep_transfer_type(uint8 endpoint_addr,uint8 attributes) {
    uint32 ep_type = 0;
    if (endpoint_addr & 0x80) {
        switch (attributes) {
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
        switch (attributes) {
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

//配置端点
static inline void xhci_config_endpoint(usb_dev_t *usb_dev, usb_config_descriptor_t *config_desc) {
    xhci_controller_t *xhci_controller = usb_dev->xhci_controller;
    xhci_input_context_t *input_ctx = kzalloc(align_up(sizeof(xhci_input_context_t), xhci_controller->align_size));
    xhci_context_t ctx;
    trb_t trb;

    uint32 context_entries = 0;
    void* desc_end = (usb_config_descriptor_t *)((uint64)config_desc+config_desc->total_length);
    while (config_desc < desc_end) {
        usb_interface_descriptor_t *interface_desc;
        usb_interface_t* usb_interface;
        uint8 ep_idx;
        switch (config_desc->descriptor_type) {
            case USB_DESC_TYPE_CONFIGURATION:
                usb_dev->interfaces_count = config_desc->num_interfaces;
                usb_dev->interfaces = kzalloc(usb_dev->interfaces_count * sizeof(usb_interface_t));
                break;
            case USB_DESC_TYPE_INTERFACE:
                interface_desc = (usb_interface_descriptor_t*)config_desc;
                usb_interface = &usb_dev->interfaces[interface_desc->interface_number];
                if (interface_desc->alternate_setting == 0) {
                    //扫描备用设置数量
                    usb_interface_descriptor_t *tmp_if_desc = interface_desc;
                    while (tmp_if_desc<desc_end){
                         if (tmp_if_desc->descriptor_type == USB_DESC_TYPE_INTERFACE && tmp_if_desc->interface_number == interface_desc->interface_number) {
                             usb_interface->alternate_count++;
                         }
                        tmp_if_desc = (usb_interface_descriptor_t*)((uint64)tmp_if_desc + tmp_if_desc->length);
                    }
                    usb_interface->interface_number = interface_desc->interface_number;
                    usb_interface->alternate_setting = kzalloc(sizeof(usb_alt_setting_t) * usb_interface->alternate_count);
                }
                usb_alt_setting_t* usb_alt_setting = &usb_interface->alternate_setting[interface_desc->alternate_setting];
                usb_alt_setting->class = interface_desc->interface_class;
                usb_alt_setting->subclass = interface_desc->interface_subclass;
                usb_alt_setting->protocol = interface_desc->interface_protocol;
                usb_alt_setting->alt_setting_num = interface_desc->alternate_setting;
                usb_alt_setting->endpoints_count = interface_desc->num_endpoints;
                usb_alt_setting->endpoints = kzalloc(usb_alt_setting->endpoints_count*sizeof(usb_endpoint_t));
                ep_idx = 0;
                break;
            case USB_DESC_TYPE_ENDPOINT:
                usb_endpoint_descriptor_t* endpoint_desc = (usb_endpoint_t *)config_desc;
                usb_endpoint_t* endpoint = &usb_alt_setting->endpoints[ep_idx];

                uint32 max_burst;
                usb_ss_ep_comp_descriptor_t* ss_ep_comp_desc = (usb_ss_ep_comp_descriptor_t*)((uint64)config_desc+config_desc->length);
                if (ss_ep_comp_desc->descriptor_type == USB_DESC_TYPE_SS_EP_COMP) {
                    max_burst = ss_ep_comp_desc->max_burst;
                    config_desc = (usb_config_descriptor_t *) ((uint64) config_desc + config_desc->length);
                }else {
                    max_burst = 0;
                }

                //初始化端点传输环
                xhci_ring_init(&endpoint->transfer_ring, xhci_controller->align_size);

                endpoint->ep_num = (endpoint_desc->endpoint_address & 0xF) << 1 | endpoint_desc->endpoint_address >> 7;
                if (endpoint->ep_num > context_entries) context_entries = endpoint->ep_num;
                //获取端点类型
                uint32 ep_transfer_type = get_ep_transfer_type(endpoint_desc->endpoint_address,endpoint_desc->attributes);

                //增加端点
                ctx.reg0 = 0;
                ctx.reg1 = ep_transfer_type | endpoint_desc->max_packet_size << 16 | max_burst<<8 | 3<<1;
                ctx.reg2 = va_to_pa(endpoint->transfer_ring.ring_base) | 1;
                ctx.reg3 = 0;
                xhci_input_context_add(input_ctx, xhci_controller->context_size, endpoint->ep_num, &ctx);
                ep_idx++;
                if (usb_alt_setting->class == 0x08 && usb_alt_setting->subclass == 0x6) {
                    color_printk(RED,BLACK,"ep_num:%d ",endpoint->ep_num);
                }
                break;
            case USB_DESC_TYPE_PIPE_USGAGE:
                usb_pipe_usage_descriptor_t* pipe_usage_desc = (usb_pipe_usage_descriptor_t*)config_desc;
                color_printk(RED,BLACK,"pipe_usage:%d  \n",pipe_usage_desc->pipe_id);
                break;
            case USB_DESC_TYPE_STRING:
                break;
            case USB_DESC_TYPE_HID:
                break;
            case USB_DESC_TYPE_HUB:
                break;
        }
        config_desc = (usb_config_descriptor_t *) ((uint64) config_desc + config_desc->length);
    }

    //更新slot
    ctx.reg0 = context_entries << 27 | ((usb_dev->xhci_controller->op_reg->portregs[usb_dev->port_id - 1].portsc &
                                         0x3C00) << 10);
    ctx.reg1 = usb_dev->port_id << 16;
    ctx.reg2 = 0;
    ctx.reg3 = 0;
    xhci_input_context_add(input_ctx, xhci_controller->context_size, 0, &ctx);

    config_endpoint_com_trb(&trb, va_to_pa(input_ctx), usb_dev->slot_id);
    xhci_ring_enqueue(&xhci_controller->cmd_ring, &trb);
    xhci_ring_doorbell(xhci_controller, 0, 0);
    timing();
    xhci_ering_dequeue(xhci_controller, &trb);
    kfree(input_ctx);
}

//获取usb设备描述符
static inline usb_device_descriptor_t *usb_get_device_descriptor(usb_dev_t *usb_dev) {
    xhci_controller_t *xhci_controller = usb_dev->xhci_controller;
    usb_device_descriptor_t *dev_desc = kzalloc(align_up(sizeof(usb_device_descriptor_t),64));

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
    xhci_context_t ctx;
    xhci_devctx_read(xhci_controller, usb_dev->slot_id, 1, &ctx);
    ctx.reg1 = EP_TYPE_CONTROL | max_packe_size << 16;
    xhci_input_context_add(input_ctx, xhci_controller->context_size, 1, &ctx);

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

    return dev_desc;
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
    config_desc = kzalloc(align_up(config_desc_length,64));

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
int usb_set_interface(usb_dev_t* usb_dev, int64 if_num , int64 alt_num) {
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

//测试逻辑单元是否有效
static inline boolean bot_msc_test_lun(xhci_controller_t *xhci_controller,usb_dev_t *usb_dev,uint8 lun_id) {
    //测试状态检测3次不成功则视为无效逻辑单元
    boolean flags = FALSE;
    usb_msc_t *drive_data = usb_dev->interfaces->drive_data;
    usb_alt_setting_t* alternate_setting = usb_dev->interfaces->alternate_setting;
    usb_endpoint_t *in_ep = &alternate_setting->endpoints[drive_data->ep_in_num];
    usb_endpoint_t *out_ep = &alternate_setting->endpoints[drive_data->ep_out_num];
    usb_cbw_t *cbw = kzalloc(align_up(sizeof(usb_cbw_t), 64));
    usb_csw_t *csw = kzalloc(align_up(sizeof(usb_csw_t), 64));
    trb_t trb;
    for (uint8 j=0;j<3;j++) {
        mem_set(csw, 0, sizeof(usb_csw_t));
        mem_set(cbw, 0, sizeof(usb_cbw_t));
        cbw->cbw_signature = 0x43425355; // 'USBC'
        cbw->cbw_tag = ++drive_data->tag; // 唯一标签（示例）
        cbw->cbw_data_transfer_length = 0;
        cbw->cbw_flags = 0;
        cbw->cbw_lun = lun_id;
        cbw->cbw_cb_length = 6;

        // 1. 发送 CBW（批量 OUT 端点）
        normal_transfer_trb(&trb, va_to_pa(cbw), disable_ch, sizeof(usb_cbw_t), disable_ioc);
        xhci_ring_enqueue(&out_ep->transfer_ring, &trb);
        // 3. 接收 CSW（批量 IN 端点）
        normal_transfer_trb(&trb, va_to_pa(csw), disable_ch, sizeof(usb_csw_t), enable_ioc);
        xhci_ring_enqueue(&in_ep->transfer_ring, &trb);

        xhci_ring_doorbell(xhci_controller, usb_dev->slot_id, out_ep->ep_num);
        xhci_ring_doorbell(xhci_controller, usb_dev->slot_id, in_ep->ep_num);
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
static inline uint8 bot_msc_read_max_lun(xhci_controller_t *xhci_controller,usb_dev_t *usb_dev) {
    trb_t trb;
    setup_stage_trb(&trb, setup_stage_interface, setup_stage_calss, setup_stage_in, usb_req_get_max_lun, 0, 0, usb_dev->interfaces->interface_number, 8,
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
static inline uint8 bot_msc_read_vid(xhci_controller_t *xhci_controller,usb_dev_t *usb_dev,uint8 lun_id) {
    usb_msc_t *drive_data = usb_dev->interfaces->drive_data;
    usb_alt_setting_t* alternate_setting = usb_dev->interfaces->alternate_setting;
    usb_lun_t *lun = &drive_data->lun[lun_id];
    usb_endpoint_t *in_ep = &alternate_setting->endpoints[drive_data->ep_in_num];
    usb_endpoint_t *out_ep = &alternate_setting->endpoints[drive_data->ep_out_num];
    usb_cbw_t *cbw = kzalloc(align_up(sizeof(usb_cbw_t), 64));
    usb_csw_t *csw = kzalloc(align_up(sizeof(usb_csw_t), 64));
    trb_t trb;
    cbw->cbw_signature = 0x43425355; // 'USBC'
    cbw->cbw_tag = ++drive_data->tag; // 唯一标签（示例）
    cbw->cbw_data_transfer_length = sizeof(inquiry_data_t);
    cbw->cbw_flags = 0x80; // IN 方向
    cbw->cbw_lun = lun->lun_id; // 逻辑单元号
    cbw->cbw_cb_length = 6; //
    cbw->cbw_cb[0] = 0x12;
    cbw->cbw_cb[4] = sizeof(inquiry_data_t);

    // 1. 发送 CBW（批量 OUT 端点)
    normal_transfer_trb(&trb, va_to_pa(cbw), disable_ch, sizeof(usb_cbw_t), disable_ioc);
    xhci_ring_enqueue(&out_ep->transfer_ring, &trb);
    //2. 接收数据（批量 IN 端点）
    inquiry_data_t *inquiry_data = kzalloc(align_up(sizeof(inquiry_data_t), 64));
    normal_transfer_trb(&trb, va_to_pa(inquiry_data), enable_ch, sizeof(inquiry_data_t), disable_ioc);
    xhci_ring_enqueue(&in_ep->transfer_ring, &trb);
    // 3. 接收 CSW（批量 IN 端点）
    normal_transfer_trb(&trb, va_to_pa(csw), disable_ch, sizeof(usb_csw_t), enable_ioc);
    xhci_ring_enqueue(&in_ep->transfer_ring, &trb);

    xhci_ring_doorbell(xhci_controller, usb_dev->slot_id, out_ep->ep_num);
    xhci_ring_doorbell(xhci_controller, usb_dev->slot_id, in_ep->ep_num);
    timing();
    xhci_ering_dequeue(xhci_controller, &trb);

    mem_cpy(&inquiry_data->vendor_id, &lun->vid, 24);
    lun->vid[24] = 0;

    color_printk(GREEN,BLACK,"scsi-version:%d    \n",inquiry_data->version);

    kfree(cbw);
    kfree(csw);
    kfree(inquiry_data);
    return 0;
}

//获取u盘容量信息
static inline uint8  bot_msc_read_capacity(xhci_controller_t *xhci_controller,usb_dev_t *usb_dev,uint8 lun_id) {
    usb_msc_t *drive_data = usb_dev->interfaces->drive_data;
    usb_alt_setting_t* alternate_setting = usb_dev->interfaces->alternate_setting;
    usb_lun_t *lun = &drive_data->lun[lun_id];
    usb_endpoint_t *in_ep = &alternate_setting->endpoints[drive_data->ep_in_num];
    usb_endpoint_t *out_ep = &alternate_setting->endpoints[drive_data->ep_out_num];
    usb_cbw_t *cbw = kzalloc(align_up(sizeof(usb_cbw_t), 64));
    usb_csw_t *csw = kzalloc(align_up(sizeof(usb_csw_t), 64));
    trb_t trb;

    cbw->cbw_signature = 0x43425355; // 'USBC'
    cbw->cbw_tag = ++drive_data->tag; // 唯一标签（示例）
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
    xhci_ring_enqueue(&out_ep->transfer_ring, &trb);
    //2. 接收数据（批量 IN 端点
    read_capacity_16_t *capacity_data = kzalloc(align_up(sizeof(read_capacity_16_t), 64));
    normal_transfer_trb(&trb, va_to_pa(capacity_data), enable_ch, sizeof(read_capacity_16_t), disable_ioc);
    xhci_ring_enqueue(&in_ep->transfer_ring, &trb);
    // 3. 接收 CSW（批量 IN 端点
    normal_transfer_trb(&trb, va_to_pa(csw), disable_ch, sizeof(usb_csw_t), enable_ioc);
    xhci_ring_enqueue(&in_ep->transfer_ring, &trb);

    xhci_ring_doorbell(xhci_controller, usb_dev->slot_id, out_ep->ep_num);
    xhci_ring_doorbell(xhci_controller, usb_dev->slot_id, in_ep->ep_num);
    timing();
    xhci_ering_dequeue(xhci_controller, &trb);

    lun->block_count = bswap64(capacity_data->last_lba) + 1;
    lun->block_size = bswap32(capacity_data->block_size);

    kfree(cbw);
    kfree(csw);
    return 0;
}

//读u盘
uint8 bot_scsi_read16(xhci_controller_t* xhci_controller,usb_dev_t *usb_dev, uint8 lun_id,uint64 lba, uint32 block_count,uint32 block_size, void *buf) {
    usb_msc_t *drive_data = usb_dev->interfaces->drive_data;
    usb_alt_setting_t* alternate_setting = usb_dev->interfaces->alternate_setting;
    usb_lun_t *lun = &drive_data->lun[lun_id];
    usb_endpoint_t *in_ep = &alternate_setting->endpoints[drive_data->ep_in_num];
    usb_endpoint_t *out_ep = &alternate_setting->endpoints[drive_data->ep_out_num];
    usb_cbw_t *cbw = kzalloc(align_up(sizeof(usb_cbw_t), 64));
    usb_csw_t *csw = kzalloc(align_up(sizeof(usb_csw_t), 64));
    trb_t trb;

    cbw->cbw_signature = 0x43425355; // 'USBC'
    cbw->cbw_tag = ++drive_data->tag; // 唯一标签（示例）
    cbw->cbw_data_transfer_length = block_count*block_size;
    cbw->cbw_flags = 0x80; // IN 方向
    cbw->cbw_lun = lun->lun_id; // 逻辑单元号
    cbw->cbw_cb_length = 16; // READ CAPACITY (16) 命令长度
    // 构造 READ(16) 命令块
    cbw->cbw_cb[0] = 0x88; //READ(16)
    *(uint64*)&cbw->cbw_cb[2] = bswap64(lba);
    *(uint32*)&cbw->cbw_cb[10] = bswap32(block_count);

    // 1. 发送 CBW（批量 OUT 端点）
    normal_transfer_trb(&trb, va_to_pa(cbw), disable_ch, sizeof(usb_cbw_t), disable_ioc);
    xhci_ring_enqueue(&out_ep->transfer_ring, &trb);

    //2. 接收数据（批量 IN 端点
    normal_transfer_trb(&trb, va_to_pa(buf), enable_ch, block_count*block_size, disable_ioc);
    xhci_ring_enqueue(&in_ep->transfer_ring, &trb);

    // 3. 接收 CSW（批量 IN 端点
    normal_transfer_trb(&trb, va_to_pa(csw), disable_ch, sizeof(usb_csw_t), enable_ioc);
    xhci_ring_enqueue(&in_ep->transfer_ring, &trb);

    xhci_ring_doorbell(xhci_controller, usb_dev->slot_id, out_ep->ep_num);
    xhci_ring_doorbell(xhci_controller, usb_dev->slot_id, in_ep->ep_num);
    timing();
    xhci_ering_dequeue(xhci_controller, &trb);
    color_printk(GREEN,BLACK,"read16 m1:%#lx m2:%#lx   \n",trb.member0,trb.member1);

    kfree(cbw);
    kfree(csw);
    return 0;
}

uint8 bot_scsi_write16(xhci_controller_t* xhci_controller,usb_dev_t *usb_dev, uint8 lun_id,uint64 lba, uint32 block_count,uint32 block_size, void *buf) {
    usb_msc_t *drive_data = usb_dev->interfaces->drive_data;
    usb_alt_setting_t* alternate_setting = usb_dev->interfaces->alternate_setting;
    usb_lun_t *lun = &drive_data->lun[lun_id];
    usb_endpoint_t *in_ep = &alternate_setting->endpoints[drive_data->ep_in_num];
    usb_endpoint_t *out_ep = &alternate_setting->endpoints[drive_data->ep_out_num];
    usb_cbw_t *cbw = kzalloc(align_up(sizeof(usb_cbw_t), 64));
    usb_csw_t *csw = kzalloc(align_up(sizeof(usb_csw_t), 64));
    trb_t trb;

    cbw->cbw_signature = 0x43425355; // 'USBC'
    cbw->cbw_tag = ++drive_data->tag; // 唯一标签（示例）
    cbw->cbw_data_transfer_length = block_count*block_size; // READ CAPACITY (16) 返回32 字节
    cbw->cbw_flags = 0x00; // OUT方向（主机->设备）
    cbw->cbw_lun = lun->lun_id; // 逻辑单元号
    cbw->cbw_cb_length = 16; // READ CAPACITY (16) 命令长度
    // 构造 write(16) 命令块
    cbw->cbw_cb[0] = 0x8A; //write(16)
    *(uint64*)&cbw->cbw_cb[2] = bswap64(lba);
    *(uint32*)&cbw->cbw_cb[10] = bswap32(block_count);

    // 1. 发送 CBW（批量 OUT 端点）
    normal_transfer_trb(&trb, va_to_pa(cbw), disable_ch, sizeof(usb_cbw_t), disable_ioc);
    xhci_ring_enqueue(&out_ep->transfer_ring, &trb);

    //2. 发送数据（批量 OUT 端点）
    normal_transfer_trb(&trb, va_to_pa(buf), enable_ch, block_count*block_size, disable_ioc);
    xhci_ring_enqueue(&out_ep->transfer_ring, &trb);

    // 3. 接收 CSW（批量 IN 端点
    normal_transfer_trb(&trb, va_to_pa(csw), disable_ch, sizeof(usb_csw_t), enable_ioc);
    xhci_ring_enqueue(&in_ep->transfer_ring, &trb);

    xhci_ring_doorbell(xhci_controller, usb_dev->slot_id, out_ep->ep_num);
    xhci_ring_doorbell(xhci_controller, usb_dev->slot_id, in_ep->ep_num);
    timing();
    xhci_ering_dequeue(xhci_controller, &trb);
    color_printk(GREEN,BLACK,"wirte16 m1:%#lx m2:%#lx   \n",trb.member0,trb.member1);

    kfree(cbw);
    kfree(csw);
    return 0;
}

uint8 bot_scsi_read10(xhci_controller_t* xhci_controller,
                  usb_dev_t *usb_dev,
                  uint8 lun_id,
                  uint32 lba,
                  uint16 block_count,
                  uint32 block_size,
                  void *buf)
{
    usb_msc_t *drive_data = usb_dev->interfaces->drive_data;
    usb_alt_setting_t* alternate_setting = usb_dev->interfaces->alternate_setting;
    usb_lun_t *lun = &drive_data->lun[lun_id];
    usb_endpoint_t *in_ep  = &alternate_setting->endpoints[drive_data->ep_in_num];
    usb_endpoint_t *out_ep = &alternate_setting->endpoints[drive_data->ep_out_num];

    usb_cbw_t *cbw = kzalloc(align_up(sizeof(usb_cbw_t), 64));
    usb_csw_t *csw = kzalloc(align_up(sizeof(usb_csw_t), 64));
    trb_t trb;

    cbw->cbw_signature = 0x43425355; // "USBC"
    cbw->cbw_tag = ++drive_data->tag;
    cbw->cbw_data_transfer_length = block_count * block_size;
    cbw->cbw_flags = 0x80; // IN 方向
    cbw->cbw_lun = lun->lun_id;
    cbw->cbw_cb_length = 10; // READ(10) 长度

    // READ(10) 命令格式
    cbw->cbw_cb[0] = 0x28;                            // 操作码：READ(10)
    *(uint32*)&cbw->cbw_cb[2] = bswap32(lba);
    *(uint16*)&cbw->cbw_cb[7] = bswap16(block_count);  // 要读的块数

    // 1. 发送 CBW（Bulk OUT）
    normal_transfer_trb(&trb, va_to_pa(cbw), disable_ch, sizeof(usb_cbw_t), disable_ioc);
    xhci_ring_enqueue(&out_ep->transfer_ring, &trb);

    // 2. 接收数据（Bulk IN）
    normal_transfer_trb(&trb, va_to_pa(buf), enable_ch, block_count * block_size, disable_ioc);
    xhci_ring_enqueue(&in_ep->transfer_ring, &trb);

    // 3. 接收 CSW（Bulk IN）
    normal_transfer_trb(&trb, va_to_pa(csw), disable_ch, sizeof(usb_csw_t), enable_ioc);
    xhci_ring_enqueue(&in_ep->transfer_ring, &trb);

    // Doorbell
    xhci_ring_doorbell(xhci_controller, usb_dev->slot_id, out_ep->ep_num);
    xhci_ring_doorbell(xhci_controller, usb_dev->slot_id, in_ep->ep_num);

    timing();
    xhci_ering_dequeue(xhci_controller, &trb);
    color_printk(GREEN,BLACK,"read10 m1:%#lx m2:%#lx   \n",trb.member0,trb.member1);

    kfree(cbw);
    kfree(csw);
    return 0;
}

uint8 bot_scsi_write10(xhci_controller_t* xhci_controller,
                   usb_dev_t *usb_dev,
                   uint8 lun_id,
                   uint32 lba,
                   uint16 block_count,
                   uint32 block_size,
                   void *buf)
{
    usb_msc_t *drive_data = usb_dev->interfaces->drive_data;
    usb_alt_setting_t* alternate_setting = usb_dev->interfaces->alternate_setting;
    usb_lun_t *lun = &drive_data->lun[lun_id];
    usb_endpoint_t *in_ep  = &alternate_setting->endpoints[drive_data->ep_in_num];
    usb_endpoint_t *out_ep = &alternate_setting->endpoints[drive_data->ep_out_num];

    usb_cbw_t *cbw = kzalloc(align_up(sizeof(usb_cbw_t), 64));
    usb_csw_t *csw = kzalloc(align_up(sizeof(usb_csw_t), 64));
    trb_t trb;

    cbw->cbw_signature = 0x43425355; // "USBC"
    cbw->cbw_tag = ++drive_data->tag;
    cbw->cbw_data_transfer_length = block_count * block_size;
    cbw->cbw_flags = 0x00; // OUT 方向
    cbw->cbw_lun = lun->lun_id;
    cbw->cbw_cb_length = 10; // WRITE(10)

    // === 构造 WRITE(10) 命令块 ===
    cbw->cbw_cb[0] = 0x2A;                            // 操作码：READ(10)
    *(uint32*)&cbw->cbw_cb[2] = bswap32(lba);
    *(uint16*)&cbw->cbw_cb[7] = bswap16(block_count);  // 要读的块数

    // 1. 发送 CBW（Bulk OUT）
    normal_transfer_trb(&trb, va_to_pa(cbw), disable_ch, sizeof(usb_cbw_t), disable_ioc);
    xhci_ring_enqueue(&out_ep->transfer_ring, &trb);

    // 2. 发送数据（Bulk OUT）
    normal_transfer_trb(&trb, va_to_pa(buf), enable_ch, block_count * block_size, disable_ioc);
    xhci_ring_enqueue(&out_ep->transfer_ring, &trb);

    // 3. 接收 CSW（Bulk IN）
    normal_transfer_trb(&trb, va_to_pa(csw), disable_ch, sizeof(usb_csw_t), enable_ioc);
    xhci_ring_enqueue(&in_ep->transfer_ring, &trb);

    // Doorbell
    xhci_ring_doorbell(xhci_controller, usb_dev->slot_id, out_ep->ep_num);
    xhci_ring_doorbell(xhci_controller, usb_dev->slot_id, in_ep->ep_num);

    timing();
    xhci_ering_dequeue(xhci_controller, &trb);
    color_printk(GREEN,BLACK,"wirte10 m1:%#lx m2:%#lx   \n",trb.member0,trb.member1);

    kfree(cbw);
    kfree(csw);
    return 0;
}

//获取u盘信息（u盘品牌,容量等）
void usb_get_disk_info(usb_dev_t *usb_dev) {
    xhci_controller_t *xhci_controller = usb_dev->xhci_controller;
    usb_interface_t *interface = usb_dev->interfaces;
    usb_alt_setting_t* alternate_setting = interface->alternate_setting;

    for (uint8 i = 0;i<interface->alternate_count;i++) {
        if (alternate_setting[i].protocol == 0x62) {
            alternate_setting = alternate_setting + i;
            usb_set_interface(usb_dev,interface->interface_number,alternate_setting->alt_setting_num);
        }
    }

    if (alternate_setting->protocol == 0x62) {//uasp协议u盘


    }else {//bot 协议u盘
        usb_msc_t *drive_data = kzalloc(sizeof(usb_msc_t));
        interface->drive_data = drive_data;
        drive_data->dev = usb_dev;

        //查找 in out端点
        for (uint8 i = 0; i < alternate_setting->endpoints_count; i++) {
            if (alternate_setting->endpoints[i].ep_num & 1) {
                drive_data->ep_in_num = i;
            } else {
                drive_data->ep_out_num = i;
            }
        }
        //获取最大逻辑单元
        drive_data->lun_count = bot_msc_read_max_lun(xhci_controller, usb_dev);
        //枚举逻辑单元
        for (uint8 i=0;i<drive_data->lun_count;i++) {
            drive_data->lun[i].lun_id = i;
            if (bot_msc_test_lun(xhci_controller,usb_dev,i) == FALSE) break; //测试逻辑单元是否有效
            bot_msc_read_vid(xhci_controller, usb_dev,i);        //获取u盘厂商信息
            bot_msc_read_capacity(xhci_controller,usb_dev,i);        //获取u盘容量

            uint64* write = kzalloc(4096);
            mem_set(write,0x23,4096);
            //bot_scsi_write10(xhci_controller, usb_dev, i,0,2,drive_data->lun[i].block_size,write);

            uint64* buf = kzalloc(4096);
            bot_scsi_read10(xhci_controller, usb_dev, i,0,2,drive_data->lun[i].block_size,buf);

            color_printk(BLUE,BLACK,"buf:");
            for (uint32 i=0;i<100;i++) {
                color_printk(BLUE,BLACK,"%#lx",buf[i]);
            }
            color_printk(BLUE,BLACK,"\n");
            color_printk(GREEN,BLACK, "vid:%#x pid:%#x mode:%s block_num:%#lx block_size:%#x    \n", usb_dev->vid, usb_dev->pid,
                         drive_data->lun[i].vid, drive_data->lun[i].block_count, drive_data->lun[i].block_size);
        }
    }

}

//创建usb设备
usb_dev_t *create_usb_dev(xhci_controller_t *xhci_controller, uint32 port_id) {
    usb_dev_t *usb_dev = kzalloc(sizeof(usb_dev_t));
    usb_dev->xhci_controller = xhci_controller;
    usb_dev->port_id = port_id + 1;
    usb_dev->slot_id = xhci_enable_slot(xhci_controller);   //启用插槽
    xhci_address_device(usb_dev);                           //设置设备地址
    usb_device_descriptor_t *dev_desc = usb_get_device_descriptor(usb_dev); //获取设备描述符
    usb_dev->usb_ver = dev_desc->usb_version;
    usb_dev->vid = dev_desc->vendor_id;
    usb_dev->pid = dev_desc->product_id;
    usb_dev->dev_ver = dev_desc->device_version;
    usb_config_descriptor_t *config_desc = usb_get_config_descriptor(usb_dev);  //获取配置描述符
    xhci_config_endpoint(usb_dev, config_desc);                                 //配置端点
    usb_set_config(usb_dev, config_desc->configuration_value);                  //激活配置
    kfree(dev_desc);
    kfree(config_desc);
    list_add_head(&usb_dev_list, &usb_dev->list);

    if (*(uint16 *)&usb_dev->interfaces->alternate_setting->class == 0x0608) {
        usb_get_disk_info(usb_dev); //获取u盘信息
    }

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