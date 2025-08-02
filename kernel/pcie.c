#include "pcie.h"
#include "acpi.h"
#include "printk.h"
#include "slub.h"
#include "vmalloc.h"
#include "vmm.h"

struct {
    UINT32 class_code;
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

//pcie设备全局链表
list_head_t pcie_dev_list;

//查找pcie的类名
static inline char *find_pcie_clasename(pcie_dev_t *pcie_dev) {
    UINT32 class_code = get_pcie_classcode(pcie_dev);
    for (int i = 0; pcie_classnames[i].name != NULL; i++) {
        if (class_code == pcie_classnames[i].class_code) return pcie_classnames[i].name;
    }
}

/*
 * 创建pcie_dev结构
 */
static inline void create_pcie_dev(pcie_config_space_t *pcie_config_space, UINT8 bus, UINT8 dev, UINT8 func) {
    pcie_dev_t *pcie_dev = kzalloc(sizeof(pcie_dev_t));
    pcie_dev->bus = bus;
    pcie_dev->dev = dev;
    pcie_dev->func = func;
    pcie_dev->pcie_config_space = iomap(pcie_config_space,PAGE_4K_SIZE,PAGE_4K_SIZE,PAGE_ROOT_RW_UC_4K);
    pcie_dev->name = find_pcie_clasename(pcie_dev);
    list_add_head(&pcie_dev_list, &pcie_dev->list);
}

/*
 * pcie配置空间地址计算
 */
static inline pcie_config_space_t *ecam_bdf_to_pcie_config_space_addr(UINT64 ecam_base, UINT8 bus, UINT8 dev,
                                                                      UINT8 func) {
    return (pcie_config_space_t *) (ecam_base + (bus << 20) + (dev << 15) + (func << 12));
}

/*
 * pcie总线扫描
 */
static inline void pcie_scan(UINT64 ecam_base, UINT8 bus) {
    // 遍历当前总线上的32个设备(0-31)
    for (UINT8 dev = 0; dev < 32; dev++) {
        // 遍历设备上的8个功能(0-7)
        for (UINT8 func = 0; func < 8; func++) {
            // 将BDF(总线-设备-功能)转换为配置空间地址
            pcie_config_space_t *pcie_config_space = ecam_bdf_to_pcie_config_space_addr(ecam_base, bus, dev, func);
            // 检查设备是否存在 (无效设备的vendor_id=0xFFFF)
            if (pcie_config_space->vendor_id == 0xFFFF) {
                // 功能0不存在意味着整个设备不存在， 跳过该设备的后续功能
                if (func == 0) break;
                // 继续检查下一个功能
                continue;
            }
            //创建pcie_dev
            create_pcie_dev(pcie_config_space, bus, dev, func);
            //type1 为pcie桥优先扫描下游设备（深度优先）
            if (pcie_config_space->header_type & 1) pcie_scan(ecam_base, pcie_config_space->type1.secondary_bus);
            //如果功能0不是多功能设备，则跳过该设备的后续功能
            if ((pcie_config_space->header_type & 0x80) == 0 && func == 0) break;

        }
    }
}

/*
 * pcie设备搜索
 * 参数 class_code
 * 返回一个pcie_dev_t指针
 */
list_head_t *next_pcie_dev = &pcie_dev_list;
pcie_dev_t *find_pcie_dev(UINT32 class_code) {
    if (next_pcie_dev == &pcie_dev_list) next_pcie_dev = pcie_dev_list.next;
    while (next_pcie_dev != &pcie_dev_list) {
        pcie_dev_t *pcie_dev = CONTAINER_OF(next_pcie_dev,pcie_dev_t,list);
        if (get_pcie_classcode(pcie_dev) == class_code) return pcie_dev;
        next_pcie_dev = next_pcie_dev->next;
    }
    return NULL;
}

//搜索能力链表
//参数capability_id
//返回一个capability_t* 指针
cap_t *find_pcie_cap(pcie_dev_t *pcie_dev, cap_id_e cap_id) {
    //检测是否支持能力链表,不支持返回空指针
    if (!(pcie_dev->pcie_config_space->status & 0x10)) return NULL;
    //计算能力链表起始地址
    UINT8 next_ptr = pcie_dev->pcie_config_space->type0.cap_ptr;
    while (next_ptr){
        cap_t *cap = (cap_t*)((UINT64)pcie_dev->pcie_config_space + next_ptr);
        if (cap->cap_id == cap_id) return cap;
        next_ptr = cap->next_ptr;
    }
    return NULL;
}

//判断bar 64位或32位
//0等于32位，4等于32位
static inline UINT64 is_bar_bit(UINT64 bar_data) {
    return bar_data & 0x6;
}


//配置bar寄存器
//参数bar寄存器号
//返回bar虚拟地址
void *set_bar(pcie_dev_t *pcie_dev,UINT8 number) {
    if (number > 5) return 0;
    UINT64 *bar = &pcie_dev->pcie_config_space->type0.bar[number];
    UINT64 addr = *bar;
    *bar = 0xFFFFFFFFFFFFFFFFUL;
    UINT64 size = *bar;
    *bar = addr;
    if (is_bar_bit(addr)) {
        addr &= 0xFFFFFFFFFFFFFFF0UL;
        size &= 0xFFFFFFFFFFFFFFF0UL;
    }else {
        addr &= 0xFFFFFFF0UL;
        size &= 0xFFFFFFF0UL;
        size |= 0xFFFFFFFF00000000UL;
    }
    size = -size;
    return iomap(addr,size,PAGE_4K_SIZE,PAGE_ROOT_RW_UC_4K);
}

//获取msi_x控制寄存器
UINT16 *get_msi_x_control(pcie_dev_t *pcie_dev) {
    cap_t *cap= find_pcie_cap(pcie_dev,msi_x_e);
    if (!cap) return NULL;
    return &cap->msi_x.control;
}

//启用msi-x中断
void enable_msi_x(pcie_dev_t *pcie_dev) {
    cap_t *cap= find_pcie_cap(pcie_dev,msi_x_e);
    cap->msi_x.control |= 0x8000;
    cap->msi_x.control &= ~0x4000;
}

//禁用msi-x中断
void disable_msi_x(pcie_dev_t *pcie_dev) {
    cap_t *cap= find_pcie_cap(pcie_dev,msi_x_e);
    cap->msi_x.control |= 0x4000;
}

//获取msi-x终端数量
//参数pcie_config_space_t
//数量
UINT32 get_msi_x_irq_number(pcie_dev_t *pcie_dev) {
    cap_t *cap= find_pcie_cap(pcie_dev,msi_x_e);
    if (!cap) return 0;
    return (cap->msi_x.control & 0x7FF)+1;
}

/*
 *获取msi-x表bar号
 *参数pcie_config_space_t
 *数量
 */
static inline UINT8 get_msi_x_bar_number(pcie_dev_t *pcie_dev) {
    UINT32 table_offset = *(UINT32*)((UINT64)pcie_dev->msi_x_control + sizeof(UINT16));
    return table_offset & 0x7;
}

/*
 * 获取msi-x表相对bar偏移量
 */
static inline UINT32 get_msi_x_offset(pcie_dev_t *pcie_dev) {
    UINT32 table_offset = *(UINT32*)((UINT64)pcie_dev->msi_x_control + sizeof(UINT16));
    return table_offset & ~0x7;
}

//获取msi-x中断表地址
//参数1 pcie_config_space_t
//返回msi_x_t结构地址
msi_x_table_entry_t *get_msi_x_table(pcie_dev_t *pcie_dev) {
    UINT32 msi_x_bar_number = get_msi_x_bar_number(pcie_dev);
    return (msi_x_table_entry_t*)(pcie_dev->bar[msi_x_bar_number] + get_msi_x_offset(pcie_dev));
}

/*
 *获取pda表bar号
 *参数pcie_config_space_t
 *数量
 */
static inline UINT8 get_pda_bar_number(pcie_dev_t *pcie_dev) {
    UINT32 table_offset = *(UINT32*)((UINT64)pcie_dev->msi_x_control + sizeof(UINT16) + sizeof(UINT32));
    return table_offset & 0x7;
}

/*
 * 获取pda表相对bar偏移量
 */
static inline UINT32 get_pda_offset(pcie_dev_t *pcie_dev) {
    UINT32 table_offset = *(UINT32*)((UINT64)pcie_dev->msi_x_control + sizeof(UINT16)+ sizeof(UINT32));
    return table_offset & ~0x7;
}

//获取pda中断表地址
//参数1 pcie_config_space_t
//返回msi_x_t结构地址
UINT64 *get_pda_table(pcie_dev_t *pcie_dev) {
    UINT32 pda_bar_number = get_pda_bar_number(pcie_dev);
    return (UINT64*)(pcie_dev->bar[pda_bar_number] + get_pda_offset(pcie_dev));
}


INIT_TEXT void init_pcie(void) {
    //初始化pcie设备链表
    list_head_init(&pcie_dev_list);
    //查找mcfg表
    mcfg_t *mcfg = acpi_get_table('GFCM');
    mcfg_entry_t *mcfg_entry = &mcfg->entry;
    UINT32 mcfg_count = (mcfg->acpi_header.length - sizeof(acpi_header_t) - sizeof(mcfg->reserved)) / sizeof(
                            mcfg_entry_t);
    //遍历pcie设备
    for (UINT32 i = 0; i < mcfg_count; i++) {
        color_printk(GREEN,BLACK, "ECAM base:%#lX Segment:%d StartBus:%d EndBus:%d\n",
                     mcfg_entry[i].base_address, mcfg_entry[i].pci_segment, mcfg_entry[i].start_bus,
                     mcfg_entry[i].end_bus);
        pcie_scan(mcfg_entry[i].base_address, mcfg_entry[i].start_bus);
    }
    //打印pcie设备
    list_head_t *next = pcie_dev_list.next;
    while (next != &pcie_dev_list) {
        pcie_dev_t *pcie_dev = CONTAINER_OF(next, pcie_dev_t, list);
        color_printk(GREEN,BLACK, "bus:%d dev:%d func:%d vendor_id:%#lx device_id:%#lx class_code:%#lx %s\n",
                     pcie_dev->bus, pcie_dev->dev, pcie_dev->func, pcie_dev->pcie_config_space->vendor_id,
                     pcie_dev->pcie_config_space->device_id, get_pcie_classcode(pcie_dev), pcie_dev->name);
        next = next->next;
    }
}
