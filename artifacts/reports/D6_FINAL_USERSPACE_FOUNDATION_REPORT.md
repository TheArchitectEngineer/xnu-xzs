# Phase D6: Final Userspace Foundation Report

## Executive Summary

Phase D6 ("PID 1 / First EL0 Userspace") of the xnu-xzs project is formally **COMPLETE** and **SEALED** on physical Sony Xperia XZs (Qualcomm MSM8996) silicon.

The kernel bootstraps through the read-only XZSFS v1 root filesystem, constructs native Mach/BSD process structures for PID 1 (`/sbin/launchd`), establishes user virtual memory with hard PAGEZERO and non-executable stack, performs ARM64 exception level transition from EL1 to EL0, routes genuine Darwin syscalls (`write`, `getpid`) through canonical kernel dispatchers, establishes standard file descriptors bound to `/dev/console` (cdev 0:0, `VCHR`), emits proven userspace console stdout, and sustains an indefinite EL0 execution loop without faulting.

---

## 1. Complete Hardware-Verified Progression

```text
PID 1 Process/Task/Thread Created (D6-M1)
       │
       ▼
Static ARM64 Mach-O Loaded & Validated (D6-M2)
       │
       ▼
User VM Map Established: PAGEZERO + RX __TEXT + RW/NX Stack (D6-M3)
       │
       ▼
EL1 -> EL0 Transition via eret & First Instruction Executed (D6-M4)
       │
       ▼
SVC #0x80 Captured with Canonical ARM64 Register Signature (D6-M4)
       │
       ▼
Syscall Dispatched through unix_syscall & sysent[4] (write) (D6-M5)
       │
       ▼
Normal Return-to-EL0 with Post-Return Execution (D6-M5)
       │
       ▼
Native fd 0, 1, 2 Bound to /dev/console (cdev 0:0, VCHR) (D6-M6)
       │
       ▼
EL0 write(1) Verified to /dev/console (26 bytes) (D6-M6)
       │
       ▼
Stable Indefinite PID 1 Runtime (sustained getpid loop) (D6-M6)
       │
       ▼
Full 6-Milestone Regression Verification Sweep (D6-M7)
       │
       ▼
PHASE D6 COMPLETE & SEALED
```

---

## 2. Milestone Regression Matrix

| Milestone | Capability | Verifier Output | Hardware Checkpoint Span |
| :--- | :--- | :---: | :---: |
| **D6-M1** | PID 1 BSD process (`initproc`), Mach task, thread, uthread | **PASS** | `D600/00`..`91` |
| **D6-M2** | Mach-O 64-bit loader, segment enumeration, static entrypoint | **PASS** | `D610/00`..`91` |
| **D6-M3** | User VM map, PAGEZERO guard, RX `__TEXT`, RW/NX stack, W^X | **PASS** | `D620/00`..`91` |
| **D6-M4** | First EL0 instruction execution, Strategy A2 AF/UXN, SVC #0x80 | **PASS** | `D630/00`..`91` |
| **D6-M5** | Real BSD syscall dispatch (`unix_syscall`), `sysent[4]`, return | **PASS** | `D640/00`..`91` |
| **D6-M6** | Native `/dev/console` stdio, EL0 `write(1)`, sustained `getpid` loop | **PASS** | `D650/00`..`91` |
| **D6-M7** | Full regression sweep, independent verifier, roadmap seal | **PASS** | `D650/01` (terminal) |

```text
D6_M1_REGRESSION_VERIFIER=PASS
D6_M2_REGRESSION_VERIFIER=PASS
D6_M3_REGRESSION_VERIFIER=PASS
D6_M4_REGRESSION_VERIFIER=PASS
D6_M5_REGRESSION_VERIFIER=PASS
D6_M6_REGRESSION_VERIFIER=PASS

D6_FINAL_ACCEPTANCE_VERIFIER=PASS
D6_COMPLETE=yes
D6_SEALED=yes
```

---

## 3. Physical Hardware Evidence Archive

| Parameter | Value |
| :--- | :--- |
| **Target Silicon** | Sony Xperia XZs (Model G8231, Platform Tone, Board Keyaki) |
| **SoC / CPU** | Qualcomm Snapdragon 820 (MSM8996 Pro / 4x Kryo ARMv8.0-A) |
| **Tested Source Commit** | `f5dd7a36bfedfbcf68d7a6bf2a95d4d09dab5ac0` |
| **Kernel Binary SHA-256** | `6c94a0fc37468e425288e5c3e0331b82c2507ba1b20026417f69daff681e96cf` |
| **Boot Image SHA-256** | `806d2c5d0f791e8e24a53f2ddd002a898cdcde7fa83cb752189e3d0ab0428129` |
| **Raw Console Log SHA-256** | `6b5ce071ef9387e4d490b71968e07a30db1566d641a5f85e55f57b0a57e6889f` |
| **Log File Location** | `artifacts/logs/xnu-console-extracted.log` |
| **Verifier Tool** | `scripts/verify_d6_acceptance.py` |
| **Automated Return** | +4s automated warm reboot to Fastboot |
| **Fatal Checkpoints** | 0 detected (zero ERR >= 0xEE00) |
| **Unexpected Exceptions** | 0 detected |

---

## 4. Evidence Classification Taxonomy

### HARDWARE VERIFIED
* **4-Core Mach SMP**: All 4 Kryo cores online in `pset0` via PSCI `CPU_ON`; IPI reschedule and preemption operational.
* **VFS Rootfs**: Read-only XZSFS v1 mounted at `/` from RAMDisk (`rd=md0`).
* **devfs Overlay**: Character device `/dev/console` (cdev 0:0, `VCHR`).
* **PID 1 Creation & VM**: Task, thread, and VM map; hard PAGEZERO (`0x0..0x100000000`), RX `__TEXT` (`0x100000000`), RW/NX stack (`0x16fde0000..0x16fe00000`).
* **W^X Enforcement**: Zero unexpected RWX mappings (`PID1_VM_UNEXPECTED_RWX_COUNT=0`).
* **EL0 Execution**: Hardware execution of user instructions at `PC=0x1000002f0`.
* **Syscall Round-Trip**: `svc #0x80` exception entry -> `unix_syscall` -> `sysent[4]` handler -> `arm_prepare_syscall_return` -> resumption at `ELR=0x10000030c`.
* **Native Console Descriptors**: PID 1 holds fd 0, 1, 2 allocated via native `open1()` in `struct proc.p_fd`.
* **Console Stdout**: Real EL0 `write(1)` emitting 26 bytes (`[XZS-INIT] launchd entered\n`).
* **Stable Userland Loop**: Sustained consecutive successful `getpid` round-trips in EL0.

### SOURCE AUDITED
* File descriptor table allocation and credential lifecycle in `mach_loader.c` (`xzs_d6m6_setup_console_stdio`).
* Exception vector dispatch in `sleh.c` (`handle_svc`) and argument extraction in `systemcalls.c`.
* Line discipline and tty forwarding path (`kmwrite -> kmstart -> kmoutput -> console_write -> UARTDM TX`).
* Stubbed status of UARTDM RX in `pe_serial.c` (`msm_uart_receive_ready()` and `msm_uart_receive_data()` returning 0).

### KNOWN LIMITATIONS AT D6 CLOSE
1. **UARTDM RX Not Implemented**: Qualcomm MSM8996 UARTDM RX is currently unmapped (`PHYSICAL_CONSOLE_RX_AVAILABLE=no`). No keystrokes can be received from the serial console.
2. **Interactive Stdin Not Hardware Verified**: Interactive userland input is blocked on UARTDM RX bring-up (Phase D7 target).
3. **Interactive Shell Not Yet Implemented**: Userspace currently executes `/sbin/launchd` directly; `/bin/sh` interactive REPL is the target of Phase D7.
4. **Physical Display Uninitialized**: Xperia 1080x1920 LCD is dark under XNU; framebuffer text console will be brought up in Phase D8.

### PLATFORM WORKAROUNDS
* **`3e417bb` (`devfs_getattr` pointer-hardening bypass)**:
  - **Classification**: `XZS PLATFORM WORKAROUND / BRING-UP COMPATIBILITY FIX`.
  - **Nature**: Returns static non-pointer fsid `(uint32_t)0x64657666` (`"devf"`) to bypass `vm_kernel_addrhash()` SHA-256 stall on MSM8996.
  - **Migration Note**: Must be re-audited under Phase D9 (`XZSPlatform`) to prevent carrying bring-up exceptions as permanent generic XNU-core behavior.
