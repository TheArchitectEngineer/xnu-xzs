# Phase D7-M2: PID1 -> /bin/sh EL0 Handoff Report

## Executive Summary

Phase D7-M2 is **COMPLETE**, **SEALED**, and **VERIFIED** on physical Sony Xperia XZs (`G8231`, `MSM8996`) hardware.

The running PID1 thread (`proc 1`, `task_t`, native thread) successfully transformed its userspace image from the bootstrap loop into `/bin/sh`, loaded from the on-flash XZSFS root filesystem, preserved all kernel identity and `/dev/console` stdio file descriptors (fd 0, 1, 2), installed shell entry state (`PC=0x1000002f0`, `SP=0x16fdfffb0`), returned to EL0 via native XNU exception return dispatch, and executed real shell instructions at EL0.

Real shell execution was irrefutably proven through:
1. Shell EL0 execution of `SYS_write(1, "XZS: /bin/sh EL0 online\n", 24)` returning 24 with carry clear.
2. Return to userspace at EL0 PC `0x100000308` and execution of subsequent shell instruction sequence.
3. EL0 execution of `SYS_exit(0)` (`x0=0`, `x16=1`) at EL0 PC `0x100000310`.

---

## Authoritative Acceptance Telemetry

```text
=======================================================
=== D7-M2 ACCEPTANCE TELEMETRY BEGIN ===
D6_REGRESSION_VERIFIER=PASS
PID1_IDENTITY_PRESERVED=yes
PID1_PROC_PRESERVED=yes
PID1_TASK_PRESERVED=yes
PID1_THREAD_CONTEXT_VALID=yes
PID1_FD0_PRESERVED=yes
PID1_FD1_PRESERVED=yes
PID1_FD2_PRESERVED=yes
OLD_IMAGE_VM_START=0x0000000100000000
OLD_IMAGE_VM_END=0x0000000100004000
OLD_IMAGE_RANGE_VERIFIED=yes
SHELL_OLD_IMAGE_REMOVED=yes
OLD_TEXT_MAPPING_PRESENT=no
SHELL_IMAGE_IDENTITY_VERIFIED=yes
SHELL_IMAGE_LOADED=yes
SHELL_STATIC=yes
SHELL_DYLD_REQUIRED=no
DYNAMIC_LIBRARY_DEPENDENCY_COUNT=0
SHELL_TEXT_MAPPED=yes
SHELL_TEXT_CONTENT_VERIFIED=yes
SHELL_TEXT_PROTECTION=RX
SHELL_TEXT_WRITABLE=no
SHELL_TEXT_CRC_MATCH=yes
SHELL_STACK_REINITIALIZED=yes
SHELL_STACK_READY=yes
SHELL_STACK_PROTECTION=RW
SHELL_STACK_EXECUTABLE=no
SHELL_ARGC=1
SHELL_ARGV0=/bin/sh
SHELL_INITIAL_SP=0x000000016fdfffb0
SHELL_INITIAL_SP_ALIGNED=yes
SHELL_INITIAL_PC=0x00000001000002f0
SHELL_INITIAL_PC_VALID=yes
SHELL_INITIAL_SP_VALID=yes
SHELL_REGISTER_STATE_READY=yes
SHELL_VM_MAP_VALID=yes
SHELL_VM_UNEXPECTED_RWX_COUNT=0
SHELL_EL0_EXEC_PERMISSION_CORRECT_BEFORE_ERET=yes
NATIVE_EXCEPTION_RETURN_REUSED=yes
SHELL_EL0_ENTRY_ATTEMPTED=yes
SHELL_FIRST_EL0_INSTRUCTION_EXECUTED=yes
SHELL_EXECUTION_PROOF=shell-specific write syscall signature
SHELL_EXECUTION_SIGNATURE_VALID=yes
SHELL_WRITE_SYSCALL_ENTERED=yes
SHELL_WRITE_SYSCALL_HANDLER_COMPLETED=yes
SHELL_WRITE_RETURN_VALUE=24
SHELL_WRITE_RETURN_ERROR=0
SHELL_WRITE_RETURN_TO_EL0=yes
SHELL_POST_WRITE_EL0_INSTRUCTION_EXECUTED=yes
SHELL_EXIT_SVC_OBSERVED=yes
SHELL_RUNNING_IN_EL0=yes
SHELL_STDIN_WORKING=no
UARTDM_RX_AVAILABLE=no
SHELL_INTERACTIVE=no
D7-M2_COMPLETE=yes
ROADMAP_ADVANCED_TO=D7-M3
=== D7-M2 ACCEPTANCE TELEMETRY END ===
=======================================================
```

---

## Canonical Checkpoint Verification

Every checkpoint in the canonical D710 sequence executed and was verified in strict monotonic order without fatal traps or unexpected exceptions:

| Checkpoint | Status | Description |
| :--- | :--- | :--- |
| `D710/00` | **PASS** | Enter D7-M2 PID1 shell handoff |
| `D710/10` | **PASS** | PID1 identity verified (`proc != NULL`, `pid == 1`, `task != kernel_task`, `map != kernel_map`) |
| `D710/11` | **PASS** | PID1 fd 0 -> `/dev/console` verified (`VCHR 0:0`, `FREAD\|FWRITE`) |
| `D710/12` | **PASS** | PID1 fd 1 -> `/dev/console` verified (`VCHR 0:0`, `FREAD\|FWRITE`) |
| `D710/13` | **PASS** | PID1 fd 2 -> `/dev/console` verified (`VCHR 0:0`, `FREAD\|FWRITE`) |
| `D710/20` | **PASS** | Begin old image transition |
| `D710/21` | **PASS** | Old image range verified (`0x100000000..0x100004000`) |
| `D710/22` | **PASS** | Old `__TEXT` removed via `mach_vm_deallocate`; hard pagezero guard intact |
| `D710/30` | **PASS** | Resolve `/bin/sh` vnode via `namei`, verified `VREG` and `size == 16472` |
| `D710/31` | **PASS** | Validate ARM64 Mach-O header (`MH_MAGIC_64`, `CPU_TYPE_ARM64`, `MH_EXECUTE`) |
| `D710/32` | **PASS** | Validate static/no-dyld contract (`LC_LOAD_DYLINKER absent`, `dylib count == 0`, `entry == 0x1000002f0`) |
| `D710/40` | **PASS** | Allocate shell `__TEXT` (`mach_vm_allocate_kernel` at `0x100000000`, 16 KB) |
| `D710/41` | **PASS** | Copy shell payload from vnode via `vn_rdwr` and write to user map via `vm_map_write_user` |
| `D710/42` | **PASS** | Verify shell payload identity via dual CRC32 and byte-for-byte `memcmp` |
| `D710/43` | **PASS** | Finalize shell text `RX` (`vm_map_protect` current & max to `VM_PROT_READ \| VM_PROT_EXECUTE`) |
| `D710/50` | **PASS** | Reinitialize user stack (`0x16fde0000..0x16fe00000`, `RW/NX`) |
| `D710/51` | **PASS** | Construct canonical initial stack frame: `argc=1`, `argv[0]="/bin/sh"`, `envp=NULL`, `apple=NULL` |
| `D710/52` | **PASS** | Verify SP alignment (`initial_sp = 0x16fdfffb0`, 16-byte aligned) |
| `D710/60` | **PASS** | Install shell `PC=0x1000002f0`, `SP=0x16fdfffb0`, `CPSR=0` into `arm_saved_state64_t`; clear ASTs |
| `D710/70` | **PASS** | Complete shell VM map audit: single `__TEXT` (RX), single stack (RW), zero unexpected RWX |
| `D710/71` | **PASS** | Leaf PTE audit before promotion |
| `D710/72` | **PASS** | Shell leaf PTE promotion (`UXN=0`, `AF=1`) |
| `D710/73` | **PASS** | Zero unexpected RWX mappings re-verified post-promotion |
| `D710/80` | **PASS** | Native return toward EL0 via `fleh_synchronous -> exception_return_dispatch -> return_to_user -> eret` |
| `D710/90` | **PASS** | Real shell EL0 execution proven via observed write and exit syscall trap signatures |
| `D710/91` | **PASS** | Phase D7-M2 complete and verified |
| `D710/01` | **PASS** | Terminal before D7-M3 |

---

## Architecture Decisions & Safety Constraints

1. **Same-Thread Execution Model (`PID1_IMAGE_RELOAD_OWNER=same PID1 thread`)**:
   - As mandated by the architecture audit, image reloading is executed strictly on CPU 1 by the PID1 thread itself upon entering kernel space via SVC from userspace.
   - Remote VM reloading (`REMOTE_VM_RELOAD_USED=no`), cross-CPU saved state mutation (`REMOTE_SAVED_STATE_MUTATION_USED=no`), and thread parking (`CPU_HARD_PARK_USED=no`) were completely eliminated.
2. **Interrupt Context & Lock Safety**:
   - `ml_set_interrupts_enabled(TRUE)` is invoked immediately before starting the transition, ensuring all `vm_map` rwlocks, memory allocations, and vnode I/O execute in legal, preemptible/interruptible thread context.
3. **Demand Paging Preservation (`is_user` guard)**:
   - Sleh synchronous exception probe was audited and restricted to userspace exceptions (`if (is_user && xzs_d6m4_probe_armed && ...)`).
   - This prevents kernel-mode page faults (such as `copyout` allocating resident pages during `vm_map_write_user`) from being trapped by userspace test probes, enabling native on-demand memory virtualization.
4. **Scope Boundaries Preserved**:
   - Physical display is not initialized or required.
   - D7-M3 banner/prompt formatting was NOT started.
   - D7-M4 UART RX was NOT started.
   - Full backward compatibility with D6 established: `D6_REGRESSION_VERIFIER=PASS` (all milestones D6-M1 through D6-M6 100% passing).

---

## Hardware Test Artifacts

| Item | Value |
| :--- | :--- |
| Device Serial | `BH905SX976` (Sony Xperia XZs, G8231) |
| SoC | Qualcomm Snapdragon 820 (`MSM8996`) |
| Git Branch | `xzs-d7m2-shell-entry` |
| Test Commit | `694446c0af85095e3045fb3d15bd12215aea203e` |
| Boot Image SHA256 | `a78af05bbd39e2c93a04d3b3384d885a83005e40b25dd6cb5b7140795c71e24e` |
| Console Log | `artifacts/logs/d7m2-pass/xnu-console-extracted.log` |
| Dmesg Log | `artifacts/logs/d7m2-pass/xnu-dmesg-extracted.log` |
| TZDBG Status | `artifacts/logs/d7m2-pass/tzdbg-status.log` |
| Verifier Script | `scripts/verify_d7m2_acceptance.py` |
| D6 Full Regression Verifier | **100% PASS** (`python3 scripts/verify_d6_acceptance.py`) |
| D7-M2 Acceptance Verifier | **100% PASS** (`python3 scripts/verify_d7m2_acceptance.py`) |
