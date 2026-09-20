# Phase D6-M4: First EL0 Transition Acceptance Report

## Result

```text
D6-M4_COMPLETE=yes
D630_91_REACHED=yes
D6_M3_REGRESSION_VERIFIER=PASS
D6_M4_ACCEPTANCE_VERIFIER=PASS
FIRST_REAL_EL0_INSTRUCTION_HARDWARE_VERIFIED=yes
CANONICAL_SVC64_SIGNATURE_HARDWARE_VERIFIED=yes
SYSCALL_DISPATCH_REACHED=no
ROADMAP_ADVANCED_TO=D6-M5
```

Sony Xperia XZs G8231 hardware reached the full D630 checkpoint sequence, successfully entered EL0 from EL1, executed the canonical 5-instruction `/sbin/launchd` preamble in userspace, and trapped back into EL1 via `svc #0x80` before normal syscall dispatch.

## Tested Source and Artifacts

| Item | Value |
| :--- | :--- |
| Tested source commit | `4bff94a46a01558746a04097d94c12ac51c27c2d` |
| Branch | `xzs-d6m4-first-el0` |
| Kernel (`mach_kernel`) SHA-256 | `fdb618acc7a09ba638ccd2847df0e5e3590781b575e38ea3e642ec099f22bc6c` |
| Flattened kernel SHA-256 | `a2d3acf9b05435df1e9c485cc1ddd19600812c9d9db0125254e3a55aa5a709fb` |
| vmapple kernel SHA-256 | `53f7728b66bcdcbce78589a5f571290eb4af52791c4a5717db746df7a36e0d7f` |
| Boot image (`xzs-xnu-boot.img`) SHA-256 | `0a49fd191645b829fe356e777af84ef7009be875c076ee7f03c61c8f4815a0a0` |
| Raw console log (`console.log`) SHA-256 | `a9ca1f7b7d2a20bb81697497bd5af0eb7e3476a6f309f994bdf10cb138da1d1e` |
| Raw dmesg log (`dmesg.log`) SHA-256 | `b7ee309c0f6c82d442172d08aa1c9ce93cc70721f213e76ae85103522494b439` |

Immutable archival copies are preserved under `artifacts/archive/d6m4-pass/` and `artifacts/logs/d6m4-pass/`.

## Hardware Checkpoint Evidence

The physical hardware trace contains this ordered D6-M4 sequence:

```text
D630/00 -> D630/10 -> D630/20 -> D630/21
-> D630/30 -> D630/31 -> D630/32 -> D630/33
-> D630/34 -> D630/35 -> D630/36 -> D630/37
-> D630/40 -> D630/41 -> D630/50
-> D630/90 -> D630/91 -> D630/01
```

Checkpoints `D630/33` through `D630/37` prove the full native return-to-user path:
`task_wait_to_return` -> `thread_bootstrap_return` -> `arm64_thread_exception_return` -> `return_to_user` -> `eret`.

## Three-State Descriptor Transition (Strategy A2)

### State A: BEFORE
```text
TEXT_L3_PTE_BEFORE=0x00600000843a4ac3
VALID=1, AF=0, AP=RORO, SH=0b10, nG=1, PXN=1, UXN=1
L1_UXN_TABLE=0, L2_UXN_TABLE=0, PARENT_EL0_EXEC_BLOCKED=no
PP_ATTR_REFERENCED=0, PP_ATTR_REFFAULT=1
```

### State B: AFTER_NATIVE_AF
After `pmap_protect_options(pmap, text_va, text_end, VM_PROT_READ|VM_PROT_EXECUTE, PMAP_OPTIONS_PROTECT_IMMEDIATE, NULL)`:
```text
TEXT_L3_PTE_AFTER_NATIVE_AF=0x00600000843a4ec3
VALID=1, AF=1, AP=RORO, SH=0b10, nG=1, PXN=1, UXN=1
Delta vs State A: 0x0000000000000400 (strictly ARM_PTE_AF)
NATIVE_AF_DELTA_ONLY_AF=yes
PP_ATTR_REFERENCED=1, PP_ATTR_REFFAULT=0
```

### State C: FINAL
After scoped `xzs_promote_launchd_text_exec(pmap, text_va, text_sz)`:
```text
TEXT_L3_PTE_FINAL=0x00200000843a4ec3
VALID=1, AF=1, AP=RORO, SH=0b10, nG=1, PXN=1, UXN=0
Delta vs State B: 0x0040000000000000 (strictly ARM_PTE_NX)
XZS_EXEC_DELTA_ONLY_UXN=yes
TOTAL_BOOTSTRAP_DELTA_AF_AND_UXN=yes
UNRELATED_PTE_BITS_UNCHANGED=yes
```

## EL0 Execution & Canonical SVC64 Signature

PID1 entered EL0 at entrypoint `0x1000002f0` and executed the canonical preamble:
```text
0x1000002f0: mov x0, #1
0x1000002f4: adr x1, 0x100000320
0x1000002f8: mov x2, #0x1a
0x1000002fc: mov x16, #4
0x100000300: svc #0x80
```

Hardware-captured state at trap checkpoint `D630/40` / `D630/41`:
```text
ESR_EL1=0x0000000056000080  (EC=0x15 SVC64, ISS=0x80)
ELR_EL1=0x0000000100000304  (PC immediately following svc #0x80)
FAR_EL1=0x0000000000000000
SPSR_EL1=0x0000000000000000 (EL0t, 64-bit user)
SP_EL0=0x000000016fdfffb0
x0=0x0000000000000001
x1=0x0000000100000320
x2=0x000000000000001a
x16=0x0000000000000004
SVC_IMMEDIATE=0x80
SVC_SYSCALL_NUMBER=4 (SYS_write)
EL0_REGISTER_SIGNATURE_VALID=yes
```

## D6-M4 / D6-M5 Phase Boundary

Boundary strictly preserved before syscall dispatch:
```text
SYSCALL_DISPATCH_REACHED=no
FIRST_SYSCALL_ROUNDTRIP_COMPLETE=no
SYSCALL_HANDLER_COMPLETED=no
SYSCALL_RETURN_TO_EL0=no
```

## Independent Acceptance Verifier

```text
$ python3 scripts/verify_d6m4_acceptance.py artifacts/logs/xnu-console-extracted.log
...
============================================================
D6_M3_REGRESSION_VERIFIER: PASS
D6_M4_ACCEPTANCE_VERIFIER: PASS
Known launchd instructions executed in EL0; first SVC captured before dispatch.
============================================================
```
