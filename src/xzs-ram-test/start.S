/*
 * start.S - AArch64 entry point for Xperia XZs RAM persistence test writer
 */

.section ".text.boot"
.globl _start

_start:
    b       _real_start

    .long   0
    .quad   0x00080000
    .quad   0x00004000
    .quad   0
    .long   0
    .long   0x016f2818
    .long   0x00000000
    .long   0x00004000
    .quad   0
    .ascii  "ARM\x64"
    .long   0x00004000

_real_start:
    msr     daifset, #0xf

    /* Clear BSS section */
    ldr     x1, =__bss_start
    ldr     x2, =__bss_end
1:
    cmp     x1, x2
    b.ge    2f
    str     xzr, [x1], #8
    b       1b
2:
    ldr     x4, =_stack_top
    mov     sp, x4

    bl      test_writer_main

dead_loop:
    wfe
    b       dead_loop

    .section .bss
    .align 4
_stack_bottom:
    .skip 0x4000
_stack_top:
