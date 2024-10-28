.section    .init_text, "ax"
.code16

    cli
    wbinvd

    movw    %cs,%ax
    movw    %ax,%ds
    movw	%ax,%es
    movw    $0x8000,%ax
    movw	%ax,%ss
    movw    $0,%sp

    lgdt    ap_gdt_ptr

    movl    %cr0,%eax        // 读取 CR0
    rol     $0x1,%eax        // 设置 PE 位（bit 0）
    movl    %eax,%cr0        // 写回 CR0，开启保护模式

    ljmp    $CODE32_SEL,$ap_code32

.code32
ap_code32:
    movw    $DATA32_SEL,%ax
    movw    %ax,%ss
    movw    %ax,%ds
    movw    %ax,%es
    movl    $0x90000,%esp

    movl    %cr4,%eax
    orl     $0x20,%eax       //设置 PAE 位（bit 5）
    movl    %eax,%cr4

    movl	$0x90000,%eax
    movl	%eax,%cr3

    movl    $0xC0000080,%ecx  //EFER MSR 的地址
    rdmsr                     //读取 EFER
    orl     $0x100,%eax       //设置 LME（Long Mode Enable）位（bit 8）
    wrmsr                     //写回 EFER

    movl    %cr0, %eax
    orl     $0x80000001, %eax  //设置 PG（Paging）和 PE（Protected Mode）位
    movl    %eax, %cr0

    ljmp    $CODE64_SEL,$ap_code64

.code64
ap_code64:
    movw    $DATA64_SEL,%ax
    movw    %ax,%ss
    movw    %ax,%ds
    movw    %ax,%es
    movq    $0x90000,%rsp

    movq    $0x100000,%rax
    jmpq    *%rax                 //进入内核


    /*AP_GDT*/
    .balign 8
    ap_gdt:
    reserved:        .quad      0
    code32_desc:     .quad      0x00CF9A000000FFFF
    data32_desc:	 .quad	    0x00CF92000000FFFF
    code64_desc:	 .quad  	0x0020980000000000
    data64_desc:	 .quad  	0x0000920000000000
    ap_gdt_end:

    ap_gdt_ptr:
    ap_gdt_limit:      .word   ap_gdt_end-ap_gdt-1
    ap_gdt_baseaddr:   .quad   ap_gdt

    .equ    CODE32_SEL,code32_desc-ap_gdt
    .equ    DATA32_SEL,data32_desc-ap_gdt
    .equ    CODE64_SEL,code64_desc-ap_gdt
    .equ    DATA64_SEL,data64_desc-ap_gdt

