.section .init_text,"ax"
.code16
.globl _apboot_start
_apboot_start:    #实模式
    cli
    wbinvd

    movw    %cs,%ax
    movw    %ax,%ss
    movw    %ax,%ds
    movw    %ax,%es

    movzxw  %ax,%esi                #重定位设置32位 和 64位代码指针 /gdt指针
    shll    $4,%esi
    leal    (protected_mode-_apboot_start)(%esi),%eax
    movl    %eax,code32_ptr-_apboot_start

    leal    (long_mode-_apboot_start)(%esi),%eax
    movl    %eax,code64_ptr-_apboot_start

    leal    (ap_gdt-_apboot_start)(%esi),%eax
    movl    %eax,ap_gdt_ptr+2-_apboot_start

    lgdtl   ap_gdt_ptr-_apboot_start

    movl    %cr0,%eax                        # 读取 CR0
    orl     $1,%eax                          # 设置 PE 位 (bit 0)
    movl    %eax,%cr0                        # 写回 CR0，开启保护模式
    ljmpl   *(code32_ptr - _apboot_start)    # 远跳转到保护模式

.code32
protected_mode:    #保护模式
    movw    $DATA32_SEL,%ax
    movw    %ax,%ss
    movw    %ax,%ds
    movw    %ax,%es

    movl    %cr4,%eax
    orl     $0x20,%eax              # 设置 PAE 位 (bit 5)
    movl    %eax,%cr4

    movl    (ap_tmp_pml4t_ptr-_apboot_start)(%esi),%eax
    movl    %eax,%cr3               # 设置临时页表

    movl    $0xC0000080,%ecx        # EFER MSR 的地址
    rdmsr                           # 读取 EFER
    orl     $0x100,%eax             # 设置 LME (Long Mode Enable) 位 (bit 8)
    wrmsr                           # 写回 EFER

    movl    %cr0,%eax
    orl     $0x80000001,%eax        # 设置 PG (Paging) 和 PE (Protected Mode) 位
    movl    %eax,%cr0
    ljmp    *(code64_ptr-_apboot_start)(%esi)    #跳入长模式

.code64
long_mode:    #长模式
    movl    $0xB,%eax
    movl    $1,%ecx
    cpuid
    movl    %edx,%eax                        #apic_id

    movq    $0xFFFFFFFFFFFFFFFF,%rcx
    movq   apic_id_table_ptr(%rip),%rdi
    cld
    repne   scasl
    je      j_apic_id
    hlt                                         #没找到停机

j_apic_id:
    notq    %rcx
    decq    %rcx
    shlq    $14,%rcx                        #每个核心16Kb栈空间
    movq    ap_rsp_ptr(%rip),%rsp
    addq    %rcx,%rsp

    movq    ap_main_ptr(%rip),%rax
    jmp     *%rax                           #ap_main

##################数据###############
    .balign 8
    ap_gdt:
                        .quad 0                        # 保留项
        code32_desc:    .quad 0x00CF9A000000FFFF
        data32_desc:    .quad 0x00CF92000000FFFF
        code64_desc:    .quad 0x0020980000000000
    ap_gdt_end:

    .balign 8
    ap_gdt_ptr:
        .word    ap_gdt_end - ap_gdt - 1
        .long    0

        .equ CODE32_SEL,code32_desc - ap_gdt
        .equ DATA32_SEL,data32_desc - ap_gdt
        .equ CODE64_SEL,code64_desc - ap_gdt

    .balign 8
    code32_ptr:
        .long   0
        .word   CODE32_SEL

    .balign 8
    code64_ptr:
        .long   0
        .word   CODE64_SEL

    .globl  ap_tmp_pml4t_ptr
    ap_tmp_pml4t_ptr:   .quad   0

    .globl  ap_main_ptr
    ap_main_ptr:        .quad   0

    .globl  ap_rsp_ptr
    ap_rsp_ptr:         .quad   0

    .globl  apic_id_table_ptr
    apic_id_table_ptr:  .quad   0

.globl   _apboot_end
_apboot_end:
