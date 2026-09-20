# D6-M4 First EL0 Transition Source Audit

## Baseline

- D6-M3 sealed commit: `51cf6eceac0b3b903caea9d1b95f1cc15cba8f5e`.
- Branch: `xzs-d6m4-first-el0`.
- PID1 saved entry PC: `0x1000002f0`.
- PID1 saved SP: `0x16fdfffb0`.

## Return-to-user path

This tree creates PID1's main thread with `main_thread_create_waiting()` and
continuation `task_wait_to_return()`. The canonical activation sequence in
`bsd_utaskbootstrap()` is `vm_map_setup()`, `ipc_task_enable()`, then
`task_clear_return_wait(TCRW_CLEAR_ALL_WAIT)`. `task_wait_to_return()` ends at
`thread_bootstrap_return()`, which branches to
`arm64_thread_exception_return()` and the `return_to_user` assembly path before
the final exception return to EL0.

D6-M1 added exactly one user thread suspension and one normal task suspension.
D6-M4 enables IPC, releases the task hold with `task_resume_internal()`, and
releases the thread user hold with `thread_resume()`. Only after both holds are
gone does it call `task_clear_return_wait(TCRW_CLEAR_ALL_WAIT)`, matching the
native main-thread wake model without trying to wake a still-suspended thread.
The bootstrap parent then returns normally instead of occupying its context in
a diagnostic busy-wait.

Physical runs through `D630/32` showed that the synthetic PID1 main thread did
not enter its continuation after the event wake. As an explicitly scoped XZS
workaround, M4 follows the native wake with `clear_wait(target_thread,
THREAD_AWAKENED)`. `KERN_SUCCESS` proves it removed the residual creation wait;
`KERN_NOT_WAITING` is also safe and means the native wake won the race. No
unrelated thread is touched.

## Positive first-instruction proof

The static `/sbin/launchd` entry sequence is:

```text
0x1000002f0  mov x0, #1
0x1000002f4  adr x1, 0x100000320
0x1000002f8  mov x2, #0x1a
0x1000002fc  mov x16, #4
0x100000300  svc #0x80
```

The target-specific D6-M4 hook runs at the start of `sleh_synchronous()` before
`handle_svc()`. It accepts only the armed PID1 thread, EL0 origin, and SVC64
class, then requires `ELR=0x100000304`, ISS/SVC immediate `0x80`, `x0=1`,
`x1=0x100000320`, `x2=0x1a`, and `x16=4`. This register signature cannot be
produced merely by the kernel executing `eret`; it proves the known EL0
instructions ran through the first SVC.

The hook captures ESR_EL1, ELR_EL1, FAR_EL1, SPSR_EL1, saved SP_EL0, and CPU.
It deliberately terminates before `handle_svc()`, so syscall dispatch and
return-to-EL0 remain unclaimed and reserved for D6-M5.
