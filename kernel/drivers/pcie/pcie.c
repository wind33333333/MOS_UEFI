#include "pcie.h"
#include "../../init/acpi_init.h"
#include "printk.h"
#include "slub.h"
#include "vmalloc.h"
#include "bus.h"
#include "../x64/cpu.h"
#include "errno.h"


struct {
    uint32 class_code;
    char *name;
} pcie_classnames[] = {
    {UNCLASSIFIED_CLASS_CODE, "Unclassified Device"},
    {SCSI_CLASS_CODE, "SCSI Controller"},
    {AHCI_CLASS_CODE, "AHCI Controller (SATA)"},
    {NVME_CLASS_CODE, "NVMe Controller"},
    {ETHERNET_CLASS_CODE, "Ethernet Controller"},
    {VGA_CLASS_CODE, "VGA Controller"},
    {XGA_CLASS_CODE, "XGA Controller"},
    {DISPLAY_3D_CLASS_CODE, "3D Controller (Not VGA-Compatible)"},
    {DISPLAY_OTHER_CLASS_CODE, "Other Display Controller"},
    {MULTIMEDIA_VIDEO_CLASS_CODE, "Multimedia Video Controller"},
    {HOST_BRIDGE_CLASS_CODE, "Host Bridge"},
    {ISA_BRIDGE_CLASS_CODE, "ISA Bridge"},
    {EISA_BRIDGE_CLASS_CODE, "EISA Bridge"},
    {MCA_BRIDGE_CLASS_CODE, "MCA Bridge"},
    {PCI_TO_PCI_BRIDGE_CLASS_CODE, "PCI-to-PCI Bridge"},
    {PCMCIA_BRIDGE_CLASS_CODE, "PCMCIA Bridge"},
    {NUBUS_BRIDGE_CLASS_CODE, "NuBus Bridge"},
    {CARDBUS_BRIDGE_CLASS_CODE, "CardBus Bridge"},
    {RACEWAY_BRIDGE_CLASS_CODE, "RACEway Bridge"},
    {PCI_TO_PCI_ALT_CLASS_CODE, "PCI-to-PCI Bridge"},
    {INFINIBAND_TO_PCI_CLASS_CODE, "InfiniBand-to-PCI Host Bridge"},
    {BRIDGE_OTHER_CLASS_CODE, "Other Bridge"},
    {UHCI_CLASS_CODE, "UHCI Controller (USB1.1)"},
    {OHCI_CLASS_CODE, "OHCI Controller (USB1.1)"},
    {EHCI_CLASS_CODE, "EHCI Controller (USB2.0)"},
    {XHCI_CLASS_CODE, "XHCI Controller (USB3.0)"},
    {FIBRE_CHANNEL_CLASS_CODE, "Fibre Channel"},
    {SMBUS_CLASS_CODE, "SMBus Controller"},
    {INFINIBAND_CLASS_CODE, "InfiniBand Controller"},
    {IPMI_CLASS_CODE, "IPMI Interface"},
    {SERCOS_CLASS_CODE, "SERCOS Interface (IEC 61491)"},
    {CANBUS_CLASS_CODE, "CANbus Controller"},
    {SERIAL_BUS_OTHER_CLASS_CODE, "Other Serial Bus Controller"},
    {0x000000, "Unknown device"}
};

/*
 *获取msi-x表bar号
 *参数pcie_config_space_t
 *数量
 */
static inline uint8 get_msix_bir(msix_cap_t *msix_cap) {
    uint32 bir = msix_cap->table_offset;
    return bir & 0x7;
}

/*
 * 获取msi-x表相对bar偏移量
 */
static inline uint32 get_msix_offset(msix_cap_t *msix_cap) {
    uint32 table_offset = msix_cap->table_offset;
    return table_offset & ~0x7;
}

/*
 *获取pda表bar号
 *参数pcie_config_space_t
 *数量
 */
static inline uint8 get_pda_bir(msix_cap_t *msix_cap) {
    uint32 bir = msix_cap->pba_offset;
    return bir & 0x7;
}

/*
 * 获取pda表相对bar偏移量
 */
static inline uint32 get_pda_offset(msix_cap_t *msix_cap) {
    uint32 table_offset = msix_cap->pba_offset;
    return table_offset & ~0x7;
}

//判断bar 64位或32位
//0等于32位，4等于32位
static inline uint64 is_bar64(uint64 bar_data) {
    return bar_data & 0x6;
}

//bar寄存器初始化
void pcie_bar_init(pcie_dev_t *pcie_dev) {
    pcie_config_space_t *pcie_config_space = pcie_dev->pcie_config_space;
    uint8 bir = 0;
    uint8 i = 0;
    while (bir < 6){
        uint32 *bar = &pcie_config_space->type0.bar[bir];
        uint64 addr = *bar;
        *bar = 0xFFFFFFFF;
        uint64 size = *bar;
        *bar = (uint32)addr;
        if (is_bar64(addr)) {
            bar++;
            uint64 addr_h = *bar;
            *bar = 0xFFFFFFFF;
            uint64 size_h = *bar;
            *bar = (uint32)addr_h;
            size |= size_h << 32;
            addr |= addr_h << 32;
            size &= 0xFFFFFFFFFFFFFFF0UL;
            size = -size;
            bir++;
        }else {
            size &= 0xFFFFFFF0UL;
            size = -(uint32)size;
        }
        addr &= 0xFFFFFFFFFFFFFFF0UL;
        pcie_dev->bar[i].paddr = addr;
        pcie_dev->bar[i].size = size;
        pcie_dev->bar[i].vaddr = ioremap(addr,size);
        i++;
        bir++;
    }
}

//搜索能力链表
//参数capability_id
//返回一个capability_t* 指针
void *pcie_cap_find(pcie_dev_t *pcie_dev, cap_type_e cap_type) {
    //检测是否支持能力链表,不支持返回空指针
    if (!(pcie_dev->pcie_config_space->status & 0x10)) return NULL;
    //计算能力链表起始地址
    uint8 next_ptr = pcie_dev->pcie_config_space->type0.cap_ptr;
    while (next_ptr){
        cap_head_t *cap_head = (cap_head_t*)((uint64)pcie_dev->pcie_config_space + next_ptr);
        if (cap_head->cap_id == cap_type) return cap_head;
        next_ptr = cap_head->next_ptr;
    }
    return NULL;
}

//pcie设备msi中断功能解析
static inline void pcie_parse_irq(pcie_dev_t *pcie_dev) {
    pcie_dev->active_irq_type = PCIE_IRQ_NONE;

    msi_cap_t *msi_cap = pcie_cap_find(pcie_dev,msi_e);
    if (msi_cap) {
        //启用msi中断
        pcie_dev->msi = &msi_cap->msi;
    }

    //msi-x中断,优先启用
    msix_cap_t *msix_cap = pcie_cap_find(pcie_dev,msi_x_e);
    if (msix_cap) {
        //启用mmsi-x中断
        pcie_dev->msix.msg_control = &msix_cap->msg_control;
        pcie_dev->msix.msix_entry = (void*)(pcie_dev->bar[get_msix_bir(msix_cap)].vaddr+get_msix_offset(msix_cap)); //计算msix表起始地址
        pcie_dev->msix.pba = (void*)(pcie_dev->bar[get_pda_bir(msix_cap)].vaddr+get_pda_offset(msix_cap));
    }

}


// 辅助函数：简单的多核轮询负载均衡分配器
// 每次调用返回下一个 CPU 的 ID，保证多队列设备的中断均匀散布到各个核心
static uint32 get_next_routing_cpu(void) {
    static uint32 current_cpu_idx = 0;
    // 使用原子操作或加锁获取下一个 CPU (这里假设有个简单的原子递增)
    // 假设 MAX_CPU_NUM 是系统当前激活的 CPU 数量
    uint32 target = (current_cpu_idx++) % cpu_info.run_cpu_count;
    (void)target;
    return 0;   //暂时统一返回bsp核心
}

/**
 * @brief 为 PCIe 设备动态分配并路由中断 (支持 MSI-X 稀疏分配与 MSI 连续对齐降级)
 * @param pdev  PCIe 设备结构体
 * @param count 驱动请求的中断队列数量
 * @return 成功返回实际分配的数量，失败返回负数错误码
 */
int32 pcie_alloc_irq(pcie_dev_t *pdev, uint8 count) {
    if (count == 0 || count > PCIE_MAX_VECTORS) return -EINVAL;
    if (pdev->allocated_vectors > 0) return -EBUSY;

    // 默认高速设备策略：从 0xDF 向下分配
    irq_alloc_strategy_t strategy = IRQ_ALLOC_HIGH_SPEED;

    // =========================================================================
    // 路线 A：MSI-X (现代设备的巅峰，支持跨 CPU 自由散布，完全无需连续对齐)
    // =========================================================================
    if (pdev->msix.msg_control != NULL) {
        uint8 allocated = 0;

        for (uint8 i = 0; i < count; i++) {
            // 🌟 负载均衡：每个队列都去寻找下一个空闲的 CPU
            uint32 target_cpu = get_next_routing_cpu();

            // 🌟 调用新 API：申请 1 个，无需对齐 (align=1)
            int32 vector = alloc_irq_vector(target_cpu, 1, 1, strategy);

            if (vector < 0) {
                // 如果当前 CPU 池子满了，在工业级代码中，可以重试另一个 CPU。
                // 这里为了简化，直接中断分配。已分配的不会丢失。
                break;
            }

            // 记录下这个队列被路由到了哪个 CPU 的哪个 Vector
            pdev->cpu_vectors[i].cpu_id = target_cpu;
            pdev->cpu_vectors[i].vector = vector;
            allocated++;
        }

        if (allocated == 0) return -ENOSPC;

        pdev->active_irq_type = PCIE_IRQ_MSIX;
        pdev->allocated_vectors = allocated;
        return allocated;
    }

    // =========================================================================
    // 路线 B：古董 MSI (必须在同一个 CPU 上，必须绝对连续且 Base 按 2 的次幂对齐)
    // =========================================================================
    else if (pdev->msi->msg_control != 0) {

        // 规范化 count：MSI 必须申请 2 的次幂
        int8 power2_count = 1;
        while (power2_count < count) { power2_count <<= 1; }
        if (power2_count > 32) power2_count = 32;

        // 🌟 负载均衡：因为传统 MSI 只能绑定一个 CPU，所以整个设备的所有队列只能路由给这 1 个 CPU
        uint32 target_cpu = get_next_routing_cpu();

        int32 base_vector = -1;

        // 尝试分配 (自带回退降级逻辑)
        while (power2_count >= 1) {
            // 🌟 调用新 API：必须申请连续的 count 个，且首地址必须按 count 对齐！
            base_vector = alloc_irq_vector(target_cpu, power2_count, power2_count, strategy);

            if (base_vector >= 0) {
                break; // 成功拿到连续对齐的资源！
            }

            // 降级：连这都分不出来？砍半重试！
            PR_WARN("PCIE MSI: %d contiguous vectors alloc failed on CPU %u. Downgrading...\n",
                    power2_count, target_cpu);
            power2_count >>= 1;
        }

        if (base_vector < 0) return -ENOSPC; // 连 1 个都分不出来，资源彻底枯竭

        // 成功拿到了连续的队列，记录到设备结构体中
        for (uint8 i = 0; i < power2_count; i++) {
            pdev->cpu_vectors[i].cpu_id = target_cpu;
            pdev->cpu_vectors[i].vector = base_vector + i;
        }

        pdev->active_irq_type = PCIE_IRQ_MSI;
        pdev->allocated_vectors = power2_count;
        return power2_count;
    }

    return -ENODEV; // 既不支持 MSI-X 也不支持 MSI
}

/**
 * @brief 释放 PCIe 设备占据的中断向量
 */
void pcie_free_irq(pcie_dev_t *pdev) {
    if (pdev->allocated_vectors == 0) return;

    if (pdev->active_irq_type == PCIE_IRQ_MSI) {
        // 🌟 调用新 API：MSI 是挂在同一个 CPU 上的连续分配，一把全部释放
        uint32 target_cpu = pdev->cpu_vectors[0].cpu_id;
        int32 base_vector = pdev->cpu_vectors[0].vector;

        free_irq_vector(target_cpu, base_vector, pdev->allocated_vectors);
    }
    else if (pdev->active_irq_type == PCIE_IRQ_MSIX) {
        // 🌟 调用新 API：MSI-X 是散布在各个 CPU 上的，必须逐个归还地皮
        for (uint8 i = 0; i < pdev->allocated_vectors; i++) {
            uint32 target_cpu = pdev->cpu_vectors[i].cpu_id;
            int32 vector      = pdev->cpu_vectors[i].vector;

            // 逐个归还 (每次归还 1 个)
            free_irq_vector(target_cpu, vector, 1);
        }
    }

    pdev->active_irq_type = PCIE_IRQ_NONE;
    pdev->allocated_vectors = 0;
}


/**
 * @brief 辅助宏：根据目标 CPU 的 APIC ID，生成 MSI Address
 */
static uint32 build_msi_address(uint32 apic_id) {
    // 🌟 工业级防火墙：如果内核尚未开启 VT-d 中断重映射，绝对禁止路由到高级核心！
    // 注：0xFF (255) 在传统 APIC 中通常被保留为 Broadcast（广播）地址，不能用于单播。
    if (apic_id >= 255) {
        PR_ERROR("FATAL: Cannot route MSI to x2APIC ID %u without IOMMU Interrupt Remapping!\n", apic_id);

        // 【降级策略】：强行把中断降级路由给 CPU 0 (Boot Processor)
        // 保证系统还能运行，但失去了该设备在超多核上的负载均衡
        apic_id = 0;
    }
    // 物理目标模式 (RH=0, DM=0)，将 APIC ID 放在 Bit 12~19
    return X86_MSI_BASE_ADDRESS | ((apic_id & 0xFF) << 12);
}

/**
 * @brief 启用设备中断 (写硬件寄存器，正式下发路由表)
 */
int32 pcie_enable_irq(pcie_dev_t *pdev) {
    if (pdev->allocated_vectors == 0) return -EINVAL;

    if (pdev->active_irq_type == PCIE_IRQ_MSI) {
        // ========== 启用纯 MSI (所有队列绑在同一个 CPU) ==========
        msi_t *msi = pdev->msi;

        // 1. 🌟 提取目标 CPU 的 APIC ID
        uint32 target_cpu = pdev->cpu_vectors[0].cpu_id;
        uint32 apic_id    = get_lid_by_per_cpu(target_cpu)->apic_id; // 需确保 cpu_core 结构体里有 apic_id

        // 2. 🌟 动态生成 Address 和获取 Vector
        msi->msg_addr_lo = build_msi_address(apic_id);
        uint32 vector    = pdev->cpu_vectors[0].vector;

        uint16 ctrl = msi->msg_control;
        if (ctrl & 0x80) { // 64位地址支持
            msi->_64bit.msg_addr_hi = 0;
            msi->_64bit.msg_data = vector;
        } else {
            msi->_32bit.msg_data = vector;
        }

        // 3. 配置多消息使能 (Multiple Message Enable: Bits 4-6)
        uint8 log2_count = 0;
        while ((1 << log2_count) < pdev->allocated_vectors && log2_count < 5) {
            log2_count++;
        }
        ctrl &= ~(0x07 << 4);      // 先清空 Bits 4-6
        ctrl |= (log2_count << 4); // 填入启用的数量

        // 4. 打开 MSI 硬件总开关 (Bit 0)
        ctrl |= 0x0001;
        msi->msg_control = ctrl;

        return 0;
    }

    if (pdev->active_irq_type == PCIE_IRQ_MSIX) {
        // ========== 启用 MSI-X (终极自由：跨 CPU 散布) ==========
        msix_entry_t *msix_entry = pdev->msix.msix_entry;

        for (int i = 0; i < pdev->allocated_vectors; i++) {
            // 1. 🌟 逐个提取每个队列的目标 CPU 的 APIC ID
            uint32 target_cpu = pdev->cpu_vectors[i].cpu_id;
            uint32 apic_id    = get_lid_by_per_cpu(target_cpu)->apic_id;
            uint32 vector     = pdev->cpu_vectors[i].vector;

            // 2. 🌟 硬件编程：分别指引这 N 个队列走向不同的 CPU！
            msix_entry[i].msg_addr_lo = build_msi_address(apic_id);
            msix_entry[i].msg_addr_hi = 0;
            msix_entry[i].msg_data    = vector;

            // 3. 清除硬件 Mask 位，允许网卡开始发信号
            msix_entry[i].vector_control &= ~0x1;
        }

        // 4. 修改 MSI-X Control 寄存器
        uint16 ctrl = *pdev->msix.msg_control;
        ctrl |= 0x8000;  // MSI-X Enable 位 (Bit 15) 打开
        ctrl &= ~0x4000; // Function Mask 位 (Bit 14) 清除，全放行
        *pdev->msix.msg_control = ctrl;

        return 0;
    }

    return -ENOTSUP;
}


/**
 * @brief 关闭设备中断 (拉下硬件总闸，让设备彻底闭嘴)
 */
void pcie_disable_irq(pcie_dev_t *pdev) {
    if (!pdev) return;

    if (pdev->active_irq_type == PCIE_IRQ_MSI) {
        // ==========================================
        // 关闭纯 MSI (风格统一，极简操作)
        // ==========================================
        msi_t *msi = pdev->msi;
        uint16 ctrl = msi->msg_control;

        ctrl &= ~0x0001; // 清除 Bit 0: MSI Enable
        msi->msg_control = ctrl;

    } else if (pdev->active_irq_type == PCIE_IRQ_MSIX) {
        // ==========================================
        // 关闭 MSI-X (废弃旧宏，全面拥抱结构体指针)
        // ==========================================

        // ==========================================
        //  👑 你的神级补漏：逐个屏蔽 Table 中的独立 Entry
        // ==========================================
        msix_entry_t *msix_entry = pdev->msix.msix_entry;
        for (int i = 0; i < pdev->allocated_vectors; i++) {
            // 动作 A：设置 Bit 0，独立屏蔽该向量 (Per-Vector Mask)
            msix_entry[i].vector_control |= 0x1;

            // 动作 B：极其硬核的防御性编程，清空残留的旧地址和旧向量号
            // 这样即使硬件发疯，发出的也是目标地址为 0 的无效写，不会打乱 CPU 中断表
            msix_entry[i].msg_addr_lo = 0;
            msix_entry[i].msg_addr_hi = 0;
            msix_entry[i].msg_data    = 0;
        }

        // 注意：这里直接使用你在 enable 时定义的 msg_control 指针！
        uint16 ctrl = *pdev->msix.msg_control;

        ctrl &= ~0x8000; // 👑 动作 1：清除 Bit 15 (MSI-X Enable)，关闭总闸
        ctrl |= 0x4000;  // 🛡️ 动作 2：设置 Bit 14 (Function Mask)，开启全局屏蔽！

        *pdev->msix.msg_control = ctrl;
    }
}


/**
 * @brief 在已分配的 PCIe 中断通道上，正式挂载/注册驱动服务程序 (ISR)
 * @param pdev  PCIe 设备控制块 (包含已分配的 cpu_id 和 vector)
 * @param index 设备的第几个中断队列 (0 ~ allocated_vectors - 1)
 * @param isr   驱动提供的中断处理函数指针
 * @param name  驱动的文本标识 (用于 /proc/interrupts 日志)
 * @return 0 成功；负数表示失败
 */
int32 pcie_register_isr(pcie_dev_t *pdev, uint8 index, irq_handler_f isr, const char *name) {
    // 1. 边界防御：只能在之前 alloc 分配到的数量范围内注册
    if (index >= pdev->allocated_vectors) return -EINVAL;

    // 2. 提取属于这个队列的专属 CPU ID 和 Vector 号
    uint32 target_cpu = pdev->cpu_vectors[index].cpu_id;
    uint32 vector     = pdev->cpu_vectors[index].vector;

    // 3. 呼叫底层的跨核安全注册大管家
    // 注意：把 pdev 作为 dev_id (安全凭证和私有数据) 传递给底层
    return register_irq(target_cpu, vector, isr, pdev, name);
}

/**
 * @brief 卸载 PCIe 设备的某个中断队列的 ISR
 * @param pdev  PCIe 设备控制块
 * @param index 设备的第几个中断队列
 */
void pcie_unregister_isr(pcie_dev_t *pdev, uint8 index) {
    // 1. 边界防御
    if (index >= pdev->allocated_vectors) return;

    // 2. 提取属于这个队列的专属 CPU ID 和 Vector 号
    uint32 target_cpu = pdev->cpu_vectors[index].cpu_id;
    uint32 vector     = pdev->cpu_vectors[index].vector;

    // 3. 呼叫底层的跨核安全卸载大管家
    // 注意：必须传入 pdev 作为 dev_id 安全凭证，证明这是我的设备，我才有权卸载
    unregister_irq(target_cpu, vector, pdev);
}

//匹配驱动id
static inline pcie_id_t *pcie_match_id(pcie_dev_t *pcie_dev,driver_t *drv) {
    pcie_id_t *id_table = drv->id_table;
    for (;(id_table->vendor&&id_table->device)||id_table->class_code;id_table++) {
        if ((id_table->vendor==pcie_dev->vendor&&id_table->device==pcie_dev->device) || id_table->class_code==pcie_dev->class_code)
            return id_table;
    }
    return NULL;
}

//pcie总线层设备驱动匹配
int pcie_bus_match(device_t *dev,driver_t *drv) {
    pcie_dev_t *pcie_dev = CONTAINER_OF(dev,pcie_dev_t,dev);
    pcie_id_t *id = pcie_match_id(pcie_dev,drv);
    return id ? 1 : 0;
}

//pcie总线层探测初始化回调
int pcie_bus_probe(device_t *dev) {
    pcie_dev_t *pcie_dev = CONTAINER_OF(dev,pcie_dev_t,dev);
    // 1) enable memory/io/bus mastering
    pcie_dev->pcie_config_space->command = PCIE_ENABLE_MEM_SPACE|PCIE_ENABLE_BUS_MASTER|PCIE_DISABLE_INTER;
    // 2) 读取/分配 BAR（你 OS 里也许固定映射或自己分配）
    pcie_bar_init(pcie_dev);
    // 3) 初始化msi/msi-x
    pcie_parse_irq(pcie_dev);
    return 0;
}

//pcie总线层卸载在回调
void pcie_bus_remove(device_t *dev) {
    (void)dev;
}

//pcie驱动层探测初始化回调
int pcie_drv_probe(device_t *dev) {
    pcie_dev_t *pcie_dev = CONTAINER_OF(dev,pcie_dev_t,dev);
    pcie_drv_t *pcie_drv = CONTAINER_OF(dev->drv,pcie_drv_t,drv);
    pcie_id_t *id = pcie_match_id(pcie_dev,dev->drv);
    pcie_drv->probe(pcie_dev,id); //加载pcie设备专属驱动初
    return 0;
}

//pcie驱动层卸载回调
void pcie_drv_remove(device_t *dev) {
    (void)dev;
}

//获取pice设备的class_code
static inline uint32 pcie_get_classcode(pcie_dev_t *pcie_dev) {
    uint32 *class_code = (uint32*)&pcie_dev->pcie_config_space->revision_id;
    return *class_code >> 8;
}

//查找pcie的类名
static inline char *pcie_clasename_find(pcie_dev_t *pcie_dev) {
    uint32 class_code = pcie_get_classcode(pcie_dev);
    uint32 i = 0;
    for (; pcie_classnames[i].class_code != 0; i++) {
        if (class_code == pcie_classnames[i].class_code) return pcie_classnames[i].name;
    }
    return pcie_classnames[i].name;
}

extern bus_type_t pcie_bus_type;
//创建pcie设备
static inline pcie_dev_t *pcie_dev_create(pcie_config_space_t *pcie_config_space, uint8 bus, uint8 dev, uint8 func,device_t *parent) {
    pcie_dev_t *pcie_dev = kzalloc(sizeof(pcie_dev_t));
    pcie_dev->bus_num = bus;
    pcie_dev->dev_num = dev;
    pcie_dev->func_num = func;
    pcie_dev->pcie_config_space = pcie_config_space;
    pcie_dev->class_code = pcie_get_classcode(pcie_dev);
    pcie_dev->vendor = pcie_dev->pcie_config_space->vendor_id;
    pcie_dev->device = pcie_dev->pcie_config_space->device_id;

    pcie_dev->dev.name = pcie_clasename_find(pcie_dev);
    pcie_dev->dev.bus = &pcie_bus_type;
    pcie_dev->dev.parent = parent;
    return pcie_dev;
}

//pcie设备注册
static inline void pcie_dev_register(pcie_dev_t *pcie_dev) {
    device_register(&pcie_dev->dev);
}

//pcie设备驱动注册
void pcie_drv_register(pcie_drv_t *pcie_drv) {
    pcie_drv->drv.bus = &pcie_bus_type;
    pcie_drv->drv.probe = pcie_drv_probe;
    pcie_drv->drv.remove = pcie_drv_remove;
    driver_register(&pcie_drv->drv);
}


/*
 * pcie配置空间地址计算
 */
static inline pcie_config_space_t *ecam_bdf_to_pcie_config_space_addr(void *ecam_base, uint8 bus, uint8 dev,
                                                                      uint8 func) {
    return (pcie_config_space_t *) ((uint64)ecam_base + (bus << 20) + (dev << 15) + (func << 12));
}

/*
 * pcie总线扫描
 */
static inline void pcie_scan_dev(void* ecam_base,uint8 bus,device_t* parent) {
    // 遍历当前总线上的32个设备(0-31)
    for (uint8 dev = 0; dev < 32; dev++) {
        // 遍历设备上的8个功能(0-7)
        for (uint8 func = 0; func < 8; func++) {
            // 将BDF(总线-设备-功能)转换为配置空间地址
            pcie_config_space_t *pcie_config_space = ecam_bdf_to_pcie_config_space_addr(ecam_base, bus, dev, func);
            // 检查设备是否存在 (无效设备的vendor_id=0xFFFF)
            if (pcie_config_space->vendor_id == 0xFFFF) {
                // 功能0不存在意味着整个设备不存在， 跳过该设备的后续功能
                if (func == 0) break;
                // 继续检查下一个功能
                continue;
            }
            //pcie_dev创建
            pcie_dev_t *pcie_dev = pcie_dev_create(pcie_config_space, bus, dev, func,parent);
            //pcie_dev注册
            pcie_dev_register(pcie_dev);
            //type1==pcie桥优先扫描下游设备（深度优先）
            if (pcie_config_space->header_type & 1) pcie_scan_dev(ecam_base,pcie_config_space->type1.secondary_bus,&pcie_dev->dev);
            //如果功能0不是多功能设备，则跳过该设备的后续功能
            if (!func && !(pcie_config_space->header_type & 0x80)) break;
        }
    }
}

//创建pcie根复合体
pcie_root_complex_t *pcie_rc_create(mcfg_entry_t* mcfg_entry) {
    pcie_root_complex_t *pcie_rc = kzalloc(sizeof(pcie_root_complex_t));
    pcie_rc->ecam_phy_base = mcfg_entry->base_address;
    pcie_rc->pcie_segment = mcfg_entry->pci_segment;
    pcie_rc->start_bus = mcfg_entry->start_bus;
    pcie_rc->end_bus = mcfg_entry->end_bus;
    uint64 ecma_size = (pcie_rc->end_bus - pcie_rc->start_bus + 1)*32*8*4096;
    pcie_rc->ecam_vir_base = ioremap(pcie_rc->ecam_phy_base,ecma_size);
    pcie_rc->dev.name = "pcie-root";
    pcie_rc->dev.parent = NULL;
    list_head_init(&pcie_rc->dev.child_node);
    list_head_init(&pcie_rc->dev.child_list);
    return pcie_rc;
}

//pcie总线初始化
INIT_TEXT void pcie_bus_init(void) {
    //查找mcfg表
    mcfg_t *mcfg = acpi_get_table(ACPI_SIG_MCFG,0);
    uint32 ecma_count = (mcfg->acpi_header.length - sizeof(acpi_header_t) - sizeof(mcfg->reserved)) / sizeof(
                            mcfg_entry_t);
    //创建pcie根复合体
    pcie_root_complex_t **pcie_rc = kzalloc(sizeof(void*)*ecma_count);

    //扫描pcie总线，把pcie设备挂在到系统总线
    for (uint32 i = 0; i < ecma_count; i++) {
        pcie_rc[i] = pcie_rc_create(&mcfg->entry[i]);
        pcie_rc[i]->num = i;

        color_printk(GREEN,BLACK, "ECAM Pbase:%#lx -> Vbase:%#lx Segment:%d StartBus:%d EndBus:%d\n",
                     pcie_rc[i]->ecam_phy_base,pcie_rc[i]->ecam_vir_base, pcie_rc[i]->pcie_segment, pcie_rc[i]->start_bus,pcie_rc[i]->end_bus);

        pcie_scan_dev(pcie_rc[i]->ecam_vir_base, pcie_rc[i]->start_bus,&pcie_rc[i]->dev);
    }

    //打印pcie设备
    for (list_head_t *next = pcie_bus_type.dev_list.next;next != &pcie_bus_type.dev_list;next = next->next){
        device_t *dev = CONTAINER_OF(next, device_t,bus_node );
        pcie_dev_t *pcie_dev = CONTAINER_OF(dev,pcie_dev_t,dev);
        color_printk(GREEN,BLACK, "bus:%d dev:%d func:%d vendor_id:%#lx device_id:%#lx class_code:%#lx %s\n",
                     pcie_dev->bus_num, pcie_dev->dev_num, pcie_dev->func_num, pcie_dev->pcie_config_space->vendor_id,
                     pcie_dev->pcie_config_space->device_id, pcie_dev->class_code, dev->name);
    }

    //注册驱动程序
    pcie_drv_t *xhci_drv_init(void);
    pcie_drv_register(xhci_drv_init());


}
