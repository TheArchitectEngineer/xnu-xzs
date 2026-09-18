OUTPUT_FORMAT("elf64-littleaarch64")
OUTPUT_ARCH(aarch64)
ENTRY(_start)

SECTIONS
{
    . = 0x80080000;

    .text : {
        *(.text.boot)
        *(.text*)
    }

    .rodata : {
        *(.rodata*)
    }

    .data : {
        *(.data*)
    }

    . = ALIGN(16);
    __bss_start = .;
    .bss : {
        *(.bss*)
        *(COMMON)
    }
    . = ALIGN(16);
    __bss_end = .;

    _end = .;
}
