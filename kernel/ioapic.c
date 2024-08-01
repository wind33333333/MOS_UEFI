#include "ioapic.h"

//初始化ioapic
__attribute__((section(".init_text"))) void ioapic_init(unsigned char bsp_flags) {
    /*初始化ioapic
     * 索引寄存器0xFEC00000 32bit bit0-7
     * 数据寄存器0xFEC00010 32bit
     * EOI寄存器0xFEC00040 32bit bit0-7
     * 索引0 ioapic id寄存器 读写 32bit bit24-27
     * 索引1 ioapic版本寄存器 读 32bit bit0-7apic版本 bit16-23 +1可用rte寄存器数
     * 索引0x10-0x11 中断投递寄存器0 读写 0x10低32bit 0x11高32bit bit0-7中断号 bit16中断屏蔽位 bit56-63 local apic id
     * ...
     * 索引0x3E-0x3F 中断投递寄存器23 读写
    */

    if(bsp_flags) {
        __asm__ __volatile__ (
                "mov    $0xFF,%%al \n\t"
                "out    %%al,$0x21 \n\t"
                "out    %%al,$0xA1 \n\t"               //禁用主从8259A

                "mov    $0x30,%%al  \n\t"
                "out    %%al,$0x43  \n\t"
                "mov    $0,%%al     \n\t"
                "out    %%al,$0x40  \n\t"            //禁用8054计时器0
                "out    %%al,$0x40  \n\t"

                "mov    $0x70,%%al  \n\t"
                "out    %%al,$0x43  \n\t"
                "mov    $0,%%al     \n\t"
                "out    %%al,$0x41  \n\t"            //禁用8054计时器1
                "out    %%al,$0x41  \n\t"

                "mov    $0xB0,%%al \n\t"
                "out    %%al,$0x43 \n\t"
                "mov    $0,%%al    \n\t"
                "out    %%al,$0x42 \n\t"            //禁用8054计时器2
                "out    %%al,$0x42 \n\t"
                :::"%rax");

        __asm__ __volatile__(
                /*
                "movl   $0x0,(%%rdi)         \n\t"
                "mfence                      \n\t"
                "movl    $0x1000000,(%%rsi)  \n\t"
                "mfence                      \n\t"
                "mov    (%%rsi),%%eax        \n\t"           //设置ioapic id
                "mfence                      \n\t"
                */

                "mov    $0x10030,%%rax       \n\t"
                "movl   $0x10,(%%rdi)        \n\t"
                "mfence                      \n\t"
                "mov     %%eax,(%%rsi)       \n\t"
                "mfence                      \n\t"
                "shr    $32,%%rax            \n\t"
                "movl   $0x11,(%%rdi)        \n\t"
                "mfence                      \n\t"
                "mov    %%eax,(%%rsi)        \n\t"           //主8259A中断
                "mfence                      \n\t"

                "mov    $0x00031,%%rax       \n\t"
                "movl   $0x12,(%%rdi)        \n\t"
                "mfence                      \n\t"
                "mov     %%eax,(%%rsi)       \n\t"
                "mfence                      \n\t"
                "shr    $32,%%rax            \n\t"
                "movl   $0x13,(%%rdi)        \n\t"
                "mfence                      \n\t"
                "mov    %%eax,(%%rsi)        \n\t"          //ps2键盘中断
                "mfence                      \n\t"

                "mov     $0x00032,%%rax      \n\t"
                "movl    $0x14,(%%rdi)       \n\t"
                "mfence                      \n\t"
                "mov     %%eax,(%%rsi)       \n\t"
                "mfence                      \n\t"
                "shr     $32,%%rax           \n\t"
                "movl    $0x15,(%%rdi)       \n\t"
                "mfence                      \n\t"
                "mov     %%eax,(%%rsi)       \n\t"          //8254定时器0/HPTE定时器0
                "mfence                      \n\t"

                "mov     $0x10033,%%rax      \n\t"
                "movl    $0x16,(%%rdi)       \n\t"
                "mfence                      \n\t"
                "mov     %%eax,(%%rsi)       \n\t"
                "mfence                      \n\t"
                "shr     $32,%%rax           \n\t"
                "movl    $0x17,(%%rdi)       \n\t"
                "mfence                      \n\t"
                "mov     %%eax,(%%rsi)       \n\t"          //串口2中断
                "mfence                      \n\t"

                "mov     $0x10034,%%rax      \n\t"
                "movl    $0x18,(%%rdi)       \n\t"
                "mfence                      \n\t"
                "mov     %%eax,(%%rsi)       \n\t"
                "mfence                      \n\t"
                "shr     $32,%%rax           \n\t"
                "movl    $0x19,(%%rdi)       \n\t"
                "mfence                      \n\t"
                "mov     %%eax,(%%rsi)       \n\t"        //串口1中断
                "mfence                      \n\t"

                "mov     $0x10035,%%rax      \n\t"
                "movl    $0x1A,(%%rdi)       \n\t"
                "mfence                      \n\t"
                "mov     %%eax,(%%rsi)       \n\t"
                "mfence                      \n\t"
                "shr     $32,%%rax           \n\t"
                "movl    $0x1B,(%%rdi)       \n\t"
                "mfence                      \n\t"
                "mov     %%eax,(%%rsi)       \n\t"       //并口2中断
                "mfence                      \n\t"

                "mov     $0x10036,%%rax      \n\t"
                "movl    $0x1C,(%%rdi)       \n\t"
                "mfence                      \n\t"
                "mov %%eax,(%%rsi)           \n\t"
                "mfence                      \n\t"
                "shr     $32,%%rax           \n\t"
                "movl    $0x1D,(%%rdi)       \n\t"
                "mfence                      \n\t"
                "mov     %%eax,(%%rsi)       \n\t"          //软驱中断
                "mfence                      \n\t"

                "mov     $0x10037,%%rax      \n\t"
                "movl    $0x1E,(%%rdi)       \n\t"
                "mfence                      \n\t"
                "mov     %%eax,(%%rsi)       \n\t"
                "mfence                      \n\t"
                "shr     $32,%%rax           \n\t"
                "movl    $0x1F,(%%rdi)       \n\t"
                "mfence                      \n\t"
                "mov %%eax,(%%rsi)           \n\t"      //并口1中断
                "mfence                      \n\t"

                "mov     $0x10038,%%rax      \n\t"
                "movl    $0x20,(%%rdi)       \n\t"
                "mfence                      \n\t"
                "mov     %%eax,(%%rsi)       \n\t"
                "mfence                      \n\t"
                "shr     $32,%%rax           \n\t"
                "movl    $0x21,(%%rdi)       \n\t"
                "mfence                      \n\t"
                "mov     %%eax,(%%rsi)       \n\t"   //CMOS RTC中断/HPTE定时器1
                "mfence                      \n\t"

                "mov     $0x10039,%%rax      \n\t"
                "movl    $0x26,(%%rdi)       \n\t"
                "mfence                      \n\t"
                "mov     %%eax,(%%rsi)       \n\t"
                "mfence                      \n\t"
                "shr     $32,%%rax           \n\t"
                "movl    $0x27,(%%rdi)       \n\t"
                "mfence                      \n\t"
                "mov     %%eax,(%%rsi)       \n\t"      //HPTE 定时器2
                "mfence                      \n\t"

                "mov     $0x1003A,%%rax      \n\t"
                "movl    $0x28,(%%rdi)       \n\t"
                "mfence                      \n\t"
                "mov     %%eax,(%%rsi)       \n\t"
                "mfence                      \n\t"
                "shr     $32,%%rax           \n\t"
                "movl    $0x29,(%%rdi)       \n\t"
                "mfence                      \n\t"
                "mov     %%eax,(%%rsi)       \n\t"  //ps2鼠标 /HPET定时器3
                "mfence                      \n\t"

                "mov     $0x1003B,%%rax      \n\t"
                "movl    $0x2A,(%%rdi)       \n\t"
                "mfence                      \n\t"
                "mov     %%eax,(%%rsi)       \n\t"
                "mfence                      \n\t"
                "shr     $32,%%rax           \n\t"
                "movl    $0x2B,(%%rdi)       \n\t"
                "mfence                      \n\t"
                "mov     %%eax,(%%rsi)       \n\t"  //FERR/DMA
                "mfence                      \n\t"

                "mov     $0x1003C,%%rax      \n\t"
                "movl    $0x2C,(%%rdi)       \n\t"
                "mfence                      \n\t"
                "mov     %%eax,(%%rsi)       \n\t"
                "mfence                      \n\t"
                "shr     $32,%%rax           \n\t"
                "movl    $0x2D,(%%rdi)       \n\t"
                "mfence                      \n\t"
                "mov     %%eax,(%%rsi)       \n\t"  //主SATA中断
                "mfence                      \n\t"

                "mov     $0x1003D,%%rax      \n\t"
                "movl    $0x2E,(%%rdi)       \n\t"
                "mfence                      \n\t"
                "mov     %%eax,(%%rsi)       \n\t"
                "mfence                      \n\t"
                "shr     $32,%%rax           \n\t"
                "movl    $0x2F,(%%rdi)       \n\t"
                "mfence                      \n\t"
                "mov     %%eax,(%%rsi)       \n\t" //从SATA中断
                "mfence                      \n\t"
                ::"D"(ioapic_baseaddr),"S"((unsigned long)ioapic_baseaddr+0x10):"%rax", "%rcx", "%rdx");
    }
    return;
}
