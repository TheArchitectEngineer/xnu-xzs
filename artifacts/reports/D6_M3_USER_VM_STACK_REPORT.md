# Phase D6-M3: PID1 User VM + Initial Stack Acceptance Report

## Result

```text
D6-M3_COMPLETE=yes
D620_91_REACHED=yes
D6_M3_ACCEPTANCE_VERIFIER=PASS
ROADMAP_ADVANCED_TO=D6-M4
```

Sony Xperia XZs G8231 hardware reached the full D620 checkpoint sequence and
returned through the project recovery path. PID1 remained suspended throughout;
no EL0 entry was attempted in this milestone.

## Tested source and artifacts

| Item | Value |
| :--- | :--- |
| Tested source commit | `ab019a3128659b097fd7ef5dd8a9e6f7a541e0b7` |
| Branch | `xzs-d6m3-user-vm-stack` |
| Kernel SHA-256 | `69f374be01204b684f4f79c9a7fb454343f01ed0d42f427d7b06622a4648da06` |
| Flattened kernel SHA-256 | `d5756ff21f4ffb2307602b25f9b4eb15b7422c099f3603304857055985f81ae7` |
| Boot image SHA-256 | `ba26b737ca32426176282bc6a1099e05bb9717ac5bf3a635df9aeee5ae6c8056` |
| Console log SHA-256 | `8be854ddde32056a737305276d3acddc34c6cf1a745562395f7f29c8a5f5af13` |
| Dmesg log SHA-256 | `d7fbcf85954cf33f39f34ff08f6f35ecd7e262e00b286d9b193b32ff5b9fb9b5` |

Local immutable copies are stored under `artifacts/archive/d6m3-pass/` and
`artifacts/logs/d6m3-pass/`.

## Hardware checkpoint evidence

The physical trace contains this ordered D6-M3 sequence:

```text
D620/00 -> D620/10 -> D620/20
-> D620/30 -> D620/31 -> D620/32
-> D620/40 -> D620/41
-> D620/50 -> D620/51 -> D620/52
-> D620/60 -> D620/61
-> D620/70 -> D620/71 -> D620/72
-> D620/90 -> D620/91 -> D620/01
```

The independent verifier completed with exit status 0:

```text
python3 scripts/verify_d6m3_acceptance.py artifacts/logs/d6m3-pass/console.log
D6-M3 ACCEPTANCE VERIFICATION RESULT: PASS
```

## VM and protection contract

All `__TEXT` geometry and protections were read from the Mach-O
`LC_SEGMENT_64` metadata rather than hardcoded into the loader:

```text
TEXT_VMADDR=0x0000000100000000
TEXT_VMSIZE=0x0000000000004000
TEXT_FILEOFF=0x0000000000000000
TEXT_FILESIZE=0x0000000000004000
TEXT_CURRENT_PROT=RX
TEXT_MAX_PROT=RX
TEXT_REQUESTED_FINAL_PROT=RX
```

Hardware verified file-byte CRC32 and readback equality:

```text
USER_TEXT_EXPECTED_CRC32=0x0a100b37
USER_TEXT_MAPPED_CRC32=0x0a100b37
USER_TEXT_CONTENT_VERIFIED=yes
USER_TEXT_WRITABLE_AFTER_FINALIZE=no
```

The bootstrap-time `mach_vm_region_recurse()` readback that did not return on
silicon was replaced by native map inspection under `vm_map_lock_read()` in an
OSFMK-private helper. The final audit verified PAGEZERO has zero overlapping
entries, `__TEXT` is RX, the stack is RW/NX, and there are zero current RWX
mappings.

## Darwin stack and saved register state

The initial frame follows this tree's `exec_copyout_strings()` layout: a
pointer-sized `argc` slot, `argv`, `envp`, Apple-vector terminator, alignment,
and string area.

```text
PID1_ARGC=1
PID1_ARGV0=/sbin/launchd
PID1_ENVC=0
PID1_INITIAL_SP=0x000000016fdfffb0
PID1_INITIAL_SP_ALIGNED=yes
PID1_INITIAL_PC=0x00000001000002f0
PID1_REGISTER_STATE_READY=yes
PID1_REGISTER_STATE_INSTALLED=yes
```

The stack mapping is `[0x16fde0000, 0x16fe00000)`, RW and non-executable.
Both PC and SP were read back from the saved ARM64 user state.

## Controlled failure and fix history

The first pushed implementation run used commit `d222b22` and boot image
`b5a05b02c243a5b982717b7d5ffa7f98366a3fa714a42d65bd12390cfeb39a20`.
It proved D620/32 through D620/50, then failed at D620/51 because the initial SP
and string addresses were missing one hexadecimal digit and therefore lay
outside the mapped stack. Commit `ab019a3` corrected only those addresses and
the associated verifier/source-audit values. The next controlled run passed.

## Boundary and reset classification

```text
PID1_THREAD_REMAINS_SUSPENDED=yes
PID1_TASK_REMAINS_SUSPENDED=yes
PID1_STARTED=no
EL0_ENTRY_ATTEMPTED=no
FIRST_EL0_INSTRUCTION_EXECUTED=no
```

Evidence classification:

- D620/00 through D620/91 and D620/01: **HARDWARE VERIFIED**.
- VM locking, Darwin stack layout, and Mach-O metadata derivation:
  **SOURCE-AUDITED FACT**.
- Kernel build and zero-PAC scan: **BUILD-VERIFIED FACT**.
- `PLUS_40S_WATCHDOG_CORRELATION=strong` remains historical correlation.
- `APCS_WATCHDOG_CAUSAL=UNPROVEN`; this run supplies no authoritative reset
  source and does not upgrade that hypothesis.

## Next milestone

D6-M4 will start from this sealed D6-M3 commit and will prove a real first EL0
instruction with exception telemetry enabled. D6-M4 is not allowed to claim a
complete syscall round trip; that remains D6-M5.
