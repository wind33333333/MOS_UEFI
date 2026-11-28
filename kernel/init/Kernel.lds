OUTPUT_FORMAT("elf64-x86-64","elf64-x86-64","elf64-x86-64")
OUTPUT_ARCH(i386:x86-64)
ENTRY(_start)

/* 定义内核高半区的虚拟基址常量 */
KERNEL_VMA = 0xFFFFFFFF80000000;

SECTIONS
{
    /* ================================================================= */
    /* 核心改变：我们只指定 VMA（虚拟内存地址），不再绑定死 LMA（物理加载地址） */
    /* 此时 . 的值是 0xFFFFFFFF80000000，这是代码在开启分页后期望运行的地址 */
    /* ================================================================= */
    . = KERNEL_VMA;

    /* ================================================================= */
    /*                       一、 初始化丢弃区 (Init)                    */
    /* ================================================================= */

    /* 1. 内核初始化代码段 */
    .init_text :
    {
       _start_init_text = .;
       *(.init_text)
       *(.init_text.*)
       /* 暴力吸入所有名字带 init 的 .o 文件的代码段 */
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
       /* 暴力吸入 init 文件的常量和字符串 */
       *init.o(.rodata .rodata.*)
       *init_*.o(.rodata .rodata.*)
       . = ALIGN(4096);
       _end_init_rodata = .;
    }

    /* 2.5 Per-CPU 静态变量模板段 (VMA=0 偏移，LMA 嵌入 Init 丢弃区) */
    . = ALIGN(4096);
    _percpu_load = .;
    .data..percpu 0x0 : AT(_percpu_load)
    {
       _percpu_start = .;
       /* 偏移 0 固定保留给高频核心控制块 / self_base，避免普通静态变量地址等于 NULL(0) */
       *(.data..percpu..first)
       /* 4KB 页对齐静态 Per-CPU 变量（无此类变量时自动零填充） */
       *(.data..percpu..page_aligned)
       . = ALIGN(64);
       /* 64B 缓存行对齐与常规静态 Per-CPU 变量 */
       *(.data..percpu..cacheline_aligned)
       *(.data..percpu)
       *(.data..percpu.*)
       . = ALIGN(64);
       _percpu_end = .;
    }
    _percpu_size = _percpu_end - _percpu_start;
    /* 恢复位置计数器到高半区 Init 丢弃区，并按 4KB 对齐，与 .init_data 页隔离 */
    . = ALIGN(_percpu_load + SIZEOF(.data..percpu), 4096);

    /* 3. 内核初始化数据段（显式 AT 重置 LMA == VMA，避免继承上一段的 VMA/LMA 偏移差） */
    .init_data : AT(ADDR(.init_data))
    {
       _start_init_data = .;
       *(.init_data)
       *(.init_data.*)
       /* 暴力吸入 init 文件的全局读写变量 */
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
       /* 吸入 init 文件的未初始化变量 */
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
       *(EXCLUDE_FILE(*init.o *init_*.o) .text .text.*)
       . = ALIGN(4096);
       _end_text = .;
    }

    /* 只读数据段 (RO) */
    .rodata :
    {
       _start_rodata = .;
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