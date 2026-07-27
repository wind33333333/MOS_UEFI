#include "linkage.h"

/***************************内核初始化代码入口*******************************/
.code64
.section    .init_text, "ax"
.globl _start
_start:
    cli
    movq     %rdi,boot_info(%rip)       //保存loader通过rdi传递过来的boot_info指针

/*初始化一个临时内核页表
*物理内存地址
*[0x0 - 0x8000000000](512G)对等映射到
*虚拟内存地址
*[0x0 - 0x8000000000]
*[0xFFFF800000000000 - 0xFFFF808000000000]
*[0xFFFFFF8000000000 - 0xFFFFFF8000000000]*/
    leaq     tmp_pml4t(%rip),%rdi    //初始化pml4t
    leaq     tmp_pdpt0(%rip),%rax
    orq      $0x3,%rax
    movq     %rax,(%rdi)            //物理地址虚拟地址对等映射

    movq     $DIRECT_MAP_OFFSET,%rsi
    shrq     $39,%rsi
    andq     $0x1ff,%rsi
    shlq     $3,%rsi
    movq     %rax,(%rdi,%rsi)       //直接映射区pml4e

    leaq     tmp_pdpt1(%rip),%rax
    orq      $0x3,%rax
    movq     $KERNEL_OFFSET,%rsi
    shrq     $39,%rsi
    andq     $0x1ff,%rsi
    shlq     $3,%rsi
    movq     %rax,(%rdi,%rsi)       //内核映射区pml4e

    leaq     tmp_pdpt0(%rip),%rdi    //初始化直接映射区pdpt
    movq     $0x183,%rax
    movq     $512,%rcx
init_tmp_pdptt0:
    stosq
    addq     $0x40000000,%rax
    loopq    init_tmp_pdptt0

    leaq     tmp_pdpt1(%rip),%rdi   //初始化内核映射区pdpt
    movq     $KERNEL_OFFSET,%rsi
    shrq     $30,%rsi
    andq     $0x1ff,%rsi
    shlq     $3,%rsi

    leaq     _start(%rip),%rax
    movq     $0xFFFFFFFFC0000000,%rbx
    andq     %rbx,%rax
    orq      $0x183,%rax
    movq     %rax,(%rdi,%rsi)       //内核占2G虚拟内存
    addq     $0x40000000,%rax
    movq     %rax,8(%rdi,%rsi)

    leaq     tmp_pml4t(%rip),%rax
    movq     %rax,%cr3               //启用临时页表
/************************************************************/

    leaq     _end_stack(%rip),%rsp
    movabs   $KERNEL_OFFSET,%rax
    orq      %rax,%rsp       //设置bsp核栈

    leaq     init_kernel(%rip),%rax
    movabs   $KERNEL_OFFSET,%rbx
    orq      %rbx,%rax
    jmp      *%rax           //切换到0xFFFFFFFF80000000内核空间，进入C语言init_kernel

/**********************内核初始化数据**************************/
.section    .init_data, "wa"
    /*页表需要对齐4K，分配12K内存：一个pmlt4t,两个pdpt */
    .balign 4096
    .globl  tmp_pml4t
    tmp_pml4t:  .fill   0x1000,1,0
    tmp_pdpt0:  .fill   0x1000,1,0
    tmp_pdpt1:  .fill   0x1000,1,0

    .globl kernel_stack_top
    kernel_stack_top:   .quad   0

    .globl boot_info
    boot_info:     .quad   0

/*************************正式内核代码****************************/
.section    .text, "ax"

    //长模式系统调用入口
.globl  long_mode_syscall_entry
long_mode_syscall_entry:
    //手动保存用户态rip(rcx)用户态rsp,rflags(r11),其他通用寄存器。
    //手动恢复用户态rip(rcx)用户态rsp,rflags(r11),其他通用寄存器。
    sysretq

    //兼容模式系统调用入口
//.globl  compatible_mode_syscall_entry
//compatible_mode_syscall_entry:
    //手动保存用户态rip(rcx)用户态rsp,rflags(r11),其他通用寄存器。
    //call    print_h
    //jmp .
    //手动恢复用户态rip(rcx)用户态rsp,rflags(r11),其他通用寄存器。
    //sysretl

