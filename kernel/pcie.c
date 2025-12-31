#include "pcie.h"
#include "acpi.h"
#include "apic.h"
#include "printk.h"
#include "slub.h"
#include "vmalloc.h"
#include "vmm.h"
#include "bus.h"

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

//启用msi中断
void pcie_enable_msi_intrs(pcie_dev_t *pcie_dev) {
    if (pcie_dev->msi_x_flags) {
        *pcie_dev->msi_x.msg_control |= 0x8000;
        *pcie_dev->msi_x.msg_control &= ~0x4000;
    }else {
        pcie_dev->msi->msg_control |= 1;
    }
}

//禁用msi中断
void pcie_disable_msi_intrs(pcie_dev_t *pcie_dev) {
    if (pcie_dev->msi_x_flags) {
        *pcie_dev->msi_x.msg_control |= 0x4000;
    }else {
        pcie_dev->msi->msg_control &= ~1;
    }

}

/*
 *获取msi-x表bar号
 *参数pcie_config_space_t
 *数量
 */
static inline uint8 get_msi_x_bir(cap_t *cap) {
    uint32 bir = cap->msi_x.table_offset;
    return bir & 0x7;
}

/*
 * 获取msi-x表相对bar偏移量
 */
static inline uint32 get_msi_x_offset(cap_t *cap) {
    uint32 table_offset = cap->msi_x.table_offset;
    return table_offset & ~0x7;
}

/*
 *获取pda表bar号
 *参数pcie_config_space_t
 *数量
 */
static inline uint8 get_pda_bir(cap_t *cap) {
    uint32 bir = cap->msi_x.pba_offset;
    return bir & 0x7;
}

/*
 * 获取pda表相对bar偏移量
 */
static inline uint32 get_pda_offset(cap_t *cap) {
    uint32 table_offset = cap->msi_x.pba_offset;
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
        i++;
        bir++;
    }
}

//搜索能力链表
//参数capability_id
//返回一个capability_t* 指针
cap_t *pcie_cap_find(pcie_dev_t *pcie_dev, cap_id_e cap_id) {
    //检测是否支持能力链表,不支持返回空指针
    if (!(pcie_dev->pcie_config_space->status & 0x10)) return NULL;
    //计算能力链表起始地址
    uint8 next_ptr = pcie_dev->pcie_config_space->type0.cap_ptr;
    while (next_ptr){
        cap_t *cap = (cap_t*)((uint64)pcie_dev->pcie_config_space + next_ptr);
        if (cap->cap_id == cap_id) return cap;
        next_ptr = cap->next_ptr;
    }
    return NULL;
}

//pcie设备msi中断功能初始化
void pcie_msi_intrpt_init(pcie_dev_t *pcie_dev) {
    cap_t *cap = pcie_cap_find(pcie_dev,msi_x_e);
    //优先启用msi-x中断
    if (cap) {
        //初始化msi-x中断表
        pcie_dev->msi_x_flags = 1;
        pcie_dev->msi_x.msg_control = &cap->msi_x.msg_control;
        pcie_dev->msi_x.table_bir = get_msi_x_bir(cap);
        pcie_dev->msi_x.table_offset = get_msi_x_offset(cap);
        pcie_dev->msi_x.pba_bir = get_pda_bir(cap);
        pcie_dev->msi_x.pba_offset = get_pda_offset(cap);
    }else {
        //启用msi中断
        pcie_dev->msi_x_flags = 0;
        cap = pcie_cap_find(pcie_dev,msi_e);
        pcie_dev->msi = &cap->msi;
    }
}

//匹配驱动id
pcie_id_t *pcie_match_id(pcie_id_t* id_table,pcie_dev_t *pcie_dev) {
    for (;(id_table->vendor&&id_table->device)||id_table->class_code;id_table++) {
        if ((id_table->vendor==pcie_dev->vendor&&id_table->device==pcie_dev->device) || id_table->class_code==pcie_dev->class_code)
            return id_table;
    }
    return NULL;
}

//pcie设备驱动匹配
int pcie_bus_match(device_t *dev,driver_t *drv) {
    pcie_dev_t *pcie_dev = CONTAINER_OF(dev,pcie_dev_t,dev);
    pcie_drv_t *pcie_drv = CONTAINER_OF(drv,pcie_drv_t,drv);
    pcie_id_t *id = pcie_match_id(pcie_drv->id_table,pcie_dev);
    return id ? 1 : 0;
}

//pcie设备通用初始化程序
int pcie_bus_probe(device_t *dev) {
    pcie_dev_t *pcie_dev = CONTAINER_OF(dev,pcie_dev_t,dev);
    // 1) enable memory/io/bus mastering
    pcie_dev->pcie_config_space->command = PCIE_ENABLE_MEM_SPACE|PCIE_ENABLE_BUS_MASTER|PCIE_DISABLE_INTER;
    // 2) 读取/分配 BAR（你 OS 里也许固定映射或自己分配）
    pcie_bar_init(pcie_dev);
    // 3) 初始化msi/msi-x
    pcie_msi_intrpt_init(pcie_dev);
    return 0;
}

//pcie设备通用卸载程序
void pcie_bus_remove(device_t *dev) {
}

//pcie设备加载驱动外壳
int pcie_drv_probe_wrapper(device_t *dev) {
    pcie_dev_t *pcie_dev = CONTAINER_OF(dev,pcie_dev_t,dev);
    pcie_drv_t *pcie_drv = CONTAINER_OF(dev->drv,pcie_drv_t,drv);
    pcie_id_t *id = pcie_match_id(pcie_drv->id_table,pcie_dev);
    pcie_drv->probe(pcie_dev,id); //加载pcie设备专属驱动初
    return 0;
}

//pcie设备卸载驱动外壳
void pcie_drv_remove_wrapper(device_t *dev) {
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

extern bus_type_t pcie_bus;
//创建pcie设备
static inline pcie_dev_t *pcie_dev_create(pcie_root_complex_t *pcie_rc,pcie_config_space_t *pcie_config_space, uint8 bus, uint8 dev, uint8 func) {
    pcie_dev_t *pcie_dev = kzalloc(sizeof(pcie_dev_t));
    pcie_dev->bus_num = bus;
    pcie_dev->dev_num = dev;
    pcie_dev->func_num = func;
    pcie_dev->pcie_config_space = pcie_config_space;
    pcie_dev->class_code = pcie_get_classcode(pcie_dev);
    pcie_dev->vendor = pcie_dev->pcie_config_space->vendor_id;
    pcie_dev->device = pcie_dev->pcie_config_space->device_id;
    pcie_dev->rc = pcie_rc;
    list_add_head(&pcie_rc->rc_list,&pcie_dev->rc_node);
    return pcie_dev;
}

//pcie设备注册
static inline void pcie_dev_register(pcie_dev_t *pcie_dev,device_t *parent) {
    pcie_dev->dev.name = pcie_clasename_find(pcie_dev);
    pcie_dev->dev.bus = &pcie_bus;
    pcie_dev->dev.parent = parent;
    pcie_dev->dev.type = pcie_dev_e;
    device_register(&pcie_dev->dev);
}

//pcie设备类驱动注册
void pcie_drv_register(pcie_drv_t *pcie_drv) {
    pcie_drv->drv.bus = &pcie_bus;
    pcie_drv->drv.probe = pcie_drv_probe_wrapper;
    pcie_drv->drv.remove = pcie_drv_remove_wrapper;
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
static inline void pcie_scan_dev(pcie_root_complex_t *pcie_rc,uint8 bus,device_t* parent) {
    // 遍历当前总线上的32个设备(0-31)
    for (uint8 dev = 0; dev < 32; dev++) {
        // 遍历设备上的8个功能(0-7)
        for (uint8 func = 0; func < 8; func++) {
            // 将BDF(总线-设备-功能)转换为配置空间地址
            pcie_config_space_t *pcie_config_space = ecam_bdf_to_pcie_config_space_addr(pcie_rc->ecam_vir_base, bus, dev, func);
            // 检查设备是否存在 (无效设备的vendor_id=0xFFFF)
            if (pcie_config_space->vendor_id == 0xFFFF) {
                // 功能0不存在意味着整个设备不存在， 跳过该设备的后续功能
                if (func == 0) break;
                // 继续检查下一个功能
                continue;
            }
            //pcie_dev注册
            pcie_dev_t *pcie_dev = pcie_dev_create(pcie_rc,pcie_config_space, bus, dev, func);
            pcie_dev_register(pcie_dev,parent);
            //type1 为pcie桥优先扫描下游设备（深度优先）
            if (pcie_config_space->header_type & 1) pcie_scan_dev(pcie_rc, pcie_config_space->type1.secondary_bus,&pcie_dev->dev);
            //如果功能0不是多功能设备，则跳过该设备的后续功能
            if (!func && !(pcie_config_space->header_type & 0x80)) break;
        }
    }
}

//pcie总线初始化
INIT_TEXT void pcie_bus_init(void) {
    //查找mcfg表
    mcfg_t *mcfg = acpi_get_table('GFCM');
    uint32 ecma_count = (mcfg->acpi_header.length - sizeof(acpi_header_t) - sizeof(mcfg->reserved)) / sizeof(
                            mcfg_entry_t);
    //创建pcie根复合体
    pcie_root_complex_t *pcie_rc = kzalloc(sizeof(pcie_root_complex_t)*ecma_count);

    //扫描pcie总线，把pcie设备挂在到系统总线
    for (uint32 i = 0; i < ecma_count; i++) {
        pcie_rc[i].num = i;
        pcie_rc[i].ecam_phy_base = mcfg[i].entry->base_address;
        pcie_rc[i].pcie_segment = mcfg[i].entry->pci_segment;
        pcie_rc[i].start_bus = mcfg[i].entry->start_bus;
        pcie_rc[i].end_bus = mcfg[i].entry->end_bus;
        uint64 ecma_size = (pcie_rc[i].end_bus - pcie_rc[i].start_bus + 1)*32*8*4096;
        pcie_rc[i].ecam_vir_base = iomap(pcie_rc[i].ecam_phy_base,ecma_size,PAGE_4K_SIZE,PAGE_ROOT_RW_UC_4K);
        pcie_rc[i].dev.name = "pcie-root";
        pcie_rc[i].dev.type = pcie_rc_e;
        pcie_rc[i].dev.parent = NULL;
        list_head_init(&pcie_rc[i].rc_list);
        list_head_init(&pcie_rc[i].dev.child_node);
        list_head_init(&pcie_rc[i].dev.child_list);

        color_printk(GREEN,BLACK, "ECAM Pbase:%#lx -> Vbase:%#lx Segment:%d StartBus:%d EndBus:%d\n",
                     pcie_rc[i].ecam_phy_base,pcie_rc[i].ecam_vir_base, pcie_rc[i].pcie_segment, pcie_rc[i].start_bus,pcie_rc[i].end_bus);
        pcie_scan_dev(&pcie_rc[i], pcie_rc[i].start_bus,&pcie_rc[i].dev);
    }

    //打印pcie设备
    for (list_head_t *next = pcie_bus.dev_list.next;next != &pcie_bus.dev_list;next = next->next){
        device_t *dev = CONTAINER_OF(next, device_t,bus_node );
        pcie_dev_t *pcie_dev = CONTAINER_OF(dev,pcie_dev_t,dev);
        color_printk(GREEN,BLACK, "bus:%d dev:%d func:%d vendor_id:%#lx device_id:%#lx class_code:%#lx %s\n",
                     pcie_dev->bus_num, pcie_dev->dev_num, pcie_dev->func_num, pcie_dev->pcie_config_space->vendor_id,
                     pcie_dev->pcie_config_space->device_id, pcie_dev->class_code, dev->name);
    }

    //注册驱动程序
    pcie_drv_t *xhci_drv_init(void);
    //pcie_drv_register(xhci_drv_init());


}
