/*
 * start.S - AArch64 entry point for Xperia XZs debug dumper
 */

.section ".text.boot"
.globl _start

_start:
    /* Offset 0x00: Branch over header to real code */
    b       _real_start

    /* Offset 0x04: Reserved / code1 */
    .long   0

    /* Offset 0x08: text_offset (8 bytes) */
    .quad   0x00080000

    /* Offset 0x10: image_size (8 bytes) */
    .quad   0x00008000

    /* Offset 0x18: flags (8 bytes) */
    .quad   0

    /* Offset 0x20: res2 (8 bytes) */
    .quad   0

    /* Offset 0x28: res3 (8 bytes) */
    .quad   0

    /* Offset 0x30: res4 (8 bytes) */
    .quad   0

    /* Offset 0x38: ARM64 magic "ARM\x64" (4 bytes) */
    .ascii  "ARM\x64"

    /* Offset 0x3C: res5 (4 bytes) */
    .long   0x00000040


    /* Offset 0x40: Real entry point */
_real_start:
    /* Disable interrupts (FIQ, IRQ, SError, Debug) */
    msr     daifset, #0xf

    /* Save boot arguments passed by S1 Bootloader:
     * x0: Device Tree Blob (DTB) physical address
     */
    mov     x19, x0     /* Save DTB pointer in x19 */

    /* Read CurrentEL */
    mrs     x20, CurrentEL
    lsr     x20, x20, #2

    /* Read MPIDR_EL1 */
    mrs     x21, mpidr_el1

    /* Clear BSS section */
    ldr     x1, =__bss_start
    ldr     x2, =__bss_end
1:
    cmp     x1, x2
    b.ge    2f
    str     xzr, [x1], #8
    b       1b
2:
    /* Set up stack pointer in verified BSS */
    ldr     x4, =_stack_top
    mov     sp, x4

    /* Install exception vector table */
    adr     x0, dumper_exc_vectors
    msr     vbar_el1, x0
    isb

    /* Call C main: dumper_main(dtb_phys, current_el, mpidr) */
    mov     x0, x19
    mov     x1, x20
    mov     x2, x21
    bl      dumper_main

    /* If dumper_main returns, spin */
dead_loop:
    wfe
    b       dead_loop

    /* ============================================================
     * Dumper Exception Vectors (Aligned to 2KB)
     * ============================================================ */
    .align 11
dumper_exc_vectors:
    /* Current EL with SP0 */
    .align 7
    b       dumper_exc_handler
    .align 7
    b       dumper_exc_handler
    .align 7
    b       dumper_exc_handler
    .align 7
    b       dumper_exc_handler

    /* Current EL with SPx */
    .align 7
    b       dumper_exc_handler
    .align 7
    b       dumper_exc_handler
    .align 7
    b       dumper_exc_handler
    .align 7
    b       dumper_exc_handler

    /* Lower EL using AArch64 */
    .align 7
    b       dumper_exc_handler
    .align 7
    b       dumper_exc_handler
    .align 7
    b       dumper_exc_handler
    .align 7
    b       dumper_exc_handler

    /* Lower EL using AArch32 */
    .align 7
    b       dumper_exc_handler
    .align 7
    b       dumper_exc_handler
    .align 7
    b       dumper_exc_handler
    .align 7
    b       dumper_exc_handler

dumper_exc_handler:
    mrs     x0, esr_el1
    mrs     x1, elr_el1
    mrs     x2, far_el1
    mrs     x3, spsr_el1

    /* Store directly into 0x80060018 */
    mov     x4, #0x80060000
    str     x1, [x4, #0x18] /* last_elr */
    str     x0, [x4, #0x20] /* last_esr */
    str     x2, [x4, #0x28] /* last_far */
    str     x3, [x4, #0x30] /* last_spsr */

    /* Increment exception count */
    ldr     w5, [x4, #0x10]
    add     w5, w5, #1
    str     w5, [x4, #0x10]

    /* Call C handler if stack is valid */
    bl      dumper_on_exception

1:  wfe
    b       1b

    .section .bss
    .align 4
_stack_bottom:
    .skip 0x4000
_stack_top:

