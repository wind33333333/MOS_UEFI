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
    {0x000000, "Unclassified Device"},

    {0x010000, "SCSI Controller"},
    {0x010601, "SATA Controller (AHCI)"},
    {0x010802, "NVMe Controller"},

    {0x020000, "Ethernet Controller"},

    {0x030000, "VGA Controller"},
    {0x030100, "XGA Controller"},
    {0x030200, "3D Controller (Not VGA-Compatible)"},
    {0x038000, "Other Display Controller"},

    {0x040000, "Multimedia Video Controller"},

    {0x060000, "Host Bridge"},
    {0x060100, "ISA Bridge"},
    {0x060200, "EISA Bridge"},
    {0x060300, "MCA Bridge"},
    {0x060400, "PCI-to-PCI Bridge"},
    {0x060500, "PCMCIA Bridge"},
    {0x060600, "NuBus Bridge"},
    {0x060700, "CardBus Bridge"},
    {0x060800, "RACEway Bridge"},
    {0x060900, "PCI-to-PCI Bridge"},
    {0x060A00, "InfiniBand-to-PCI Host Bridge"},
    {0x068000, "Other Bridge"},

    {0x0C0300, "USB1.1 Controller (UHCI)"},
    {0x0C0310, "USB1.1 Controller (OHCI)"},
    {0x0C0320, "USB2.0 Controller (EHCI)"},
    {0x0C0330, "USB3.0 Controller (XHCI)"},
    {0x0C0400, "Fibre Channel"},
    {0x0C0500, "SMBus Controller"},
    {0x0C0600, "InfiniBand Controller"},
    {0x0C0700, "IPMI Interface"},
    {0x0C0800, "SERCOS Interface (IEC 61491)"},
    {0x0C0900, "CANbus Controller"},
    {0x0C8000, "Other Serial Bus Controller"},

    {0x000000, NULL}
};

//pcie设备全局链表
list_head_t pcie_dev_list;

static inline char *find_pcie_clasename(UINT32 class_code) {
    for (int i = 0; pcie_classnames[i].name != NULL; i++) {
        if (class_code == pcie_classnames[i].class_code) return pcie_classnames[i].name;
    }
}

static inline UINT32 get_pcie_classcode(pcie_config_space_t *pcie_config_space) {
    UINT32 *class_code = &pcie_config_space->class_code;
    return *class_code & 0xFFFFFF;
}

/*
 * 创建pcie_dev结构
 */
static inline void create_pcie_dev(pcie_config_space_t *pcie_config_space, UINT8 bus, UINT8 dev, UINT8 func) {
    pcie_dev_t *pcie_dev = kzalloc(sizeof(pcie_dev_t));
    pcie_dev->name = find_pcie_clasename(get_pcie_classcode(pcie_config_space));
    pcie_dev->bus = bus;
    pcie_dev->dev = dev;
    pcie_dev->func = func;
    pcie_dev->pcie_config_space = iomap(pcie_config_space,PAGE_4K_SIZE,PAGE_4K_SIZE,PAGE_ROOT_RW_UC_4K);
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
            // 检查设备是否存在 (无效设备的Vendor ID为0xFFFF)
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
        if (get_pcie_classcode(pcie_dev->pcie_config_space) == class_code) return pcie_dev;
        next_pcie_dev = next_pcie_dev->next;
    }
    return NULL;
}

//搜索能力链表
//参数capability_id
//返回一个capability_t* 指针
cap_t *find_pcie_cap(pcie_config_space_t *pcie_config_space, cap_id_e cap_id) {
    //检测是否支持能力链表
    if (!(pcie_config_space->status & 0x10)) return NULL;
    cap_t *cap = (cap_t*)((UINT64)pcie_config_space + pcie_config_space->type0.cap_ptr);
    while (TRUE){
        if (cap->cap_id == cap_id) return cap;
        if (cap->next_ptr == 0) return NULL;
        cap = (cap_t*)((UINT64)pcie_config_space + cap->next_ptr);
    }
}

//判断bar 64位或32位
//0等于32位，4等于32位
static inline UINT64 is_bar_bit(UINT64 bar_data) {
    return bar_data & 0x6;
}


//配置bar寄存器
//参数bar寄存器号
//返回bar虚拟地址
void *set_bar(pcie_config_space_t *pcie_config_space,UINT8 number) {
    if (number > 5) return 0;
    UINT64 *bar = &pcie_config_space->type0.bar[number];
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

//获取msi-x终端数量
//参数pcie_config_space_t
//数量
UINT32 get_msi_x_irq_number(pcie_config_space_t *pcie_config_space) {
    cap_t *cap= find_pcie_cap(pcie_config_space,msi_x_e);
    return (cap->msi_x.control & 0x7FF)+1;
}

/*
 *获取msi-x表bar号
 *参数pcie_config_space_t
 *数量
 */
UINT8 get_msi_x_bar_number(pcie_config_space_t *pcie_config_space) {
    cap_t *cap= find_pcie_cap(pcie_config_space,msi_x_e);
    return cap->msi_x.table_offset & 0x7;
}

/*
 * 获取msi-x表相对bar偏移量
 */
UINT32 get_msi_x_offset(pcie_config_space_t *pcie_config_space) {
    cap_t *cap= find_pcie_cap(pcie_config_space,msi_x_e);
    return cap->msi_x.table_offset & ~0x7;
}

//获取msi-x中断表地址
//参数1 pcie_config_space_t
//返回msi_x_t结构地址
msi_x_table_entry_t *get_msi_x_table(pcie_dev_t *pcie_dev) {
    UINT32 msi_x_bar_number = get_msi_x_bar_number(pcie_dev->pcie_config_space);
    return (msi_x_table_entry_t*)(pcie_dev->bar[msi_x_bar_number] + get_msi_x_offset(pcie_dev->pcie_config_space));
}

//启用msi-x中断
void enable_msi_x(pcie_config_space_t *pcie_config_space) {
    cap_t *cap= find_pcie_cap(pcie_config_space,msi_x_e);
    cap->msi_x.control |= 0x8000;
    cap->msi_x.control &= ~0x4000;
}

//禁用msi-x中断
void disable_msi_x(pcie_config_space_t *pcie_config_space) {
    cap_t *cap= find_pcie_cap(pcie_config_space,msi_x_e);
    cap->msi_x.control |= 0x4000;
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
        UINT32 *class_code = &pcie_dev->pcie_config_space->class_code;
        color_printk(GREEN,BLACK, "bus:%d dev:%d func:%d vendor_id:%#lx device_id:%#lx class_code:%#lx %s\n",
                     pcie_dev->bus, pcie_dev->dev, pcie_dev->func, pcie_dev->pcie_config_space->vendor_id,
                     pcie_dev->pcie_config_space->device_id, *class_code & 0xFFFFFF, pcie_dev->name);
        next = next->next;
    }
}
