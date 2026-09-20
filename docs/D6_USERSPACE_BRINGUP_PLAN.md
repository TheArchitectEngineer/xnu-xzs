# Phase D6: Userspace Bring-Up Plan (PID 1 & First EL0 Transition)

This document defines the architectural strategy, milestone contracts, strict boundaries, userspace binary specifications, and checkpoint namespace for **Phase D6 (PID 1 / First EL0 Userspace)** on the Sony Xperia XZs (Qualcomm MSM8996).

---

## 1. Architectural Boundary & Separation of Concerns

### Where Phase D5 Ends
- Kernel-mode execution at EL1.
- RAMDisk `md0` initialized and loaded.
- Read-only XZSFS mounted as root filesystem (`/`).
- Global `rootvnode` installed with `VROOT | VDIR`.
- Active namespace verified: `namei("/")`, `namei("/sbin/launchd")`, `namei("/bin/sh")`.
- `devfs` mounted at `/dev`, with `/dev/console` bound to cdev 0:0.
- Hard diagnostic stop before process bootstrap:
  ```text
  PID1_STARTED=no
  EXECVE_ATTEMPTED=no
  EL0_ENTRY_ATTEMPTED=no
  ```

### Where Phase D6 Begins
- Creation and activation of the first BSD process (`initproc` / PID 1), task, and thread.
- Parsing and mapping of the static ARM64 `/sbin/launchd` binary.
- Construction of user address space, page tables, user stack, and register state.
- Controlled exception return (`eret`) to Exception Level 0 (EL0).
- Execution of the first userspace instruction.
- First round-trip system call from EL0 to EL1 and back.
- Stable minimal userspace runtime.

### Strict Scope Boundary: D6 is NOT D7
Phase D6 does **NOT** attempt to establish an interactive user environment. The following are strictly out of scope for Phase D6:
- Interactive shell or terminal line discipline (`termios`, job control, signal handling).
- Dynamic linking (`dyld`), shared caches, or Apple `libSystem.dylib`.
- Apple framework stacks (CoreFoundation, LaunchServices, SpringBoard, UIKit).
- Multi-user authentication, POSIX permissions enforcement, or security sandboxing.

Phase D7 remains the interactive serial shell phase.

---

## 2. Empirical Userspace Binary Specifications

Inspection of the binaries currently embedded within the root filesystem image (`artifacts/builds/xzs-rootfs.img`) establishes the following verified facts:

| Property | `/sbin/launchd` | `/bin/sh` |
| :--- | :--- | :--- |
| **Mach-O Format** | 64-bit ARM64 Executable (`MH_EXECUTE`) | 64-bit ARM64 Executable (`MH_EXECUTE`) |
| **Linking Type** | Static (`MH_NOUNDEFS`, no `dyld`) | Static (`MH_NOUNDEFS`, no `dyld`) |
| **File Size** | 16,472 bytes | 16,472 bytes |
| **Object ID in XZSFS** | 7 | 3 |
| **File Permissions** | `0755` (`-rwxr-xr-x`) | `0755` (`-rwxr-xr-x`) |
| **SHA-256 Hash** | `d4fcc788984745766e9b7e729b79284762e0279639a501c765fb777ef4b34440` | `07ca4e95e1ed2f030745db08acf0bb18b89c44ab3b438521ef6e9cd84c549833` |
| **Load Command Count** | 7 commands (648 bytes) | 7 commands (648 bytes) |
| **`LC_SEGMENT_64 (__PAGEZERO)`** | `vmaddr=0x0, vmsize=0x100000000` (4 GiB) | `vmaddr=0x0, vmsize=0x100000000` (4 GiB) |
| **`LC_SEGMENT_64 (__TEXT)`** | `vmaddr=0x100000000, vmsize=0x4000, prot=r-x` | `vmaddr=0x100000000, vmsize=0x4000, prot=r-x` |
| **Section `__text`** | `addr=0x1000002f0, size=0x4b` | `addr=0x1000002f0, size=0x48` |
| **`LC_SEGMENT_64 (__LINKEDIT)`**| `vmaddr=0x100004000, vmsize=0x4000, prot=r--` | `vmaddr=0x100004000, vmsize=0x4000, prot=r--` |
| **`LC_UNIXTHREAD`** | Initial `PC = 0x1000002f0`, all registers zeroed | Initial `PC = 0x1000002f0`, all registers zeroed |
| **PAC Instructions** | 0 PAC instructions | 0 PAC instructions |

---

## 3. Seven-Milestone Breakdown & Contracts

```text
       ┌───────────┐
       │   D6-M1   │  PID 1 Process / Task / Thread Skeleton
       └─────┬─────┘
             │
             ▼
       ┌───────────┐
       │   D6-M2   │  Minimal Mach-O Loader (Validate & Map `/sbin/launchd`)
       └─────┬─────┘
             │
             ▼
       ┌───────────┐
       │   D6-M3   │  User VM Environment & Initial Stack Construction
       └─────┬─────┘
             │
             ▼
       ┌───────────┐
       │   D6-M4   │  First EL0 Transition (`eret` to Userspace)
       └─────┬─────┘
             │
             ▼
       ┌───────────┐
       │   D6-M5   │  First Syscall Round-Trip (EL0 -> `svc` -> EL1 -> EL0)
       └─────┬─────┘
             │
             ▼
       ┌───────────┐
       │   D6-M6   │  Minimal Stable PID 1 Userspace Runtime
       └─────┬─────┘
             │
             ▼
       ┌───────────┐
       │   D6-M7   │  Final Phase D6 Hardware Regression & Seal
       └───────────┘
```

---

### D6-M1: PID 1 Process / Task / Thread Skeleton
* **Objective**: Allocate and initialize the first BSD process (`initproc` / PID 1), its associated Mach task, and its primary thread.
* **Strict Boundary**: Do NOT parse Mach-O or attempt userspace execution.
* **Expected Telemetry Contract**:
  ```text
  PID1_PROCESS_CREATED=yes
  PID1_TASK_CREATED=yes
  PID1_THREAD_CREATED=yes
  EXECVE_ATTEMPTED=no
  EL0_ENTRY_ATTEMPTED=no
  ```

---

### D6-M2: Minimal Mach-O Loader
* **Objective**: Open `/sbin/launchd` from the mounted root filesystem, parse the Mach-O header, validate load commands, map `__TEXT` and `__LINKEDIT` into the task VM map, and extract the entrypoint address from `LC_UNIXTHREAD`.
* **Strict Boundary**: Kernel mode only. Do NOT transition to EL0.
* **Expected Telemetry Contract**:
  ```text
  LAUNCHD_OPENED=yes
  MACHO_HEADER_VALID=yes
  MACHO_SEGMENTS_MAPPED=yes
  MACHO_ENTRY_RESOLVED=yes
  MACHO_ENTRY_ADDR=0x1000002f0
  EL0_ENTRY_ATTEMPTED=no
  ```

---

### D6-M3: User VM & Initial Stack
* **Objective**: Allocate and map the user stack space (e.g., top of stack below commpage), write initial stack frames (`argc`, `argv`, `envp`), and populate the thread's saved register context (SP, PC, CPSR for EL0).
* **Strict Boundary**: Register preparation only. No exception return yet.
* **Expected Telemetry Contract**:
  ```text
  PID1_USER_VM_READY=yes
  PID1_USER_STACK_READY=yes
  PID1_REGISTER_STATE_READY=yes
  EL0_ENTRY_ATTEMPTED=no
  ```

---

### D6-M4: First EL0 Transition
* **Objective**: Execute the kernel exception return instruction (`eret`) targeting EL0 with user page tables active in TTBR0_EL1, transitioning the core to userspace and executing the first ARM64 instruction at `0x1000002f0`.
* **Strict Boundary**: Execution of at least 1 verified EL0 instruction. Syscall round-trip not yet required.
* **Expected Telemetry Contract**:
  ```text
  EL0_ENTRY_ATTEMPTED=yes
  FIRST_EL0_INSTRUCTION_EXECUTED=yes
  ```

---

### D6-M5: First Syscall Round-Trip
* **Objective**: Userspace code at EL0 issues an ARM64 `svc` instruction; the hardware traps into the XNU EL0 synchronous exception vector; the kernel syscall dispatcher decodes the call, executes a minimal handler, and returns back to EL0 via `eret`.
* **Strict Boundary**: Basic syscall verification (e.g. `getpid()` or minimal write/exit).
* **Expected Telemetry Contract**:
  ```text
  FIRST_SVC_ENTERED=yes
  SYSCALL_DISPATCH_REACHED=yes
  SYSCALL_RETURN_TO_EL0=yes
  ```

---

### D6-M6: Minimal Stable PID 1 Runtime
* **Objective**: `/sbin/launchd` executes a deterministic, self-contained userspace loop or diagnostic sequence without crashing, faulting, or triggering a watchdog panic.
* **Strict Boundary**: Stable userspace execution. No interactive shell required.
* **Expected Telemetry Contract**:
  ```text
  PID1_STARTED=yes
  PID1_STABLE_RUNTIME=yes
  ```

---

### D6-M7: Final Phase D6 Seal
* **Objective**: Full end-to-end regression across all D1–D6 milestones, automated acceptance verifier, evidence preservation, and formal roadmap advancement to Phase D7.
* **Expected Telemetry Contract**:
  ```text
  D6-M1_COMPLETE=yes
  D6-M2_COMPLETE=yes
  D6-M3_COMPLETE=yes
  D6-M4_COMPLETE=yes
  D6-M5_COMPLETE=yes
  D6-M6_COMPLETE=yes
  D6-M7_COMPLETE=yes
  D6_COMPLETE=yes
  D6_SEALED=yes
  ROADMAP_ADVANCED_TO=D7
  ```

---

## 4. Proposed Checkpoint Namespace (Family `0xD600` - `0xD660`)

To maintain clean diagnostic telemetry continuity without colliding with prior phases (`D510`..`D550`), the following checkpoint namespace is reserved for Phase D6:

| Checkpoint Family | Milestone | Diagnostic Subsystem |
| :--- | :--- | :--- |
| `0xD600` | **D6-M1** | PID 1 process/task/thread skeleton allocation |
| `0xD610` | **D6-M2** | `/sbin/launchd` Mach-O header parsing & segment mapping |
| `0xD620` | **D6-M3** | Userspace VM map, user stack & register context setup |
| `0xD630` | **D6-M4** | Exception return (`eret`) to EL0 & first instruction fetch |
| `0xD640` | **D6-M5** | Synchronous exception trap (`svc`) & syscall round-trip |
| `0xD650` | **D6-M6** | Deterministic userspace runtime execution loop |
| `0xD660` | **D6-M7** | Final Phase D6 hardware regression & acceptance seal |
