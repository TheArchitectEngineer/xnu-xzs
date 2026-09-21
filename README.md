# xnu-xzs

> Native Apple XNU Kernel on Sony Xperia XZs (Qualcomm MSM8996)

---

## Mission

**xnu-xzs** is primarily a hardware and platform porting project.

Its long-term research goal is to determine how far an older authentic **Apple iOS userland** can be brought up on **Sony Xperia XZs** hardware by combining:
- **Native XNU**: Canonical Darwin/XNU kernel running bare-metal on Qualcomm silicon.
- **Xperia/MSM8996 Platform Support**: Clocks, power management, interrupts, MMIO, and bus infrastructure.
- **Native Hardware Drivers**: IOKit drivers for storage, display, touch, USB, and power subsystems.
- **Apple-Facing Compatibility Services (`XZSAppleCompat`)**: IOKit contracts, platform properties, and device topology expected by Apple userland.
- **Darwin/iOS Userland Compatibility**: Mach traps, BSD syscalls, Mach IPC, dyld, launchd, and core frameworks.

> [!IMPORTANT]
> **Mission Clarity**:
> This project is explicitly **NOT** attempting to build an unrelated new mobile operating system.
> The custom userspace components (PID1 skeleton, `/bin/sh` interactive shell) exist strictly as:
> - Bring-up infrastructure
> - Low-level debugging environment
> - Driver-development environment
> - Hardware-validation environment
>
> The ultimate research objective is authentic old-iOS userland execution on Xperia hardware.

---

## Target Hardware

| Component | Hardware Specification |
| :--- | :--- |
| **Device** | Sony Xperia XZs (Model G8231, Platform Tone, Board Keyaki) |
| **SoC** | Qualcomm Snapdragon 820 (MSM8996SG / MSM8996 Pro) |
| **CPU Cores** | Quad-core Qualcomm Kryo ARMv8.0-A (2x Silver @ 1.59 GHz + 2x Gold @ 2.15 GHz) |
| **DRAM** | 4 GB LPDDR4 (Base physical address `0x80000000`) |
| **Interrupt Controller** | ARM GICv3 (Distributor `0x09bc0000`, Redistributors `0x09c00000`) |
| **Hardware Timer** | ARM Generic Timer (19.200 MHz, PPI 27 virtual / PPI 30 physical) |
| **Serial Console** | Qualcomm BLSP2 UARTDM UART2 (`0x075b0000`, 115200 8N1) |
| **Internal Storage** | Samsung BJNB4R 32GB eMMC 5.1 on Qualcomm SDCC1 / SDHCI (`0x07464900`) |
| **Display** | 5.2" 1080x1920 IPS LCD (DSI / MDP5) |
| **Boot Mechanism** | Sony S1 ABOOT Fastboot (`fastboot boot boot.img`) |

---

## Current Hardware-Verified Status

```text
Native XNU boot        VERIFIED
4-core SMP             VERIFIED
VFS/rootfs             VERIFIED
PID1                   VERIFIED
EL0 execution          VERIFIED
Darwin syscall path    VERIFIED
/dev/console stdout    VERIFIED
stable PID1 runtime    VERIFIED

physical UART RX       NOT IMPLEMENTED
interactive shell      NEXT PHASE
native display         FUTURE D8
```

### Verified Milestone Capabilities

* **Native Kernel Entry**: Bare-metal EL1 entry via `xzs-bootshim`, Apple Device Tree (ADT) generation, 16KB translation tables, and transition to High KVA (`0xfffffe0000000000`).
* **4-Core SMP Scheduler**: CPU0–CPU3 brought online via standard ARM PSCI `CPU_ON` (`0xC4000003`); Mach `pset0` active with `idle_thread` on all cores, GICv3 SGI 1 reschedule IPIs, and AST urgent preemption.
* **BSD Subsystem & VFS Root**: Full BSD process table, credentials, and read-only XZSFS v1 root filesystem mounted from RAMDisk (`rd=md0`).
* **devfs & Console Device**: Namespace resolution through `/dev` overlay; `/dev/console` created as character device 0:0 (`VCHR`).
* **PID 1 Userspace (EL0)**: BSD `initproc` (PID 1) mapped with PAGEZERO guard, RX `__TEXT`, and RW/NX initial stack; Strategy A2 native AF & UXN permission promotion verified on silicon.
* **Native Console Output**: PID 1 holds native file descriptors 0, 1, and 2 mapped to `/dev/console`; real Darwin `write(1)` syscall verified producing 26 bytes of console output (`[XZS-INIT] launchd entered\n`).
* **Sustained Stable Runtime**: PID 1 executes in EL0 indefinitely; completed 3,009 consecutive successful `getpid` (syscall 20) round-trips without faulting.

---

## Architecture

```text
               Authentic Apple / Darwin Userspace (launchd, dyld, daemons)
                                            │
                                            ▼
                       Standard IOKit / Platform Contracts
                 (IORegistry, AppleARMPERoot, IOPMPowerSource)
                                            │
                                            ▼
                    +──────────────────────────────────────────────+
                    |                XZSAppleCompat                |
                    |  - Translates Apple contracts to platform    |
                    |  - Emulates Apple IORegistry topologies      |
                    |  - Provides device-tree/chosen properties    |
                    |  - Exposes power/battery/display interfaces  |
                    +──────────────────────────────────────────────+
                                            │
                                            ▼
                    +──────────────────────────────────────────────+
                    |                 XZSPlatform                  |
                    |  - Clean OS-neutral Qualcomm hardware API    |
                    |  - MMIO, Clock (GCC), Reset, Regulator (RPM) |
                    |  - GPIO/TLMM pinmux, GICv3 IRQ, SMMU DMA     |
                    +──────────────────────────────────────────────+
                                            │
                                            ▼
                    +──────────────────────────────────────────────+
                    |          Native Qualcomm / Sony Drivers      |
                    |  - BLSP UARTDM, SDCC1 eMMC, MDP5 display,    |
                    |  - Synaptics ClearPad touch, DWC3 USB, etc.  |
                    +──────────────────────────────────────────────+
                                            │
                                            ▼
                                  MSM8996 Hardware
```

---

## Current Development Phase

* **Current Milestone**: **Phase D6 COMPLETE / SEALED** (Full regression verified across all milestones D6-M1 through D6-M7 on physical silicon; tag `xzs-d6-userspace-complete`).
* **Next Immediate Action**: **Phase D7 (Interactive EL0 Shell)** — Headless serial shell bring-up (`/bin/sh` REPL) and Qualcomm MSM8996 UARTDM RX driver.

---

## Roadmap

| Phase | Milestone Description | Hardware Status |
| :--- | :--- | :---: |
| **Phase A** | Native kernel entry (Bootshim, ADT, MMU, High KVA) | **COMPLETE** |
| **Phase B** | Platform bring-up (UARTDM, GICv3, Timer, Pmap, VM) | **COMPLETE** |
| **Phase C** | SMP / Mach scheduler (PSCI, 4 Kryo cores, IPI, AST, Preemption) | **COMPLETE** |
| **Phase D1** | BSD / VFS bootstrap to root-storage boundary | **COMPLETE** |
| **Phase D2** | Physical eMMC storage bring-up (SDCC1, CMD0..CMD17, PIO) | **COMPLETE** |
| **Phase D3** | GUID Partition Table (GPT) discovery & partition enumeration | **COMPLETE** |
| **Phase D4** | Block-storage driver integration (`bdevsw` / `disk0`) | **COMPLETE** |
| **Phase D5** | Real root filesystem mount (RAMDisk XZSFS v1) | **COMPLETE / SEALED** |
| **Phase D6** | PID 1 / First EL0 userspace (`initproc` / launchd) | **COMPLETE / SEALED** |
| **Phase D7** | Interactive serial shell (`/bin/sh` headless REPL) | **NEXT PHASE** |
| **Phase D8** | Native display / framebuffer / touch / recovery console | **PLANNED** |
| **Phase D9** | XZSPlatform hardware/platform compatibility layer | **PLANNED** |
| **Phase D10**| Core native device drivers | **PLANNED** |
| **Phase D11**| System hardware integration | **PLANNED** |
| **Phase D12**| XZSAppleCompat (Apple-facing hardware compatibility layer) | **PLANNED** |
| **Phase D13**| Darwin / iOS userland compatibility | **PLANNED** |
| **Phase D14**| First old-iOS userland boot | **PLANNED** |
| **Phase D15**| iOS service bring-up | **PLANNED** |
| **Phase D16**| Graphical iOS userland / SpringBoard investigation | **PLANNED** |

Full specifications and milestone criteria are detailed in [`docs/XZS_ROADMAP.md`](docs/XZS_ROADMAP.md).


---

## Hardware / Platform Strategy

To maintain engineering rigor and keep generic XNU maintainable:
1. **Upstream Alignment**: Keep generic XNU code as close to canonical Apple/Darwin semantics as practical.
2. **Platform Encapsulation**: All Qualcomm MSM8996 and Sony Xperia specific hardware behaviors must be encapsulated within `XZSPlatform`.
3. **Compatibility Shimming**: Apple-specific expectations (IORegistry planes, property trees, power sources) are isolated within `XZSAppleCompat`.
4. **Audit and Retirement**: Temporary bring-up workarounds (e.g. `devfs_getattr` pointer-hardening bypass `3e417bb`) are tracked as technical debt and slated for systematic retirement under Phase D9.

---

## iOS Userland Research Goal

The project models authentic iOS userland execution through an extracted-image paradigm:
```text
Legally obtained Apple IPSW
        ↓
Extract compatible iOS root/userland
        ↓
Prepare project-specific root filesystem/image
        ↓
Boot using xnu-xzs / target-compatible XNU
        ↓
Mount iOS userland
        ↓
Execute authentic Apple launchd
```

### Version Compatibility Principle
An older iOS userland cannot automatically be assumed compatible with the current macOS Sequoia bring-up XNU kernel. The project audits:
```text
Target iOS Version ↔ Darwin Version ↔ XNU Version ↔ dyld Version ↔ launchd Version ↔ IOKit ABI
```
The modular `XZSPlatform` design ensures that board support and native drivers can be re-targeted to an XNU branch matching the selected iOS version if needed.

---

## Debug / Recovery Workflow

* **Boot Mechanism**: Automated testing boots through Sony S1 Fastboot (`fastboot boot boot.img`).
* **Diagnostic Telemetry**: Multi-tier persistent logging:
  - IMEM SRAM breadcrumbs (`0x066bf660`): Hardware checkpoint IDs and error codes.
  - DRAM scratch ring buffer (`0x80060000`): Lockless early character buffer.
  - Persistent RAM ramoops (`0xa7f00000` dmesg, `0xa7fbe000` console): Extracted post-mortem via TWRP.
* **Automated Recovery**: On panic or test completion, the kernel writes `0x77665500` to IMEM and triggers a warm reset directly back to Fastboot mode.
* **Automated Acceptance Verification**: Independent Python verifiers in `scripts/verify_*.py` audit checkpoint sequences and telemetry keys.

---

## Known Limitations

1. **UARTDM RX Not Implemented**: Qualcomm MSM8996 UARTDM RX driver is currently stubbed (`PHYSICAL_CONSOLE_RX_AVAILABLE=no`). Serial console input is not yet available; interactive input will be brought up in Phase D7.
2. **Display & GPU Uninitialized**: Physical screen and GPU acceleration are uninitialized; on-device console rendering is planned for Phase D8.
3. **devfs Pointer-Hardening Bypass**: Commit `3e417bb` bypasses `vm_kernel_addrhash` in `devfs_getattr` to prevent a SHA-256 address hashing hang during early devfs open. Classified as `XZS PLATFORM WORKAROUND` to be re-audited under Phase D9.
4. **Deferred Subsystems**: Advanced networking (Skywalk, lo0) and DTrace FBT are temporarily deferred until required drivers are active.

---

## Documentation Index

### Core Architecture & Strategy
* [Project Roadmap](docs/XZS_ROADMAP.md) — Authoritative multi-phase development roadmap (Phases A through D16).
* [Architecture Overview](docs/XZS_ARCHITECTURE.md) — End-to-end boot architecture, execution flow, and platform design.
* [Workarounds & Compatibility Matrix](docs/XZS_WORKAROUNDS.md) — Active shims, workarounds, and classification taxonomy.
* [Technical Debt & Backlog](docs/XZS_TECHNICAL_DEBT.md) — Architectural debt inventory, risks, and remediation plans.
* [Current Session Handoff](docs/CURRENT_HANDOFF.md) — Concise current state, commits, and next actions for AI sessions.

### Hardware & Bring-up Reference
* [Hardware Map](docs/HARDWARE_MAP.md) — Audited MMIO register bases, IRQs, and clock domains.
* [Memory Map](docs/XZS_MEMORY_MAP.md) — Physical memory layout, carveouts, and pstore allocations.
* [Early Debug Architecture](docs/XZS_EARLY_DEBUG.md) — Diagnostic telemetry, breadcrumbs, and ramoops recovery.
* [Hardware Verification Guide](docs/XZS_HARDWARE_VERIFICATION.md) — Flashing, testing, and extraction procedures.

### Sealed Phase Reports & Audits
* [Phase D6 Final Userspace Foundation Report](artifacts/reports/D6_FINAL_USERSPACE_FOUNDATION_REPORT.md) — Comprehensive multi-milestone seal and regression report.
* [Phase D6-M6 Stable PID1 Report](artifacts/reports/D6_M6_STABLE_PID1_REPORT.md) — D6-M6 hardware acceptance report.
* [Phase D6-M6 Source Audit](docs/D6_M6_STABLE_PID1_SOURCE_AUDIT.md) — File descriptor bootstrap, console call path, and telemetry audit.
* [Phase D6-M5 Syscall Round-Trip Report](artifacts/reports/D6_M5_FIRST_SYSCALL_REPORT.md) — First EL0 syscall dispatcher and return proof.
* [Phase D6-M4 First EL0 Transition Report](artifacts/reports/D6_M4_FIRST_EL0_REPORT.md) — First userland instruction execution proof.
* [Phase D5 Rootfs Final Architecture](docs/D5_ROOTFS_FINAL_ARCHITECTURE.md) — RAMDisk transport and XZSFS v1 filesystem architecture.
