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

    /*AP_GDT*/
    .balign 8
    ap_gdt:
    reserved:        .quad      0
    code32_desc:     .quad      0x00CF9A000000FFFF
    data32_desc:	 .quad	    0x00CF92000000FFFF
    code64_desc:	 .quad  	0x0020980000000000
    data64_desc:	 .quad  	0x0000920000000000
    ap_gdt_end:

    gdt_ptr:
    gdt_limit:      .word   ap_gdt_end-ap_gdt
    gdt_baseaddr:   .quad   ap_gdt

    .equ    CODE32_SEL,code32_desc-ap_gdt
    .equ    DATA32_SEL,data32_desc-ap_gdt
    .equ    CODE64_SEL,code64_desc-ap_gdt
    .equ    DATA64_SEL,data64_desc-ap_gdt

