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

static inline char *pcie_clasename(UINT32 class_code) {
    for (int i = 0; pcie_classnames[i].name != NULL; i++) {
        if (class_code == pcie_classnames[i].class_code) return pcie_classnames[i].name;
    }
}

static inline void create_pcie_dev(pcie_config_space_t *pcie_config_space, UINT8 bus, UINT8 dev, UINT8 func) {
    pcie_dev_t *pcie_dev = kmalloc(sizeof(pcie_dev_t));
    mem_set(pcie_dev, 0, sizeof(pcie_dev_t));
    UINT32 *class_code = &pcie_config_space->class_code;
    pcie_dev->name = pcie_clasename(*class_code & 0xFFFFFF);
    pcie_dev->bus = bus;
    pcie_dev->dev = dev;
    pcie_dev->func = func;
    pcie_dev->pcie_config_space = iomap(pcie_config_space,PAGE_4K_SIZE,PAGE_4K_SIZE,PAGE_ROOT_RW_UC_4K);
    list_add_head(&pcie_dev_list, &pcie_dev->list);
}

/*
 * pcie 地址计算
 */
static inline pcie_config_space_t *ecam_bdf_to_pcie_config_space_addr(UINT64 ecam_base, UINT8 bus, UINT8 dev,
                                                                      UINT8 func) {
    return (pcie_config_space_t *) (ecam_base + (bus << 20) + (dev << 15) + (func << 12));
}

/*
 * pcie 总线扫描
 */
static inline void pcie_scan(UINT64 ecam_base, UINT8 bus) {
    for (UINT8 dev = 0; dev < 32; dev++) {
        for (UINT8 func = 0; func < 8; func++) {
            pcie_config_space_t *pcie_config_space = ecam_bdf_to_pcie_config_space_addr(ecam_base, bus, dev, func);
            if (pcie_config_space->vendor_id == 0xFFFF && func == 0) break;
            if (pcie_config_space->vendor_id == 0xFFFF) continue;
            if (pcie_config_space->header_type & 1) {
                //type1 pcie桥
                create_pcie_dev(pcie_config_space, bus, dev, func);
                pcie_scan(ecam_base, pcie_config_space->type1.secondary_bus);
            } else {
                //type0 终端设备
                create_pcie_dev(pcie_config_space, bus, dev, func);
                if ((pcie_config_space->header_type & 0x80) == 0) break;
            }
        }
    }
}

INIT_TEXT void init_pcie(void) {

    UINT32 *i = acpi_get_table('SRVI');
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
