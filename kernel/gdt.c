#include "gdt.h"
#include "tss.h"
#include "vmm.h"
#include "cpu.h"
#include "slub.h"

INIT_DATA gdt_ptr_t gdt_ptr;

INIT_TEXT void init_gdt(void) {
    //gdt-limit限长=cpu核心数量*tss选择子字节数（tss选择子16字节每个）+ tss描述符起始索引号*16字节（tss起始描述符前是其他系统段描述符），limt对齐4K界限-1
    gdt_ptr.limit = cpu_info.logical_processors_number * sizeof(tss_t) + TSS_DESCRIPTOR_START_INDEX*16 - 1;
    gdt_ptr.base = kzalloc(gdt_ptr.limit+1);

    *(gdt_ptr.base + 0) = 0;               /*0	NULL descriptor		           	0x00*/
    *(gdt_ptr.base + 1) = CODE64_0;        /*1	KERNEL	Code	64-bit	Segment	0x08*/
    *(gdt_ptr.base + 2) = DATA64_0;        /*2	KERNEL	Data	64-bit	Segment	0x10*/
    *(gdt_ptr.base + 3) = CODE32_3;        /*3	USER	Code	32-bit	Segment	0x18*/
    *(gdt_ptr.base + 4) = DATA32_3;        /*4	USER	Data	32-bit	Segment	0x20*/
    *(gdt_ptr.base + 5) = CODE64_3;        /*5	USER	Code	64-bit	Segment	0x28*/
    //*(gdt_ptr.base + 6) = DATA64_3;        /*6	USER	Data	64-bit	Segment	0x30*/

    lgdt(&gdt_ptr,0x8,0x10);     //加载新gdt
}
