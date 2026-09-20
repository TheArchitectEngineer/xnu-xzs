# Phase D6-M5: First Darwin/XNU Syscall Round-Trip Report

## Result

```text
D6-M4_REGRESSION_PASS=yes
D6-M5_COMPLETE=yes
D6-M5_SEALED=yes
D640_91_REACHED=yes
FIRST_SVC_ENTERED=yes
SYSCALL_DISPATCH_REACHED=yes
SYSCALL_HANDLER_ENTERED=yes
SYSCALL_HANDLER_COMPLETED=yes
SYSCALL_RETURN_TO_EL0=yes
POST_SYSCALL_EL0_INSTRUCTION_EXECUTED=yes
FIRST_SYSCALL_ROUNDTRIP_COMPLETE=yes
D6_M5_ACCEPTANCE_VERIFIER=PASS
ROADMAP_ADVANCED_TO=D6-M6
```

Sony Xperia XZs G8231 hardware executed a real Darwin ARM64 BSD syscall
round-trip. PID1 entered with `svc #0x80`, normal XNU exception handling
classified syscall number 4 from `x16`, `unix_syscall()` selected
`sysent[4]`, the real `write()` handler ran to completion, and normal return
machinery resumed execution in EL0. A second SVC captured the deterministic
post-return register signature.

## Tested Source and Artifacts

| Item | Value |
| :--- | :--- |
| D6-M4 main merge commit | `024d23ac5e0b22d42139b769b557bc8177bdf876` |
| Hardware-tested D6-M5 source commit | `6669437fbe9d4c9a5f33bb87fc1a443a4346b703` |
| Branch | `xzs-d6m5-first-syscall` |
| Kernel SHA-256 | `84d419ffe75c0a7ce8e04be394f40d4e887c16ff629526b0b5b4b5954b94673f` |
| Flattened kernel SHA-256 | `69fb9e4f21ef8d618e42b32d6a82fa8358e390af4682befe8c424ff73f7a6e48` |
| Boot image SHA-256 | `87d2b6c609f3b7348e347d53db7de6655eef849dffe51054c71ba2887098ccac` |
| Raw console log SHA-256 | `dd084c9e3901b86fb4513fc0aadafc3dd7d4ef88917134cd05b09adb49f51747` |
| Raw dmesg log SHA-256 | `7e02a73fbd229b5570391cb99df9c6b947c9bb2235334befe187ccaf2e9aa927` |
| Verifier output SHA-256 | `f34a8ac87e4c831f0c725f60887bdcb3a24c68dcd73994afc7d7241a81545e39` |

Immutable local copies are preserved under `artifacts/archive/d6m5-pass/`
and `artifacts/logs/d6m5-pass/`.

## Source-Audited ABI

```text
DARWIN_ARM64_SVC_IMMEDIATE=0x80
BSD_SYSCALL_NUMBER_REGISTER=x16
BSD_ARGUMENT_REGISTERS=x0-x8 (write uses x0-x2)
BSD_RETURN_REGISTER=x0 (x1 secondary)
BSD_ERROR_CONVENTION=errno in x0, x1=0, carry set
BSD_SYSCALL_DISPATCHER=unix_syscall
MACH_TRAP_DISPATCHER=mach_syscall
```

The full source audit and PID1 descriptor-state proof are recorded in
`docs/D6_M5_FIRST_SYSCALL_SOURCE_AUDIT.md`.

## Selected Syscall and Real Call Path

The frozen launchd image issues:

```text
write(1, 0x100000320, 26)
x16=4
svc #0x80
```

PID1 inherits the zero-initialized `kernproc` descriptor table, so fd 1 is
absent at this milestone. The real handler therefore returns the expected
`EBADF` result. This is intentional and deterministic; D6-M5 proves the
dispatcher/handler/return machinery, not console binding.

```text
SVC_TO_DISPATCH_CALL_PATH=sleh_synchronous -> handle_svc -> unix_syscall
DISPATCH_TO_HANDLER_CALL_PATH=unix_syscall -> sysent[4].sy_call -> write
HANDLER_TO_EL0_RETURN_PATH=write -> arm_prepare_syscall_return -> synchronous exception return -> EL0
SYSCALL_RETURN_VALUE=9
SYSCALL_RETURN_ERROR=EBADF
SYSCALL_RETURN_CARRY=set
```

## Hardware Checkpoint Evidence

The physical trace contains the ordered D6-M5 sequence:

```text
D640/00 -> D640/10 -> D640/20 -> D640/30 -> D640/40
-> D640/50 -> D640/60 -> D640/70 -> D640/90 -> D640/91 -> D640/01
```

- `D640/10`: canonical first SVC64 entered.
- `D640/20`: `handle_svc()` classified trap 4 as BSD.
- `D640/30`: `unix_syscall()` reached.
- `D640/40`: the selected `sysent[4]` handler entered.
- `D640/50`: the real `write()` handler returned `EBADF`.
- `D640/60`: normal synchronous return-to-user path reached.
- `D640/70`: second userspace SVC observed after return to EL0.

## Post-Syscall EL0 Proof

After the first SVC, hardware executed these frozen userspace instructions:

```text
0x100000304  mov x0, #0
0x100000308  mov x16, #1
0x10000030c  svc #0x80
```

The second SVC was intercepted before syscall 1 dispatch and captured:

```text
POST_SYSCALL_EL0_PC=0x0000000100000310
POST_SYSCALL_REGISTER_SIGNATURE=x0:0,x16:1,carry:set
POST_SYSCALL_EL0_INSTRUCTION_EXECUTED=yes
```

`mov` does not modify condition flags, so carry remaining set also preserves
the first syscall's canonical error-return convention across the EL0
instruction sequence.

## Independent Verification

```text
$ scripts/verify_d6m5_acceptance.py artifacts/logs/d6m5-pass/console.log
...
D6_M4_REGRESSION_VERIFIER: PASS
D6_M5_ACCEPTANCE_VERIFIER: PASS
Real sysent[4]/write handler completed and returned to EL0.
```

The D6-M5 verifier invokes the D6-M4 verifier in regression mode, which in
turn runs the complete D6-M3 regression verifier. The historical D6-M4
acceptance mode remains valid for the sealed D6-M4 log.

## Evidence Classification

- HARDWARE VERIFIED: ordered D640 checkpoints, real handler completion,
  `EBADF=9` ABI state, return to EL0, and post-return register signature.
- SOURCE-AUDITED FACT: Darwin ARM64 ABI, dispatcher/table/handler path,
  descriptor inheritance, and error convention.
- BUILD-VERIFIED FACT: ARM64 DEVELOPMENT build passed; zero PAC instructions;
  boot image packaged with the hashes above.
- XZS WORKAROUND: shared telemetry is captured on the user-pmap CPU and
  emitted from CPU0; the second SVC is a terminal observation sentinel.
- PLATFORM FIX: none.
- INFERENCE/HYPOTHESIS: no inference is promoted to hardware fact.

## Milestone Boundary

```text
D6-M5 COMPLETE / SEALED
D6-M6 NOT STARTED
```
