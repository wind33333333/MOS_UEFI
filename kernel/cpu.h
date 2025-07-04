#ifndef _CPU_H
#define _CPU_H
#include "moslib.h"

#define IA32_PAT_MSR        0x277        //设置页属性PAT类型
#define IA32_EFER_MSR       0xC0000080   // 扩展功能寄存器（Extended Feature Enable Register）
#define IA32_STAR_MSR       0xC0000081   // 系统调用目标寄存器（Segment Target Address Register）
#define IA32_LSTAR_MSR      0xC0000082   // 64位系统调用入口寄存器（Long Mode System Call Target Address Register）
#define IA32_CSTAR_MSR      0xC0000083   // 兼容模式系统调用入口寄存器（Compatibility Mode System Call Target Address Register）
#define IA32_FMASK_MSR      0xC0000084   // 系统调用掩码寄存器（System Call Flag Mask Register）

UINT32 apicid_to_cpuid(UINT32 apic_id);
UINT32 cpuid_to_apicid(UINT32 cpu_id);
void init_bsp(void);
void enable_cpu_amode(void);

typedef struct {
    CHAR8 manufacturer_name[13];
    CHAR8 model_name[49];
    UINT32 fundamental_frequency;
    UINT32 maximum_frequency;
    UINT32 logical_processors_number;
    UINT32 bus_frequency;
    UINT32 tsc_frequency;
}cpu_info_t;

extern cpu_info_t cpu_info;
extern UINT32 *apic_id_table;

void init_ap(void);
void ap_main(void);

typedef struct {
    UINT64 rsp;
    UINT16 tr;
}__attribute__((packed)) apboot_data_t;

extern UINT8 _apboot_start[];
extern UINT8 _apboot_end[];

extern UINT64 ap_rsp_ptr;
extern void *ap_main_ptr;
extern UINT64* ap_tmp_pml4t_ptr;
extern UINT32 *apic_id_table_ptr;
extern UINT64 ap_boot_loader_address;


#endif