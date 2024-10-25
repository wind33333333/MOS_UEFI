#include "linkage.h"

//===============================初始化程序代码=================================================
.section    .init_text, "ax"
.globl _start
_start:
////////////关中断
    cli
////////////////////////////bsp/ap核
    movl    $0x1B,%ecx           //IA32_APIC_BASE=0x1b 寄存器
    rdmsr
    or      $0xc00,%eax          //bit10 X2APIC使能   bit11 APIC全局使能
    wrmsr
    test    $0x100,%eax         //测试bit8 1=bsp 0=ap
    jz      kernel_stack_init

/////////////////////////保存loader通过rdi传递过来的boot_info指针
    mov     %rdi,boot_info(%rip)

///////////////////////////获取cpu核心数量
    mov        $1,%eax
    cpuid
    shr        $16,%ebx         //右移16位得到cpu数量

///////////////////////////开辟多核栈空间
    shl        $14,%rbx                 //每个核心分配16k栈
    mov        kernel_stack_top(%rip),%rax
    add        %rbx,%rax
    mov        %rax,kernel_stack_top(%rip)       //内核栈顶

///////////////////////////切换页表
kernel_stack_init:
    mov     $tmp_pml4t-HBASE_ADDR,%rax
    mov     %rax,%cr3

/////////初始化多核内核栈
    mov     $0x802,%ecx
    rdmsr                           //获取APIC ID
    shl     $14,%rax                //每个核心分配16k栈
    mov     kernel_stack_top(%rip),%rsp
    sub     %rax,%rsp               //计算每个内核栈 栈顶=kernelstack_top-(APIC ID*16K)

//切换到0xFFFF800000000000内核空间，进入C语言init_kernel
    movabs     $init_kernel,%rax
    jmp        *%rax

//===============================初始化程序数据=================================================
.section    .init_data, "wa"

    .globl tmp_pml4t
    tmp_pml4t:
    .quad	(tmp_pdptt-HBASE_ADDR)+0x3
    .fill   0x800-(. - tmp_pml4t),1,0
    .quad	(tmp_pdptt-HBASE_ADDR)+0x3
    .fill	0xFF8-(. - tmp_pml4t),1,0
    .quad   (tmp_pml4t - HBASE_ADDR)+0x103

    .globl tmp_pdptt
    tmp_pdptt:
    .quad   0x00000083  //0
    .quad   0x40000083  //1
    .quad   0x80000083  //2
    .quad   0xC0000083  //3
    .quad   0x100000083  //4
    .quad   0x140000083  //5
    .quad   0x180000083  //6
    .quad   0x1C0000083  //7
    .fill   0x800-0x40,1,0
    .quad   0x4000000083 //256G 显存
    .fill	0x1000-(. - tmp_pdptt),1,0

    .globl   idt_ptr
    idt_ptr:
    idt_limit:	.word	idt_end - idt - 1
    idt_base:	.quad	idt

    .globl kernel_stack_top
    kernel_stack_top:   .quad   _end

    .globl boot_info
    boot_info:     .quad   0

    .equ    HBASE_ADDR,0xffff800000000000      //内核虛擬地址基址


//========================================正式内核代码=================================================
.section    .text, "ax"
////////////////////////////0-20异常处理程序入口
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

////////////////////////中断处理程序入口
ENTRY(ignore,do_ignore,1)
ENTRY(keyboard,do_keyboard,1)
ENTRY(hpet,do_hpet,1)
ENTRY(apic_timer,do_apic_timer,1)


////////////////////////中断和异常复用处理程序
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

    .globl idt
    idt:
    	.fill  0x1000-(.-idt),1,0
    idt_end:

    .globl pml4t
    pml4t:
    	.quad	0
    	.fill	0xFF8-(. -pml4t),1,0
    	.quad   (pml4t - HBASE_ADDR)+0x103
