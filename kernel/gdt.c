#include "gdt.h"
#include "memory.h"
#include "cpu.h"

__attribute__((section(".init_data"))) gdt_ptr_t gdt_ptr;

__attribute__((section(".init_text"))) void init_gdt(void) {
    //gdt-limit限长=cpu核心数量*tss选择子字节数（tss选择子16字节每个）+ tss描述符起始索引号*8字节（tss起始描述符前是其他系统段描述符），limt对齐4K界限-1
    gdt_ptr.limit = PAGE_4K_ALIGN(cpu_info.logical_processors_number * 16 + TSS_DESCRIPTOR_START_INDEX* 8) - 1;
    //alloc_pages分配的是物理页起始地址，gdt-base是虚拟地址需要通过LADDR_TO_HADDR宏把地址转换
    gdt_ptr.base = LADDR_TO_HADDR(alloc_pages((gdt_ptr.limit + 1) >> PAGE_4K_SHIFT));
    map_pages(HADDR_TO_LADDR((UINT64)gdt_ptr.base),(UINT64)gdt_ptr.base,(gdt_ptr.limit + 1) >> PAGE_4K_SHIFT,PAGE_ROOT_RW);
    mem_set((void*)gdt_ptr.base,0,PAGE_4K_ALIGN(cpu_info.logical_processors_number * 16 + TSS_DESCRIPTOR_START_INDEX* 8));

    *(gdt_ptr.base + 0) = 0;               /*0	NULL descriptor		           	0x00*/
    *(gdt_ptr.base + 1) = CODE64_0;        /*1	KERNEL	Code	64-bit	Segment	0x08*/
    *(gdt_ptr.base + 2) = DATA64_0;        /*2	KERNEL	Data	64-bit	Segment	0x10*/
    *(gdt_ptr.base + 3) = CODE32_3;        /*3	USER	Code	32-bit	Segment	0x18*/
    *(gdt_ptr.base + 4) = DATA32_3;        /*4	USER	Data	32-bit	Segment	0x20*/
    *(gdt_ptr.base + 5) = CODE64_3;        /*5	USER	Code	64-bit	Segment	0x28*/
    *(gdt_ptr.base + 6) = DATA64_3;        /*6	USER	Data	64-bit	Segment	0x30*/

    LGDT(gdt_ptr,0x8UL,0x10UL);     //加载新gdt

    return;
}
