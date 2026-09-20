#include "acpi_init.h"
#include "slub.h"
#include "uefi_init.h""


/*
 * 查找acpi表
 * 参数用法 talbe = 宏名 ，indx = 第几个
 * 返回acpi表的指针
 */
void *acpi_get_table(uint32 signature, uint32 index) {
    rsdp_t *rsdp = pa_to_va((uint64)tmp_boot_info->rsdp);
    xsdt_t *xsdt = pa_to_va((uint64)rsdp->xsdt_address);
    uint32 acpi_count = (xsdt->acpi_header.length - sizeof(acpi_header_t)) /8;
    uint32 match_count = 0;

    for (uint32 i = 0; i < acpi_count; i++) {
        uint64 table_phys = (uint64)xsdt->apci_table[i];
        if (table_phys == 0) continue; // 防御性跳过空槽位
        acpi_header_t *acpi_table = (acpi_header_t *)pa_to_va(table_phys);
        if (acpi_table->signature == signature) {
            if (match_count == index) {
                return acpi_table;
            }
            match_count++;
        }
    }
    return NULL;
}

