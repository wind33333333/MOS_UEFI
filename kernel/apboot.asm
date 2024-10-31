org    0x10000
bits 16
    cli
    wbinvd

    mov ax,cs
    mov ds,ax
    mov es,ax
    mov ax,0x8000
    mov ss,ax
    mov sp,0

    lgdt [ap_gdt_ptr]

    mov eax,cr0                         ; 读取 CR0
    or eax,1                            ; 设置 PE 位 (bit 0)
    mov cr0,eax                         ; 写回 CR0，开启保护模式
    jmp dword   CODE32_SEL:ap_code32    ; 远跳转到 32 位代码

bits 32
ap_code32:
    mov ax,DATA32_SEL
    mov ss,ax
    mov ds,ax
    mov es,ax
    mov esp,0x90000

    mov eax,cr4
    or eax,0x20         ; 设置 PAE 位 (bit 5)
    mov cr4,eax

    mov eax,0x90000
    mov cr3,eax

    mov ecx,0xC0000080  ; EFER MSR 的地址
    rdmsr                ; 读取 EFER
    or eax,0x100        ; 设置 LME (Long Mode Enable) 位 (bit 8)
    wrmsr                ; 写回 EFER

    mov eax,cr0
    or eax,0x80000001   ; 设置 PG (Paging) 和 PE (Protected Mode) 位
    mov cr0,eax

    jmp CODE64_SEL:ap_code64

bits 64
ap_code64:
    mov ax,DATA64_SEL
    mov ss,ax
    mov ds,ax
    mov es,ax
    mov rsp,0x90000
    jmp 0x100000           ; 进入内核


align 8
ap_gdt:
                 dq 0                  ; 保留项
    code32_desc  dq 0x00CF9A000000FFFF
    data32_desc  dq 0x00CF92000000FFFF
    code64_desc  dq 0x0020980000000000
    data64_desc  dq 0x0000920000000000
ap_gdt_end:

ap_gdt_ptr  dw  ap_gdt_end - ap_gdt - 1
            dq  ap_gdt

CODE32_SEL  equ code32_desc - ap_gdt
DATA32_SEL  equ data32_desc - ap_gdt
CODE64_SEL  equ code64_desc - ap_gdt
DATA64_SEL  equ data64_desc - ap_gdt

