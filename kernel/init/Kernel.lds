OUTPUT_FORMAT("elf64-x86-64","elf64-x86-64","elf64-x86-64")
OUTPUT_ARCH(i386:x86-64)
ENTRY(_start)
SECTIONS
{
    /* 内核初始化代码段 */
    . = 0xFFFFFFFF80000000 + 0x100000;
    . = ALIGN(4096);
    .init_text :
    {
       _start_init_text = .;
       *(.init_text)
       . = ALIGN(4096);
       _end_init_text = .;
    }

    /* 内核初始化数据段 */
    . = ALIGN(4096);
    .init_data :
    {
       _start_init_data = .;
       *(.init_data)
       . = ALIGN(4096);
       _end_init_data = .;
    }

    /* 正式内核代码段 (RX) */
    . = ALIGN(4096);
    .text :
    {
       _start_text = .;
       *(.text)
       *(.text.*)        /* 兼容部分编译器生成的细分代码段 */
       . = ALIGN(4096);
       _end_text = .;
    }

    /* 正式内核读写数据段 (RW) */
    . = ALIGN(4096);
    .data :
    {
        _start_data = .;
        *(.data)
        *(.data.*)
        _end_data = .;
    }

    /* 全局未初始化变量段 (RW) */
    .bss :
    {
       _start_bss = .;
       *(.bss)
       *(COMMON)         /* 包含弱符号未初始化变量 */
       . = ALIGN(4096);
       _end_bss = .;
    }

    /* 只读数据段 (RO) —— 作为物理隔离墙！ */
    . = ALIGN(4096);
    .rodata :
    {
       _start_rodata = .;
       *(.rodata)
       *(.rodata.*)
       . = ALIGN(4096);
       _end_rodata = .;
    }

    /* 内核堆栈段 (RW) */
    . = ALIGN(4096);
    .stack :
    {
        _start_stack = .;
        . = . + 0x4000;  /* 为 BSP 主核堆栈预留 16K 空间 */
        *(.stack)
        _end_stack = .;
    }

    /*
     * 预留全局偏移表 (GOT) 空间，按需开启
     */
    /*
    .got :
    {
        _got_start = .;
        *(.got)
        _got_end = .;
    }
    .got.plt :
    {
        _got_plt_start = .;
        *(.got.plt)
        _got_plt_end = .;
    }
    */

    . = ALIGN(4096);
    _end = .;

    /* 【核心优化 2】黑洞丢弃区
     * 抛弃无用的调试栈帧、注释和警告标志，保持 ELF 文件纯净，彻底消除 .note 警告
     */
    /DISCARD/ :
    {
        *(.eh_frame)
        *(.note*)
        *(.comment)
    }
}