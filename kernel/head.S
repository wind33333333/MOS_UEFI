#include "linkage.h"

//===============================初始化程序代码=================================================
.section    .init_text, "ax"
.globl _start
_start:
    cli
    movl     $0x1B,%ecx           //IA32_APIC_BASE=0x1b 寄存器
    rdmsr
    testl    $0x100,%eax         //测试bit8 1=bsp 0=ap
    jz       kernel_stack_init

    /*保存loader通过rdi传递过来的boot_info指针*/
    movq     %rdi,boot_info(%rip)

    /*获取cpu核心数量,cpuid eax=0xB ecx=1,ebx=核心数量*/
    movl     $0xB,%eax
    movl     $1,%ecx
    cpuid

    /*开辟多核栈空间*/
    shlq     $14,%rbx                           //每个核心分配16k栈,核心数量*16K
    movq     kernel_stack_top(%rip),%rax
    addq     %rbx,%rax
    movq     %rax,kernel_stack_top(%rip)       //内核栈顶

    /*初始化临时页表*/
    movq     $TMP_PML4T,%r8      //pml4t起始地址
    movq     $2,%r9              //pml4e数量 (1=512G)

    movq     %r8,%rax            //pml4t递归映射
    movq     %r8,%rdi
    orq      $0x103,%rax
    movq     %rax,0xFF8(%rdi)

    movq     %r8,%rax
    orq      $3,%rax             //pml4e属性
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

    /*切换页表*/
    movq     $TMP_PML4T,%rax
    movq     %rax,%cr3

kernel_stack_init:
    /*初始化多核内核栈*/
    movl     $0xB,%eax
    xorl     %ecx,%ecx
    cpuid                                   //cpuid eax=0xb,ecx=0 返回edx=x2apic id
    shlq     $14,%rdx                       //每个核心分配16k栈
    movq     kernel_stack_top(%rip),%rsp
    subq     %rdx,%rsp                      //计算每个内核栈 栈顶=kernelstack_top-(APIC ID*16K)*/

    /*切换到0xFFFF800000000000内核空间，进入C语言init_kernel*/
    movabs     $init_kernel,%rax
    jmp        *%rax

//===============================初始化程序数据=================================================
.section    .init_data, "wa"

    .globl   idt_ptr
    idt_ptr:
    idt_limit:	.word	idt_end - idt - 1
    idt_base:	.quad	idt

    .globl kernel_stack_top
    kernel_stack_top:   .quad   _end

    .globl boot_info
    boot_info:     .quad   0

    .equ    HBASE_ADDR,0xffff800000000000      //内核虛擬地址基址
    .equ    TMP_PML4T,0x90000                  //临时PML4T


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

//========================================正式内核数据=================================================
.section    .data, "wa"

    /*正式内核IDT*/
    .globl idt
    idt:
    	.fill  0x1000-(.-idt),1,0
    idt_end:

    /*正式内核PML4T*/
    .globl pml4t
    pml4t:
    	.quad	0
    	.fill	0xFF8-(. -pml4t),1,0
    	.quad   (pml4t - HBASE_ADDR)+0x103
