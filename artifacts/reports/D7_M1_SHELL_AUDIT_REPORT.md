# XNU-XZS D7-M1: Shell Artifact & Dependency Audit Report

**Phase:** D7 (Interactive EL0 Shell)
**Milestone:** D7-M1 (Shell Artifact & Dependency Audit)
**Status:** COMPLETE / AUDITED
**Baseline Git Commit:** `25e291cac2d40aed1c2e02e26dfb7b8242d1ada4`
**Sealed Prior Phase:** D6 (tag `xzs-d6-userspace-complete`, commit `f57fee97e72d05b6ecee2563b8edb21ba283315a`)

---

## 1. Executive Summary

Phase D7 introduces the first interactive EL0 shell on the Sony Xperia XZs (`MSM8996`), operating **Headless First** over the Qualcomm BLSP2 UARTDM serial console (`/dev/console`).

Milestone D7-M1 conducted a comprehensive static and binary audit of `/bin/sh` as packaged in the frozen XZSFS v1 rootfs, audited its Mach-O structure, entry ABI, stdio expectations, syscall requirements, and hardware UART input dependency.

### Key Audit Findings:
1. **Binary Identity & Origin:** `/bin/sh` is a **72-byte static ARM64 Mach-O stub** originally committed in D5-M1 (`b8491f1385dc54f395bd4d1acc8ff5cd4cefd421`). It prints `[XZS-SH] micro-sh ready\n` using BSD `write(1)` (syscall 4) and exits using BSD `exit(0)` (syscall 1). It is **not** an Apple Darwin shell (`zsh`/`bash`) nor a ported BSD shell (`ash`/`dash`).
2. **Static vs Dynamic:** `/bin/sh` is **100% STATIC**. It has no `LC_LOAD_DYLINKER`, zero `LC_LOAD_DYLIB` commands, and requires **no dyld runtime**.
3. **Entry ABI Compatibility:** It does not touch `sp` or register arguments (`argc`, `argv`, `envp`). The Darwin initial user stack frame constructed in D6-M3 (`0x16FDFFFB0`) is **100% compatible** and reusable.
4. **Stdio Inheritance:** It directly inherits `fd 0`, `fd 1`, and `fd 2` opened to `/dev/console` in D6-M6. It requires **no TTY ioctl or termios configuration** for basic serial stream I/O.
5. **UART RX Hardware Gap:** The kernel console driver `src/xnu/pexpert/arm/pe_serial.c` has `msm_uart_receive_ready()` and `msm_uart_receive_data()` stubbed to return 0. The physical MMIO registers for Qualcomm UARTDM v1.4 RX have been identified (`MSM_UART_SR` `0x0008`, `UARTDM_RF` `0x0140`, `UARTDM_RX_TOTAL_SNAP` `0x00BC`). Driver implementation is assigned to D7-M4.
6. **D7-M2 Recommended Architecture:** **Strategy B** (PID1 task transformed/reloaded into `/bin/sh` using project loader machinery) is selected.

---

## 2. Binary Identity & Mach-O Header Audit

Audit performed directly on `rootfs/xzs-root/bin/sh`:

```text
SH_PATH=/bin/sh
SH_PRESENT=yes
SH_FILE_SIZE=16472
SH_SHA256=07ca4e95e1ed2f030745db08acf0bb18b89c44ab3b438521ef6e9cd84c549833
SH_FORMAT=Mach-O 64-bit executable arm64
SH_MACHO_VALID=yes
SH_CPU_TYPE=16777228 (CPU_TYPE_ARM64 / 0x0100000C)
SH_CPU_SUBTYPE=0 (CPU_SUBTYPE_ARM64_ALL)
SH_ARM64=yes
```

### Mach-O Header Structure
- **Magic:** `0xfeedfacf` (`MH_MAGIC_64`)
- **Filetype:** `2` (`MH_EXECUTE`)
- **Number of Commands:** `7`
- **Size of Commands:** `648` bytes
- **Flags:** `0x00000001` (`MH_NOUNDEFS`)

### Segments and Sections
- **`__PAGEZERO` (LC_SEGMENT_64):**
  - `vmaddr`: `0x0000000000000000`
  - `vmsize`: `0x0000000100000000` (4 GiB unmapped guard)
  - `filesize`: `0`
  - `initprot`: `VM_PROT_NONE`, `maxprot`: `VM_PROT_NONE`
- **`__TEXT` (LC_SEGMENT_64):**
  - `vmaddr`: `0x0000000100000000`
  - `vmsize`: `0x0000000000004000` (16 KiB)
  - `fileoff`: `0`
  - `filesize`: `16384` bytes
  - `initprot`: `VM_PROT_READ | VM_PROT_EXECUTE` (`0x5`)
  - `maxprot`: `VM_PROT_READ | VM_PROT_EXECUTE` (`0x5`)
  - **Section `__TEXT,__text`:**
    - `addr`: `0x00000001000002f0`
    - `size`: `0x0000000000000048` (72 bytes)
    - `offset`: `752`
    - `align`: `16` (`2^4`)
- **`__DATA`:** NOT PRESENT (`SH_DATA_VMADDR=NONE`, `SH_DATA_VMSIZE=0`, `SH_BSS_SIZE=0`)
- **`__LINKEDIT` (LC_SEGMENT_64):**
  - `vmaddr`: `0x0000000100004000`
  - `vmsize`: `0x0000000000004000`
  - `fileoff`: `16384`
  - `filesize`: `88` bytes
  - `initprot`: `VM_PROT_READ` (`0x1`)
  - `maxprot`: `VM_PROT_READ` (`0x1`)
- **`LC_SYMTAB`:** `symoff`: `16384`, `nsyms`: `3`, `stroff`: `16432`, `strsize`: `40`
  - Symbols:
    - `0x0000000100000000 A __mh_execute_header`
    - `0x00000001000002f0 T _start`
    - `0x0000000100000320 t msg`
- **`LC_UUID`:** `237AB449-629B-3078-ABCD-6A2CEEB7F23D`
- **`LC_SOURCE_VERSION`:** `0.0`
- **`LC_UNIXTHREAD`:**
  - `flavor`: `ARM_THREAD_STATE64` (`6`)
  - `pc`: `0x00000001000002f0` (`SH_ENTRYPOINT`)
  - `sp`: `0x0000000000000000`

---

## 3. Static vs Dynamic Classification

```text
SH_STATIC=yes
LC_LOAD_DYLINKER_PRESENT=no
LC_LOAD_DYLIB_COUNT=0
SH_DYLD_REQUIRED=no
SH_INTERPRETER=none
```

There are no dynamic dependencies, dynamic library load commands, or dyld requirements. This matches the exact loader contract implemented in D6-M2 (`xzs_d6m2_load_macho`).

---

## 4. Shell Origin and Source Traceability

- **Classification:** `minimal custom shell stub`
- **Source Origin:** Generated as part of the XZSFS v1 format definition in commit `b8491f1385dc54f395bd4d1acc8ff5cd4cefd421` (Author: `lechaukha12`, Date: `Sat Sep 19 19:48:37 2026 +0700`).
- **Build Recipe:** Built with `clang` targeting `arm64-apple-macos11 -nostdlib -Wl,-static -Wl,-e,_start -Wl,-pagezero_size,0x100000000 -Wl,-segalign,0x4000`, identical to `src/xzs-userland/Makefile`.
- **Embedded Implementation:**
  ```assembly
  _start:
      mov     x0, #1                  // fd 1 (stdout)
      adr     x1, 0x100000320         // msg: "[XZS-SH] micro-sh ready\n"
      mov     x2, #24                 // length = 24 bytes
      mov     x16, #4                 // SYS_write (syscall 4)
      svc     #0x80
      mov     x0, #0                  // exit status 0
      mov     x16, #1                 // SYS_exit (syscall 1)
      svc     #0x80
  1:  b       1b                      // fallback halt
  ```

---

## 5. Entry ABI & Runtime Stack Compatibility

```text
SH_INITIAL_STACK_COMPATIBLE=yes
SH_ARGC_EXPECTATION=ignored (registers overwritten immediately)
SH_ARGV_EXPECTATION=ignored
SH_ENVP_REQUIRED=no
SH_APPLE_VECTOR_REQUIRED=no
D6_STACK_REUSE_POSSIBLE=yes
```

### Stack Frame Comparison:
- **D6 Initial Stack (`0x16FDFFFB0`):**
  - `sp + 0x00`: `argc = 1`
  - `sp + 0x08`: `argv[0] -> 0x16FDFFFE0` (`"/sbin/launchd"`)
  - `sp + 0x10`: `argv[1] = NULL`
  - `sp + 0x18`: `envp[0] = NULL`
  - `sp + 0x20`: `apple[0] = NULL`
  - `sp + 0x30`: `"/sbin/launchd\0"`
- **`/bin/sh` Entry Expectations:**
  - The binary ignores `sp` entirely and does not access memory below `sp` or above `sp`.
  - Initial registers (`x0`..`x28`, `fp`, `lr`) are overwritten or ignored.
- **Delta for D7-M2:**
  - For standard Darwin compliance when executing `/bin/sh`, the loader can update `argv[0]` to point to `"/bin/sh"`. Stack reuse from D6-M3 is 100% possible with zero architectural changes.

---

## 6. Stdio & TTY Requirements Audit

```text
SH_CAN_INHERIT_FD0=yes
SH_CAN_INHERIT_FD1=yes
SH_CAN_INHERIT_FD2=yes
SH_REQUIRES_TTY_IOCTL=no
SH_REQUIRES_ISATTY_BEHAVIOR=no
SH_REQUIRES_TERMIOS=no
```

- In D6-M6, `initproc` initialized file descriptors:
  - `fd 0 -> /dev/console` (cdev major 0, minor 0, `VCHR`)
  - `fd 1 -> /dev/console`
  - `fd 2 -> /dev/console`
- Since descriptors are not marked `FD_CLOEXEC`, `/bin/sh` directly inherits standard input, output, and error.
- As a headless early bootstrap shell, `/bin/sh` does not require `TIOCGETA`, `TIOCSETA`, or termios line discipline; character streams are written and read directly via `read(0)` and `write(1)`.

---

## 7. Syscall Dependency Audit

### Minimum Syscall Matrix for Headless EL0 Shell

| Syscall | Syscall # | Required Stage | Current Kernel Status | Hardware / Backend Dependency | Classification |
|---|---|---|---|---|---|
| `write` | 4 | stdout | **VERIFIED D6** (`sysent[4]`) | `msm_uart_transmit_data` (UARTDM TX working) | `STDOUT_CRITICAL` / `BOOT_CRITICAL` |
| `read` | 3 | stdin | Kernel path exists (`sysent[3]`, `spec_read`) | `msm_uart_receive_data` (UARTDM RX **STUBBED**) | `STDIN_CRITICAL` / `COMMAND_LOOP_CRITICAL` |
| `getpid` | 20 | runtime | **VERIFIED D6** (`sysent[20]`) | Process table / `initproc` | `BOOT_CRITICAL` |
| `exit` | 1 | exit command | Kernel path exists (`sysent[1]`, `exit1`) | Process lifecycle | `COMMAND_LOOP_CRITICAL` |
| `ioctl` | 54 | tty setup | Partial (`sysent[54]`) | Returns `ENOTTY` / `ENODEV` (safe to ignore) | `OPTIONAL` |
| `open` / `openat`| 5 / 463 | file commands (`cat`) | Verified D5 (`sysent[5]`, `namei`) | XZSFS v1 read-only VFS driver | `FILESYSTEM_COMMAND` |
| `close` | 6 | file commands | Kernel path exists (`sysent[6]`) | Descriptor table | `FILESYSTEM_COMMAND` |
| `fstat` / `stat` | 189 / 338 | file info (`ls`) | Verified D5 (`VNOP_GETATTR`) | XZSFS v1 read-only VFS driver | `FILESYSTEM_COMMAND` |
| `getdirentries` | 196 / 344 | directory listing (`ls`) | Verified D5 (`VNOP_READDIR`) | XZSFS v1 read-only VFS driver | `FILESYSTEM_COMMAND` |
| `reboot` | 55 | system control | Kernel path exists (`sysent[55]`) | APCS / PSCI reset | `OPTIONAL` |

---

## 8. UARTDM RX Dependency Gap Audit

```text
UARTDM_RX_DRIVER_PATH=src/xnu/pexpert/arm/pe_serial.c
UARTDM_RX_READY_PRIMITIVE=msm_uart_receive_ready() (currently returns 0)
UARTDM_RX_DATA_PRIMITIVE=msm_uart_receive_data() (currently returns 0)
UARTDM_RX_IRQ_SUPPORTED=no (GIC SPI 114 is wired in hardware, but XNU console runs in polled mode)
UARTDM_RX_POLLING_POSSIBLE=yes
UARTDM_RX_MMIO_REGISTERS_IDENTIFIED=yes
UARTDM_RX_SOURCE_REFERENCE=Linux drivers/tty/serial/msm_serial.c, Coreboot src/soc/qualcomm/ipq806x/uart.c, LK platform/msm_shared/uart_dm.c
```

### Physical Register Identification (Qualcomm BLSP2 UARTDM at `0x075b0000`):
- `MSM_UART_SR` (`0x0008`): Status Register. Bit 0: `RX_READY` (data available in FIFO), Bit 1: `RX_FULL`.
- `MSM_UART_CR` (`0x0010`): Command Register (commands to clear error flags, reset receiver).
- `MSM_UART_RFWR` (`0x0020`): Receive FIFO Watermark Register.
- `UARTDM_RX_TOTAL_SNAP` (`0x00BC` on BLSP UARTDM v1.4): Latches total bytes received in RX FIFO.
- `UARTDM_RF` (`0x0140` on BLSP UARTDM v1.4): Receive FIFO word access register.

### Implementation Placement:
UARTDM RX is strictly required for **D7-M4** (Shell stdin). Shell entry and banner emission (D7-M2 / D7-M3) do not require RX input and can proceed before RX driver bring-up.

---

## 9. D7-M2 Architecture Decision

### Comparison of Evaluated Strategies:

| Criteria | Strategy A: Native `execve` | Strategy B: Project Loader Transformation | Strategy C: Temporary Shell Bypass |
|---|---|---|---|
| **Native Semantics** | High (`execve("/bin/sh")`) | High (Clean Mach-O load into EL0 task) | Low (Bypasses rootfs `/bin/sh`) |
| **New Kernel Code** | Extreme (~2,000 LOC, `kern_exec.c`, Mach task/thread dealloc, dyld image notifier) | Minimal (~50 LOC, reuses `xzs_d6m2_load_macho`) | Minimal |
| **Mach-O Loader Reuse** | None (uses separate `exec_mach_imgact`) | **100%** (reuses sealed D6-M2 loader) | None |
| **Future iOS Utility** | Medium (iOS launchd uses `posix_spawn`, not exec of `/bin/sh`) | High (preserves deterministic bringup harness) | Low |
| **Stability Risk** | **CRITICAL RISK** of regressing D6 userspace foundation | **MINIMAL RISK** | Low |

### Selected Strategy:
```text
D7_M2_RECOMMENDED_STRATEGY=Strategy B (existing PID1 task transformed/reloaded into /bin/sh using project loader machinery)

RATIONALE=
Strategy A requires activating XNU's full native execve machinery (mach_loader/kern_exec.c), which relies heavily on uninitialized subsystem contracts (POSIX credentials, audit, Mach port deallocation, dyld registration), presenting extreme stability risks to the sealed D6 baseline.
Strategy B directly leverages the sealed D6-M2 Mach-O loader (which already opens, validates, and loads static ARM64 Mach-O binaries from XZSFS) to load /bin/sh into the EL0 user VM with inherited descriptors. Furthermore, since /bin/sh in rootfs/xzs-root is currently a 72-byte non-interactive stub, an interactive shell implementation will be compiled via src/xzs-userland/ and installed to /bin/sh, preserving the canonical rootfs boundary.
```

---

## 10. Shell Visual Identity Feasibility

 Feasibility confirmed. The agreed xnu-xzs ASCII terminal banner:

```text
                 xnu-xzs
        Native XNU on Xperia XZs

+--------------------------------------------------+
| Device     Sony Xperia XZs G8231                 |
| SoC        Qualcomm MSM8996 / Kryo               |
| Kernel     XNU ARM64                             |
| Root       XZSFS                                 |
| Userspace  EL0                                   |
| Console    /dev/console                          |
| Shell      /bin/sh                               |
+--------------------------------------------------+

        [ xnu-xzs userspace online ]

xzs#
```

The string occupies ~400 bytes, which fits comfortably within the `__TEXT` segment of `/bin/sh` and will be emitted via `write(1)` in D7-M3.

---

## 11. Acceptance Checklist

- [x] `SH_ARTIFACT_IDENTIFIED=yes`
- [x] `SH_MACHO_AUDITED=yes`
- [x] `SH_ENTRY_ABI_AUDITED=yes`
- [x] `SH_DYNAMIC_DEPENDENCIES_AUDITED=yes`
- [x] `SH_STDIO_REQUIREMENTS_AUDITED=yes`
- [x] `SH_SYSCALL_DEPENDENCIES_AUDITED=yes`
- [x] `UARTDM_RX_DEPENDENCY_AUDITED=yes`
- [x] `D7_M2_STRATEGY_SELECTED=yes`
- [x] `D7-M1_COMPLETE=yes`
