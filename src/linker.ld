ENTRY(_Reset)

/* Specify the memory areas */
MEMORY
{
    RAM (xrw)       : ORIGIN = 0x40000000, LENGTH = 256M /* 256MB */
    RAM2 (xrw)      : ORIGIN = 0x50000000, LENGTH = 128K /* 128KB */
}

heap_size = 0x4000000; /* 64M */
mmu_tlb_size = 0xe000; /* 56K */

SECTIONS {
    /*. = ORIGIN(RAM) + 0x10000;*/
    .text :
    {
        . = ALIGN(8);
        KEEP(*(.isr_vector))
        _start_text = .;
        *(.text)
        *(.text*)
        *(.rodata)
        *(.rodata*)
        KEEP(*(.init))
        KEEP(*(.fini))

        /* .ctors */
        *crtbegin.o(.ctors)
        *crtbegin?.o(.ctors)
        *(EXCLUDE_FILE(*crtend?.o *crtend.o) .ctors)
        *(SORT(.ctors.*))
        *(.ctors)

        /* .dtors */
        *crtbegin.o(.dtors)
        *crtbegin?.o(.dtors)
        *(EXCLUDE_FILE(*crtend?.o *crtend.o) .dtors)
        *(SORT(.dtors.*))
        *(.dtors)
        KEEP(*(.eh_frame*))
        _end_text = .;
    } >RAM

        .ARM.extab : 
        {
                *(.ARM.extab* .gnu.linkonce.armextab.*)
        } > RAM

        __exidx_start = .;
        .ARM.exidx :
        {
                *(.ARM.exidx* .gnu.linkonce.armexidx.*)
        } > RAM
        __exidx_end = .;

    _end_text = .;

    .data :
    {
        _start_data = .;
        *(vtable)
        *(.data)
        *(.data*) 
        . = ALIGN(8);
        /* preinit data */
        PROVIDE_HIDDEN (__preinit_array_start = .);
        KEEP(*(.preinit_array))
        PROVIDE_HIDDEN (__preinit_array_end = .);

        . = ALIGN(8);
        /* init data */
        PROVIDE_HIDDEN (__init_array_start = .);
        KEEP(*(SORT(.init_array.*)))
        KEEP(*(.init_array))
        PROVIDE_HIDDEN (__init_array_end = .);


        . = ALIGN(8);
        /* finit data */
        PROVIDE_HIDDEN (__fini_array_start = .);
        KEEP(*(SORT(.fini_array.*)))
        KEEP(*(.fini_array))
        PROVIDE_HIDDEN (__fini_array_end = .);

        KEEP(*(.jcr*))
        . = ALIGN(8);
        _end_data = .;
    } >RAM 


    . = ALIGN(8);
    .heap :
    {
	__end__ = .;
        /* _heap_start = .; */
        /* "end" is used by newlib's syscalls!!! */
        PROVIDE(end = .);
        PROVIDE(_heap_start = end );
        PROVIDE(_HEAP_START = end );
        
        . = . + heap_size;
        
        PROVIDE(_heap_end = .);
        PROVIDE(_HEAP_MAX = .);
    } >RAM

    .ARM.attributes 0 : { *(.ARM.attributes) }


    .stack : {
    PROVIDE(stackEND = .);
    . = . + 0x0004000;
    PROVIDE(stackSTART = .);
    PROVIDE(stackSVC_END = .);
    . = . + 0x0004000;
    . = ALIGN(8);
    stackSVC = .;
    PROVIDE(stackSVC_START = .);
    PROVIDE(stackUND_END = .);
    . = . + 0x00004000;
    . = ALIGN(8);
    stackUND = .;
    PROVIDE(stackUND_START = .);
    PROVIDE(stackABT_END = .);
    . = . + 0x00004000;
    . = ALIGN(8);
    stackABT = .;
    PROVIDE(stackABT_START = .);
    PROVIDE(stackIRQ_END = .);
    . = . + 0x00004000;
    . = ALIGN(8);
    stackIRQ = .;
    PROVIDE(stackIRQ_START = .);
    PROVIDE(stackFIQ_END = .);
    . = . + 0x00004000;
    . = ALIGN(8);
    stackFIQ = .;   
    PROVIDE(stackFIQ_START = .);
    PROVIDE(stackUSR_END = .);
    . = . + 0x00010000;
    . = ALIGN(8);
    stackUSR = .;
    PROVIDE(stackUSR_START = .);
    stack_end = .;
    
    } >RAM


    .bss :
    {
         . = ALIGN(8);
        _start_bss = .;
        *(.bss)
        *(.bss*)
        *(COMMON)
        . = ALIGN(8);
        _end_bss = .;
	__bss_end = .;
    } >RAM

    . = ALIGN(8);
    .mmutable : {
        PROVIDE(mmu_start = .);
        *(.mmutable)
        . = . + mmu_tlb_size;
        PROVIDE(mmu_end = .);
    } >RAM2
}
_end = .;
