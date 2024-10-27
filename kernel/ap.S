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



