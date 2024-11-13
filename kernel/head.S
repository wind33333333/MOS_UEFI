#include "linkage.h"
//===============================初始化程序代码=================================================
.section    .init_text, "ax"
.globl _start
_start:
    cli
    movq     %rdi,boot_info(%rip)           //保存loader通过rdi传递过来的boot_info指针

    /*初始化临时页表*/
    movq     $tmp_pml4t-HBASE_ADDR,%r8      //pml4t起始地址
    movq     $2,%r9                         //pml4e数量 (1=512G)

    movq     %r8,%rax                       //pml4t递归映射
    movq     %r8,%rdi
    orq      $0x3,%rax
    movq     %rax,0xFF8(%rdi)

    movq     %r8,%rax
    orq      $0x3,%rax                      //pml4e属性
    movq     %r9,%rcx
init_tmp_pml4t:
    addq     $0x1000,%rax
    movq     %rax,(%rdi)
    movq     %rax,0x800(%rdi)
    addq     $8,%rdi
    loopq    init_tmp_pml4t

    movq     %r8,%rdi
    addq     $0x1000,%rdi        //pdptt起始地址
    movq     $0x83,%rax          //物理地址0开始对等映射
    movq     %r9,%rcx
    shlq     $9,%rcx             //pml4e*512
init_tmp_pdptt:
    stosq
    addq     $0x40000000,%rax
    loopq    init_tmp_pdptt

    /*设置临时页表*/
    movq     %r8,%cr3

    movq     kernel_stack_top(%rip),%rsp
    addq     $0x4000,%rsp                     //初始化bsp核心栈，内核结束处+16K
    movq     %rsp,kernel_stack_top(%rip)

    /*切换到0xFFFF800000000000内核空间，进入C语言init_kernel*/
    movabs     $init_kernel,%rax
    jmp        *%rax

//===============================初始化程序数据=================================================
.section    .init_data, "wa"
    .globl  tmp_pml4t
    tmp_pml4t:
        .fill   0x3000,1,0

    .globl kernel_stack_top
    kernel_stack_top:   .quad   _end

    .globl boot_info
    boot_info:     .quad   0

    .equ    HBASE_ADDR,0xffff800000000000      //内核虛擬地址基址


//========================================正式内核代码=================================================
.section    .text, "ax"
    /*0-20异常处理程序入口*/
    ENTRY(divide_error,do_divide_error,1)
    ENTRY(debug,do_debug,1)
    ENTRY(nmi,do_nmi,1)
    ENTRY(int3,do_int3,1)
    ENTRY(overflow,do_overflow,1)
    ENTRY(bounds,do_bounds,1)
    ENTRY(undefined_opcode,do_undefined_opcode,1)
    ENTRY(dev_not_available,do_dev_not_available,1)
    ENTRY(double_fault,do_double_fault,0)
    ENTRY(coprocessor_segment_overrun,do_coprocessor_segment_overrun,1)
    ENTRY(invalid_TSS,do_invalid_TSS,0)
    ENTRY(segment_not_present,do_segment_not_present,0)
    ENTRY(stack_segment_fault,do_stack_segment_fault,0)
    ENTRY(general_protection,do_general_protection,0)
    ENTRY(page_fault,do_page_fault,0)
    ENTRY(x87_FPU_error,do_x87_FPU_error,1)
    ENTRY(alignment_check,do_alignment_check,0)
    ENTRY(machine_check,do_machine_check,1)
    ENTRY(SIMD_exception,do_SIMD_exception,1)
    ENTRY(virtualization_exception,do_virtualization_exception,1)

    /*中断处理程序入口*/
    ENTRY(ignore,do_ignore,1)
    ENTRY(keyboard,do_keyboard,1)
    ENTRY(hpet,do_hpet,1)
    ENTRY(apic_timer,do_apic_timer,1)

    /*中断和异常复用处理程序*/
interrupt_entry:
	pushq	%rax
	movq	%es,	%rax
	pushq	%rax
	movq	%ds,	%rax
	pushq	%rax
	xorq	%rax,	%rax

	pushq	%rbp
	pushq	%rdi
	pushq	%rsi
	pushq	%rdx
	pushq	%rcx
	pushq	%rbx
	pushq	%r8
	pushq	%r9
	pushq	%r10
	pushq	%r11
	pushq	%r12
	pushq	%r13
	pushq	%r14
	pushq	%r15

	movq	ERRCODE(%rsp),	%rsi
	movq	FUNC(%rsp),	%rdx

	movq	$0x10,	%rdi
	movq	%rdi,	%ds
	movq	%rdi,	%es

	movq	%rsp,	%rdi
	////GET_CURRENT(%ebx)

	callq 	*%rdx

	popq	%r15;
	popq	%r14;
	popq	%r13;
	popq	%r12;
	popq	%r11;
	popq	%r10;
	popq	%r9;
	popq	%r8;
	popq	%rbx;
	popq	%rcx;
	popq	%rdx;
	popq	%rsi;
	popq	%rdi;
	popq	%rbp;
	popq	%rax;
	movq	%rax,	%ds;
	popq	%rax;
	movq	%rax,	%es;
	popq	%rax;
	addq	$0x10,	%rsp;
	iretq;

    /*系统调用入口*/
.globl  syscall_entry
syscall_entry:
    //手动保存用户态rip(rcx)用户态rsp,rflags(r11),其他通用寄存器。

    call    print_h
    jmp .
    //手动恢复用户态rip(rcx)用户态rsp,rflags(r11),其他通用寄存器。
    sysretq
