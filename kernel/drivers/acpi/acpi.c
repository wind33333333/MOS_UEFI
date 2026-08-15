#include "acpi.h"
#include "../../init/uefi.h"

/*
 * 查找acpi表
 * 参数用法 talbe = 'TEPH' //hpet表
 * 返回acpi表的指针
 */
INIT_TEXT void *acpi_get_table(uint32 table) {
    xsdt_t *xsdt = boot_info->rsdp->xsdt_address;
    uint32 acpi_count = (xsdt->acpi_header.length - sizeof(acpi_header_t)) / sizeof(uint32 *);
    for (uint32 i = 0; i < acpi_count; i++) {
        acpi_header_t *acpi_table = xsdt->table_pointers[i];
        if (acpi_table->signature == table) return acpi_table;
    }
    return NULL;
}


