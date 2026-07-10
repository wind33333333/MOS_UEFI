#include "xhci.h"
#include "xhci-ring.h"
#include "xhci-service.h"
#include "usb-core.h"
#include "printk.h"
#include "vmm.h"
#include "errno.h"
#include "usb-hub.h"
#include "slub.h"


// 提取大于等于 val 的下一个 2 的幂次 (内核神技)
// 如果 val = 12KB，算出来就是 16KB
static inline uint64 roundup_pow_of_two(uint64 val) {
    val--;
    val |= val >> 1;
    val |= val >> 2;
    val |= val >> 4;
    val |= val >> 8;
    val |= val >> 16;
    val |= val >> 32;
    return val + 1;
}

/**
 * @brief 通过硬件返回的物理地址，极速逆向找回 Stream Ring
 * 依赖底层伙伴算法 (Buddy Allocator) 提供的自然对齐特性
 */
static inline xhci_submit_ring_t* xhci_get_ring_by_pa(usb_ep_t *ep, uint64 trb_pa) {
    if (ep->enable_streams_exp == 0) {
        return &ep->ring_arr[0];
    }

    // 1. 获取物理字节大小
    uint64 ring_byte_size = ep->ring_arr[1].size << 4;

    // 🌟 防御陷阱：强制向上取整到 2 的幂次，对齐伙伴系统的真实分配粒度！
    uint64 buddy_aligned_size = roundup_pow_of_two(ring_byte_size);

    // 2. 生成绝对安全的物理掩码
    uint64 pa_mask = ~(buddy_aligned_size - 1);

    // 3. ⚡️ 一步提取物理基址 (纳秒级计算),并转换成虚拟地址
    void *target_base_va = pa_to_va(trb_pa & pa_mask);


    // 4. 极速命中匹配
    uint32 streams_count = (1 << ep->enable_streams_exp) + 1;
    for (uint32 s = 1; s < streams_count; s++) {
        // 由于是严格相等的寄存器比较，这里绝不会有分支预测惩罚
        if (ep->ring_arr[s].ring_base == target_base_va) {
            return &ep->ring_arr[s];
        }
    }

    return NULL;
}


static inline void xhci_submit_ring_update_deq_idx(xhci_submit_ring_t *ring, uint64 comp_trb_pa) {
    // 根据物理地址，反算出它在环里的数组索引
    uint32 deq_idx = (comp_trb_pa - va_to_pa(ring->ring_base))>>4;

    // 硬件执行完这个了，说明下一个是待处理的，更新消费者游标
    ring->deq_idx = xhci_submit_ring_next_idx(deq_idx,ring->size);
}


/**
 * @brief 将 xHCI 硬件专属的 TRB 完成码，翻译成全宇宙通用的 POSIX 错误码
 * @param comp_code  xHCI 硬件抛出的完成码
 * @return int32     返回 0 表示正常，负数表示各种 POSIX 异常
 */
static inline int32 xhci_translate_error(uint8 comp_code) {
    switch (comp_code) {

        // ==========================================================
        // 🟢 【第一梯队：和平时期 (Happy Path)】
        // 处理策略：一路绿灯，继续推进业务状态机
        // ==========================================================
        case COMP_SUCCESS:
        case COMP_SHORT_PACKET:        // 短包在 BOT/UAS 中是合法的结束标志
            return 0;

        // ==========================================================
        // 🔵 【第二梯队：优雅刹车 (Graceful Stop)】
        // 出现场景：上层主动调用了 Stop Endpoint 或 Abort Command
        // 处理策略：清理残留的面单内存，不需要物理抢救
        // ==========================================================
        case COMP_COMMAND_RING_STOPPED:
        case COMP_COMMAND_ABORTED:
        case COMP_STOPPED:
        case COMP_STOPPED_LENGTH_INVALID:
        case COMP_STOPPED_SHORT_PACKET:
            return -ESHUTDOWN;              // POSIX: 传输端点已被安全关闭/停机

        // ==========================================================
        // 🟡 【第三梯队：数据与协议死锁 (Data/Protocol Halt)】
        // 出现场景：Data 环或 Cmd 环报错，硬件端点坠入 `Halted` 状态。
        // 处理策略：调用 `xhci_cmd_reset_ep` 抢救主板，上发 TMF/ClearHalt 安抚 U 盘。
        // ==========================================================
        case COMP_STALL_ERROR:
            return -EPIPE;                  // POSIX: 管道破裂 (U 盘主动拒绝)

        case COMP_USB_TRANSACTION_ERROR:
            return -EPROTO;                 // POSIX: 协议错误 (线缆松动、CRC校验错、超时)

        case COMP_BABBLE_ERROR:
        case COMP_RING_OVERRUN:
        case COMP_ISOCH_BUFFER_OVERRUN:
        case COMP_BANDWIDTH_OVERRUN_ERROR:
            return -EOVERFLOW;              // POSIX: 数值溢出 (设备像漏水一样疯狂发数据)

        case COMP_SPLIT_TRANSACTION_ERROR:
            return -ECOMM;                  // POSIX: 发送时通信错误 (通常是 USB Hub 级错误)

        case COMP_TRB_ERROR:
        case COMP_DATA_BUFFER_ERROR:
        case COMP_INVALID_STREAM_ID_ERROR:
            return -EILSEQ;                 // POSIX: 非法的字节序/操作 (驱动传给主板的 TRB 地址乱了)

        // ==========================================================
        // 🟠 【第四梯队：资源与配置拒绝 (Config Rejection)】
        // 出现场景：主板发脾气，拒绝执行你的 Configure Endpoint 等配置命令。
        // 处理策略：向上层汇报设备无法接入，释放分配的上下文内存。
        // ==========================================================
        case COMP_BANDWIDTH_ERROR:
        case COMP_SECONDARY_BANDWIDTH_ERROR:
            return -ENOSPC;                 // POSIX: 设备上没有空间 (总线带宽被其他设备占满了)

        case COMP_NO_SLOTS_AVAILABLE_ERROR:
        case COMP_RESOURCE_ERROR:
            return -ENOMEM;                 // POSIX: 内存分配不足 (主板内部寄存器/内存池枯竭)

        case COMP_INVALID_STREAM_TYPE_ERROR:
        case COMP_PARAMETER_ERROR:
            return -EINVAL;                 // POSIX: 无效的参数 (驱动代码写 Bug 了，填错了结构体)

        case COMP_SLOT_NOT_ENABLED_ERROR:
        case COMP_ENDPOINT_NOT_ENABLED_ERROR:
        case COMP_INCOMPATIBLE_DEVICE_ERROR:
        case COMP_NO_PING_RESPONSE_ERROR:
            return -ENODEV;                 // POSIX: 没有这样的设备 (端点未激活 或 U 盘休眠叫不醒)

        case COMP_CONTEXT_STATE_ERROR:
            return -EPERM;                  // POSIX: 操作不允许 (状态机时序违规，如在 Running 时改指针)

        // ==========================================================
        // 🔴 【第五梯队：灾难级系统崩溃 (Catastrophic Error)】
        // 出现场景：中断系统瘫痪，或主板硬件发生物理级坏死。
        // 处理策略：触发内核级总线大复位 (Host Controller Reset)，甚至 Kernel Panic。
        // ==========================================================
        case COMP_EVENT_RING_FULL_ERROR:
        case COMP_VF_EVENT_RING_FULL_ERROR:
        case COMP_EVENT_LOST_ERROR:
            return -ENOBUFS;                // POSIX: 没有可用的缓冲区 (你的死循环事件泵卡了，主板回执爆仓)

        case COMP_UNDEFINED_ERROR:
        default:
            return -EIO;                    // POSIX: 物理输入/输出错误 (万劫不复的底线错误)
    }
}


// 传输任务处理 (多核完美并发版)
static inline void xhci_handle_transfer_event(xhci_hcd_t *xhcd, xhci_trb_t *evt_trb) {
    // =======================================================
    // 🛡️ 1. 纯粹的宏解析：彻底告别联合体位域的隐患
    // =======================================================
    // 物理地址直接从 DW0 & DW1 (parameter) 提取
    uint64 trb_pa     = evt_trb->parameter;

    // 剩余数据长度和完成码从 DW2 (status) 提取
    uint32 remainder  = TRB_GET_TR_LEN(evt_trb->status);
    uint32 comp_code  = TRB_GET_COMP_CODE(evt_trb->status);

    // 端点号和槽位号从 DW3 (control) 提取
    uint8  evt_ep_dci = TRB_GET_EP_ID(evt_trb->control);
    uint8  slot_id    = TRB_GET_SLOT_ID(evt_trb->control);

    // =======================================================
    // 2. 软硬件映射验证
    // =======================================================
    usb_dev_t *udev = xhcd->udevs[slot_id];
    if (udev == NULL) return;
    usb_ep_t *ep = udev->eps[evt_ep_dci];
    if (ep == NULL) return;

    // 🌟 3. 逆向定位：找出真正发生事件的那个环！(支持多 Stream)
    xhci_submit_ring_t *target_ring = xhci_get_ring_by_pa(ep, trb_pa);
    if (target_ring == NULL) {
        color_printk(RED, BLACK, "xHCI: Ghost TRB PA %llx from hardware!\n", trb_pa);
        return;
    }

    // 提前声明一个指针，用来接住刚刚完成结算的那个 URB
    usb_urb_t *completed_urb = NULL;

    // =======================================================
    // 🔒 4. 获取该环的专属锁 (绝不影响其他 Stream 的提交)
    // =======================================================
    uint64 cpu_flags;
    spin_lock_irqsave(&target_ring->ring_lock, &cpu_flags);

    // 在目标环的安全链表里进行小范围遍历
    list_head_t *curr, *next;
    list_for_each_safe(curr, next, &target_ring->pending_list){
        usb_urb_t *urb = CONTAINER_OF(curr, usb_urb_t, node);

        // 🎯 物理地址对上了！这就是硬件刚刚做完的任务
        if (urb->last_trb_pa == trb_pa) {
            // 结算账单
            xhci_submit_ring_update_deq_idx(target_ring, trb_pa); // 更新软件维护的出队指针
            list_del_init(curr);                                  // 从 pending 链表摘除

            urb->status = xhci_translate_error(comp_code);
            // 🎯 使用刚提取的 remainder 计算实际传输长度
            urb->actual_length = urb->transfer_len - remainder;
            urb->is_done = TRUE;

            // 抓获这个 URB，赋值给外部指针
            completed_urb = urb;
            break; // 找到了就跳出循环
        }
    }

    // 🔓 5. 解锁释放
    spin_unlock_irqrestore(&target_ring->ring_lock, cpu_flags);

    // 如果没找到对应的 URB，直接退出
    if (completed_urb == NULL) return;

    // =======================================================
    // 🌟 6. 回调与唤醒：特殊通道与常规通道流转
    // =======================================================

    // A. 特殊通道：Hub 的 Interrupt IN Endpoint
    if (udev->is_hub) {
        usb_hub_t *hub = (usb_hub_t *)udev->drv_data;
        // 🛡️ 核心防线：必须严格比对地址！
        if (hub && completed_urb == hub->int_urb) {
            // 将 Hub 的端口状态机推入工作队列异步执行
            usb_event_queue_push(USB_EVENT_HUB_WORK, udev, 0);
        }
    }

    // B. 常规通道：唤醒在此 URB 上休眠的同步线程 (伪代码示意)
    // if (completed_urb->complete_func) {
    //     completed_urb->complete_func(completed_urb);
    // } else {
    //     semaphore_up(&completed_urb->wait_sem);
    // }
}



// =========================================================================
// 🚀 命令完成事件 TRB 处理程序 (宏解析重构版)
// =========================================================================
static inline void xhci_handle_cmd_completion(xhci_hcd_t *xhcd, xhci_trb_t *evt_trb) {
    xhci_submit_ring_t *cmd_ring = &xhcd->cmd_ring;

    // =======================================================
    // 🛡️ 1. 纯粹的宏解析：精准剥离，拒绝联合体位域陷阱
    // =======================================================
    // DW0 & DW1: 原始命令 TRB 的 64 位物理地址
    uint64 trb_pa     = evt_trb->parameter;

    // DW2: 提取附加参数与完成码
    uint32 comp_param = TRB_GET_CMD_COMP_PARAM(evt_trb->status);
    uint32 comp_code  = TRB_GET_COMP_CODE(evt_trb->status);

    // DW3: 提取可能分配到的 Slot ID
    uint8  slot_id    = TRB_GET_SLOT_ID(evt_trb->control);

    // =======================================================
    // 🔒 2. 获取命令环自旋锁，保护 pending_list
    // =======================================================
    uint64 cpu_flags;
    spin_lock_irqsave(&cmd_ring->ring_lock, &cpu_flags);

    // 3. 遍历该命令环上所有正在飞的 "面单"
    list_head_t *curr, *next;
    list_for_each_safe(curr, next, &cmd_ring->pending_list) {
        xhci_command_t *command = CONTAINER_OF(curr, xhci_command_t, node);

        if (command->cmd_trb_pa == trb_pa) {
            // 结算账单，更新软件出队指针
            xhci_submit_ring_update_deq_idx(cmd_ring, trb_pa);
            list_del_init(curr);

            // 填充完成状态，供发起命令的线程读取
            command->slot_id    = slot_id;
            command->comp_code  = comp_code;
            command->comp_param = comp_param;
            command->status     = xhci_translate_error(comp_code);
            command->is_done = TRUE;
            break;
        }
    }

    // 🔓 4. 解锁释放
    spin_unlock_irqrestore(&cmd_ring->ring_lock, cpu_flags);

    // 注意：如果是阻塞式 API (xhci_execute_command_sync)，
    // 你可能还需要在这里调用 semaphore_up / completion_done 唤醒等待的线程。
}


// =========================================================================
// 🚀 端口状态改变事件处理 (宏解析重构版)
// =========================================================================
static inline void xhci_handle_port_status_change(xhci_hcd_t *xhcd, xhci_trb_t *evt_trb) {
    // =======================================================
    // 🛡️ 宏解析：提取发生突变的物理端口号
    // =======================================================
    // 核心技巧：parameter 包含 DW0(低32位)和 DW1(高32位)。强转为 uint32 即可无损截断高位，获取原汁原味的 DW0！
    uint32 dw0 = (uint32)evt_trb->parameter;
    uint8 port_num = TRB_GET_PORT_ID(dw0);

    // 将端口突变事件推入全局事件队列，交由 Hub 守护线程异步处理物理插拔
    usb_event_queue_push(USB_EVENT_XHCI_ROOT_PORT, xhcd, port_num);
}


/**
 * @brief 解析并分发单一事件 (纯业务逻辑)
 * @note 依赖最新版宏定义的 16 字节防弹 TRB 结构体
 */
static inline void xhci_process_single_event(xhci_hcd_t *xhcd, xhci_trb_t *trb) {
    // 🌟 1. 一击必杀：直接从 DW3(control) 提取当前事件的真实身份
    uint32 trb_type = TRB_GET_TYPE(trb->control);

    // 2. 根据类型进行精准的路由分发
    switch (trb_type) {
        case TRB_TYPE_TRANSFER_EVENT: // 【类型 32：传输事件】(U 盘业务收发)
            // 在具体的 handle 函数中再去提取它专属的 COMP_CODE 和残余长度
            xhci_handle_transfer_event(xhcd, trb);
            break;

        case TRB_TYPE_CMD_COMPLETION: // 【类型 33：命令完成事件】(主板建桥图纸回执)
            // 在具体的 handle 函数中再去提取它专属的 Slot ID 等
            xhci_handle_cmd_completion(xhcd, trb);
            break;

        case TRB_TYPE_PORT_STATUS_CHG: // 【类型 34：端口状态改变事件】(物理插拔感知)
            // 在具体的 handle 函数中，通过 TRB_GET_PORT_ID(trb->parameter) 提取出事端口
            xhci_handle_port_status_change(xhcd, trb);
            break;

        case TRB_TYPE_HOST_CTRL: // 【类型 37：主板级遗言】
            // 此时主板已经硬件级崩溃，提取它的死因 (COMP_CODE)
            color_printk(RED, BLACK, "\n[FATAL KERNEL PANIC] xHCI Host Controller Event Triggered! Reason Code: %d\n", TRB_GET_COMP_CODE(trb->status));
            break;

        default:
            // 忽略未知的事件 (比如厂商自定义的保留事件)
            color_printk(YELLOW, BLACK, "[xHCI] Warning: Ignored unknown event TRB type %d\n", trb_type);
            break;
    }
}



//xhci中断服务函数
irqreturn_e xhci_isr(cpu_registers_t *regs,void *dev_id) {
    pcie_dev_t *xdev = dev_id;
    xhci_hcd_t *xhcd = xdev->priv_data;

    //清除硬件中断挂起标志
    uint32 usbsts = xhcd->op_reg->usbsts;
    usbsts &= XHCI_STS_HSE | XHCI_STS_EINT | XHCI_STS_PCD;
    xhcd->op_reg->usbsts = usbsts;


    uint8 evtnt_idx = 0;
    xhci_event_ring_t *evt_ring = &xhcd->event_ring_arr[evtnt_idx];

    uint64 flags;
    spin_lock_irqsave(&evt_ring->ring_lock, &flags);

    // =================================================================
    // 🛡️ 硬件级防御：清除可能残留的 IMAN_IP (防老旧/非标主板中断风暴)
    // =================================================================
    uint32 iman = xhcd->rt_reg->intr_regs[evtnt_idx].iman;
    if (iman & XHCI_IMAN_IP) {
        // RW1C: iman 变量里的 IP 位此时是 1，直接写回，触发硬件清零！
        // 这一步也顺便保持了 IE (Interrupt Enable) 位的原状态不变
        xhcd->rt_reg->intr_regs[evtnt_idx].iman = iman;
    }

    boolean processed_any = FALSE;
    xhci_trb_t current_trb; // 在栈上分配 16 字节，极其高效且安全

    // =================================================================
    // 👑 优雅的迭代循环：一边取，一边分发！
    // =================================================================
    while (xhci_event_ring_deq(evt_ring, &current_trb) == 0) {
        xhci_process_single_event(xhcd, &current_trb);
        processed_any = TRUE;

    }

    // =================================================================
    // 签收中断，归还硬件控制权
    // =================================================================
    if (processed_any) {
        // 由于 xhci_get_next_event 已经帮我们把 dequeue_ptr 移到了最新位置
        // 这里直接计算物理地址，写入 ERDP 即可// 置 EHB 位为 1，清除硬件挂起状态
        uint64 erdp_pa = va_to_pa(&evt_ring->ring_base[evt_ring->deq_idx]) | XHCI_ERDP_EHB;
        xhcd->rt_reg->intr_regs[evtnt_idx].erdp = erdp_pa;
    }

    spin_unlock_irqrestore(&evt_ring->ring_lock, flags);
}


static inline void xhci_port_dev_create(xhci_hcd_t *xhcd, uint8 port_num) {
    usb_dev_t *udev = kzalloc(sizeof(usb_dev_t));
    udev->xhcd = xhcd;

    udev->root_hub_port_num = port_num; // 🌟 物理坐标在这里！(1 ~ MaxPorts)
    udev->tt_hub_slot_id = 0;
    udev->tt_port_num = 0;          // 🌟 没爹，当然是0！
    udev->route_string = 0;         // 直连无路由
    udev->hub_depth = 0;
    udev->parent_hub = NULL;

    // 【速率继承】：直接读取 xHCI 寄存器，并从 SPC (支持协议表) 中提权
    uint8 psi = xhci_get_psi(xhcd, port_num);
    uint8 spc_idx = xhcd->port_to_spc[port_num];

    xhci_psi_t *psi_dect = &xhcd->spc[spc_idx].psi_dict[psi];
    udev->port_speed = psi_dect->mapped_speed;
    udev->speed_kbps = psi_dect->speed_kbps;
    udev->psiv = psi_dect->psiv;

    //usb设备初始化
    usb_dev_init(udev);
}



/**
 * @brief 处理主板直连端口的插拔与复位逻辑 (全状态位覆盖)
 */
void xhci_process_port_event(xhci_hcd_t *xhcd, uint8 port_num) {
    uint32 portsc = xhci_read_portsc(xhcd, port_num);
    usb_hub_port_t *port = &xhcd->ports[port_num];

    //color_printk(GREEN, BLACK, "[xHCI Port %d] Async IRQ! PORTSC: %#x, Current State: %d\n",port_num, portsc, port->state);

    // =========================================================================
    // 🧽 1. 硬件保洁区 (Acknowledge) - 靶向批量清零
    // =========================================================================
    uint32 changes_to_clear = portsc & XHCI_PORTSC_CHANGE_MASK;
    if (changes_to_clear) {
        // 使用白名单安全掩码，防止 PED 断连和 PP 断电
        uint32 clear_val = (portsc & XHCI_PORTSC_PRESERVE_MASK) | changes_to_clear;
        xhci_write_portsc(xhcd, port_num, clear_val);
    }

    // =========================================================================
    // 🚨 2. 灾难级错误处理 (最高优先级)
    // =========================================================================

    // 💥 OCC (位 20): 过流变化 (Over-Current Change)
    if (portsc & XHCI_PORTSC_OCC) {
        if (portsc & XHCI_PORTSC_OCA) { // OCA(位3)=1 表示当前正处于短路状态
            color_printk(RED, BLACK, "[xHCI Port %d] FATAL: Over-current detected! Powering down.\n", port_num);
            xhci_port_power_off(xhcd, port_num); // 紧急切断 5V 供电
            port->state = PORT_STATE_DISCONNECTED;
            if (port->child_dev) {
                // TODO: 销毁该设备及其子设备
                port->child_dev = NULL;
            }
            return; // 发生短路，终止后续处理
        } else {
            color_printk(YELLOW, BLACK, "[xHCI Port %d] Over-current resolved. Restoring power.\n", port_num);
            xhci_port_power_on(xhcd, port_num); // 短路解除，重新上电
        }
    }

    // 💥 CEC (位 23): 配置错误变化 (Config Error Change) - USB 3.0 专属
    if (portsc & XHCI_PORTSC_CEC) {
        color_printk(RED, BLACK, "[xHCI Port %d] Bad Cable or PHY Error! Configuration failed.\n", port_num);
        port->state = PORT_STATE_DISCONNECTED;
        return;
    }

    // =========================================================================
    // 🔌 3. 物理插拔处理 (Connection & Detach)
    // =========================================================================

    // CSC (位 17): 热插拔 / CAS (位 24): 冷启动设备检测 / 软件兜底检测
    if ((portsc & XHCI_PORTSC_CSC) ||
        (portsc & XHCI_PORTSC_CAS) ||
        ((portsc & XHCI_PORTSC_CCS) && port->state == PORT_STATE_DISCONNECTED)) {

        if (portsc & XHCI_PORTSC_CCS) { // 物理层有设备，准备发起复位
            uint8 spc_idx = xhcd->port_to_spc[port_num];
            boolean is_usb3 = (xhcd->spc[spc_idx].major_bcd >= 0x03);

            if (is_usb3 && (portsc & XHCI_PORTSC_PLS_MASK) == XHCI_PORTSC_PLS_INACTIVE) {
                color_printk(YELLOW, BLACK, "[xHCI Port %d] USB 3.0 Deadlock. Issuing Warm Reset.\n", port_num);
                xhci_port_reset_warm(xhcd, port_num); // 暖复位抡大锤
                port->state = PORT_STATE_WAITING_WARM_RESET;
            } else {
                //color_printk(GREEN, BLACK, "[xHCI Port %d] Issuing Hot Reset.\n", port_num);
                xhci_port_reset_hot(xhcd, port_num);  // 常规热复位
                port->state = PORT_STATE_WAITING_HOT_RESET;
            }
            return; // 🎯 关键：发射复位命令后立刻返回，等待复位完成的中断
        } else {
            // 设备拔出
            color_printk(YELLOW, BLACK, "[xHCI Port %d] Device Detached.\n", port_num);
            port->state = PORT_STATE_DISCONNECTED;
            if (port->child_dev) {
                // TODO: 执行设备销毁逻辑，释放槽位 (Slot ID) 和相关数据结构
                port->child_dev = NULL;
            }
            return;
        }
    }

    // =========================================================================
    // 🚀 4. 复位完成回执 (Reset Completion)
    // =========================================================================

    // PRC (位 21): 热复位完成 / WRC (位 19): 暖复位完成
    if ((portsc & XHCI_PORTSC_PRC) || (portsc & XHCI_PORTSC_WRC)) {
        if (port->state == PORT_STATE_WAITING_HOT_RESET ||
            port->state == PORT_STATE_WAITING_WARM_RESET) {

            // 防御性编程：复位完成了，端口真的 Enabled 了吗？
            if (portsc & XHCI_PORTSC_PED) {
                //uint32 portsc = xhci_read_portsc(xhcd, port_num);
                //color_printk(GREEN, BLACK, "[xHCI Port %d] Reset Complete & portsc:%#x & Enabled! Enumerating...\n", port_num,portsc);
                port->state = PORT_STATE_ENABLED;

                // 💥 终极动作：下发 Enable Slot -> Set Address -> 获取描述符
                xhci_port_dev_create(xhcd,port_num);
            } else {
                color_printk(RED, BLACK, "[xHCI Port %d] Reset Complete but port NOT Enabled! Reset failed.\n", port_num);
                port->state = PORT_STATE_DISCONNECTED;
            }
        }
    }

    // =========================================================================
    // 📉 5. 异常掉线与链路变化 (State Drops & Link Changes)
    // =========================================================================

    // PEC (位 18): 端口使能变化 (Port Enabled/Disabled Change)
    if (portsc & XHCI_PORTSC_PEC) {
        // 如果端口突然变成 Disabled (PED=0)，且我们原本以为它是 Enabled 的
        if (!(portsc & XHCI_PORTSC_PED) && port->state == PORT_STATE_ENABLED) {
            color_printk(RED, BLACK, "[xHCI Port %d] Spontaneous Disable (EMI or Error)!\n", port_num);
            port->state = PORT_STATE_DISCONNECTED;
            if (port->child_dev) port->child_dev = NULL; // 销毁设备
        }
    }

    // PLC (位 22): 端口链路状态变化 (Port Link State Change)
    if (portsc & XHCI_PORTSC_PLC) {
        uint32 link_state = (portsc & XHCI_PORTSC_PLS_MASK) >> 5;
        // 这里常用于处理高级电源管理 (LPM, U1/U2/U3 状态机的休眠与唤醒)
        // 对于基础 USB 栈，我们目前只做日志记录
        // color_printk(WHITE, BLACK, "[xHCI Port %d] Link State changed to: %x\n", port_num, link_state);

        if (link_state == XHCI_PORTSC_PLS_RESUME) {
            // 收到唤醒信号 (Resume)
            // TODO: 发送 U0 信号结束休眠
        }
    }
}


//xhci port扫描
void xhci_port_scan(xhci_hcd_t *xhcd){
    for (uint8 port_num = 1; port_num <= xhcd->max_ports; port_num++) {
        uint32 portsc = xhci_read_portsc(xhcd, port_num);
        usb_hub_port_t *port = &xhcd->ports[port_num];

        // 🎯 核心过滤条件：只把真正有设备（CCS=1），且我们软件系统还没记录的端口塞进队列。
        // 这完美过滤了那几十个空端口，不浪费一丝一毫 CPU 性能。
        if ((portsc & XHCI_PORTSC_CCS) && port->state == PORT_STATE_DISCONNECTED) {
            // 作为兜底，直接送入队列。
            // 当你的底半部 xhci_process_port_event 被唤醒时，
            // 它会看到 CCS=1 且状态是 DISCONNECTED，自动触发复位流程！
            usb_event_queue_push(USB_EVENT_XHCI_ROOT_PORT, xhcd, port_num);
        }
    }
}


// =========================================================================
// 2. 核心操作 API
// =========================================================================
// 全局唯一的 USB 事件队列实例 (开机分配在 BSS 段，完全告别 kmalloc)
static usb_event_queue_t g_usb_event_queue;
/**
 * @brief 初始化队列 (系统启动时调用一次)
 */
void usb_event_queue_init(void) {
    g_usb_event_queue.head = 0;
    g_usb_event_queue.tail = 0;
}



/**
 * @brief [生产者] 投递端口事件
 * @note 运行在硬件中断 (ISR) 上下文中，要求极速，不可阻塞！
 * @return boolean TRUE-成功，FALSE-队列溢出满
 */
boolean usb_event_queue_push(usb_event_type_e type, void *parent, uint8 port_num) {
    // 预测下一个写入位置
    uint32 next_tail = (g_usb_event_queue.tail + 1) % USB_EVENT_QUEUE_SIZE;

    // 检查是否撞上消费者指针 (队列满)
    if (next_tail == g_usb_event_queue.head) {
        // 内核级警告：主循环处理太慢，或者发生了极其严重的中断风暴！
        // color_printk(RED, BLACK, "FATAL: USB Event Queue Overflow!\n");
        return FALSE;
    }

    // 写入数据 (纯内存拷贝，耗时极短)
    usb_port_event_t *evt = &g_usb_event_queue.events[g_usb_event_queue.tail];
    evt->type = type;
    evt->parent_dev = parent;
    evt->port_num = port_num;

    // 🌟 核心防线：编译器内存屏障，强制确保数据先落盘，再更新 tail 指针
    __asm__ __volatile__("": : :"memory");

    g_usb_event_queue.tail = next_tail;
    return TRUE;
}

/**
 * @brief [消费者] 弹出端口事件
 * @note 运行在主循环底半部，安全、无惧阻塞。
 * @param out_event 弹出的事件拷贝存放处
 * @return boolean TRUE-成功拿到任务，FALSE-队列为空
 */
boolean usb_event_queue_pop(usb_port_event_t *out_event) {
    // 检查队列是否为空
    if (g_usb_event_queue.head == g_usb_event_queue.tail) {
        return FALSE;
    }

    // 拷贝数据
    usb_port_event_t *evt = &g_usb_event_queue.events[g_usb_event_queue.head];
    out_event->type = evt->type;
    out_event->parent_dev = evt->parent_dev;
    out_event->port_num = evt->port_num;
    g_usb_event_queue.head = (g_usb_event_queue.head + 1) % USB_EVENT_QUEUE_SIZE;
    return TRUE;
}



