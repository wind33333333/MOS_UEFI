OUTPUT_FORMAT("elf64-x86-64","elf64-x86-64","elf64-x86-64")
OUTPUT_ARCH(i386:x86-64)
ENTRY(_start)
SECTIONS
{
    /* 内核初始化起始地址 */
    . = 0xFFFFFFFF80000000 + 0x100000;

    /* ================================================================= */
    /*                       一、 初始化丢弃区 (Init)                    */
    /* ================================================================= */

    /* 1. 内核初始化代码段 */
    .init_text :
    {
       _start_init_text = .;
       *(.init_text)
       *(.init_text.*)
       /* 💡 降维打击：暴力吸入所有名字带 init 的 .o 文件的代码段 */
       *init.o(.text .text.*)
       *init_*.o(.text .text.*)
       . = ALIGN(4096);
       _end_init_text = .;
    }

    /* 2. 内核初始化只读数据段 (破解 GCC 字符串池的核心) */
    .init_rodata :
     {
       _start_init_rodata = .;
       *(.init_rodata)
       *(.init_rodata.*)
       /* 💡 降维打击：暴力吸入 init 文件的常量和字符串 (如 .rodata.str1.1) */
       *init.o(.rodata .rodata.*)
       *init_*.o(.rodata .rodata.*)
       . = ALIGN(4096);
       _end_init_rodata = .;
    }

    /* 3. 内核初始化数据段 */
    .init_data :
    {
       _start_init_data = .;
       *(.init_data)
       *(.init_data.*)
       /* 💡 降维打击：暴力吸入 init 文件的全局读写变量 */
       *init.o(.data .data.*)
       *init_*.o(.data .data.*)

       /* 修补引擎的“档案表”塞进 INIT 数据区 */
       . = ALIGN(8);
       __alt_instructions = .;
       *(.altinstructions)
       __alt_instructions_end = .;

       /* 修补引擎的“捐献器官”也塞进 INIT 数据区 */
       *(.altinstr_replacement)

       _end_init_data = .;
    }

    /* 4. 内核初始化未初始化变量段 (BSS) */
    .init_bss :
    {
       _start_init_bss = .;
       /* 💡 降维打击：吸入 init 文件的未初始化变量 */
       *init.o(.bss COMMON)
       *init_*.o(.bss COMMON)
       . = ALIGN(4096);
       _end_init_bss = .;
    }

    /* ================================================================= */
    /*                       二、 内核正式运行区 (Core)                  */
    /* ================================================================= */

    /* 正式内核代码段 (RX) */
    .text :
    {
       _start_text = .;
       /* 💡 EXCLUDE_FILE：排除掉刚才被吸走的文件，防止链接器报重复定义的错误 */
       *(EXCLUDE_FILE(*init.o *init_*.o) .text .text.*)
       . = ALIGN(4096);
       _end_text = .;
    }

    /* 只读数据段 (RO) */
    .rodata :
    {
       _start_rodata = .;
       /* 💡 排除掉初始化文件的字符串和常量 */
       *(EXCLUDE_FILE(*init.o *init_*.o) .rodata .rodata.*)
       . = ALIGN(4096);
       _end_rodata = .;
    }

    /* 读写数据段 (RW) */
    .data :
    {
       _start_data = .;
       *(EXCLUDE_FILE(*init.o *init_*.o) .data .data.*)
       _end_data = .;
    }

    /* 未初始化变量段 (RW) */
    .bss :
    {
       _start_bss = .;
       *(EXCLUDE_FILE(*init.o *init_*.o) .bss COMMON)
       . = ALIGN(4096);
       _end_bss = .;
    }

    _end = .;

    /* ================================================================= */
    /*                       三、 垃圾桶区 (Discard)                     */
    /* ================================================================= */

    /* 黑洞丢弃区：保持 ELF 纯净 */
    /DISCARD/ :
    {
       *(.eh_frame)
       *(.note*)
       *(.comment)
    }
}