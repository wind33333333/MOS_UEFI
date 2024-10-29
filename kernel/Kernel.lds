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
	. = ALIGN(4096);
	.init_data :
	{
		_start_init_data = .;
		*(.init_data)
		. = ALIGN(4096);
		_end_init_data = .;
	}

    .apboot_data ALIGN(4096) :
    {
        _apboot_start = .;
        *(.apboot)
        _apboot_end = .;
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

    . = ALIGN(8);
	.bss :
	{
		_start_bss = .;
		*(.bss)
		_end_bss = .;
	}

    . = ALIGN(4096);
	_end = .;
}