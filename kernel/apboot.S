.section .apboot
.code16
    cli
    wbinvd

    mov    %cs,%ax
    mov    %ax,%ds
    mov    %ax,%es
    mov    $0x8000,%ax
    mov    %ax,%ss
    mov    $0,%sp

    lgdt   ap_gdt_ptr

    mov    %cr0,%eax               # 读取 CR0
    or     $1,%eax                 # 设置 PE 位 (bit 0)
    mov    %eax,%cr0               # 写回 CR0，开启保护模式
    ljmp   $CODE32_SEL,$ap_code32  # 远跳转到 32 位代码

.code32
ap_code32:
    mov    $DATA32_SEL,%ax
    mov    %ax,%ss
    mov    %ax,%ds
    mov    %ax,%es
    mov    $0x90000,%esp

    mov    %cr4,%eax
    or     $0x20,%eax              # 设置 PAE 位 (bit 5)
    mov    %eax,%cr4

    mov    $0x90000,%eax
    mov    %eax,%cr3

    mov    $0xC0000080,%ecx        # EFER MSR 的地址
    rdmsr                          # 读取 EFER
    or     $0x100,%eax             # 设置 LME (Long Mode Enable) 位 (bit 8)
    wrmsr                          # 写回 EFER

    mov    %cr0,%eax
    or     $0x80000001,%eax        # 设置 PG (Paging) 和 PE (Protected Mode) 位
    mov    %eax,%cr0

    ljmp   $CODE64_SEL,$ap_code64

.code64
ap_code64:
    mov    $DATA64_SEL,%ax
    mov    %ax,%ss
    mov    %ax,%ds
    mov    %ax,%es
    mov    $0x90000,%rsp
    jmp    $0x100000               # 进入内核

.align 8

ap_gdt:
                .quad 0                        # 保留项
code32_desc:    .quad 0x00CF9A000000FFFF
data32_desc:    .quad 0x00CF92000000FFFF
code64_desc:    .quad 0x0020980000000000
data64_desc:    .quad 0x0000920000000000
ap_gdt_end:

ap_gdt_ptr:
    .word    ap_gdt_end - ap_gdt - 1
    .quad    ap_gdt

.equ CODE32_SEL,code32_desc - ap_gdt
.equ DATA32_SEL,data32_desc - ap_gdt
.equ CODE64_SEL,code64_desc - ap_gdt
.equ DATA64_SEL,data64_desc - ap_gdt
