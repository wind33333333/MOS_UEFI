#include "xhci-hw.h"
#include "printk.h"
#include "pcie.h"
#include "slub.h"
#include "vmm.h"
#include "driver.h"
#include "vmalloc.h"
#include "usb-core.h"
#include "errno.h"
#include "usb-hub.h"
#include "xhci-ring.h"
#include "../include/xhci-service.h"
#include "xhci-hcd.h"

//xhci设备操作命令
//=====================================================================================

//停止xhci
static inline int32 xhci_stop(xhci_hcd_t *xhcd) {
    xhcd->op_reg->usbcmd &= ~XHCI_CMD_RS; //停止xhci
    uint32 times = 20000000;
    while (times--) {
        if ((xhcd->op_reg->usbsts & XHCI_STS_HCH) != 0)
            return 0;
    }
    color_printk(RED, BLACK, "xHCI: Stop timeout! Controller refused to halt.\n");
    return -ETIMEDOUT;
}

//复位xhci
static inline int32 xhci_reset(xhci_hcd_t *xhcd) {
    // 规范防线：确保复位前已经停止！
    if ((xhcd->op_reg->usbsts & XHCI_STS_HCH) == 0) {
        color_printk(YELLOW, BLACK, "xHCI: Warning, halting controller before reset...\n");
        xhci_stop(xhcd);
    }

    // 触发复位！主板 xHC 开始脑裂重启
    xhcd->op_reg->usbcmd |= XHCI_CMD_HCRST;

    uint32 times = 20000000;
    while (times--) {
        // 条件 1: 硬件完成复位操作后，会自动将 HCRST 清零
        uint8 reset_done = (xhcd->op_reg->usbcmd & XHCI_CMD_HCRST) == 0;

        // 条件 2: 硬件内部微码加载完毕，准备好接客，会将 CNR (未准备好) 清零
        uint8 is_ready = ((xhcd->op_reg->usbsts & XHCI_STS_CNR) == 0);

        if (reset_done && is_ready) {
            return 0; // 完美复位并就绪！
        }
    }

    color_printk(RED, BLACK, "xHCI: Reset timeout! Controller died during reset.\n");
    return -ETIMEDOUT;
}


/**
 * @brief 启动 xHCI 控制器
 */
static inline int32 xhci_start(xhci_hcd_t *xhcd) {
    if (!xhcd || !xhcd->op_reg) return -EINVAL;

    // 1. 读取当前的 USBCMD 寄存器值
    // 💡 架构师习惯：尽量避免直接对硬件寄存器使用 |= 操作符，
    // 先读到局部变量，修改完再统一写回，防止引发意料之外的总线写事务。
    uint32 cmd = xhcd->op_reg->usbcmd;

    // 2. ★ 核心修正：同时开启运行、中断和系统错误报警！
    cmd |= (XHCI_CMD_RS | XHCI_CMD_INTE | XHCI_CMD_HSEE);

    // 3. 点火！写入硬件
    xhcd->op_reg->usbcmd = cmd;

    // 4. ★ 科学的超时等待 (xHCI 规范指出 HCH 通常在 16ms 内清零)
    // 我们给予 50 毫秒的宽限期，使用内核标准的 mdelay (毫秒级延时)
    uint32 times = 20000000;
    while (times--) {
        if ((xhcd->op_reg->usbsts & XHCI_STS_HCH) == 0)
            return 0;
    }

    // 5. 如果 50ms 后 HCH 依然为 1，说明芯片死机或硬件故障
    color_printk(RED, BLACK, "xHCI: Start timeout! Controller refused to run. USBSTS: %#x\n",
                 xhcd->op_reg->usbsts);
    return -ETIMEDOUT;
}

//启用xhci中断
static inline void xhci_enable_intr(xhci_hcd_t *xhcd,uint16 intr_number) {
    uint32 iman = xhcd->rt_reg->intr_regs[intr_number].iman;
    iman |= XHCI_IMAN_IE;
    xhcd->rt_reg->intr_regs[intr_number].iman = iman;
}

//禁用xhci中断
static inline void xhci_disable_intr(xhci_hcd_t *xhcd,uint16 intr_number) {
    int32 iman = xhcd->rt_reg->intr_regs[intr_number].iman;
    iman &= ~XHCI_IMAN_IE;
    xhcd->rt_reg->intr_regs[intr_number].iman = iman;
}
//=========================================================================================

//xhic扩展能力搜索
static inline uint8 xhci_ecap_find(xhci_hcd_t *xhcd, void *ecap_arr, uint8 cap_id) {
    uint32 offset = xhcd->cap_reg->hccparams1 >> 16;
    uint32 *ecap = (uint32 *) xhcd->cap_reg;
    uint8 count = 0;
    while (offset) {
        ecap += offset;
        if ((*ecap & 0xFF) == cap_id) {
            ((uint64*)ecap_arr)[count++] = (uint64)ecap;
        };
        offset = (*ecap >> 8) & 0xFF;
    }
    return count;
}


/**
 * @brief 解析 xHCI 支持的协议扩展能力 (Supported Protocol Capabilities)
 *        并建立软件抽象层字典和 O(1) 端口映射表。
 * @param xhcd xHCI 控制器核心上下文
 * @return int32 0 表示成功，<0 表示内存分配等严重失败
 */
static inline int32 xhci_parse_supported_protocols(xhci_hcd_t *xhcd) {
    /* 定义一个指针数组，最多容纳 16 个协议能力块 (一般主板也就 2~3 个，如 USB 2.0 和 USB 3.0) */
    xhci_ecap_supported_protocol *ecap_spc_arr[16];

    asm_mem_set(xhcd->port_to_spc,0xFF,sizeof(xhcd->port_to_spc));

    /* 调用扩展能力雷达，寻找所有 ID 为 2 (Supported Protocol Capability) 的能力块 */
    xhcd->spc_count = xhci_ecap_find(xhcd, ecap_spc_arr, 2);

    // =========================================================================
    // 阶段 2：深度解析硬件协议表 (将硬件寄存器状态翻译为内核软件结构)
    // =========================================================================
    for (uint8 i = 0; i < xhcd->spc_count; i++) {
        xhci_spc_t *spc = &xhcd->spc[i];                                // 软件结构指针
        xhci_ecap_supported_protocol *spc_ecap = ecap_spc_arr[i];       // 硬件寄存器指针

        /* 解析 USB 协议版本号 (例如：0x0300 代表 USB 3.0, 0x0200 代表 USB 2.0) */
        spc->major_bcd = spc_ecap->protocol_ver >> 24;                  // 提取主版本号 (Major)
        spc->minor_bcd = spc_ecap->protocol_ver >> 16 & 0xFF;           // 提取次版本号 (Minor)

        /* 巧妙提取名称：直接进行 4 字节 (uint32) 的内存拷贝，通常是 "USB " */
        *(uint32 *) spc->name = *(uint32 *) spc_ecap->name;
        /* 把第 4 个字符 (索引3) 强行置 0，将 "USB " 截断成完美的 C 语言字符串 "USB\0" */
        spc->name[3] = 0;

        /* 解析协议自定义字段 (12 Bits)，通常用于集线器层级深度限制等高级特性 */
        spc->proto_defined = spc_ecap->port_info >> 16 & 0xFFF;

        /* ★ 核心拓扑数据 ★ */
        spc->port_first = spc_ecap->port_info & 0xFF;                   // 属于该协议的起始端口号 (注意：硬件是从 1 开始的！)
        spc->port_count = spc_ecap->port_info >> 8 & 0xFF;              // 属于该协议的连续端口总数

        /* 解析协议插槽类型 (5 Bits)，用于后续设备上下文的初始化 */
        spc->slot_type = spc_ecap->protocol_slot_type & 0x1F;

        /* 解析自定义速率表 (PSI) 的数量 (4 Bits) */
        uint8 psi_count = spc_ecap->port_info >> 28 & 0xF;

        // =========================================================================
        // 阶段 3：装载“动态速率翻译字典” (应对 USB 3.1+ 的非标准速率)
        // =========================================================================
        if (psi_count) {
            /* 将硬件提供的速率映射表一一拷贝到内核中，供以后查询 */
            for (uint8 j = 0; j < psi_count; j++) {
                uint32 protocol_speed = spc_ecap->protocol_speed[j];
                uint8 psiv = protocol_speed & 0xF;
                xhci_psi_t *parsed_psi = &spc->psi_dict[psiv];
                parsed_psi->psiv = psiv;
                parsed_psi->is_full_duplex = (protocol_speed >> 8) & 0x1;
                parsed_psi->is_symmetric   = ((protocol_speed >> 6) & 0x3) == 0;

                // 统一换算为 Kbps
                uint32 mantissa = (protocol_speed >> 16) & 0xFFFF;
                uint8 exponent = (protocol_speed >> 4) & 0x3;
                if (exponent == 0)      parsed_psi->speed_kbps = mantissa / 1000;   // bps (极罕见，可能截断，但USB不存在低于1Kbps的设备)
                else if (exponent == 1) parsed_psi->speed_kbps = mantissa;          // Kbps (如低速: 1500)
                else if (exponent == 2) parsed_psi->speed_kbps = mantissa * 1000;   // Mbps (如全速: 12 * 1000 = 12000)
                else if (exponent == 3) parsed_psi->speed_kbps = mantissa * 1000000;// Gbps (如极速: 5 * 1000000 = 5000000)

                if (spc->major_bcd == 0x02) {
                    // ----------------------------------------------------
                    // USB 2.0 协议族：老老实实靠绝对速度阈值来划分
                    // ----------------------------------------------------
                    if (parsed_psi->speed_kbps <= 1500) {
                        parsed_psi->mapped_speed = USB_SPEED_LOW;
                    } else if (parsed_psi->speed_kbps <= 12000) {
                        parsed_psi->mapped_speed = USB_SPEED_FULL;
                    } else {
                        parsed_psi->mapped_speed = USB_SPEED_HIGH;
                    }

                } else if (spc->major_bcd == 0x03) {
                    // ----------------------------------------------------
                    // USB 3.x 协议族：绝对不能靠速度猜，必须看 LP 字段！
                    // ----------------------------------------------------
                    uint8 lp = (protocol_speed>> 14) & 0x3; // 提取 Link Protocol

                    if (lp == 1) {
                        // 🌟 核心分流：通过真实的 kbps 速率，区分 10G 和 20G
                        if (parsed_psi->speed_kbps >= 20000000) {
                            parsed_psi->mapped_speed = USB_SPEED_SUPER_20G;
                        } else {
                            parsed_psi->mapped_speed = USB_SPEED_SUPER_10G;
                        }
                    } else {
                        // LP = 00 代表 SuperSpeed (Gen 1)
                        // 🌟 此时，即使 SSIC 跑到了 5830 Mbps，它依然会乖乖待在 SUPER_SPEED 阵营！
                        parsed_psi->mapped_speed = USB_SPEED_SUPER_5G;
                    }
                }

            }
        }else {
            // 🌟 QEMU 命中这里！直接伪造完美的“软件视图”塞进固定抽屉！
            if (spc->major_bcd == 0x02) {
                // PSIV = 1 (Full-speed, 12Mbps)
                spc->psi_dict[1].psiv = 1;
                spc->psi_dict[1].speed_kbps = 12000;
                spc->psi_dict[1].mapped_speed = USB_SPEED_FULL;

                // PSIV = 2 (Low-speed, 1.5Mbps)
                spc->psi_dict[2].psiv = 2;
                spc->psi_dict[2].speed_kbps = 1500;
                spc->psi_dict[2].mapped_speed = USB_SPEED_LOW;

                // PSIV = 3 (High-speed, 480Mbps)
                spc->psi_dict[3].psiv = 3;
                spc->psi_dict[3].speed_kbps = 480000;
                spc->psi_dict[3].mapped_speed = USB_SPEED_HIGH;

            } else if (spc->major_bcd == 0x03) {
                // PSIV = 4 (SuperSpeed, 5Gbps)
                spc->psi_dict[4].psiv = 4;
                spc->psi_dict[4].speed_kbps = 5000000;
                spc->psi_dict[4].mapped_speed = USB_SPEED_SUPER_5G;
            }
        }

        // =========================================================================
        // 阶段 4：建立 O(1) 端口映射表 (极其关键的防雷区)
        // =========================================================================
        /* * 硬件大坑：spc->port_first 是从 1 开始计数的 (物理世界的习惯)
         */
        uint8 end_port = spc->port_first + spc->port_count;
        for (uint8 j = spc->port_first; j < end_port; j++) {
            /* 将逻辑端口号 j 映射到当前协议的索引 i 上 */
            /* 以后只要知道端口号 j，读取 port_to_spc[j]，瞬间就能知道它是 USB 2.0 还是 3.0 */
            xhcd->port_to_spc[j] = i;
        }
    }

}


//xhci设备探测初始化驱动
int32 xhci_probe(pcie_dev_t *xdev, pcie_id_t *id) {
    xdev->dev.drv_data = kzalloc(sizeof(xhci_hcd_t)); //存放xhci相关信息
    xhci_hcd_t *xhcd = xdev->dev.drv_data;
    xhcd->xdev = xdev;
    xdev->priv_data = xhcd;
    xdev->bar[0].vaddr = iomap(xdev->bar[0].paddr, xdev->bar[0].size,PAGE_4K_SIZE,PAGE_ROOT_RW_UC_4K);

    /*初始化xhci寄存器*/
    xhcd->cap_reg = xdev->bar[0].vaddr; //xhci能力寄存器基地址
    xhcd->op_reg = xdev->bar[0].vaddr + xhcd->cap_reg->cap_length; //xhci操作寄存器基地址
    xhcd->rt_reg = xdev->bar[0].vaddr + xhcd->cap_reg->rtsoff; //xhci运行时寄存器基地址
    xhcd->db_reg = xdev->bar[0].vaddr + xhcd->cap_reg->dboff; //xhci门铃寄存器基地址

    /*停止复位xhci*/
    if (xhci_reset(xhcd) == -ETIMEDOUT) {
        while (1);
    }

    xhcd->ctx_size = 32 << ((xhcd->cap_reg->hccparams1 & HCCP1_CSZ) >> 2);     /*设备上下文字节数*/
    xhcd->major_bcd = xhcd->cap_reg->hciversion >> 8; //xhci主版本
    xhcd->minor_bcd = xhcd->cap_reg->hciversion & 0xFF; //xhci次版本
    xhcd->max_ports = xhcd->cap_reg->hcsparams1 >> 24; //xhci最大端口数
    xhcd->max_intrs = xhcd->cap_reg->hcsparams1 >> 8 & 0x7FF; //xhci最大中断数
    xhcd->max_streams_exp = ((xhcd->cap_reg->hccparams1 >> 12) & 0xF)+1; //计算xhci支持的最大流 2^(N+1)

    /*初始化设备上下文*/
    xhcd->max_slots = xhcd->cap_reg->hcsparams1 & 0xff;
    xhcd->dcbaap = kzalloc_dma((xhcd->max_slots+1)<<3);
    //分配设备上下文插槽内存,最大插槽数量(插槽从1开始需要+1)*8字节内存
    xhcd->op_reg->dcbaap = va_to_pa(xhcd->dcbaap); //把设备上下文基地址数组表的物理地址写入寄存器
    xhcd->op_reg->config = xhcd->max_slots; //把最大插槽数量写入寄存器

    //xhci支持多少个slot就分配多少个udev结构指针
    xhcd->udevs = kzalloc((xhcd->max_slots+1) * sizeof(void*));

    //xhci原生端口分配usb_hub_t结构内存,并设置好端口状态信息
    xhcd->ports = kzalloc((xhcd->max_ports+1) * sizeof(usb_hub_port_t));
    for (uint8 port_num = 1; port_num <= xhcd->max_ports; port_num++) {
        uint32 portsc = xhci_read_portsc(xhcd,port_num);
        xhcd->ports[port_num].is_removable = !((portsc & XHCI_PORTSC_DR)>>30);
        xhcd->ports[port_num].port_num = port_num;
        xhcd->ports[port_num].state = PORT_STATE_DISCONNECTED;

    }

    /*初始化命令环*/
    xhci_alloc_submit_ring(&xhcd->cmd_ring,32); //命令环分配32个槽位
    xhcd->op_reg->crcr = va_to_pa(xhcd->cmd_ring.ring_base) | 1; //命令环物理地址写入crcr寄存器，置位rcs

    /*初始化中断器*/
    //可以根据cpu核心和MaxIntrs取小值设置多事件环。暂时设置1个事件环
    xhcd->enable_num_event_ring = 1;
    xhci_event_ring_t *event_ring_arr = kzalloc(sizeof(xhci_event_ring_t) * xhcd->enable_num_event_ring);
    xhcd->event_ring_arr = event_ring_arr;
    for (uint16 i = 0; i < xhcd->enable_num_event_ring; i++) {
        xhci_alloc_event_ring(&event_ring_arr[i],1024); //每个事件环设置1024个槽位

        xhcd->rt_reg->intr_regs[i].erstsz = 1; //设置1,单事件环段
        xhcd->rt_reg->intr_regs[i].erstba = va_to_pa(event_ring_arr[i].erst_base); //事件环段表物理地址写入寄存器
        xhcd->rt_reg->intr_regs[i].erdp = va_to_pa(event_ring_arr[i].ring_base); //事件环物理地址写入寄存器
    }

    /*初始化暂存器缓冲区*/
    uint32 spb_number = (xhcd->cap_reg->hcsparams2 & 0x1f << 21) >> 16 | xhcd->cap_reg->hcsparams2>> 27;
    if (spb_number) {
        uint64 *spb_array = kzalloc_dma(spb_number << 3); //分配暂存器缓冲区指针数组
        for (uint32 i = 0; i < spb_number; i++) spb_array[i] = va_to_pa(kzalloc(PAGE_4K_SIZE << asm_tzcnt(xhcd->op_reg->pagesize)));
        //分配暂存器缓存区
        xhcd->dcbaap[0] = va_to_pa(spb_array); //暂存器缓存去数组指针写入设备上下写文数组0
    }

    // =========================================================================
    // 搜寻与分配 (寻找主板上所有的协议支持清单)
    // =========================================================================
    xhci_parse_supported_protocols(xhcd);

    // =========================================================================
    // 👑 必须先铺设“中断管线” (从 CPU 到 PCIe 总线)
    // =========================================================================
    /* 1. 向大管家申请中断向量号 */
    pcie_alloc_irq(xdev, 1);

    /* 2. 注册内核软件 ISR (此时 CPU 准备好接客了) */
    pcie_register_isr(xdev, 0, xhci_isr, xdev->dev.name);

    /* 3. 填入 MSI-X Table 并拉下总线电闸 (此时 PCIe 链路打通) */
    pcie_enable_irq(xdev);

    // =========================================================================
    // 🚀 管线铺好后，最后一步才能“放水” (启动 xHCI 外设)
    // =========================================================================
    /* 4. 打开具体的事件环阀门 (配置 IMAN 寄存器，启用 0 号队列) */
    xhci_enable_intr(xhcd, 0);

    /* 5. 轰鸣点火！启动全局 xHCI 控制器 (此时 USBCMD.RS 和 INTE 置 1) */
    /* 一旦执行完这句代码，随时可能有真实的硬件中断砸进 xhci_isr！*/
    xhci_start(xhcd);

    //6 给xhci所有端口上电
    for (uint8 port_num = 1; port_num <= xhcd->max_ports; port_num++) {
        xhci_port_power_on(xhcd, port_num);
    }

    for (uint8 i = 0; i < xhcd->spc_count; i++) {
        xhci_spc_t *spc = &xhcd->spc[i];
        color_printk(GREEN,BLACK, "spc%d %s%x.%x port_first:%d port_count:%d   \n", i, spc->name,
                     spc->major_bcd, spc->minor_bcd, spc->port_first, spc->port_count);
    }

    color_printk(
        GREEN,BLACK,
        "XHCI Version:%x.%x MaxSlots:%d MaxIntrs:%d MaxPorts:%d Dev_Ctx_Size:%d USBcmd:%#x USBsts:%#x    \n",
        xhcd->major_bcd, xhcd->minor_bcd, xhcd->max_slots,
        xhcd->max_intrs, xhcd->max_ports,
        xhcd->ctx_size, xhcd->op_reg->usbcmd,
        xhcd->op_reg->usbsts);

    xhci_port_scan(xhcd);

}

void xhci_remove(pcie_dev_t *xhci_dev) {
}

//xhci驱动初始化
pcie_drv_t *xhci_drv_init(void) {
    pcie_drv_t *xhci_drv = kmalloc(sizeof(pcie_drv_t));
    pcie_id_t *id_table = kzalloc(sizeof(pcie_id_t) * 2);
    id_table->class_code = XHCI_CLASS_CODE;
    xhci_drv->drv.name = "XHCI-driver";
    xhci_drv->drv.id_table = id_table;
    xhci_drv->probe = xhci_probe;
    xhci_drv->remove = xhci_remove;
    return xhci_drv;
}
