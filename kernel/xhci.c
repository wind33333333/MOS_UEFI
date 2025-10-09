#include "xhci.h"
#include "moslib.h"
#include "pcie.h"
#include "printk.h"
#include "slub.h"
#include "vmm.h"

//usb设备全局链
list_head_t usb_dev_list;

xhci_cap_t *xhci_cap_find(xhci_controller_t *xhci_reg, uint8 cap_id) {
    uint32 offset = xhci_reg->cap_reg->hccparams1 >> 16;
    while (offset) {
        xhci_cap_t *xhci_cap = (void *) xhci_reg->cap_reg + (offset << 2);
        if ((xhci_cap->cap_id & 0xFF) == cap_id) return xhci_cap;
        offset = (xhci_cap->next_ptr >> 8) & 0xFF;
    }
    return NULL;
}

static inline uint8 get_sts_c(uint64 ptr) {
    return ptr & TRB_CYCLE;
}

static inline xhci_trb_t *get_queue_ptr(uint64 ptr) {
    return (xhci_trb_t *) (ptr & ~(TRB_CYCLE));
}

//响铃
static inline void xhci_ring_doorbell(xhci_controller_t *xhci_controller, uint8 db_number, uint32 value) {
    xhci_controller->db_reg[db_number] = value;
}

//命令环/传输环入队列
int xhci_ring_enqueue(xhci_ring_t *ring, xhci_trb_t *trb) {
    if (ring->index >= TRB_COUNT - 1) {
        ring->index = 0;
        ring->status_c ^= TRB_CYCLE;
        ring->ring_base[TRB_COUNT - 1].parameter = va_to_pa(ring->ring_base);
        ring->ring_base[TRB_COUNT - 1].status = 0;
        ring->ring_base[TRB_COUNT - 1].control = TRB_LINK | TRB_TOGGLE_CYCLE | ring->status_c;
    }
    ring->ring_base[ring->index].parameter = trb->parameter;
    ring->ring_base[ring->index].status = trb->status;
    ring->ring_base[ring->index].control = trb->control | ring->status_c;
    ring->index++;
    return 0;
}

//事件环出队列
int xhci_ering_dequeue(xhci_controller_t *xhci_controller, xhci_trb_t *evt_trb) {
    xhci_ring_t *event_ring = &xhci_controller->event_ring;
    while ((event_ring->ring_base[event_ring->index].control & TRB_CYCLE) == event_ring->status_c) {
        evt_trb->parameter = event_ring->ring_base[event_ring->index].parameter;
        evt_trb->status = event_ring->ring_base[event_ring->index].status;
        evt_trb->control = event_ring->ring_base[event_ring->index].control;
        event_ring->index++;
        if (event_ring->index >= TRB_COUNT) {
            event_ring->index = 0;
            event_ring->status_c ^= TRB_CYCLE;
        }
        xhci_controller->rt_reg->intr_regs[0].erdp =
                va_to_pa(&event_ring->ring_base[event_ring->index]) | XHCI_ERDP_EHB;
    }
    return 0;
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
 *                位48-52 slot type
 */
static inline void enable_slot_com_trb(trb_t *trb,uint8 slot_type) {
    trb->member0 = 0;
    trb->member1 = TRB_TYPE_ENABLE_SLOT | (slot_type<<48);
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
static inline void addr_dev_com_trb(trb_t *trb,uint64 input_ctx_ptr,uint8 slot_id) {
    trb->member0 = input_ctx_ptr;
    trb->member1 = TRB_TYPE_ADDRESS_DEVICE|(slot_id<<56);
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
static inline void config_endpoint_com_trb(trb_t *trb,uint64 input_ctx_ptr,uint8 slot_id) {
    trb->member0 = input_ctx_ptr;
    trb->member1 = TRB_TYPE_CONFIGURE_ENDPOINT | (slot_id<<56);
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
static inline void evaluate_context_com_trb(trb_t *trb,uint64 input_ctx_ptr,uint8 slot_id) {
    trb->member0 = input_ctx_ptr;
    trb->member1 = TRB_TYPE_EVALUATE_CONTEXT  | (slot_id<<56);
}

//endregion

//region 端点控制环trb

#define TRB_TYPE_SETUP_STAGE             (2UL << 42)   // 设置阶段
#define TRB_TYPE_DATA_STAGE              (3UL << 42)   // 数据阶段
#define TRB_TYPE_STATUS_STAGE            (4UL << 42)   // 状态阶段

typedef enum {
    disable_ioc=0,
    enable_ioc=1,
}config_ioc_e;

typedef enum {
    out = 0,
    in  = 1,
}trb_dir_e;

typedef enum {
    usb_req_get_status    =    0x00,  /* 获取状态
                                               - 接收者：设备、接口、端点
                                               - 返回：设备/接口/端点的状态（如挂起、遥控唤醒）
                                               - w_value: 0
                                               - w_index: 设备=0，接口=接口号，端点=端点号
                                               - w_length: 2（返回 2 字节状态） */
    usb_req_clear_feature  =   0x01,  /* 清除特性
                                               - 接收者：设备、接口、端点
                                               - 用途：清除特定状态（如取消遥控唤醒或端点暂停）
                                               - w_value: 特性选择（如 0=设备遥控唤醒，1=端点暂停）
                                               - w_index: 设备=0，接口=接口号，端点=端点号
                                               - w_length: 0 */
    usb_req_set_feature    =   0x03,  /* 设置特性
                                               - 接收者：设备、接口、端点
                                               - 用途：启用特定特性（如遥控唤醒、测试模式）
                                               - w_value: 特性选择（如 0=设备遥控唤醒，1=端点暂停）
                                               - w_index: 设备=0，接口=接口号，端点=端点号
                                               - w_length: 0 */
    usb_req_set_address    =   0x05,  /* 设置设备地址
                                               - 接收者：设备
                                               - 用途：在枚举过程中分配设备地址（1-127）
                                               - w_value: 新地址（低字节）
                                               - w_index: 0
                                               - w_length: 0 */
    usb_req_get_descriptor  =  0x06,  /* 获取描述符
                                               - 接收者：设备、接口
                                               - 用途：获取设备、配置、接口、字符串等描述符
                                               - w_value: 高字节=描述符类型（如 0x01=设备，0x02=配置），低字节=索引
                                               - w_index: 0（设备/配置描述符）或语言 ID（字符串描述符）
                                               - w_length: 请求的字节数 */
    usb_req_set_descriptor  =  0x07,  /* 设置描述符
                                               - 接收者：设备、接口
                                               - 用途：更新设备描述符（较少使用）
                                               - w_value: 高字节=描述符类型，低字节=索引
                                               - w_index: 0 或语言 ID
                                               - w_length: 数据长度 */
    usb_req_get_config      = 0x08,  /* 获取当前配置
                                               - 接收者：设备
                                               - 用途：返回当前激活的配置值
                                               - w_value: 0
                                               - w_index: 0
                                               - w_length: 1（返回 1 字节配置值） */
    usb_req_set_config      = 0x09,  /* 设置配置
                                               - 接收者：设备
                                               - 用途：激活指定配置
                                               - w_value: 配置值（来自配置描述符的 b_configuration_value）
                                               - w_index: 0
                                               - w_length: 0 */
    usb_req_get_interface   = 0x0A,  /* 获取接口的备用设置
                                               - 接收者：接口
                                               - 用途：返回当前接口的备用设置编号
                                               - w_value: 0
                                               - w_index: 接口号
                                               - w_length: 1（返回 1 字节备用设置值） */
    usb_req_set_interface   = 0x0B,  /* 设置接口的备用设置
                                               - 接收者：接口
                                               - 用途：选择接口的备用设置
                                               - w_value: 备用设置编号
                                               - w_index: 接口号
                                               - w_length: 0 */
    usb_req_synch_frame     = 0x0C,  /* 同步帧
                                               - 接收者：端点
                                               - 用途：为同步端点（如音频设备）提供帧编号
                                               - w_value: 0
                                               - w_index: 端点号
                                               - w_length: 2（返回 2 字节帧号） */

}setup_stage_req_e;

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
    setup_stage_norm = 0,
    setup_stage_calss = 1,
    setup_stage_firm = 2,
    setup_stage_reserve = 2
}setup_stage_type_e;
typedef enum {
    setup_stage_out = 0,
    setup_stage_in = 1
}setup_stage_dir_e;
typedef enum {
    no_data_stage = 0,
    out_data_stage = 2,
    in_data_stage  = 3
}trb_trt_e;
typedef enum {
    setup_stage_device = 0,
    setup_stage_interface  = 1,
    setup_stage_endpoint   = 2
}setup_stage_receiver_e;
static inline void setup_stage_trb(trb_t *trb,setup_stage_receiver_e setup_stage_receiver,setup_stage_type_e setup_stage_type,setup_stage_dir_e setup_stage_dir,\
    setup_stage_req_e req,uint16 value,uint16 index,uint16 length,uint16 trb_tran_length,config_ioc_e ioc,trb_trt_e trt) {
    trb->member0 = (setup_stage_receiver<<0) |(setup_stage_type<<5)|(setup_stage_dir<<7)| (req<<8) | (value<<16) | (index<<32) | (length<<48);
    trb->member1 = (trb_tran_length<<0) | (ioc<<37) | (1<<38) | TRB_TYPE_SETUP_STAGE | (trt<<48);
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
static inline void data_stage_trb(trb_t *trb,uint64 data_buff_ptr,uint16 trb_tran_length,trb_dir_e dir) {
    trb->member0 = data_buff_ptr;
    trb->member1 = (trb_tran_length<<0)|TRB_TYPE_DATA_STAGE |(1<<36)|(dir<<48);
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
static inline void status_stage_trb(trb_t *trb,config_ioc_e ioc,trb_dir_e dir) {
    trb->member0 = 0;
    trb->member1 = (ioc<<37)|TRB_TYPE_STATUS_STAGE|(dir<<48);
}
//endregion

//region 传输环trb
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
static inline void normal_transfer_trb(trb_t *trb,uint64 data_buff_ptr,uint8 ent,uint16 trb_tran_length,uint8 ch,uint8 ioc,uint8 trb_type) {
    trb->member0 = data_buff_ptr;
    trb->member1 = (ent<<33)|(trb_tran_length<<0)|(ch<<36)|(ioc<<37)|(trb_type<<42);
}
//endregion

//region 事件环trb

//endregion


//分配插槽
uint8 xhci_enable_slot(xhci_controller_t *xhci_controller) {
    xhci_trb_t trb = {
        0,
        0,
        TRB_ENABLE_SLOT
    };
    xhci_ring_enqueue(&xhci_controller->cmd_ring, &trb);
    xhci_ring_doorbell(xhci_controller, 0, 0);
    timing();
    xhci_ering_dequeue(xhci_controller, &trb);
    if ((trb.control >> 10 & 0x3F) == 33 && trb.control >> 24) {
        return trb.control >> 24 & 0xFF;
    }
    return -1;
}

//初始化环
int xhci_ring_init(xhci_ring_t *ring, uint32 align_size) {
    ring->ring_base = kzalloc(align_up(TRB_COUNT * sizeof(xhci_trb_t), align_size));
    ring->index = 0;
    ring->status_c = TRB_CYCLE;
    ring->ring_base[TRB_COUNT - 1].parameter = va_to_pa(ring->ring_base);
    ring->ring_base[TRB_COUNT - 1].status = 0;
    ring->ring_base[TRB_COUNT - 1].control = TRB_LINK | TRB_TOGGLE_CYCLE | TRB_CYCLE;
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
void xhci_address_device(usb_dev_t *usb_dev) {
    xhci_controller_t *xhci_controller = usb_dev->xhci_controller;
    //分配设备插槽上下文内存
    usb_dev->dev_context = kzalloc(align_up(sizeof(xhci_device_context_t), xhci_controller->align_size));
    xhci_controller->dcbaap[usb_dev->slot_id] = va_to_pa(usb_dev->dev_context);

    //分配传输环内存
    xhci_ring_init(&usb_dev->trans_ring[0], xhci_controller->align_size);

    //配置设备上下文
    xhci_input_context_t *input_ctx = kzalloc(align_up(sizeof(xhci_input_context_t), xhci_controller->align_size));
    xhci_context_t ctx;
    ctx.reg0 = 1 << 27 | ((usb_dev->xhci_controller->op_reg->portregs[usb_dev->port_id - 1].portsc & 0x3C00) << 10);
    ctx.reg1 = usb_dev->port_id << 16;
    ctx.reg2 = 0;
    ctx.reg3 = 0;
    xhci_input_context_add(input_ctx, xhci_controller->context_size, 0, &ctx); // 启用 Slot Context

    ctx.reg0 = 1;
    ctx.reg1 = EP_TYPE_CONTROL | (8 << 16) | (3 << 1);
    ctx.reg2 = va_to_pa(usb_dev->trans_ring[0].ring_base) | TRB_CYCLE;
    ctx.reg3 = 0;
    xhci_input_context_add(input_ctx, xhci_controller->context_size, 1, &ctx); //Endpoint 0 Context

    xhci_trb_t trb = {
        va_to_pa(input_ctx),
        0,
        TRB_ADDRESS_DEVICE | usb_dev->slot_id << 24
    };
    xhci_ring_enqueue(&xhci_controller->cmd_ring, &trb);
    xhci_ring_doorbell(xhci_controller, 0, 0);
    timing();
    xhci_ering_dequeue(xhci_controller, &trb);
    kfree(input_ctx);
}

//配置端点
void xhci_config_endpoint(usb_dev_t *usb_dev) {
    xhci_controller_t *xhci_controller = usb_dev->xhci_controller;
    xhci_input_context_t *input_ctx = kzalloc(align_up(sizeof(xhci_input_context_t), xhci_controller->align_size));
    xhci_context_t ctx;
    uint32 context_entries = 0;

    //增加端点
    for (uint8 i = 0; i < usb_dev->interface_desc->num_endpoints; i++) {
        uint8 ep_num = (usb_dev->endpoint_desc[i]->endpoint_address & 0xF) << 1 | usb_dev->endpoint_desc[i]->
                       endpoint_address >> 7;
        if (ep_num > context_entries) context_entries = ep_num;
        uint8 tr_idx = ep_num - 1;
        //分配传输环内存
        xhci_ring_init(&usb_dev->trans_ring[tr_idx], xhci_controller->align_size);

        //识别端点类型
        uint32 ep_type = 0;
        if (ep_num & 1) {
            switch (usb_dev->endpoint_desc[i]->attributes) {
                case USB_EP_ISOCH:
                    ep_type = EP_TYPE_ISOCH_IN;
                    break;
                case USB_EP_BULK:
                    ep_type = EP_TYPE_BULK_IN;
                    usb_dev->in_ep = ep_num;
                    usb_dev->in_ring = usb_dev->trans_ring[tr_idx];
                    break;
                case USB_EP_INTERRUPT:
                    ep_type = EP_TYPE_INTERRUPT_IN;
            }
        } else {
            switch (usb_dev->endpoint_desc[i]->attributes) {
                case USB_EP_ISOCH:
                    ep_type = EP_TYPE_ISOCH_OUT;
                    break;
                case USB_EP_BULK:
                    ep_type = EP_TYPE_BULK_OUT;
                    usb_dev->out_ep = ep_num;
                    usb_dev->out_ring = usb_dev->trans_ring[tr_idx];
                    break;
                case USB_EP_INTERRUPT:
                    ep_type = EP_TYPE_INTERRUPT_OUT;
            }
        }

        ctx.reg0 = 1;
        ctx.reg1 = ep_type | (usb_dev->endpoint_desc[i]->max_packet_size << 16) | (
                       usb_dev->ep_comp_des[i]->max_burst << 8) | (3 << 1);
        ctx.reg2 = va_to_pa(usb_dev->trans_ring[tr_idx].ring_base) | TRB_CYCLE;
        ctx.reg3 = 0;
        xhci_input_context_add(input_ctx, xhci_controller->context_size, ep_num, &ctx);
    }

    //更新slot
    ctx.reg0 = context_entries << 27 | ((usb_dev->xhci_controller->op_reg->portregs[usb_dev->port_id - 1].portsc &
                                         0x3C00) << 10);
    ctx.reg1 = usb_dev->port_id << 16;
    ctx.reg2 = 0;
    ctx.reg3 = 0;
    xhci_input_context_add(input_ctx, xhci_controller->context_size, 0, &ctx);

    xhci_trb_t trb = {
        va_to_pa(input_ctx),
        0,
        TRB_CONFIGURE_ENDPOINT | usb_dev->slot_id << 24
    };
    xhci_ring_enqueue(&xhci_controller->cmd_ring, &trb);
    xhci_ring_doorbell(xhci_controller, 0, 0);
    timing();
    xhci_ering_dequeue(xhci_controller, &trb);
    kfree(input_ctx);
}

//获取usb设备描述符
int usb_get_device_descriptor(usb_dev_t *usb_dev) {
    xhci_controller_t *xhci_controller = usb_dev->xhci_controller;
    usb_device_descriptor_t *dev_desc = kzalloc(sizeof(usb_device_descriptor_t));
    //第一次先获取设备描述符前8字节，拿到max_pack_size后更新端点1，再重新获取描述符。
    xhci_trb_t trb;
    // Setup TRB
    usb_setup_packet_t setup = {0x80, USB_REQ_GET_DESCRIPTOR, 0x0100, 0x0000, 8}; // 统一为8
    trb.parameter = *(uint64 *) &setup; // 完整 8 字节
    trb.status = 8; // TRB Length=8 (Setup 阶段长度)
    trb.control = TRB_SETUP_STAGE | TRB_IDT | (3 << 16);
    xhci_ring_enqueue(&usb_dev->trans_ring[0], &trb);

    // Data TRB
    trb.parameter = va_to_pa(dev_desc);
    trb.status = 8; // 匹配 w_length
    trb.control = TRB_DATA_STAGE | (1 << 16);
    xhci_ring_enqueue(&usb_dev->trans_ring[0], &trb);

    // Status TRB
    trb.parameter = 0;
    trb.status = 0;
    trb.control = TRB_STATUS_STAGE;
    xhci_ring_enqueue(&usb_dev->trans_ring[0], &trb);

    xhci_ring_doorbell(xhci_controller, usb_dev->slot_id, 1);
    timing();

    //更新端点0的最大包
    uint32 max_packe_size = dev_desc->usb_version >= 0x300
                                ? 1 << dev_desc->max_packet_size0
                                : dev_desc->max_packet_size0;
    xhci_input_context_t *input_ctx = kzalloc(align_up(sizeof(xhci_input_context_t), xhci_controller->align_size));
    xhci_context_t ctx;
    xhci_devctx_read(xhci_controller, usb_dev->slot_id, 1, &ctx);
    ctx.reg1 = EP_TYPE_CONTROL | max_packe_size << 16;
    xhci_input_context_add(input_ctx, xhci_controller->context_size, 1, &ctx);

    trb.parameter = va_to_pa(input_ctx);
    trb.status = 0;
    trb.control = usb_dev->slot_id << 24 | TRB_EVALUATE_CONTEXT;
    xhci_ring_enqueue(&xhci_controller->cmd_ring, &trb);
    xhci_ring_doorbell(xhci_controller, 0, 0);
    timing();
    kfree(input_ctx);

    //第二次获取整个设备描述符
    setup.length = 18;
    trb.parameter = *(uint64 *) &setup; // 完整 8 字节
    trb.status = 8; // TRB Length=8 (Setup 阶段长度)
    trb.control = TRB_SETUP_STAGE | TRB_IDT | (3 << 16);
    xhci_ring_enqueue(&usb_dev->trans_ring[0], &trb);

    // Data TRB
    trb.parameter = va_to_pa(dev_desc);
    trb.status = 18; // 匹配 w_length
    trb.control = TRB_DATA_STAGE | (1 << 16);
    xhci_ring_enqueue(&usb_dev->trans_ring[0], &trb);

    // Status TRB
    trb.parameter = 0;
    trb.status = 0;
    trb.control = TRB_STATUS_STAGE;
    xhci_ring_enqueue(&usb_dev->trans_ring[0], &trb);
    xhci_ring_doorbell(xhci_controller, usb_dev->slot_id, 1);
    timing();

    usb_dev->dev_desc = dev_desc;
}

//获取usb配置描述符
int usb_get_config_descriptor(usb_dev_t *usb_dev) {
    xhci_controller_t *xhci_controller = usb_dev->xhci_controller;
    //第一次先获取配置描述符前9字节
    xhci_trb_t trb;
    usb_config_descriptor_t *config_desc = kmalloc(9);
    usb_setup_packet_t setup = {0x80, USB_REQ_GET_DESCRIPTOR, 0x0200, 0x0000, 9}; //9
    trb.parameter = *(uint64 *) &setup;
    trb.status = 8;
    trb.control = TRB_SETUP_STAGE | TRB_IDT | (3 << 16);
    xhci_ring_enqueue(&usb_dev->trans_ring[0], &trb);

    // Data TRB
    trb.parameter = va_to_pa(config_desc);
    trb.status = 9;
    trb.control = TRB_DATA_STAGE | (1 << 16);
    xhci_ring_enqueue(&usb_dev->trans_ring[0], &trb);

    // Status TRB
    trb.parameter = 0;
    trb.status = 0;
    trb.control = TRB_STATUS_STAGE;
    xhci_ring_enqueue(&usb_dev->trans_ring[0], &trb);

    // 响铃
    xhci_ring_doorbell(xhci_controller, usb_dev->slot_id, 1);
    timing();

    //第二次从配置描述符中得到总长度获取整个配置描述符
    setup.length = config_desc->total_length;
    kfree(config_desc);
    config_desc = kzalloc(setup.length);

    trb.parameter = *(uint64 *) &setup;
    trb.status = 8;
    trb.control = TRB_SETUP_STAGE | TRB_IDT | (3 << 16);
    xhci_ring_enqueue(&usb_dev->trans_ring[0], &trb);

    // Data TRB
    trb.parameter = va_to_pa(config_desc);
    trb.status = setup.length;
    trb.control = TRB_DATA_STAGE | (1 << 16);
    xhci_ring_enqueue(&usb_dev->trans_ring[0], &trb);

    // Status TRB
    trb.parameter = 0;
    trb.status = 0;
    trb.control = TRB_STATUS_STAGE;
    xhci_ring_enqueue(&usb_dev->trans_ring[0], &trb);

    // 响铃
    xhci_ring_doorbell(xhci_controller, usb_dev->slot_id, 1);
    timing();

    usb_config_descriptor_t *config_desc_end = (usb_config_descriptor_t *) (
        (uint64) config_desc + config_desc->total_length);
    uint32 ep_idx = 0;
    uint8 comp_idx = 0;
    while (config_desc < config_desc_end) {
        switch (config_desc->head.descriptor_type) {
            case USB_DESC_TYPE_CONFIGURATION:
                usb_dev->config_desc = config_desc;
                break;
            case USB_DESC_TYPE_STRING:
                usb_dev->string_desc = config_desc;
                break;
            case USB_DESC_TYPE_INTERFACE:
                usb_dev->interface_desc = config_desc;
                break;
            case USB_DESC_TYPE_ENDPOINT:
                usb_dev->endpoint_desc[ep_idx] = config_desc;
                ep_idx++;
                break;
            case USB_DESC_TYPE_SS_EP_COMP:
                usb_dev->ep_comp_des[comp_idx] = config_desc;
                comp_idx++;
            case USB_DESC_TYPE_HID:
                usb_dev->hid_desc = config_desc;
                break;
            case USB_DESC_TYPE_HUB:
                usb_dev->hub_desc = config_desc;
                break;
        }
        config_desc = (usb_config_descriptor_t *) ((uint64) config_desc + config_desc->head.length);
    }
    return 0;
}

//激活usb配置
int usb_set_config(usb_dev_t *usb_dev) {
    xhci_controller_t *xhci_controller = usb_dev->xhci_controller;
    //设置配置
    usb_setup_packet_t setup;
    xhci_trb_t trb;
    setup.request_type = 0;
    setup.request = USB_REQ_SET_CONFIGURATION;
    setup.value = usb_dev->config_desc->configuration_value;
    setup.index = 0;
    setup.length = 0;

    trb.parameter = *(uint64 *) &setup;
    trb.status = 8;
    trb.control = TRB_SETUP_STAGE | TRB_IDT | (3 << 16);
    xhci_ring_enqueue(&usb_dev->trans_ring[0], &trb);

    trb.parameter = 0;
    trb.status = 0;
    trb.control = TRB_STATUS_STAGE | TRB_IOC;
    xhci_ring_enqueue(&usb_dev->trans_ring[0], &trb);

    xhci_ring_doorbell(xhci_controller, usb_dev->slot_id,1);
    timing();
    xhci_ering_dequeue(xhci_controller,&trb);
    color_printk(GREEN,BLACK,"set config trb p:%#lx s:%#x c%#x    \n",trb.parameter,trb.status,trb.control);
    return 0;
}

//创建usb设备
usb_dev_t *create_usb_dev(xhci_controller_t *xhci_controller, uint32 port_id) {
    usb_dev_t *usb_dev = kzalloc(sizeof(usb_dev_t));
    usb_dev->xhci_controller = xhci_controller;
    usb_dev->port_id = port_id + 1;
    usb_dev->slot_id = xhci_enable_slot(xhci_controller);
    xhci_address_device(usb_dev);
    usb_get_device_descriptor(usb_dev);
    usb_get_config_descriptor(usb_dev);
    usb_set_config(usb_dev);
    xhci_config_endpoint(usb_dev);
    list_add_head(&usb_dev_list, &usb_dev->list);
    color_printk(
        GREEN,BLACK,
        "port_id:%d slot_id:%d portsc:%#x USB_vir:%x.%x VID:%#x PID:%#x if_num:%d ep_num:%d if_class:%#x if_subclass:%#x if_pro:%#x max_pack:%d\n",
        usb_dev->port_id, usb_dev->slot_id, xhci_controller->op_reg->portregs[usb_dev->port_id - 1].portsc,
        usb_dev->dev_desc->usb_version >> 8, usb_dev->dev_desc->usb_version & 0xFF, usb_dev->dev_desc->vendor_id,
        usb_dev->dev_desc->product_id, usb_dev->config_desc->num_interfaces, usb_dev->interface_desc->num_endpoints,
        usb_dev->interface_desc->interface_class, usb_dev->interface_desc->interface_subclass,
        usb_dev->interface_desc->interface_protocol, usb_dev->endpoint_desc[0]->max_packet_size);

    //发现u盘
    if (usb_dev->interface_desc->interface_class == 0x8 && usb_dev->interface_desc->interface_subclass == 0x6) {
        color_printk(GREEN,BLACK, "out_ring:%#lx out_ep_num:%d in_ring%#lx in_ep_num:%d  \n",
                     usb_dev->out_ring.ring_base, usb_dev->out_ep, usb_dev->in_ring.ring_base, usb_dev->in_ep);

        usb_cbw_t *cbw = kzalloc(align_up(sizeof(usb_cbw_t), 0x1000));
        usb_csw_t *csw = kzalloc(align_up(sizeof(usb_csw_t), 0x1000));
        usb_setup_packet_t setup;
        xhci_trb_t trb;

        //Get Max LUN
        setup.request_type = 0xA1;
        setup.request = 0xfe;
        setup.value = 0;
        setup.index = 0;
        setup.length = 1;

        trb.parameter = *(uint64 *) &setup;
        trb.status = 8;
        trb.control = TRB_SETUP_STAGE  | TRB_IDT | (3 << 16);
        xhci_ring_enqueue(&usb_dev->trans_ring[0], &trb);

        uint8 *max_lun = kmalloc(sizeof(uint8));
        trb.parameter = va_to_pa(max_lun);
        trb.status = 1;
        trb.control = TRB_DATA_STAGE | (1 << 16);
        xhci_ring_enqueue(&usb_dev->trans_ring[0],&trb);

        trb.parameter = 0;
        trb.status = 0;
        trb.control = TRB_STATUS_STAGE | TRB_IOC;
        xhci_ring_enqueue(&usb_dev->trans_ring[0],&trb);

        xhci_ring_doorbell(xhci_controller, usb_dev->slot_id,1);
        timing();
        xhci_ering_dequeue(xhci_controller,&trb);
        color_printk(GREEN,BLACK,"get max lun:%d trb p:%#lx s:%#x c%#x    \n",*max_lun,trb.parameter,trb.status,trb.control);

        //测试状态
        do{
            mem_set(csw, 0, 0x1000);
            mem_set(cbw, 0, 0x1000);
            cbw->cbw_signature = 0x43425355; // 'USBC'
            cbw->cbw_tag = ++usb_dev->tag; // 唯一标签（示例）
            cbw->cbw_data_transfer_length = 0;
            cbw->cbw_flags = 0;
            cbw->cbw_lun = 0;
            cbw->cbw_cb_length = 6;

            // 1. 发送 CBW（批量 OUT 端点）
            trb.parameter = va_to_pa(cbw);
            trb.status = sizeof(usb_cbw_t);
            trb.control = TRB_NORMAL | TRB_IOC;
            xhci_ring_enqueue(&usb_dev->out_ring, &trb);

            // 3. 接收 CSW（批量 IN 端点）
            trb.parameter = va_to_pa(csw);
            trb.status = sizeof(usb_csw_t);
            trb.control = TRB_NORMAL | TRB_IOC; // Normal TRB
            xhci_ring_enqueue(&usb_dev->in_ring, &trb);

            xhci_ring_doorbell(xhci_controller, usb_dev->slot_id, usb_dev->out_ep);
            timing();
            xhci_ering_dequeue(xhci_controller,&trb);

            xhci_ring_doorbell(xhci_controller, usb_dev->slot_id, usb_dev->in_ep);
            timing();
            xhci_ering_dequeue(xhci_controller,&trb);
            color_printk(GREEN,BLACK,"test csw.status:%d trb p:%#lx s:%#x c%#x    \n",csw->csw_status,trb.parameter,trb.status,trb.control);
        }while (csw->csw_status);

        //获取u盘信息
        mem_set(csw, 0, 0x1000);
        mem_set(cbw, 0, 0x1000);
        cbw->cbw_signature = 0x43425355; // 'USBC'
        cbw->cbw_tag = ++usb_dev->tag; // 唯一标签（示例）
        cbw->cbw_data_transfer_length = sizeof(inquiry_data_t);
        cbw->cbw_flags = 0x80; // IN 方向
        cbw->cbw_lun = 0; // 逻辑单元号
        cbw->cbw_cb_length = 6; //
        cbw->cbw_cb[0] = 0x12;
        cbw->cbw_cb[4] = sizeof(inquiry_data_t);

        // 1. 发送 CBW（批量 OUT 端点)
        trb.parameter = va_to_pa(cbw);
        trb.status = sizeof(usb_cbw_t);
        trb.control = TRB_NORMAL| TRB_IOC;
        xhci_ring_enqueue(&usb_dev->out_ring, &trb);

        //2. 接收数据（批量 IN 端点）
        inquiry_data_t *inquiry_data = kzalloc(align_up(sizeof(inquiry_data_t), 0x1000));
        trb.parameter = va_to_pa(inquiry_data);
        trb.status = sizeof(inquiry_data_t);
        trb.control = TRB_NORMAL; // Normal TRB
        xhci_ring_enqueue(&usb_dev->in_ring, &trb);

        // 3. 接收 CSW（批量 IN 端点）
        trb.parameter = va_to_pa(csw);
        trb.status = sizeof(usb_csw_t);
        trb.control = TRB_NORMAL| TRB_IOC; // Normal TRB
        xhci_ring_enqueue(&usb_dev->in_ring, &trb);

        xhci_ring_doorbell(xhci_controller, usb_dev->slot_id, usb_dev->out_ep);
        timing();
        xhci_ering_dequeue(xhci_controller,&trb);
        color_printk(GREEN,BLACK,"get usb info0 trb p:%#lx s:%#x c%#x    \n",trb.parameter,trb.status,trb.control);

        xhci_ring_doorbell(xhci_controller, usb_dev->slot_id, usb_dev->in_ep);
        timing();
        xhci_ering_dequeue(xhci_controller,&trb);
        color_printk(GREEN,BLACK,"get usb info1 trb p:%#lx s:%#x c%#x    \n",trb.parameter,trb.status,trb.control);

        color_printk(GREEN,BLACK, "dev_type:%#x rmb:%#x scsi_ver:%#x vid:%s pid:%s rev:%s     \n",
                     inquiry_data->device_type, inquiry_data->rmb, inquiry_data->version, inquiry_data->vendor_id,
                     inquiry_data->product_id, inquiry_data->revision);


        //获取u盘容量
        mem_set(csw, 0, 0x1000);
        mem_set(cbw, 0, 0x1000);
        cbw->cbw_signature = 0x43425355; // 'USBC'
        cbw->cbw_tag = ++usb_dev->tag; // 唯一标签（示例）
        cbw->cbw_data_transfer_length = 32; // READ CAPACITY (16) 返回32 字节
        cbw->cbw_flags = 0x80; // IN 方向
        cbw->cbw_lun = 0; // 逻辑单元号
        cbw->cbw_cb_length = 16; // READ CAPACITY (16) 命令长度

        //填充 SCSI READ CAPACITY (16) 命令 #2##1#
        cbw->cbw_cb[0] = 0x9E; // 操作码：READ CAPACITY (16)
        cbw->cbw_cb[1] = 0x10; // 服务动作：0x10
        cbw->cbw_cb[13] = 32; // 分配长度低字节（32 字节）

        // 1. 发送 CBW（批量 OUT 端点）
        trb.parameter = va_to_pa(cbw);
        trb.status = sizeof(usb_cbw_t);
        trb.control = TRB_NORMAL | TRB_IOC;
        xhci_ring_enqueue(&usb_dev->out_ring, &trb);

        //2. 接收数据（批量 IN 端点
        read_capacity_16_t *capacity_data = kzalloc(sizeof(read_capacity_16_t));
        trb.parameter = va_to_pa(capacity_data);
        trb.status = sizeof(read_capacity_16_t);
        trb.control = TRB_NORMAL; // Normal TRB
        xhci_ring_enqueue(&usb_dev->in_ring, &trb);

        // 3. 接收 CSW（批量 IN 端点
        csw = kzalloc(sizeof(usb_csw_t));
        trb.parameter = va_to_pa(csw);
        trb.status = sizeof(usb_csw_t);
        trb.control = TRB_NORMAL | TRB_IOC; // Normal TRB
        xhci_ring_enqueue(&usb_dev->in_ring, &trb);

        xhci_ring_doorbell(xhci_controller, usb_dev->slot_id, usb_dev->out_ep);
        timing();
        xhci_ering_dequeue(xhci_controller,&trb);
        color_printk(GREEN,BLACK,"get usb rongliang trb p:%#lx s:%#x c%#x    \n",trb.parameter,trb.status,trb.control);

        xhci_ring_doorbell(xhci_controller, usb_dev->slot_id, usb_dev->in_ep);
        timing();
        xhci_ering_dequeue(xhci_controller,&trb);
        color_printk(GREEN,BLACK,"get usb rongliang trb p:%#lx s:%#x c%#x    \n",trb.parameter,trb.status,trb.control);

        color_printk(GREEN,BLACK, "cbw_ptr:%#lx cap16_ptr:%#lx csw_ptr:%#lx   \n", cbw, capacity_data, csw);
        color_printk(GREEN,BLACK, "usb lba:%#lx block_size:%#x    \n", big_to_little_endian_64(capacity_data->last_lba),
                     big_to_little_endian_32(capacity_data->block_size));

    }
}

//枚举usb设备
void usb_dev_enum(xhci_controller_t *xhci_controller) {
    xhci_trb_t trb;
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
    xhci_controller->op_reg->crcr = va_to_pa(xhci_controller->cmd_ring.ring_base) | TRB_CYCLE; //命令环物理地址写入crcr寄存器，置位rcs

    /*初始化事件环*/
    xhci_controller->event_ring.ring_base = kzalloc(
        align_up(TRB_COUNT * sizeof(xhci_trb_t), xhci_controller->align_size)); //分配事件环空间256* sizeof(xhci_trb_t) = 4K
    xhci_controller->event_ring.index = 0;
    xhci_controller->event_ring.status_c = TRB_CYCLE;
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
