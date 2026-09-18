/*
 * start.S - AArch64 entry point for Xperia XZs boot shim
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
    .quad   0x00020000

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

    /* Offset 0x3C: res5 / PE offset (4 bytes) */
    .long   0x00000040

    /* Offset 0x40: Real entry point */
_real_start:
    /* Disable interrupts (FIQ, IRQ, SError, Debug) */
    msr     daifset, #0xf

    /* Clear SCTLR_EL1.A (Alignment check enable bit 1) */
    mrs     x1, sctlr_el1
    bic     x1, x1, #(1 << 1)
    msr     sctlr_el1, x1
    isb

    /* Save boot arguments passed by S1 Bootloader:
     * x0: Device Tree Blob (DTB) physical address
     * x1: 0
     * x2: 0
     * x3: 0
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

    /* Install early bootshim exception vector table */
    adr     x0, bootshim_vectors
    msr     vbar_el1, x0
    isb

    /* Call C main: bootshim_main(dtb_phys, current_el, mpidr) */
    mov     x0, x19
    mov     x1, x20
    mov     x2, x21
    bl      bootshim_main

    /* If bootshim_main returns, spin */
dead_loop:
    wfe
    b       dead_loop

    .align 11
bootshim_vectors:
    /* Current EL with SP0 */
    .align 7
    b       bootshim_exc_entry
    .align 7
    b       bootshim_exc_entry
    .align 7
    b       bootshim_exc_entry
    .align 7
    b       bootshim_exc_entry

    /* Current EL with SPx */
    .align 7
    b       bootshim_exc_entry
    .align 7
    b       bootshim_exc_entry
    .align 7
    b       bootshim_exc_entry
    .align 7
    b       bootshim_exc_entry

    /* Lower EL using AArch64 */
    .align 7
    b       bootshim_exc_entry
    .align 7
    b       bootshim_exc_entry
    .align 7
    b       bootshim_exc_entry
    .align 7
    b       bootshim_exc_entry

    /* Lower EL using AArch32 */
    .align 7
    b       bootshim_exc_entry
    .align 7
    b       bootshim_exc_entry
    .align 7
    b       bootshim_exc_entry
    .align 7
    b       bootshim_exc_entry

bootshim_exc_entry:
    /* Read exception state registers */
    mrs     x0, esr_el1
    mrs     x1, elr_el1
    mrs     x2, far_el1
    mrs     x3, spsr_el1

    /* Store into persistent DRAM buffer 0x80060000 (g_dlog) */
    ldr     x4, =0x80060000
    str     x0, [x4, #0x20]     /* last_esr */
    str     x1, [x4, #0x18]     /* last_elr */
    str     x2, [x4, #0x28]     /* last_far */
    str     x3, [x4, #0x30]     /* last_spsr */

    /* Increment exception_count */
    ldr     w5, [x4, #0x10]
    add     w5, w5, #1
    str     w5, [x4, #0x10]

    /* Call C function to format and log to pstore and UART */
    bl      bootshim_on_exception

    /* Set restart reason to Fastboot (0x77665500 matches Linux "bootloader") */
    ldr     x6, =0x066bf65c
    ldr     w7, =0x77665500
    str     w7, [x6]
    dsb     sy
    isb     sy

    /* Call PSCI SYSTEM_RESET via SMC #0 (function ID 0x84000009) */
    ldr     x0, =0x84000009
    smc     #0

    /* Fallback: Trigger Qualcomm APCS Watchdog Bite */
    ldr     x6, =0x09830014
    mov     w7, #1
    str     w7, [x6]
    ldr     x6, =0x09830004
    str     w7, [x6]
    dsb     sy
    isb     sy

    /* Fallback: Pull PSHOLD low */
    ldr     x6, =0x004ab000
    str     wzr, [x6]
    dsb     sy
    isb     sy

1:  wfe
    b       1b

    .section .bss
    .align 4
_stack_bottom:
    .skip 0x4000
_stack_top:
    .section .text

.globl jump_to_xnu
jump_to_xnu:
    /* x0: pointer to struct boot_args (physical)
     * x1: entry point address in RAM (0x8258c480)
     */
    mov     x19, x0
    mov     x20, x1

    /* Invalidate instruction cache */
    ic      iallu
    dsb     sy
    isb     sy

    /* Set up x0 = boot_args and jump */
    mov     x0, x19
    br      x20
