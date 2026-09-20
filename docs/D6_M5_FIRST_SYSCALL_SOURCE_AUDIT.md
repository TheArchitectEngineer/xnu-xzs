# D6-M5 Darwin ARM64 First-Syscall Source Audit

Status: source-audited against the D6-M5 branch based on main commit
`024d23ac5e0b22d42139b769b557bc8177bdf876`.

## ABI contract

```text
DARWIN_ARM64_SVC_IMMEDIATE=0x80
BSD_SYSCALL_NUMBER_REGISTER=x16
BSD_ARGUMENT_REGISTERS=x0-x8 (selected direct syscall uses x0-x2)
BSD_RETURN_REGISTER=x0 (x1 is the secondary return register)
BSD_ERROR_CONVENTION=errno in x0, x1=0, carry set; success clears carry
BSD_SYSCALL_DISPATCHER=unix_syscall
MACH_TRAP_DISPATCHER=mach_syscall
```

`sleh_synchronous()` classifies `ESR_EC_SVC_64` and calls `handle_svc()`.
`handle_svc()` reads the trap number through `get_saved_state_svc_number()`;
on ARM64 that accessor reads `x16`. Negative trap numbers enter
`mach_syscall()`. Non-negative numbers enter `unix_syscall()`.

For direct BSD syscalls, `arm_get_syscall_number()` uses non-zero `x16` and
`arm_get_syscall_args()` copies arguments from the saved general registers.
`unix_syscall()` selects `sysent[code]`, calls its real `sy_call`, and then
uses `arm_prepare_syscall_return()`. The synchronous exception path returns
through the normal ARM64 exception-return machinery; D6-M5 does not advance
ELR or synthesize a return value.

```text
SVC_TO_BSD_CALL_PATH=sleh_synchronous -> handle_svc -> unix_syscall
DISPATCH_TO_HANDLER_CALL_PATH=unix_syscall -> sysent[4].sy_call -> write
BSD_RETURN_TO_EL0_CALL_PATH=write return -> arm_prepare_syscall_return -> sleh_synchronous return -> ARM64 exception return -> EL0
```

## Selected syscall

`bsd/kern/syscalls.master` defines syscall 4 as:

```text
write(int fd, user_addr_t cbuf, user_size_t nbyte)
```

The handler is `write()` in `bsd/kern/sys_generic.c`. It enters
`write_nocancel()` and the ordinary file-descriptor write path.

```text
SYSCALL_4_NAME=write
SYSCALL_4_HANDLER=write
D6M5_SELECTED_SYSCALL_NUMBER=4
D6M5_SELECTED_SYSCALL_NAME=write
D6M5_SELECTED_SYSCALL_REASON=already hardware-proven EL0 sequence; real shell-relevant handler; deterministic non-mutating EBADF result while fd 1 is absent
```

## PID1 descriptor state

`bsd_init()` initializes `kernproc` with `fdt_init()`. That routine initializes
locks only; the zero-initialized kernel process has no open descriptors. The
synthetic PID1 is created with `cloneproc(..., kernproc,
CLONEPROC_INITPROC)`. `forkproc()` calls `fdt_fork()`, which allocates the
child table and copies only non-null parent entries. No source path between
those operations binds descriptor 1 to `/dev/console`.

Therefore the first real `write(1, 0x100000320, 26)` reaches `write()` and
deterministically returns `EBADF` (9). That error is an intended ABI result,
not a fake success and not a failure of the round-trip.

```text
PID1_FD1_EXISTS=no
PID1_FD1_TARGET=none
PID1_FD1_READY_FOR_WRITE=no
EXPECTED_SYSCALL_RETURN=EBADF(9), carry set
```

## Post-return EL0 proof

The frozen static launchd sequence already contains, after its first SVC:

```text
0x100000304  mov x0, #0
0x100000308  mov x16, #1
0x10000030c  svc #0x80
```

The second SVC is intercepted as an observation sentinel before syscall 1 can
dispatch. It is valid only after `write()` completed and the normal first-SVC
return state was prepared. Observing its architectural return PC
`0x100000310`, `x0=0`, `x16=1`, with carry still set proves that the first SVC
returned to EL0 and all three post-syscall instructions executed. The kernel
does not emulate those instructions or manufacture their register signature.

```text
POST_SYSCALL_EL0_PC=0x100000310
POST_SYSCALL_REGISTER_SIGNATURE=x0:0,x16:1,carry:set
SECOND_SYSCALL_DISPATCHED=no
```

## Classification

- SOURCE-AUDITED FACT: the ABI, syscall table, handler path, descriptor
  inheritance, and return convention above.
- XZS WORKAROUND: telemetry is written to a shared structure on the user-pmap
  CPU and printed by CPU0; the second SVC is a terminal observation sentinel.
- PLATFORM FIX: none.
- INFERENCE/HYPOTHESIS: none is promoted to hardware fact. The EBADF result
  and post-return signature still require silicon evidence.
