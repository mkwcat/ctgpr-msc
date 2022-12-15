OUTPUT_FORMAT("elf32-powerpc", "elf32-powerpc", "elf32-powerpc");
OUTPUT_ARCH(powerpc:common);
EXTERN(_start);
ENTRY(_start);

PHDRS
{
	stub PT_LOAD FLAGS(5);
	text PT_LOAD FLAGS(6);
	data PT_LOAD FLAGS(6);
	bss PT_LOAD;
}

SECTIONS
{
	. = 0x80003400;

	.stub : {
		STUB_START = .;
		*(.stub)
		. = ALIGN(32);
		STUB_END = .;
	} : stub = 0
	
	. = 0x80004000;
	
	.text : {
		TEXT_START = .;
		*(.text*)
		. = ALIGN(32);
		TEXT_END = .;
	} : text = 0

	.data : {
		RODATA_START = .;
		KEEP(*(.bdkey))
		*(.rodata*)
		RODATA_END = .;
		. = ALIGN(4);
		DATA_START = .;
		*(.data*)
		. = ALIGN(32);
		DATA_END = .;
	} : data = 0

	.bss : {
		PROVIDE(__bss_start = .);
		*(.dynsbss)
		*(.sbss)
		*(.sbss.*)
		*(.gnu.linkonce.sb.*)
		*(.scommon)
		*(.sbss2)
		*(.sbss2.*)
		*(.gnu.linkonce.sb2.*)
		*(.bss)
		*(.bss.*)
		*(.dynbss)
		*(.gnu.linkonce.b.*)
		*(COMMON)
		. = ALIGN(32);
		PROVIDE (__bss_end = .);
	} : bss = 0
}
