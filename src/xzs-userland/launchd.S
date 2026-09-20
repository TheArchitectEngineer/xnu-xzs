.section __TEXT,__text,regular,pure_instructions
.p2align 4
.globl _start
_start:
    // Preserve the D6-M4/D6-M5 first-instruction signature exactly.
    mov     x0, #1
    adr     x1, msg
    mov     x2, #26
    mov     x16, #4
    svc     #0x80

1:
    // A real, side-effect-free BSD syscall keeps PID1 alive in EL0.
    mov     x16, #20
    svc     #0x80
    b       1b

    // Keep the first-write buffer at the certified 0x100000320 address.
    .space  16
msg:
    .ascii  "[XZS-INIT] launchd entered\n"
