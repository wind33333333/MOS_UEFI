#include "usb.h"
#include "slub.h"
#include "vmm.h"
#include "printk.h"
#include "pcie.h"

device_type_t usb_dev_type = {"usb-dev"};
device_type_t usb_if_type = {"usb-if"};

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

//获取usb设备描述符
int usb_get_device_descriptor(usb_dev_t *usb_dev) {
    xhci_controller_t *xhci_controller = usb_dev->xhci_controller;
    usb_device_descriptor_t *dev_desc = kzalloc(align_up(sizeof(usb_device_descriptor_t), 64));

    //第一次先获取设备描述符前8字节，拿到max_pack_size后更新端点1，再重新获取描述符。
    trb_t trb;
    // Setup TRB
    setup_stage_trb(&trb, setup_stage_device, setup_stage_norm, setup_stage_in, usb_req_get_descriptor, 0x100, 0, 8, in_data_stage);
    xhci_ring_enqueue(&usb_dev->ep0, &trb);
    // Data TRB
    data_stage_trb(&trb, va_to_pa(dev_desc), 8, trb_in);
    xhci_ring_enqueue(&usb_dev->ep0, &trb);
    // Status TRB
    status_stage_trb(&trb, ENABLE_IOC, trb_out);
    xhci_ring_enqueue(&usb_dev->ep0, &trb);

    xhci_ring_doorbell(xhci_controller, usb_dev->slot_id, 1);
    timing();
    xhci_ering_dequeue(xhci_controller, &trb);

    //更新端点0的最大包
    uint32 max_packe_size = dev_desc->usb_version >= 0x300
                                ? 1 << dev_desc->max_packet_size0
                                : dev_desc->max_packet_size0;
    xhci_input_context_t *input_ctx = kzalloc(align_up(sizeof(xhci_input_context_t), xhci_controller->align_size));
    ep64_t ep_ctx;
    xhci_context_read(usb_dev->dev_ctx, &ep_ctx, xhci_controller->ctx_size, 1);
    ep_ctx.ep_type_size = 4 << 3 | max_packe_size << 16 | 3 << 1;
    xhci_input_context_add(input_ctx, &ep_ctx, xhci_controller->ctx_size, 1);
    evaluate_context_com_trb(&trb, va_to_pa(input_ctx), usb_dev->slot_id);
    xhci_ring_enqueue(&xhci_controller->cmd_ring, &trb);
    xhci_ring_doorbell(xhci_controller, 0, 0);
    timing();
    xhci_ering_dequeue(xhci_controller, &trb);
    kfree(input_ctx);

    //第二次获取整个设备描述符
    setup_stage_trb(&trb, setup_stage_device, setup_stage_norm, setup_stage_in, usb_req_get_descriptor, 0x100, 0,
                    sizeof(usb_device_descriptor_t),in_data_stage);
    xhci_ring_enqueue(&usb_dev->ep0, &trb);
    // Data TRB
    data_stage_trb(&trb, va_to_pa(dev_desc), sizeof(usb_device_descriptor_t), trb_in);
    xhci_ring_enqueue(&usb_dev->ep0, &trb);
    // Status TRB
    status_stage_trb(&trb, ENABLE_IOC, trb_out);
    xhci_ring_enqueue(&usb_dev->ep0, &trb);

    xhci_ring_doorbell(xhci_controller, usb_dev->slot_id, 1);
    timing();
    xhci_ering_dequeue(xhci_controller, &trb);

    usb_dev->dev_desc = dev_desc;
    return 0;
}

//获取usb配置描述符
int usb_get_config_descriptor(usb_dev_t *usb_dev) {
    xhci_controller_t *xhci_controller = usb_dev->xhci_controller;
    usb_config_descriptor_t *config_desc = kzalloc(align_up(sizeof(usb_config_descriptor_t), 64));

    //第一次先获取配置描述符前9字节
    trb_t trb;
    setup_stage_trb(&trb, setup_stage_device, setup_stage_norm, setup_stage_in, usb_req_get_descriptor, 0x200, 0, 9, in_data_stage);
    xhci_ring_enqueue(&usb_dev->ep0, &trb);
    // Data TRB
    data_stage_trb(&trb, va_to_pa(config_desc), 9, trb_in);
    xhci_ring_enqueue(&usb_dev->ep0, &trb);
    // Status TRB
    status_stage_trb(&trb, ENABLE_IOC, trb_out);
    xhci_ring_enqueue(&usb_dev->ep0, &trb);

    // 响铃
    xhci_ring_doorbell(xhci_controller, usb_dev->slot_id, 1);
    timing();
    xhci_ering_dequeue(xhci_controller, &trb);

    //第二次从配置描述符中得到总长度获取整个配置描述符
    uint16 config_desc_length = config_desc->total_length;
    kfree(config_desc);
    config_desc = kzalloc(align_up(config_desc_length, 64));

    setup_stage_trb(&trb, setup_stage_device, setup_stage_norm, setup_stage_in, usb_req_get_descriptor, 0x200, 0,
                    config_desc_length, in_data_stage);
    xhci_ring_enqueue(&usb_dev->ep0, &trb);
    // Data TRB
    data_stage_trb(&trb, va_to_pa(config_desc), config_desc_length, trb_in);
    xhci_ring_enqueue(&usb_dev->ep0, &trb);
    // Status TRB
    status_stage_trb(&trb, ENABLE_IOC, trb_out);
    xhci_ring_enqueue(&usb_dev->ep0, &trb);

    // 响铃
    xhci_ring_doorbell(xhci_controller, usb_dev->slot_id, 1);
    timing();
    xhci_ering_dequeue(xhci_controller, &trb);

    usb_dev->config_desc = config_desc;
    return 0;
}

//获取字符串描述符
int usb_get_string_descriptor(usb_dev_t *usb_dev) {
    xhci_controller_t *xhci_controller = usb_dev->xhci_controller;
    trb_t trb;

    usb_string_descriptor_t *language_desc = kzalloc(8);
    //获取语言ID描述符
    setup_stage_trb(&trb, setup_stage_device, setup_stage_norm, setup_stage_in, usb_req_get_descriptor, 0x300, 0, 8, in_data_stage);
    xhci_ring_enqueue(&usb_dev->ep0, &trb);
    // Data TRB
    data_stage_trb(&trb, va_to_pa(language_desc), 8, trb_in);
    xhci_ring_enqueue(&usb_dev->ep0, &trb);
    // Status TRB
    status_stage_trb(&trb, ENABLE_IOC, trb_out);
    xhci_ring_enqueue(&usb_dev->ep0, &trb);

    // 响铃
    xhci_ring_doorbell(xhci_controller, usb_dev->slot_id, 1);
    timing();
    xhci_ering_dequeue(xhci_controller, &trb);

    uint16 language_id;
    if (language_desc->head.descriptor_type == USB_STRING_DESCRIPTOR) {
        language_id = language_desc->string[0];
        usb_dev->language_desc = language_desc;
    }else {
        language_id = 0x0409;
        kfree(language_desc);
    }

    //默认设备都支持美式英语
    uint8 string_index[3] = {
        usb_dev->dev_desc->manufacturer_index, usb_dev->dev_desc->product_index,
        usb_dev->dev_desc->serial_number_index
    };
    usb_string_descriptor_t *string_desc[3];
    uint8 *string_ascii[3];
    usb_string_descriptor_t *string_desc_head = kzalloc(8);

    //获取制造商/产品型号/序列号字符串描述符
    for (uint8 i = 0; i < 3; i++) {
        if (string_index[i]) {
            //第一次先获取长度
            setup_stage_trb(&trb, setup_stage_device, setup_stage_norm, setup_stage_in, usb_req_get_descriptor,
                            0x300 | string_index[i], language_id, 2, in_data_stage);
            xhci_ring_enqueue(&usb_dev->ep0, &trb);
            // Data TRB
            data_stage_trb(&trb, va_to_pa(string_desc_head), 2, trb_in);
            xhci_ring_enqueue(&usb_dev->ep0, &trb);
            // Status TRB
            status_stage_trb(&trb, ENABLE_IOC, trb_out);
            xhci_ring_enqueue(&usb_dev->ep0, &trb);

            // 响铃
            xhci_ring_doorbell(xhci_controller, usb_dev->slot_id, 1);
            timing();
            xhci_ering_dequeue(xhci_controller, &trb);

            //分配内存
            uint8 string_desc_length = string_desc_head->head.length;
            string_desc[i] = kzalloc(string_desc_length);

            //第二次先正式获取字符串描述符N
            setup_stage_trb(&trb, setup_stage_device, setup_stage_norm, setup_stage_in, usb_req_get_descriptor,
                            0x300 | string_index[i], language_id, string_desc_length, in_data_stage);
            xhci_ring_enqueue(&usb_dev->ep0, &trb);
            // Data TRB
            data_stage_trb(&trb, va_to_pa(string_desc[i]), string_desc_length, trb_in);
            xhci_ring_enqueue(&usb_dev->ep0, &trb);
            // Status TRB
            status_stage_trb(&trb, ENABLE_IOC, trb_out);
            xhci_ring_enqueue(&usb_dev->ep0, &trb);

            // 响铃
            xhci_ring_doorbell(xhci_controller, usb_dev->slot_id, 1);
            timing();
            xhci_ering_dequeue(xhci_controller, &trb);

            //解析字符串描述符
            uint8 string_ascii_length = (string_desc_length-2)/2;
            string_ascii[i] = kzalloc(string_ascii_length+1);
            utf16le_to_ascii(string_desc[i]->string,string_ascii[i],string_ascii_length);
        }else {
            string_desc[i] = NULL;
        }
    }

    usb_dev->manufacturer_desc = string_desc[0];
    usb_dev->product_desc = string_desc[1];
    usb_dev->serial_number_desc = string_desc[2];
    usb_dev->manufacturer = string_ascii[0];
    usb_dev->product = string_ascii[1];
    usb_dev->serial_number = string_ascii[2];
    kfree(string_desc_head);
    return 0;
}

//激活usb配置
int usb_set_config(usb_dev_t *usb_dev) {
    xhci_controller_t *xhci_controller = usb_dev->xhci_controller;
    trb_t trb;

    setup_stage_trb(&trb, setup_stage_device, setup_stage_norm, setup_stage_out, usb_req_set_config,
                    usb_dev->config_desc->configuration_value, 0, 0, no_data_stage);
    xhci_ring_enqueue(&usb_dev->ep0, &trb);

    status_stage_trb(&trb, ENABLE_IOC, trb_in);
    xhci_ring_enqueue(&usb_dev->ep0, &trb);

    xhci_ring_doorbell(xhci_controller, usb_dev->slot_id, 1);
    timing();
    xhci_ering_dequeue(xhci_controller, &trb);
    return 0;
}

//激活接口
int usb_set_interface(usb_if_t *usb_if) {
    usb_dev_t *usb_dev = usb_if->usb_dev;
    xhci_controller_t *xhci_controller = usb_dev->xhci_controller;
    trb_t trb;

    setup_stage_trb(&trb, setup_stage_interface, setup_stage_norm, setup_stage_out, usb_req_set_interface,
                    usb_if->cur_alt->altsetting, usb_if->if_num, 0, no_data_stage);

    uint64 setup_ptr = xhci_ring_enqueue(&usb_dev->ep0, &trb);

    status_stage_trb(&trb, ENABLE_IOC, trb_in);

    uint64 status_ptr = xhci_ring_enqueue(&usb_dev->ep0, &trb);

    xhci_ring_doorbell(xhci_controller, usb_dev->slot_id, 1);

    int32 comp_code = xhci_wait_for_completion(xhci_controller, setup_ptr, 20000000);
    if (comp_code == -1) {
        comp_code = xhci_wait_for_completion(xhci_controller, status_ptr, 20000000);
    }

    if (comp_code != XHCI_COMP_SUCCESS) {
        color_printk(RED,BLACK,"ep0 c:%#x t:%#x \n",usb_dev->dev_ctx->dev_ctx32.ep[0].ep_config,usb_dev->dev_ctx->dev_ctx32.ep[0].ep_type_size);
        color_printk(RED,BLACK,"usb set if error code:%#x   \n",comp_code);
        while (1);
    }

    return 0;
}

/**
 * @brief xHCI 统一传输执行与自动错误抢救守护函数
 * * 这个函数封装了底层的轮询/中断等待逻辑。一旦检测到 U 盘/鼠标发生了 STALL 卡死，
 * 它会自动介入主板硬件层，执行 "Reset -> 挪指针 -> Clear Feature" 的完整心肺复苏。
 * * @param udev      USB 设备上下文指针
 * @param ep_dci       发生通信的端点 DCI (1 是控制端点 EP0, 2/3/4... 是普通端点)
 * @param wait_trb_pa  你刚刚在传输环中入队的最后一条 TRB 的物理地址
 * @param timeout_us   超时时间 (微秒)
 * @return xhci_comp_code_t 返回强类型的硬件完成码
 */
xhci_trb_comp_code_e xhci_execute_transfer_sync(usb_dev_t *udev, uint8 ep_dci, uint64 wait_trb_pa, uint32 timeout_us) {
    xhci_controller_t *xhci = udev->xhci_controller;

    // ==========================================================
    // 1. 挂起等待事件环 (Event Ring) 的回执
    // ==========================================================
    xhci_trb_comp_code_e comp_code = xhci_wait_for_completion(xhci, wait_trb_pa, timeout_us);

    // ==========================================================
    // 2. 完美成功或可接受的短包 (直接放行)
    // ==========================================================
    if (comp_code == XHCI_COMP_SUCCESS) {
        return comp_code;
    }

    // 在 BOT 协议的 Data IN 阶段，设备实际发送的数据少于我们请求的长度是合法的
    if (comp_code == XHCI_COMP_SHORT_PACKET) {
        // 注意：此处最好能打印出实际收到的数据长度，便于上层文件系统裁剪缓冲区
        color_printk(YELLOW, BLACK, "xHCI: Short Packet on EP (DCI=%d) - Normal for BOT.\n", ep_dci);
        return comp_code;
    }

    // ==========================================================
    // 3. 核心护城河：拦截端点 Halted 异常并全自动抢救
    // 涵盖：逻辑拒绝 (STALL) 与 物理链路崩溃 (Transaction/Babble)
    // ==========================================================
    if (comp_code == XHCI_COMP_STALL_ERROR || comp_code == XHCI_COMP_USB_TRANSACTION_ERROR || comp_code == XHCI_COMP_BABBLE_ERROR) {

        color_printk(YELLOW, BLACK, "xHCI: Halt condition (%d) on EP (DCI=%d)! Auto-Recovery engaged...\n", comp_code, ep_dci);

        // 抢救第 1 步：主板级解挂 (强制从 Halted 拽回 Stopped)
        xhci_reset_endpoint(xhci, udev->slot_id, ep_dci, 0);

        // 抢救第 2 步：跨越“坏死”的 TRB 尸体
        // 必须区分目标环：EP0 的环独立存放在 usb_dev->ep0，其他端点在 eps 数组里
        xhci_cmd_set_tr_deq_ptr(xhci, udev->slot_id, ep_dci, &udev->eps[ep_dci - 1].transfer_ring);

        // 抢救第 3 步：协议级和解 (只针对 STALL 错误)
        if (comp_code == XHCI_COMP_STALL_ERROR) {
            if (ep_dci > 1) {
                // 普通端点且是因为 STALL 挂起：必须发和解信
                color_printk(YELLOW, BLACK, "xHCI: Sending Clear Feature to unlock physical endpoint...\n");
                usb_clear_feature_halt(udev, ep_dci);
            } else {
                // EP0 的 STALL：靠下一个 Setup 包自动冲刷，什么都不用做
                color_printk(GREEN, BLACK, "xHCI: EP0 STALL CPR complete.\n");
            }
        } else {
            // 对于 Transaction 或 Babble 等物理错误：U 盘并未傲娇，坚决不能发 Clear Feature
            color_printk(YELLOW, BLACK, "xHCI: Physical Bus Error CPR complete. (No Clear Feature needed).\n");
        }

        // 抢救完毕！向上层如实汇报错误码
        return comp_code;
    }

    // ==========================================================
    // 4. 极其致命的灾难性错误 (超时、物理断开、驱动 Bug)
    // ==========================================================
    color_printk(RED, BLACK, "xHCI: FATAL Transfer Error %d on EP (DCI=%d)!\n", comp_code, ep_dci);

    // 诊断：检查是不是极其恶劣的 U 盘直接物理掉线了
    if (comp_code == XHCI_COMP_TIMEOUT) {
        uint32 portsc = xhci_read_portsc(xhci, udev->port_id);
        if ((portsc & 0x01) == 0) { // PORTSC 的 Bit 0 (CCS) 为 0 表示物理断开
            color_printk(RED, BLACK, "xHCI: Diag - Device forcefully disconnected from Port!\n");
            return XHCI_COMP_INVALID; // 设备已拔出，没救了
        } else {
            color_printk(RED, BLACK, "xHCI: Diag - Device still plugged in, but ignored doorbell. Firmware crash?\n");
        }
    }

    // 返回那些致命错误码 (比如 XHCI_COMP_TRB_ERROR)，让上层直接放弃这个任务
    return comp_code;
}


/**
 * 标准 USB 控制传输统一封装函数 (EP0 专属) - 结构体传参版
 * @param udev       USB 设备上下文
 * @param usb_req_pkg           指向 8 字节标准 Setup 结构体的指针
 * @param data_buf      数据缓冲区指针 (无数据阶段填 NULL)
 * @param timeout_us    超时时间 (微秒)
 * @return int32        0 表示成功，其他为错误码
 */
int32 usb_control_msg(usb_dev_t *udev, usb_req_pkg_t *usb_req_pkg, void *data_buf, uint32 timeout_us) {
    xhci_controller_t *xhci_controller = udev->xhci_controller;
    xhci_trb_t trb;
    uint64 setup_ptr, data_ptr = 0, status_ptr;
    xhci_trb_comp_code_e comp_code;

    uint16 length = usb_req_pkg->length;

    // 解析trb方向
    uint8 usb_req_dir = usb_req_pkg->dtd;

    // ==========================================================
    // 阶段 1：组装 Setup TRB
    // ==========================================================
    asm_mem_set(&trb, 0, sizeof(xhci_trb_t));
    asm_mem_cpy(usb_req_pkg,&trb,sizeof(usb_req_pkg_t)); //拷贝USB请求包到TRB前8字节中
    trb.setup_stage.trb_tr_len = sizeof(usb_req_pkg_t); //steup trb必须8
    trb.setup_stage.int_target = 0;     //中断号暂时统一设置0
    trb.setup_stage.idt = TRB_IDT_ENABLE;   // setup trb 必须1
    trb.setup_stage.type = XHCI_TRB_TYPE_SETUP_STAGE;
    trb.setup_stage.chain = TRB_CHAIN_DISABLE;
    trb.setup_stage.ioc = TRB_CHAIN_ENABLE;
    // 判断 TRT (Transfer Type)
    if (length == 0) {
        trb.setup_stage.trt = TRB_TRT_NO_DATA;
    } else if (usb_req_pkg->dtd == USB_DIR_IN) {
        trb.setup_stage.trt = TRB_TRT_IN_DATA;
    } else {
        trb.setup_stage.trt = TRB_TRT_OUT_DATA;
    }

    setup_ptr = xhci_ring_enqueue(&udev->eps[0].transfer_ring, &trb);

    // ==========================================================
    // 阶段 2：组装 Data TRB (如果有)
    // ==========================================================
    if (length != 0 && data_buf != NULL) {
        asm_mem_set(&trb, 0, sizeof(xhci_trb_t));
        trb.data_stage.data_buf_ptr = va_to_pa(data_buf); // 物理地址
        trb.data_stage.tr_len = length;
        trb.data_stage.type = XHCI_TRB_TYPE_DATA_STAGE;
        trb.data_stage.dir = usb_req_dir;  //数据阶段方向和usb.dtd方向一致
        trb.data_stage.chain = TRB_CHAIN_DISABLE; // 单个 Data TRB 必须为 0
        trb.data_stage.ioc = TRB_IOC_ENABLE;   // 开启中断防雷

        data_ptr = xhci_ring_enqueue(&udev->eps[0].transfer_ring, &trb);
    }

    // ==========================================================
    // 阶段 3：组装 Status TRB
    // ==========================================================
    asm_mem_set(&trb, 0, sizeof(xhci_trb_t));
    trb.status_stage.type = XHCI_TRB_TYPE_STATUS_STAGE;
    trb.status_stage.chain = TRB_CHAIN_DISABLE;
    trb.status_stage.ioc = TRB_IOC_ENABLE;
    trb.status_stage.dir = (length == 0 || usb_req_dir == USB_DIR_OUT) ? TRB_DIR_IN : TRB_DIR_OUT; // ★ 核心逻辑 2：Status 阶段的方向必须是相反的！

    status_ptr = xhci_ring_enqueue(&udev->ep0, &trb);

    // ==========================================================
    // 阶段 4：一次性鸣笛发车！
    // ==========================================================
    xhci_ring_doorbell(xhci_controller, udev->slot_id, 1);

    // ==========================================================
    // 阶段 5：步步为营的守护监听 (复用你写的完美错误处理函数)
    // ==========================================================
    // 监听 Setup 阶段
    comp_code = xhci_execute_transfer_sync(udev, 1, setup_ptr, timeout_us);
    if (comp_code != 0) return comp_code; // 如果 U 盘拒收命令，它会在这里 STALL 并被自动救活

    // 监听 Data 阶段
    if (length != 0 && data_buf != NULL) {
        comp_code = xhci_execute_transfer_sync(udev, 1, data_ptr, timeout_us);
        if (comp_code != 0 && comp_code != XHCI_COMP_SHORT_PACKET) return comp_code;
    }

    // 监听 Status 阶段
    comp_code = xhci_execute_transfer_sync(udev, 1, status_ptr, timeout_us);
    return comp_code;
}

/**
 * 清除 USB 端点的 STALL/Halt 状态 (撬开大门)
 * @param usb_dev      USB 设备上下文
 * @param ep_dci xHCI 的端点上下文索引 (DCI, 范围 2-31)
 */
int32 usb_clear_feature_halt(usb_dev_t *usb_dev, uint8 ep_dci) {
    xhci_controller_t *xhci_controller = usb_dev->xhci_controller;
    uint8 ep_addr = epdci_to_epaddr(ep_dci);
    xhci_trb_t trb = {0};

    // 2. 组装 8 字节的标准 Setup 请求包
    trb.setup_stage.recipient = USB_RECIP_ENDPOINT;
    trb.setup_stage.req_type = USB_REQ_TYPE_STANDARD;
    trb.setup_stage.dtd = USB_DIR_OUT;
    trb.setup_stage.request = USB_REQ_CLEAR_FEATURE;
    trb.setup_stage.value = USB_FEATURE_ENDPOINT_HALT;
    trb.setup_stage.index = ep_addr;
    trb.setup_stage.length = 0;

    trb.setup_stage.trb_tr_len = 8;
    trb.setup_stage.int_target = 0;
    trb.setup_stage.chain = 0;
    trb.setup_stage.ioc = 1;
    trb.setup_stage.idt = 1;
    trb.setup_stage.type = XHCI_TRB_TYPE_SETUP_STAGE;
    trb.setup_stage.trt = TRB_TRT_NO_DATA;
    uint64 setup_ptr = xhci_ring_enqueue(&usb_dev->ep0, (void*)&trb);

    asm_mem_set(&trb,0,sizeof(trb));
    trb.status_stage.int_target = 0;
    trb.status_stage.chain = 0;
    trb.status_stage.ioc = 1;
    trb.status_stage.type = XHCI_TRB_TYPE_STATUS_STAGE;
    trb.status_stage.dir = 1;
    uint64 status_ptr = xhci_ring_enqueue(&usb_dev->ep0, (void*)&trb);

    xhci_ring_doorbell(xhci_controller, usb_dev->slot_id, 1);

    int32 completion_code = xhci_wait_for_completion(xhci_controller,setup_ptr,500000000);
    if (completion_code != XHCI_COMP_SUCCESS) {
        // 如果这里打印出了 0x06 (STALL)，真相就大白了！
        color_printk(RED, BLACK, "USB: Clear Feature FATAL at SETUP stage! comp_code: %#x\n", completion_code);
        // 注意：此时 EP0 已经被 U 盘搞死锁 (Halted) 了！
        while (1);
    }

    completion_code = xhci_wait_for_completion(xhci_controller,status_ptr,500000000);
    if (completion_code != XHCI_COMP_SUCCESS) {
        color_printk(RED, BLACK, "USB: Clear Feature (Halt) failed on EP %#x  comp_code:%#x  !\n  ", ep_addr,completion_code);
        while (1);
    }
    color_printk(GREEN, BLACK, "USB: Clear Feature (Halt) sueccess ep:%d  !\n  ", ep_addr);

    color_printk(RED,BLACK,"ep:%d c:%#x t:%#x \n",ep_dci,usb_dev->dev_ctx->dev_ctx32.ep[ep_dci-1].ep_config,usb_dev->dev_ctx->dev_ctx32.ep[ep_dci-1].ep_type_size);
    color_printk(RED,BLACK,"ep0 c:%#x t:%#x \n",usb_dev->dev_ctx->dev_ctx32.ep[0].ep_config,usb_dev->dev_ctx->dev_ctx32.ep[0].ep_type_size);

    return completion_code;
}

//初始化端点
int usb_endpoint_init(usb_if_alt_t *if_alt) {
    usb_dev_t *usb_dev = if_alt->usb_if->usb_dev;
    xhci_controller_t *xhci_controller = usb_dev->xhci_controller;
    xhci_input_context_t *input_ctx = kzalloc(align_up(sizeof(xhci_input_context_t), xhci_controller->align_size));
    slot64_t slot_ctx = {0};
    ep64_t ep_ctx = {0};
    trb_t trb;
    //配置端点
    uint8 max_ep_num = 0;
    for (uint8 i = 0; i < if_alt->ep_count; i++) {
        usb_ep_t *ep_phy = &if_alt->eps[i];
        uint8 ep_num = ep_phy->ep_dci;
        if (ep_num > max_ep_num) max_ep_num = ep_num;
        endpoint_t *ep_vir = &usb_dev->eps[ep_num];
        uint32 ep_config = 0;
        uint64 tr_dequeue_ptr = 0;
        uint32 max_streams = ep_phy->max_streams > MAX_STREAMS ? MAX_STREAMS : ep_phy->max_streams;
        if (max_streams) {
            ep_config = (max_streams << 10) | (1 << 15); // MaxPStreams，LSA=1，如果使用线性数组（可选，根据实现）
            // 有流：分配Stream Context Array和per-stream rings
            uint32 streams_count = 1 << max_streams;
            uint32 streams_ctx_array_count = 1 << (max_streams + 1);
            xhci_stream_ctx_t *stream_ctx_array = kzalloc(streams_ctx_array_count * sizeof(xhci_stream_ctx_t));
            xhci_ring_t *stream_rings = kzalloc(streams_ctx_array_count * sizeof(xhci_ring_t)); //streams0 保留内存需要对齐;
            ep_vir->stream_rings = stream_rings;
            ep_vir->streams_count = streams_count;

            for (uint32 s = 1; s <= streams_count; s++) {
                // Stream ID从1开始
                xhci_ring_init(&stream_rings[s], xhci_controller->align_size);
                stream_ctx_array[s].tr_dequeue = va_to_pa(stream_rings[s].ring_base) | 1 | 1 << 1;
                stream_ctx_array[s].reserved = 0;
            }
            // Stream ID 0保留，通常设为0或无效
            stream_ctx_array[0].tr_dequeue = 0;
            stream_ctx_array[0].reserved = 0;
            tr_dequeue_ptr = va_to_pa(stream_ctx_array);
        } else {
            // 无流：单个Transfer Ring
            xhci_ring_init(&ep_vir->transfer_ring, xhci_controller->align_size);
            tr_dequeue_ptr = va_to_pa(ep_vir->transfer_ring.ring_base) | 1; // DCS=1
            ep_config = 0;
        }
        ep_ctx.ep_config = ep_config;
        ep_ctx.ep_type_size = ep_phy->ep_type << 3 | ep_phy->max_packet << 16 | ep_phy->max_burst << 8 | 3 << 1;
        ep_ctx.tr_dequeue_ptr = tr_dequeue_ptr;
        ep_ctx.trb_payload = 0;
        xhci_input_context_add(input_ctx, &ep_ctx, xhci_controller->ctx_size, ep_num);
    }

    //配置slot
    slot_ctx.route_speed = (max_ep_num << 27) | (
                               (xhci_controller->op_reg->portregs[usb_dev->port_id - 1].portsc & 0x3C00) << 10);
    slot_ctx.latency_hub = usb_dev->port_id << 16;
    slot_ctx.parent_info = 0;
    slot_ctx.addr_status = 0;
    xhci_input_context_add(input_ctx, &slot_ctx, xhci_controller->ctx_size, 0);

    config_endpoint_com_trb(&trb, va_to_pa(input_ctx), usb_dev->slot_id);
    xhci_ring_enqueue(&xhci_controller->cmd_ring, &trb);
    xhci_ring_doorbell(xhci_controller, 0, 0);
    timing();
    xhci_ering_dequeue(xhci_controller, &trb);
    kfree(input_ctx);
    return 0;
}

//匹配驱动id
static inline usb_id_t *usb_match_id(usb_if_t *usb_if, driver_t *drv) {
    usb_id_t *id_table = drv->id_table;
    uint8 if_class = usb_if->cur_alt->if_class;
    uint8 if_protocol = usb_if->cur_alt->if_protocol;
    uint8 if_subclass = usb_if->cur_alt->if_subclass;
    for (; id_table->if_class || id_table->if_protocol || id_table->if_subclass; id_table++) {
        if (id_table->if_class == if_class && id_table->if_protocol == if_protocol && id_table->if_subclass ==
            if_subclass)
            return id_table;
    }
    return NULL;
}

//usb总线层设备驱动匹配
int usb_bus_match(device_t *dev, driver_t *drv) {
    if (dev->type != &usb_if_type) return FALSE;
    usb_if_t *usb_if = CONTAINER_OF(dev, usb_if_t, dev);
    usb_id_t *id = usb_match_id(usb_if, drv);
    return id ? 1 : 0;
}

//usb总线层探测初始化回调
int usb_bus_probe(device_t *dev) {
}

//usb总线层卸载在回调
void usb_bus_remove(device_t *dev) {
}

//usb驱动层探测初始化回调
int usb_drv_probe(device_t *dev) {
    usb_if_t *usb_if = CONTAINER_OF(dev, usb_if_t, dev);
    usb_drv_t *usb_if_drv = CONTAINER_OF(dev->drv, usb_drv_t, drv);
    usb_id_t *id = usb_match_id(usb_if,dev->drv);
    usb_if_drv->probe(usb_if, id);
    return 0;
}

//usb驱动层卸载回调
void usb_drv_remove(device_t *dev) {
}

//注册usb接口
static inline void usb_if_register(usb_if_t *usb_if) {
    device_register(&usb_if->dev);
}

//注册usb设备
static inline void usb_dev_register(usb_dev_t *usb_dev) {
    device_register(&usb_dev->dev);
}

//注册usb驱动
void usb_drv_register(usb_drv_t *usb_drv) {
    usb_drv->drv.bus = &usb_bus_type;
    usb_drv->drv.probe = usb_drv_probe;
    usb_drv->drv.remove = usb_drv_remove;
    driver_register(&usb_drv->drv);
}

//解析端点
int usb_parse_endpoints(usb_dev_t *usb_dev, usb_if_alt_t *if_alt) {
    usb_ep_t *cur_ep = NULL;
    usb_descriptor_head *desc_head = usb_get_next_desc(&if_alt->if_desc->head);
    uint8 ep_idx = 0;
    void *cfg_end = usb_cfg_end(usb_dev->config_desc);
    while (desc_head < cfg_end && desc_head->descriptor_type != USB_INTERFACE_DESCRIPTOR) {
        if (desc_head->descriptor_type == USB_ENDPOINT_DESCRIPTOR) {
            usb_endpoint_descriptor_t *ep_desc = (usb_endpoint_descriptor_t *) desc_head;
            cur_ep = &if_alt->eps[ep_idx++];
            cur_ep->ep_dci = epaddr_to_epdci(ep_desc->endpoint_address);
            cur_ep->ep_type = ((ep_desc->endpoint_address & 0x80) >> 5) + (ep_desc->attributes & 3); //计算端点传输类型
            cur_ep->max_packet = ep_desc->max_packet_size & 0x07FF;
            cur_ep->mult = (ep_desc->max_packet_size >> 11) & 0x3;
            cur_ep->interval = ep_desc->interval;
            cur_ep->max_burst = 0;
            cur_ep->max_streams = 0;
            cur_ep->bytes_per_interval = 0;
            cur_ep->extras_desc = NULL;
        } else if (desc_head->descriptor_type == USB_SUPERSPEED_COMPANION_DESCRIPTOR) {
            usb_superspeed_companion_descriptor_t *ss_desc = (usb_superspeed_companion_descriptor_t *) desc_head;
            cur_ep->max_burst = ss_desc->max_burst;
            cur_ep->bytes_per_interval = ss_desc->bytes_per_interval;
            cur_ep->max_streams = ss_desc->attributes & 0x1F;
        } else {
            if (cur_ep && !cur_ep->extras_desc) cur_ep->extras_desc = desc_head; //仅保存扫描到的第一条其他类型描述符
        };
        desc_head = usb_get_next_desc(desc_head);
    }
    return 0;
}

//usb接口创建并注册总线
int usb_if_create_register(usb_dev_t *usb_dev) {
    uint8 alt_count[256]; //每个接口的替用接口数量
    usb_if_t *usb_if_map[256]; //usb_if临时缓存区
    uint8 fill_idx[256]; //下一个alts计数

    asm_mem_set(alt_count, 0, sizeof(alt_count));
    asm_mem_set(usb_if_map, 0, sizeof(usb_if_map));
    asm_mem_set(fill_idx, 0, sizeof(fill_idx));

    //给接口分配内存
    usb_dev->interfaces_count = 0;
    usb_dev->interfaces = kzalloc(sizeof(usb_if_t) * usb_dev->config_desc->num_interfaces);

    //统计每个接口的替用接口数量
    usb_interface_descriptor_t *if_desc = (usb_interface_descriptor_t *) usb_dev->config_desc;
    void *cfg_end = usb_cfg_end(usb_dev->config_desc);
    while (if_desc < cfg_end) {
        if (if_desc->head.descriptor_type == USB_INTERFACE_DESCRIPTOR) {
            alt_count[if_desc->interface_number]++;
        }
        if_desc = usb_get_next_desc(&if_desc->head);
    }

    //解析alt_cout分配usb_if_alt内存
    for (uint32 i = 0; i < 256; i++) {
        if (alt_count[i]) {
            usb_if_t *usb_if = &usb_dev->interfaces[usb_dev->interfaces_count++];
            usb_if->if_num = i;
            usb_if->alt_count = alt_count[i];
            usb_if->alts = kzalloc(sizeof(usb_if_alt_t) * usb_if->alt_count);
            usb_if->usb_dev = usb_dev;
            usb_if->dev.type = &usb_if_type;
            usb_if->dev.parent = &usb_dev->dev;
            usb_if->dev.bus = &usb_bus_type;
            usb_if_map[i] = usb_if; //把usb_if缓存在usb_if_map中
        }
    }

    //填充每个usb_if_alt
    if_desc = (usb_interface_descriptor_t *) usb_dev->config_desc;
    while (if_desc < cfg_end) {
        if (if_desc->head.descriptor_type == USB_INTERFACE_DESCRIPTOR) {
            usb_if_t *usb_if = usb_if_map[if_desc->interface_number];
            uint8 idx = fill_idx[if_desc->interface_number]++;
            usb_if_alt_t *if_alt = &usb_if->alts[idx];
            if_alt->usb_if = usb_if;
            if_alt->if_desc = if_desc;
            if_alt->altsetting = if_desc->alternate_setting;
            if_alt->if_class = if_desc->interface_class;
            if_alt->if_subclass = if_desc->interface_subclass;
            if_alt->if_protocol = if_desc->interface_protocol;
            if_alt->ep_count = if_desc->num_endpoints;
            if_alt->eps = kzalloc(if_alt->ep_count * sizeof(usb_ep_t)); //给端点分配内存
            /* 可选：此处不解析端点，延后到 probe；或预解析以便 match/probe 快速使用 */
            /* usb_parse_alt_endpoints(usb_dev, alt); */
            usb_parse_endpoints(usb_dev, if_alt);
        }
        if_desc = usb_get_next_desc(&if_desc->head);
    }

    /* 设置 cur_alt（优先 alt0，否则第一个），然后延迟注册（触发 match/probe） */
    for (uint32 i = 0; i < usb_dev->interfaces_count; i++) {
        usb_if_t *usb_if = &usb_dev->interfaces[i];
        if (usb_if) {
            usb_if_alt_t *alt0 = usb_find_alt_by_num(usb_if, 0);
            usb_if->cur_alt = alt0 ? alt0 : &usb_if->alts[0];

            /* 可选：只解析当前 alt 的端点，保证驱动 probe 一上来就能拿到 ep_count */
            /* usb_parse_alt_endpoints(usb_dev, uif->cur_alt); */

            /* 延迟注册：到这里 uif/alt 数据已完整 */
            usb_if_register(usb_if);
        }
    }
    return 0;
}

//创建usb设备
usb_dev_t *usb_dev_create(pcie_dev_t *xhci_dev, uint32 port_id) {
    usb_dev_t *usb_dev = kzalloc(sizeof(usb_dev_t));
    usb_dev->xhci_controller = xhci_dev->dev.drv_data;
    usb_dev->port_id = port_id + 1;
    usb_dev->slot_id = xhci_enable_slot(usb_dev); //启用插槽
    xhci_address_device(usb_dev); //设置设备地址
    usb_get_device_descriptor(usb_dev); //获取设备描述符
    usb_get_config_descriptor(usb_dev); //获取配置描述符
    usb_get_string_descriptor(usb_dev); //获取字符串描述符
    usb_set_config(usb_dev); //激活配置
    usb_dev->dev.type = &usb_dev_type;
    usb_dev->dev.parent = &xhci_dev->dev;
    usb_dev->dev.bus = &usb_bus_type;
    return usb_dev;
}

//usb设备初始化
void usb_dev_scan(pcie_dev_t *xhci_dev) {
    xhci_controller_t *xhci_controller = xhci_dev->dev.drv_data;
    trb_t trb;
    for (uint8 i = 0; i < xhci_controller->max_ports; i++) {
        if ((xhci_controller->op_reg->portregs[i].portsc & XHCI_PORTSC_CCS) && xhci_controller->op_reg->portregs[i].
            portsc & (XHCI_PORTSC_CSC | XHCI_PORTSC_PRC)) {
            //检测端口是否有设备
            uint8 spc_idx = xhci_controller->port_to_spc[i];
            if (xhci_controller->spc[spc_idx].major_bcd < 0x3) {
                //usb2.0
                uint32 pr = XHCI_PORTSC_PR | XHCI_PORTSC_PP;
                xhci_controller->op_reg->portregs[i].portsc = pr;
                timing();
                xhci_ering_dequeue(xhci_controller, &trb);
            }
            //usb3.x
            while (!(xhci_controller->op_reg->portregs[i].portsc & XHCI_PORTSC_PED)) asm_pause();
            usb_dev_t *usb_dev = usb_dev_create(xhci_dev, i);
            usb_dev_register(usb_dev);
            usb_if_create_register(usb_dev);
            color_printk(GREEN, BLACK, "ports[%d]:%#x    \n", i + 1, xhci_controller->op_reg->portregs[i].portsc);
            uint32 w1c = xhci_controller->op_reg->portregs[i].portsc;
            w1c &= (XHCI_PORTSC_W1C_MASK | XHCI_PORTSC_PP);
            xhci_controller->op_reg->portregs[i].portsc = w1c;
            color_printk(GREEN, BLACK, "ports[%d]:%#x    \n", i + 1, xhci_controller->op_reg->portregs[i].portsc);
            timing();
        }
    }
}
