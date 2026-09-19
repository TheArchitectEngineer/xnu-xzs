# XNU on Sony Xperia XZs

> **Apple XNU has been brought up natively on Sony Xperia XZs / Qualcomm MSM8996 through Mach SMP, BSD initialization, and physical eMMC storage bring-up (CMD17 single-block read verified byte-for-byte against independent TWRP oracle).**
>
> **Phase D2 Physical Storage is COMPLETE. Phase D3 (GPT Partition Discovery) is NEXT.**

---

## Overview

This repository hosts an experimental port of Apple's **XNU kernel** (the core of macOS and iOS) to the **Sony Xperia XZs** smartphone, powered by the **Qualcomm Snapdragon 820 (MSM8996)** System-on-Chip.

The kernel runs **bare-metal at Exception Level 1 (EL1)**, loaded natively via the Sony S1 ABOOT fastboot bootloader. A custom early bootshim bridges Qualcomm device trees into an Apple Device Tree (ADT), configures ARM64 page tables with a 16KB granule, activates Qualcomm BLSP2 UARTDM console logging, drives ARM GICv3 interrupts, and brings all 4 Kryo CPU cores online into the Mach SMP scheduler via standard ARM PSCI.

From Mach SMP, the kernel bootstraps the BSD kernel subsystem, populates VFS structures, executes IOKit autoconfiguration, and drives physical eMMC storage communication over Qualcomm SDCC1/SDHCI, verifying single-block physical sector reads (`CMD17`, LBA 1) with 100% byte-for-byte equality against an independent TWRP Linux oracle.

> [!WARNING]
> **Research & Bring-up Notice**:
> This is a low-level OS kernel research and hardware bring-up project. It is **NOT** a usable mobile operating system. It does **NOT** run iOS, has no graphical user interface, has no working userspace shell, and cannot make phone calls.

---

## Current Status

```text
Phase D2 Physical Storage Bring-up: COMPLETE
Physical eMMC sector read (CMD17, LBA 1) verified byte-for-byte on hardware.
Phase D3 GPT Partition Discovery: NEXT
```

* **Target Hardware**: Sony Xperia XZs (`G8231` / `tone` / `keyaki`)
* **SoC**: Qualcomm Snapdragon 820 (`MSM8996SG` / `MSM8996 Pro`)
* **Architecture**: Quad-core Qualcomm Kryo ARMv8.0-A (64-bit)
* **Storage Device**: Samsung BJNB4R 32GB eMMC 5.1 (`CID: 150100424a4e4234520fdac7c0381400`)
* **Kernel Baseline**: Apple XNU `xnu-12377.1.9` (macOS 15.0 Sequoia / Darwin 24.0.0)
* **Active Bring-up Branch**: `xzs-bringup`
* **Milestone Tag**: `xzs-d2-storage-complete`

---

## Hardware Target

| Component | Hardware Specification |
| :--- | :--- |
| **Device** | Sony Xperia XZs (Model G8231, Platform Tone, Board Keyaki) |
| **SoC** | Qualcomm Snapdragon 820 (MSM8996SG / MSM8996 Pro) |
| **CPU Cores** | 4x Qualcomm Kryo ARMv8.0-A (2x Silver @ 1.59 GHz + 2x Gold @ 2.15 GHz) |
| **DRAM** | 4 GB LPDDR4 (Base address `0x80000000`) |
| **Interrupt Controller** | ARM GICv3 (Distributor `0x09bc0000`, Redistributors `0x09c00000`) |
| **Hardware Timer** | ARM Generic Timer (19.200 MHz, PPI 27 virtual / PPI 30 physical) |
| **Serial Console** | Qualcomm BLSP2 UARTDM UART2 (`0x075b0000`, 115200 8N1) |
| **Storage** | Samsung BJNB4R 32GB eMMC 5.1 on Qualcomm SDCC1 / SDHCI (`0x07464900`) |
| **Boot Mechanism** | Sony S1 ABOOT fastboot (`fastboot boot boot.img`) |

---

## Verified Platform Subsystems

| Subsystem | Milestone | Status |
| :--- | :--- | :---: |
| **Native XNU Execution** | EL1 bare-metal entry, ADT generation, boot_args | ✅ |
| **MMU & Caches** | 16KB granule, TCR/MAIR, High KVA jump, WBWA coherency | ✅ |
| **Interrupts (GICv3)** | GICD/GICR, system registers (`ICC_SRE_EL1`), SPI routing | ✅ |
| **Timers** | ARM Generic Timer (PPI 27 virtual / PPI 30 physical) @ 19.2 MHz | ✅ |
| **SMP & Cores** | 4/4 Kryo CPUs online via PSCI `CPU_ON`, cross-core IPIs | ✅ |
| **Mach Scheduler** | Multi-core `pset0`, `idle_thread`, AST urgent preemption | ✅ |
| **BSD & VFS Initialization** | `kernproc`, credentials, zones, mount table, devfs | ✅ |
| **Physical eMMC Discovery** | SDCC1 host controller, clock RCG (400 kHz), controlled reset | ✅ |
| **SDHCI Host** | Host power (1.8V), clock enable, timeout control, W1C IRQ | ✅ |
| **MMC Protocol Handshake** | CMD0 (Idle), CMD1 (`CARD_READY=yes`, `FINAL_OCR=0xC0FF8080`) | ✅ |
| **Card Identification** | CMD2 (`CID_MATCH=yes`, Samsung BJNB4R) | ✅ |
| **RCA Assignment** | CMD3 (`ASSIGNED_RCA=2`, STBY state) | ✅ |
| **Card Specific Data** | CMD9 (`CSD_MATCH=yes`, `d02701320f5903fff6dbffef8e404000`) | ✅ |
| **Card Selection** | CMD7 (`CARD_SELECTION_CONFIRMED=yes`, TRAN state) | ✅ |
| **EXT_CSD Data Transfer** | CMD8 (512 bytes captured via PIO, `SEC_COUNT=61071360`) | ✅ |
| **Physical Block Read** | CMD17 (`LBA=1`, 512 bytes read via SDHCI_BUFFER) | ✅ |
| **Oracle Equality** | Byte-for-byte SHA-256 match against TWRP disk oracle | ✅ |

```text
D2 Physical Storage:   COMPLETE
D3 GPT Partition Map:  NEXT
```

### D2 Physical Sector Acceptance Evidence (LBA 1)

```text
D2 Acceptance Sector:    LBA 1 (Primary GPT Header)
Byte Count:              512 bytes

TWRP Oracle SHA-256:     e4b891b42fd57eb352ffbe0aa9098d04fe85f88e3425dcba529064cce72f862a
XNU Hardware SHA-256:    e4b891b42fd57eb352ffbe0aa9098d04fe85f88e3425dcba529064cce72f862a

BYTE_FOR_BYTE_MATCH=yes  (cmp -l exit 0, 100% exact 512/512 byte equality)
```

---

## What Has Been Verified

All items below have been certified by direct physical evidence extracted from hardware registers, persistent RAM (`0x80060000`), or pstore ramoops (`0xa7fbe000`):

- [x] **Native S1 Bootloader Handoff**: Unlocked Sony bootloader boots custom Android-format boot image containing kernel and compressed DTB.
- [x] **xzs-bootshim Runtime**: Parses Qualcomm DTB, extracts memory tags, and dynamically generates an Apple Device Tree (ADT) at `0x81810000`.
- [x] **ARM64 MMU & 16KB Granule**: Page tables constructed; TCR/MAIR set; clean jump from identity mapping to High KVA (`0xfffffe0000000000`).
- [x] **Serial & Post-Mortem Logging**: Qualcomm BLSP2 UARTDM driver active; lockless ring buffer in RAM (`0x80060000`); Linux/TWRP-compatible `pstore ramoops` (`0xa7fbe000`).
- [x] **ARM GICv3 Interrupt Subsystem**: Distributor enabled; Redistributors awake; native system register interface (`ICC_SRE_EL1.SRE = 1`).
- [x] **ARM Generic Timers**: PPI 27 (Virtual Timer) & PPI 30 (Physical Timer) firing reliably across all cores at 19.2 MHz.
- [x] **PSCI Multi-Core Bring-up**: Secondary cores booted via SMC `CPU_ON` (`0xC4000003`); shared memory handshake verified.
- [x] **Mach SMP Scheduler**: All 4 cores joined in processor set `pset0`; `idle_thread` active on all cores; inter-processor reschedule IPIs via SGI 1; AST urgent preemption operational.
- [x] **Multi-Core Cache Coherency**: Inner Shareable WBWA memory; 40,000 concurrent atomic operations completed across all 4 cores without corruption (`0x9c40`).
- [x] **BSD Subsystem Bootstrap**: Process 0 (`kernproc`), credentials, zones, domains, and sysctl tree fully initialized.
- [x] **VFS Core Framework**: Mount table structures, vnode cache pools (`vnodes=263168`), and devfs bootstrap operational.
- [x] **IOKit Autoconfiguration**: `IOKitBSDInit` publishing the `IOBSD` plane to the BSD subsystem.
- [x] **Root Device Discovery Boundary**: `IOFindBSDRoot()` executes canonical `IOMedia` matching dictionary traversal; bounded 1.0s wait loop verifies no physical disk exists; returns canonical `kIOReturnNotFound` (`0xe00002f0`).
- [x] **Block Device Vnode Probing**: `vfs_mountroot()` calls `bdevvp()` on synthetic root device `sd0a`; `VNOP_OPEN` queries the BSD block device switch table (`bdevsw`), returning canonical `ENODEV` (`0x13`).
- [x] **Automated Recovery**: Upon reaching `[D51-TERMINAL]`, the kernel writes `0x77665500` to IMEM SRAM (`0x066bf65c`) and triggers an APCS watchdog bite (`xzs_spin_halt()`), warm-rebooting the device back to Fastboot within **+6 seconds**.

---

## Current Boot Flow

```text
Sony ABOOT (Fastboot)
       │
       ▼  Loads boot.img at EL1 (0x82000000)
xzs-bootshim (src/xzs-bootshim/)
       │  Builds ADT at 0x81810000, boot_args at 0x81800000
       ▼
Apple XNU Entry (_start in osfmk/arm64/start.s)
       │  Installs vectors, configures MMU (16KB), jumps to High KVA
       ▼
arm_init() -> pmap / VM Bootstrap
       │  Initializes zones, UART console (0x075b0000), pstore ramoops
       ▼
pe_fiq() -> ARM GICv3 Distributor & CPU0 Redistributor
       │
       ▼  PSCI CPU_ON (0xC4000003) via SMC #0
4/4 CPU SMP Online (CPU0..CPU3 in pset0)
       │  Mach scheduler, SGI 1 reschedule IPIs, AST preemption
       ▼
bsd_init() -> BSD Kernel Bootstrap
       │  Credentials, proc0, kernproc, zones, devfs, sysctl
       ▼
bsd_autoconf() -> IOKitBSDInit()
       │  Publishes IOBSD plane
       ▼
IOFindBSDRoot() -> Root Device Discovery
       │  Canonical matching on "IOMedia" (1.0s bounded wait)
       │  Returns kIOReturnNotFound (0xe00002f0)
       │  setconf() configures synthetic rootdev "sd0a"
       ▼
vfs_mountroot() -> Block Device Boundary
       │  Calls bdevvp(rootdev) -> VNOP_OPEN queries bdevsw[6]
       │  Returns canonical ENODEV (0x13)
       ▼
[D51-TERMINAL] reached!
       │  xzs_spin_halt() writes IMEM restart reason 0x77665500
       ▼  Triggers APCS Watchdog Bite
Fastboot re-entry in +6 seconds (automated cycle complete)
```

---

## Phase D1 Result

The acceptance criteria for Phase D1 required the BSD/VFS subsystem to execute through autoconfiguration down to the block storage boundary.

Extracted persistent log excerpt:
```text
[XZS-BOOT] [D50] ROOT DEVICE SELECTION ENTER
[XZS-BOOT] [D50a] IOFindBSDRoot ENTER
[XZS-BOOT] [D50-IOKIT] IOFindBSDRoot ENTER
[XZS-BOOT] [D50-I0] alloc matching ENTER
[XZS-BOOT] [D50-I0a] alloc matching RETURN
[XZS-BOOT] [D50-I1] fromPath /chosen ENTER
[XZS-BOOT] [D50-I1a] fromPath /chosen RETURN
[XZS-BOOT] [D50-I2] fromPath /chosen/memory-map ENTER
[XZS-BOOT] [D50-I2a] fromPath /chosen/memory-map RETURN
[XZS-BOOT] [D50-I3] serviceMatching(IOMedia) ENTER
[XZS-BOOT] [D50-I3a] serviceMatching(IOMedia) RETURN
[XZS-BOOT] [D50-I4] waitQuiet SKIPPED (not set in gIOKitDebug)
[XZS-BOOT] [D50-I5] serialize matching ENTER
[XZS-BOOT] [XZS-WORKAROUND] IOFindBSDRoot matching serialization/logging DEFERRED
[XZS-BOOT] [D50-I5a] serialize matching RETURN / DEFERRED
[XZS-BOOT] [D50-I6] startDeferredMatches SKIPPED
[XZS-BOOT] [D50-I7] canonical root-service wait ENTER
[XZS-BOOT] [XZS-SELFTEST] bounded root-device wait (timeout=1.0s)...
[XZS-BOOT] [D50-I8] no matching physical root service
[XZS-BOOT] [XZS-SELFTEST] root-device wait timed out: no matching physical IOMedia
IOFindBSDRoot: no root device matched after timeout, failing gracefully
[XZS-BOOT] [D50-IOKIT] IOFindBSDRoot: returning kIOReturnNotFound (0xe00002bc)
[XZS-BOOT] [D50b] IOFindBSDRoot RETURN err=0x0xe00002f0
[XZS-BOOT] [D50c] synthetic rootdev selected (XZS-WORKAROUND / SELFTEST)
setconf: IOFindBSDRoot returned an error (-536870160); setting rootdevice to 'sd0a'.
[XZS-BOOT] [D50d] rootdev major=0x0x6 minor=0x0x0 rootdevice=sd0a
[XZS-BOOT] [D51] vfs_mountroot ENTER
[XZS-BOOT] [D51b] bdevvp(rootdev, ...) error=0x0x13
vfs_mountroot: can't setup bdevvp
[XZS-BOOT] [D51-TERMINAL] cannot mount root, errno = 0x0x13 (expected: physical storage / UFS not implemented)
[XZS-BOOT] PHASE D1 TERMINAL CONDITION REACHED — WARM REBOOTING TO FASTBOOT
```

---

## Current Limitations

1. **No Root Filesystem Mounted**: While physical block reads (CMD17) are verified on hardware, partition table parsing (Phase D3) and filesystem driver mounting (Phase D4/D5) have not yet been integrated.
2. **No Userspace Execution**: The kernel terminates cleanly at the hardware bring-up boundary; PID 1 (`launchd`) and userland execution are not started.
3. **Deferred Subsystems**: Advanced networking (Skywalk, lo0, gif0, ethernet) and DTrace tracing are temporarily deferred.
4. **Warm Reboot Nondeterminism**: Repeated warm reboots can cause early stalls; a cold reset is required for 100% deterministic reproduction (see [`docs/XZS_KNOWN_ISSUES.md`](docs/XZS_KNOWN_ISSUES.md)).

---

## Bring-up Workarounds

All deviations from canonical XNU behavior are cataloged in [`docs/XZS_WORKAROUNDS.md`](docs/XZS_WORKAROUNDS.md) and partitioned into:
* **`XZS-COMPAT`**: Architectural adaptations for non-Apple ARMv8.0 silicon (e.g. CTRR absence handling).
* **`XZS-WORKAROUND`**: Temporary early bring-up compromises (e.g. deferring background network daemons, static jetsam snapshot buffer, thread-call zone priming, and debug serialization deferral).
* **`XZS-SELFTEST`**: Synthetic verification probes (e.g. bounded 1.0s IOMedia wait loop and `sd0a` synthetic fallback).

---

## Repository Layout

```text
├── src/
│   ├── xnu/                 # Apple XNU kernel source (macOS 15.0 / Darwin 24.0.0)
│   │   ├── osfmk/           # Mach kernel core, SMP scheduler, ARM64 low-level start
│   │   ├── bsd/             # BSD subsystem, VFS, networking, sysctl
│   │   ├── iokit/           # IOKit C++ driver framework & device discovery
│   │   └── pexpert/         # Platform Expert (UARTDM, GICv3, device tree)
│   ├── xzs-bootshim/        # Early bootshim (ADT generator, boot_args builder)
│   ├── xzs-debug-dumper/    # USB CDC-ACM crash dumper tool
│   └── xzs-ram-test/        # RAM persistence verifier
├── device/                  # Qualcomm device tree blobs (tone-keyaki.dtb)
├── toolchain/               # Build utilities and helper libraries
├── scripts/                 # Build, PAC audit, packaging, and hardware test scripts
├── docs/                    # Complete technical documentation suite
├── LICENSE                  # Apple Public Source License (APSL) Version 2.0
├── NOTICE                   # Upstream attribution notice
└── README.md                # This document
```

---

## Build

Building requires a macOS host with Xcode and command line tools.

```bash
# 1. Compile XNU for ARM64 VMAPPLE target
DEVELOPER_DIR=/Applications/Xcode.app/Contents/Developer \
make -C src/xnu \
    KERNEL_CONFIGS=DEVELOPMENT \
    ARCH_CONFIGS=ARM64 \
    MACHINE_CONFIGS=VMAPPLE \
    RC_DARWIN_KERNEL_VERSION=24.0.0 \
    build -j$(sysctl -n hw.ncpu)

# 2. Audit that zero Apple PAC instructions exist
./scripts/check-no-pac.sh

# 3. Package Android boot image
./scripts/package-boot.sh
```

Output: `artifacts/builds/xzs-xnu-boot.img`

*(Detailed instructions: see [`docs/XZS_BUILD.md`](docs/XZS_BUILD.md))*

---

## Hardware Testing

1. **Cold-Reset Device**: Hold `Power + Volume Up` until the phone vibrates 3 times.
2. **Enter Fastboot**: Hold `Volume Down` and connect USB-C cable (blue LED turns on).
3. **Execute Automated Verification**:
   ```bash
   ./scripts/run-and-extract.sh
   ```
   The script boots XNU, monitors execution to the terminal boundary (+6s), boots recovery RAM, and extracts persistent console logs to `artifacts/logs/`.

*(Detailed guide: see [`docs/XZS_HARDWARE_TESTING.md`](docs/XZS_HARDWARE_TESTING.md))*

---

## Roadmap

```text
Phase A:   Native kernel entry                     [COMPLETE]
Phase B:   Platform bring-up (UART, GIC, Timer)    [COMPLETE]
Phase C:   Mach SMP Scheduler (4 Cores, IPI, AST)  [COMPLETE]
Phase D1:  BSD/VFS to Root Storage Boundary        [COMPLETE]
Phase D2:  Physical eMMC Storage Bring-up (CMD17)  [COMPLETE]
Phase D3:  GUID Partition Table (GPT) Discovery    [NEXT]
Phase D4:  IOKit Block Storage Integration (disk0) [NOT STARTED]
Phase D5:  Real Root Filesystem Mount (HFS+/APFS)  [NOT STARTED]
Phase E:   PID 1 Userspace Bootstrap (launchd)     [NOT STARTED]
Phase F:   Interactive Serial Console Shell        [NOT STARTED]
Phase G:   Restore Deferred Subsystems             [NOT STARTED]
Phase H:   Networking & Peripheral Device Drivers  [NOT STARTED]
Phase I:   Userspace & Platform Expansion          [NOT STARTED]
```

*(Full roadmap: see [`docs/XZS_ROADMAP.md`](docs/XZS_ROADMAP.md))*

---

## Branch Strategy

* **`main`**: Certified public release milestones.
* **`xzs-bringup`**: Active bring-up laboratory, verbose instrumentation, and temporary workarounds.
* **`xzs-port`**: Clean hardware-verified port changes intended to remain close to canonical XNU behavior.

*(Full branch guidelines: see [`docs/CONTRIBUTING.md`](docs/CONTRIBUTING.md))*

---

## Upstream

* **Upstream Project**: [Apple XNU Open Source](https://github.com/apple-oss-distributions/xnu)
* **Release Baseline**: `xnu-12377.1.9` (macOS 15.0 Sequoia / Darwin 24.0.0)
* All original Apple license headers and copyright notices are strictly preserved throughout the codebase.

---

## Disclaimer

This project is an independent research endeavor and is not affiliated with, endorsed by, or sponsored by Apple Inc. or Sony Corporation. All product names, logos, and brands are property of their respective owners.

---

## Documentation

* [`docs/XZS_PORT_STATUS.md`](docs/XZS_PORT_STATUS.md) — 2-minute project status summary.
* [`docs/XZS_WORKAROUNDS.md`](docs/XZS_WORKAROUNDS.md) — Exhaustive 17-item workaround and compatibility matrix.
* [`docs/XZS_KNOWN_ISSUES.md`](docs/XZS_KNOWN_ISSUES.md) — Known hardware behaviors, warm reset nondeterminism, and log audits.
* [`docs/XZS_HARDWARE_VERIFICATION.md`](docs/XZS_HARDWARE_VERIFICATION.md) — Rigorous 4-column hardware evidence audit.
* [`docs/XZS_ROADMAP.md`](docs/XZS_ROADMAP.md) — Detailed technical phase breakdown from Phase A to I.
* [`docs/XZS_ARCHITECTURE.md`](docs/XZS_ARCHITECTURE.md) — Architectural overview, execution pipeline, and MMIO map.
* [`docs/XZS_BUILD.md`](docs/XZS_BUILD.md) — Complete build and toolchain instructions.
* [`docs/XZS_HARDWARE_TESTING.md`](docs/XZS_HARDWARE_TESTING.md) — Deployment, test automation, and recovery procedures.
* [`docs/CONTRIBUTING.md`](docs/CONTRIBUTING.md) — Branch workflows and contribution guidelines.
