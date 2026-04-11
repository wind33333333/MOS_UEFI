#include "pcie.h"
#include "acpi.h"
#include "printk.h"
#include "slub.h"
#include "vmalloc.h"
#include "vmm.h"
#include "bus.h"
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
    {0x000000, NULL}
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
        pcie_dev->bar[i].vaddr = iomap(addr,size,PAGE_4K_SIZE,PAGE_ROOT_RW_UC_4K);
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


int32 pcie_alloc_irq(pcie_dev_t *pdev, int8 count) {
    if (count == 0 || count > PCIE_MAX_VECTORS) return -EINVAL;
    if (pdev->allocated_vectors > 0) return -EBUSY;

    // ========== 路线 A：MSI-X 降维打击 (支持稀疏分布) ==========
    if (pdev->msix.msg_control != NULL) {
        int8 allocated = 0;
        for (int8 i = 0; i < count; i++) {
            int32 vector = alloc_irq();
            if (vector < 0) break;
            pdev->cpu_vectors[i] = vector;
            allocated++;
        }

        if (allocated == 0) return -ENOSPC;
        pdev->active_irq_type = PCIE_IRQ_MSIX;
        pdev->allocated_vectors = allocated;
        return allocated;
    }

    // ========== 路线 B：古董 MSI 戴着镣铐跳舞 ==========
    else if (pdev->msi->msg_control != NULL) {
        // 规范化 count：MSI 必须申请 2 的次幂，如果驱动要 3 个，只能强行升到 4 个
        int8 power2_count = 1;
        while (power2_count < count) { power2_count <<= 1; }
        // PCIe 规范规定单个设备 MSI 最多申请 32 个
        if (power2_count > 32) power2_count = 32;

        // 呼叫咱们刚写的连续对齐分配大招！
        int32 vector = alloc_contiguous_irq(power2_count);

        if (vector < 0) {
            // ★ 工业级 OS 容错降级：如果连续分配失败，内核不能直接拒绝！
            // 而是强行降级为只分配 1 个中断！让网卡多队列退化为单队列运行。
            color_printk(YELLOW, BLACK, "PCIE MSI: Contiguous alloc failed. Downgrading to 1 vector.\n");
            vector = alloc_irq();
            if (vector < 0) return -ENOSPC;
            pdev->cpu_vectors[0] = vector;
            pdev->allocated_vectors = 1;
            return 1;
        }

        // 成功拿到了连续的队列
        for (int8 i = 0; i < power2_count; i++) {
            pdev->cpu_vectors[i] = vector + i;
        }
        pdev->active_irq_type = PCIE_IRQ_MSI;
        pdev->allocated_vectors = power2_count;
        return power2_count;
    }
}

//释放中断号和apic
void pcie_free_irq(pcie_dev_t *pdev) {
    if (pdev->allocated_vectors == 0) return;

    if (pdev->active_irq_type == PCIE_IRQ_MSI) {
        // MSI 是连续分配的，调用连续释放函数
        free_contiguous_irq(pdev->cpu_vectors[0], pdev->allocated_vectors);
    }
    else if (pdev->active_irq_type == PCIE_IRQ_MSIX) {
        // MSI-X 可能是稀疏分配的，所以必须逐个释放
        for (int8 i = 0; i < pdev->allocated_vectors; i++) {
            free_irq(pdev->cpu_vectors[i]); // 你之前写的单向量释放函数
        }
    }

    pdev->active_irq_type = PCIE_IRQ_NONE;
    pdev->allocated_vectors = 0;
}



/**
 * @brief 启用设备中断 (写硬件寄存器)
 * 这一步告诉 PCIe 设备：“有事往 x86_MSI_ADDRESS 写，数据内容就是 Vector 号！”
 */
int32 pcie_enable_irq(pcie_dev_t *pdev) {
    if (pdev->allocated_vectors == 0) return -EINVAL;

    if (pdev->active_irq_type == PCIE_IRQ_MSI) {
        // ========== 启用纯 MSI ==========
        msi_t *msi = pdev->msi;
        msi->msg_addr_lo = X86_MSI_ADDRESS;
        uint16 ctrl = msi->msg_control;
        if (ctrl & 0x80) {
            msi->_64bit.msg_addr_hi = 0;
            msi->_64bit.msg_data = pdev->cpu_vectors[0];
        }else {
            msi->_32bit.msg_data = pdev->cpu_vectors[0];
        }

        // 3. 配置多消息使能 (Multiple Message Enable: Bits 4-6)
        // 根据实际分配的向量数 (allocated_vectors)，计算对应的 2的幂次
        uint8 log2_count = 0;
        while ((1 << log2_count) < pdev->allocated_vectors && log2_count < 5) {
            log2_count++;
        }
        ctrl &= ~(0x07 << 4);      // 先清空 Bits 4-6
        ctrl |= (log2_count << 4); // 填入启用的数量

        ctrl |= 0x0001;
        msi->msg_control = ctrl;
        return 0;

    }

    if (pdev->active_irq_type == PCIE_IRQ_MSIX) {
        // ========== 启用 MSI-X ==========
        msix_entry_t *msix_entry = pdev->msix.msix_entry;
         for (int i = 0; i < pdev->allocated_vectors; i++) {
            msix_entry[i].msg_addr_lo = X86_MSI_ADDRESS;
            msix_entry[i].msg_addr_hi = 0;
            msix_entry[i].msg_data = pdev->cpu_vectors[i];
            msix_entry[i].vector_control &= ~0x1; // 清除 Mask 位，允许触发
        }

        // 修改 MSI-X Control 寄存器，打开 MSI-X Enable 位 (Bit 15)
        uint16 ctrl = *pdev->msix.msg_control;
        ctrl |= 0x8000;
        ctrl &= ~0x4000;
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
 * @brief 注册中断服务函数
 * @param index 设备的第几个中断队列 (0 ~ allocated_vectors-1)
 */
int32 pcie_register_isr(pcie_dev_t *pdev, int8 index, irq_handler_f isr, const char *name) {
    if (index >= pdev->allocated_vectors) return -EINVAL;

    int8 vector = pdev->cpu_vectors[index];
    // 调用大管家的注册接口
    return register_isr(vector, isr, pdev, name);
}

/**
 * @brief 卸载中断服务函数
 */
void pcie_unregister_isr(pcie_dev_t *pdev, int8 index) {
    if (index < pdev->allocated_vectors) {
        unregister_isr(pdev->cpu_vectors[index]);
    }
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
}

//获取pice设备的class_code
static inline uint32 pcie_get_classcode(pcie_dev_t *pcie_dev) {
    uint32 *class_code = (uint32*)&pcie_dev->pcie_config_space->revision_id;
    return *class_code >> 8;
}

//查找pcie的类名
static inline char *pcie_clasename_find(pcie_dev_t *pcie_dev) {
    uint32 class_code = pcie_get_classcode(pcie_dev);
    for (int i = 0; pcie_classnames[i].name != NULL; i++) {
        if (class_code == pcie_classnames[i].class_code) return pcie_classnames[i].name;
    }
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
    pcie_rc->ecam_vir_base = iomap(pcie_rc->ecam_phy_base,ecma_size,PAGE_4K_SIZE,PAGE_ROOT_RW_UC_4K);
    pcie_rc->dev.name = "pcie-root";
    pcie_rc->dev.parent = NULL;
    list_head_init(&pcie_rc->dev.child_node);
    list_head_init(&pcie_rc->dev.child_list);
    return pcie_rc;
}

//pcie总线初始化
INIT_TEXT void pcie_bus_init(void) {
    //查找mcfg表
    mcfg_t *mcfg = acpi_get_table('GFCM');
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
