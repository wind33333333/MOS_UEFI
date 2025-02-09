OUTPUT_FORMAT("elf64-x86-64","elf64-x86-64","elf64-x86-64")
OUTPUT_ARCH(i386:x86-64)
ENTRY(_start)
SECTIONS
{
    /*内核初始化代码段*/
	. = 0xffff800000000000 + 0x100000;
    .init_text :
    {
		_start_init_text = .;
		*(.init_text)
		_end_init_text = .;
    }

    /*内核初始化数据段*/
	.init_data :
	{
		_start_init_data = .;
		*(.init_data)
		_end_init_data = .;
	}

    /*正式内核代码段*/
    . = ALIGN(4096);
	.text :
	{
		_start_text = .;
		*(.text)
		_end_text = .;
	}

    /*正式内核数据段*/
    . = ALIGN(4096);
    .data :
    {
    	_start_data = .;
    	*(.data)
    	_end_data = .;
    }

    /*全局变量*/
	.bss :
	{
		_start_bss = .;
		*(.bss)
		_end_bss = .;
	}

    /* 内核堆栈段*/
    . = ALIGN(4096);
	.stack :
	{
	    _start_stack = .;
	    . = . + 0x4000;  /* 为堆栈预留16K空间 */
	    *(.stack)
	    _end_stack = .;
	}

    /*全局偏移表*/
	/*    .got :
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
        }*/

    . = ALIGN(4096);
	_end = .;
}