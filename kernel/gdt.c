#include "gdt.h"
#include "memory.h"
#include "cpu.h"

__attribute__((section(".init_text"))) void init_gdt(UINT8 bsp_flags) {
    if (bsp_flags) {
        //gdt-limit限长=cpu核心数量*tss选择子字节数（tss选择子16字节每个）+ tss描述符起始索引号*8字节（tss起始描述符前是其他系统段描述符），limt对齐4K界限-1
        gdt_ptr.limit = PAGE_4K_ALIGN(cpu_info.cores_number * 16 + TSS_DESCRIPTOR_START_INDEX* 8) - 1;
        //alloc_pages分配的是物理页起始地址，gdt-base是虚拟地址需要通过LADDR_TO_HADDR宏把地址转换
        gdt_ptr.base = LADDR_TO_HADDR(alloc_pages((gdt_ptr.limit + 1) >> PAGE_4K_SHIFT));
        memory_management.kernel_end_address = (UINT64)gdt_ptr.base;

        *(gdt_ptr.base + 0) = 0;               /*0	NULL descriptor		       	00*/
        *(gdt_ptr.base + 1) = CODE64_0;        /*1	KERNEL	Code	64-bit	Segment	08*/
        *(gdt_ptr.base + 2) = DATA64_0;        /*2	KERNEL	Data	64-bit	Segment	10*/
        *(gdt_ptr.base + 3) = CODE64_3;        /*3	USER	Code	64-bit	Segment	18*/
        *(gdt_ptr.base + 4) = DATA64_3;        /*4	USER	Data	64-bit	Segment	20*/
        *(gdt_ptr.base + 5) = CODE32_0;        /*5	KERNEL	Code	32-bit	Segment	28*/
        *(gdt_ptr.base + 6) = DATA32_0;        /*6	KERNEL	Data	32-bit	Segment	30*/
        *(gdt_ptr.base + 7) = 0;
    }

    __asm__ __volatile__(
            "lgdt       (%0)        \n\t"
            "pushq      $0x8        \n\t"       //0x8 64位ring0 代码选择子
            "movabs     $b1,%%rax   \n\t"
            "pushq      %%rax       \n\t"
            "lretq                  \n\t"       //切换新gdt代码选择子
//            ".byte      0x48        \n\t"
//            "ljmp       (%%rsp)     \n\t"
//            "add        $16,%%rsp   \n\t"     //jmp远跳转exsi执行报错
            "b1:                    \n\t"
            "mov        %1,%%ss     \n\t"
            "mov        %1,%%ds     \n\t"
            "mov        %1,%%es     \n\t"
//            "movw       %%ax,%%fs   \n\t"
//            "movw       %%ax,%%gs   \n\t"
            ::"r"(&gdt_ptr),"r"(0x10):"%rax");   //0x10 64位ring0 数据段选择子
    return;
}