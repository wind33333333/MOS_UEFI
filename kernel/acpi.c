#include "acpi.h"
#include "uefi.h"

/*
 * 查找acpi表
 * 参数用法 talbe = 'TEPH' //hpet表
 * 返回acpi表的指针
 */
INIT_TEXT void *acpi_get_table(UINT32 table) {
    acpi_header_t **acpi_table = &boot_info->rsdp->xsdt_address->entry;
    UINT32 acpi_count = (boot_info->rsdp->xsdt_address->acpi_header.length - sizeof(acpi_header_t)) / sizeof(UINT32 *);
    for (UINT32 i = 0; i < acpi_count; i++) {
        if (acpi_table[i]->signature == table) return acpi_table[i];
    }
    return NULL;
}


