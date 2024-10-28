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

    ;=================== GDT==========
    align 8
    gdt:
    reserved:        .quad      0
    code32_desc:     .quad      0x00CF9A000000FFFF
    data32_desc:	 .quad	    0x00CF92000000FFFF
    code64_desc:	 .quad  	0x0020980000000000
    data64_desc:	 .quad  	0x0000920000000000
    gdt_end:

    gdt_ptr:
    gdt_limit:      .word   gdt_end-gdt
    gdt_baseaddr:   .quad   gdt

    code32_sel:	    .equ	code32_desc - gdt
    data32_sel	    .equ	data32_desc - gdt
    code64_sel  	.equ	code64_desc - gdt
    data64_sel	    .equ	data64_desc - gdt

