#include "xhci.h"
#include "printk.h"
#include "pcie.h"
#include "bus.h"
#include "driver.h"
#include "vmalloc.h"
#include "usb.h"

////////////////////////////////////////////////////////////

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

//设置设备地址
static inline void xhci_address_device(struct usb_dev_t *usb_dev) {
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

///////////////////////////////////////////////////////////

xhci_cap_t *xhci_cap_find(xhci_controller_t *xhci_reg, uint8 cap_id) {
    uint32 offset = xhci_reg->cap_reg->hccparams1 >> 16;
    while (offset) {
        xhci_cap_t *xhci_cap = (void *) xhci_reg->cap_reg + (offset << 2);
        if ((xhci_cap->cap_id & 0xFF) == cap_id) return xhci_cap;
        offset = (xhci_cap->next_ptr >> 8) & 0xFF;
    }
    return NULL;
}

//xhci设备初始化驱动
int xhci_probe(pcie_dev_t *xhci_dev,pcie_id_t* id) {
    xhci_dev->dev.private = kzalloc(sizeof(xhci_controller_t)); //设备私有数据空间申请一块内存，存放xhci相关信息
    xhci_controller_t *xhci_controller = xhci_dev->dev.private;
    xhci_dev->bar[0].vaddr = iomap(xhci_dev->bar[0].paddr,xhci_dev->bar[0].size,PAGE_4K_SIZE,PAGE_ROOT_RW_UC_4K);

    /*初始化xhci寄存器*/
    xhci_controller->cap_reg = xhci_dev->bar[0].vaddr; //xhci能力寄存器基地址
    xhci_controller->op_reg = xhci_dev->bar[0].vaddr + xhci_controller->cap_reg->cap_length; //xhci操作寄存器基地址
    xhci_controller->rt_reg = xhci_dev->bar[0].vaddr + xhci_controller->cap_reg->rtsoff; //xhci运行时寄存器基地址
    xhci_controller->db_reg = xhci_dev->bar[0].vaddr + xhci_controller->cap_reg->dboff; //xhci门铃寄存器基地址

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
    xhci_erst_t *erstba = kmalloc(align_up(sizeof(xhci_erst_t), xhci_controller->align_size)); //分配单事件环段表内存
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
        xhci_dev->bar[0].paddr, xhci_dev->msi_x_flags, xhci_controller->cap_reg->hcsparams1 & 0xFF,
        xhci_controller->cap_reg->hcsparams1 >> 8 & 0x7FF,
        xhci_controller->cap_reg->hcsparams1 >> 24, xhci_controller->cap_reg->hccparams1 >> 2 & 1,
        xhci_controller->context_size, spb_number,
        xhci_controller->op_reg->usbcmd, xhci_controller->op_reg->usbsts, xhci_controller->align_size,
        xhci_controller->rt_reg->intr_regs[0].iman,
        xhci_controller->rt_reg->intr_regs[0].imod, va_to_pa(xhci_controller->cmd_ring.ring_base),
        xhci_controller->op_reg->dcbaap, xhci_controller->rt_reg->intr_regs[0].erstba,
        xhci_controller->rt_reg->intr_regs[0].erdp);

    timing();

    usb_dev_scan(xhci_dev);

    color_printk(GREEN,BLACK, "\nUSBcmd:%#x  USBsts:%#x", xhci_controller->op_reg->usbcmd,
                 xhci_controller->op_reg->usbsts);
    while (1);
}

void xhci_remove(pcie_dev_t *xhci_dev) {

}

pcie_drv_t *xhci_drv_init(void) {
    pcie_drv_t *xhci_drv = kmalloc(sizeof(pcie_drv_t));
    xhci_drv->id_table = kzalloc(sizeof(pcie_id_t)*2);
    xhci_drv->id_table->class_code = XHCI_CLASS_CODE;
    xhci_drv->drv.name = "XHCI-driver";
    xhci_drv->probe = xhci_probe;
    xhci_drv->remove = xhci_remove;
    return xhci_drv;
}
