# Phase D6-M4 First EL0 Transition Blocker Report

## Result

```text
D6-M3_COMPLETE=yes
D6-M4_COMPLETE=no
DEEPEST_D6_M4_HARDWARE_CHECKPOINT=D630/32
CURRENT_STALL_WINDOW=D630/32_to_D630/33
PID1_STARTED=no
EL0_ENTRY_ATTEMPTED=yes
FIRST_EL0_INSTRUCTION_EXECUTED=no
D6_M4_ACCEPTANCE_VERIFIER=FAIL
THREE_FIX_HARD_STOP_REACHED=yes
```

D6-M4 is stopped under the three-fix hard-stop rule. D6-M3 remains sealed and
its full regression prefix passed on every D6-M4 run. D6-M5 was not started.

## Current source and evidence

| Item | Value |
| :--- | :--- |
| Branch | `xzs-d6m4-first-el0` |
| Tested source commit | `b1720788f5ab93abf5a396d38337d3bd9efd583c` |
| Kernel SHA-256 | `39cdb894e5d0e8a3f0d922734ef98d99e329ed4fc2b8f29499dfe7c983c77b4d` |
| Flattened kernel SHA-256 | `e259f39330968327fcbf2ea3c354e96a8836e876673d313b5af9f81b2db6fd50` |
| Boot image SHA-256 | `f952e2f4c7c06defb531c0ef0cc5ee27cd2a3d1042e3bf536da8964db2a90fb7` |
| Console log SHA-256 | `a534a1ca539e92266248772eed95508b3fa103474d0883f10ef9d2469e65351e` |
| Dmesg log SHA-256 | `ac9d52aaddf2f83652e1768b46ddf307d2b47613d9fd4830448ba9767295bfe5` |

Local immutable copies are under
`artifacts/archive/d6m4-blocked-b172078/` and
`artifacts/logs/d6m4-blocked-b172078/`.

## Hardware facts

Every run reached the complete D6-M3 acceptance sequence before entering M4.
The final run reached:

```text
D630/00 -> D630/10 -> D630/20 -> D630/21
-> D630/30 -> D630/31 -> D630/32
```

It did not reach `D630/33`, the first instruction in the target PID1 thread's
`task_wait_to_return()` continuation. It also emitted:

```text
PID1_DIRECT_WAIT_CLEAR=already_awakened
```

That value is the direct `clear_wait(target_thread, THREAD_AWAKENED)` result
classification for `KERN_NOT_WAITING`. Therefore the target thread was no
longer attached to a wait queue, but there is no hardware evidence that its
continuation was scheduled or that exception return began.

No D630 fatal breadcrumb, panic, synchronous EL0 exception, or SVC telemetry
was observed. The independent D6-M4 verifier correctly failed on the missing
`D630/33` through `D630/01` suffix.

## Three materially different fixes

| Fix | Pushed commit | Boot image SHA-256 | Hardware result |
| :--- | :--- | :--- | :--- |
| Return the BSD bootstrap parent instead of holding it in a busy-wait | `921cb60` | `726863fcad12a0b69166e13150fcf60e8836fd4813ccb9a81ab6f1df27e8fea3` | stopped after `D630/31` |
| Release task/thread suspension holds before clearing the native return-wait event | `ef67286` | `b43d3dc768181dccb8d4162c0399fb64a7e1470a3f8b5461d55cd6714d90eda7` | reached `D630/32`, no continuation entry |
| Directly clear the exact PID1 thread's residual creation wait after the native wake | `b172078` | `f952e2f4c7c06defb531c0ef0cc5ee27cd2a3d1042e3bf536da8964db2a90fb7` | `KERN_NOT_WAITING`, no `D630/33` |

A separate pushed diagnostic commit (`f5d5c76`, image
`e3d19c67f069b4967da7e2f3ba94c79231809bf3a69ac955d150943452656e6b`)
instrumented the native return path and proved that execution never entered
`task_wait_to_return()`.

## Precise blocker

```text
PRIMARY_BLOCKER=PID1 target thread is no longer suspended or waiting, but its
                task_wait_to_return continuation is not observed executing
                before the hardware return window.
DEEPEST_PROVEN_OPERATION=task_clear_return_wait completed and direct clear_wait
                         reported KERN_NOT_WAITING.
FIRST_UNPROVEN_OPERATION=target thread scheduled at task_wait_to_return entry.
```

The remaining investigation is scheduler/run-queue/stack-availability state
for this synthetic main thread after wake. That is a source-audit direction,
not a hardware-proven root cause. No claim is made that the scheduler itself is
defective.

## Evidence classification

- D630/00 through D630/32 and `PID1_DIRECT_WAIT_CLEAR=already_awakened`:
  **HARDWARE VERIFIED**.
- Native main-thread creation/wait and return-to-user call graph:
  **SOURCE-AUDITED FACT**.
- Successful kernel builds and zero-PAC scans: **BUILD-VERIFIED FACT**.
- Direct target-thread `clear_wait`: **XZS WORKAROUND**.
- Scheduler/run-queue or missing kernel-stack cause: **INFERENCE/HYPOTHESIS**.
- `PLUS_40S_WATCHDOG_CORRELATION=strong` remains a correlation.
- `APCS_WATCHDOG_CAUSAL=UNPROVEN`; no reset-source evidence was obtained.

## Resume condition

Do not start D6-M5. Resume D6-M4 only after a source audit can expose or repair
the target thread's post-wake run-queue/stack state with a materially new
mechanism, or after additional authoritative scheduler telemetry becomes
available. D6-M3 must remain the certified predecessor.
