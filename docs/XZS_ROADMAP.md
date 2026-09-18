# Xperia XZs / Qualcomm MSM8996 Port Roadmap

This document outlines the technical architecture roadmap for bringing up Apple XNU natively on Sony Xperia XZs (Qualcomm MSM8996).

Progress is strictly gated by physical hardware verification. Speculative percentages and estimated completion dates (ETAs) are deliberately omitted.

---

## 1. Executive Status Summary

| Phase | Description | Current Status |
| :--- | :--- | :---: |
| **Phase A** | Native kernel entry (Bootshim, ADT, MMU, High KVA) | **COMPLETE** |
| **Phase B** | Platform bring-up (UARTDM, GICv3, Timer, Pmap, VM) | **COMPLETE for current bring-up scope** |
| **Phase C** | SMP / Mach scheduler (PSCI, 4 Kryo cores, IPI, AST, Preemption) | **COMPLETE for current bring-up scope** |
| **Phase D1** | BSD / VFS bootstrap to root-storage boundary | **COMPLETE** |
| **Phase D2** | Qualcomm MSM8996 UFS controller driver | **NOT STARTED** |
| **Phase D3** | Block device nub / GUID Partition Table (GPT) | **NOT STARTED** |
| **Phase D4** | Real root filesystem mount (APFS / HFS+) | **NOT STARTED** |
| **Phase E** | PID 1 bootstrap (`initproc` / launchd exec) | **NOT STARTED** |
| **Phase F** | Interactive serial shell (`/bin/sh` or micro-shell) | **NOT STARTED** |
| **Phase G** | Restore deferred subsystems (Skywalk, DTrace, jetsam buffer) | **NOT STARTED** |
| **Phase H** | Networking and platform device drivers | **NOT STARTED** |
| **Phase I** | Userspace and multi-process expansion | **NOT STARTED** |

---

## 2. Detailed Phase Specifications

### Phase A — Native Kernel Entry
* **Goal**: Execute Apple XNU kernel natively at EL1 on Qualcomm MSM8996 via Sony S1 bootloader.
* **Status**: **COMPLETE**
* **Hardware Acceptance Criteria**: S1 bootloader executes `xzs-bootshim`; bootshim builds Apple Device Tree (ADT); kernel `_start` sets exception vectors, configures page tables, and successfully enables MMU, jumping to High KVA (`0xfffffe0000000000`).
* **Completed Items**:
  - S1 Android-standard boot image packaging (`mkbootimg.py`).
  - `xzs-bootshim` parsing Qualcomm DTB and constructing compliant ADT.
  - Exception vectors installed at `LowExceptionVectorBase`.
  - TTBR0/TTBR1 and TCR/MAIR configuration in `start.s`.
  - Transition to High KVA C runtime in `arm_init()`.
* **Remaining Items**: None.
* **Known Blockers**: None.
* **Dependencies**: Sony S1 ABOOT bootloader.

---

### Phase B — Platform Bring-up
* **Goal**: Initialize fundamental platform peripherals (UART console, interrupt distributor, hardware timers, and memory allocators).
* **Status**: **COMPLETE for current bring-up scope**
* **Hardware Acceptance Criteria**: Qualcomm BLSP2 UARTDM transmitting serial logs at 115200 8N1; ARM GICv3 distributor (`GICD`) and redistributor (`GICR`) active; ARM generic timer PPI firing at 19.2 MHz; kernel VM zones and page allocators active.
* **Completed Items**:
  - Qualcomm BLSP2 UARTDM driver in `pexpert/arm/pe_serial.c`.
  - Persistent RAM pstore logging in `start.s` (`0xa7fbe000` console, `0xa7f00000` dmesg).
  - GICv3 distributor initialization in `pexpert/arm/pe_fiq.c`.
  - ARM Generic Timer PPI configuration (Virtual PPI 27 / Physical PPI 30).
  - Early memory allocation and pmap bootstrap.
* **Remaining Items**: Native Qualcomm PMIC RTC driver (currently using non-blocking RTC workaround).
* **Known Blockers**: None for bring-up.
* **Dependencies**: Phase A.

---

### Phase C — SMP / Mach Scheduler Integration
* **Goal**: Bring all 4 Qualcomm Kryo cores online into Mach processor set `pset0` and achieve preemption and thread migration.
* **Status**: **COMPLETE for current bring-up scope**
* **Hardware Acceptance Criteria**: CPU0-CPU3 online; secondary cores booted via standard PSCI `CPU_ON` (`0xC4000003`); Mach `idle_thread` running on all 4 cores; cross-cluster reschedule IPIs delivered via GICv3 SGI 1; 40,000 concurrent atomic lock operations completed without data corruption.
* **Completed Items**:
  - PSCI `CPU_ON` multi-core bootstrap in `osfmk/kern/startup.c`.
  - Per-core GICR configuration and system register interface (`ICC_SRE_EL1.SRE = 1`).
  - Secondary core entry trampolines (`xzs_secondary_entry`, `xzs_secondary_kva_entry`).
  - All 4 processors registered in `pset0`.
  - Reschedule AST preemption (`thread_preempted_in_kernel`).
  - Thread migration and 40k contended spinlock verification (`0x9c40`).
* **Remaining Items**: Dynamic CPU hotplug and CPU deep sleep power management.
* **Known Blockers**: None for current scope.
* **Dependencies**: Phase B.

---

### Phase D1 — BSD / VFS to Root Storage Boundary
* **Goal**: Bootstrap BSD subsystem, process table, VFS layer, devfs, and IOKit autoconfiguration up to the physical block-device rootfs boundary.
* **Status**: **COMPLETE**
* **Hardware Acceptance Criteria**: `bsd_init()` executes to completion; `IOKitBSDInit()` publishes `IOBSD`; `IOFindBSDRoot()` traverses canonical matching and returns `kIOReturnNotFound` (`0xe00002f0`) on missing physical storage; `vfs_mountroot()` probes root block device via `bdevvp()` and reports canonical `ENODEV` (`0x13`).
* **Completed Items**:
  - BSD kernel process (`kernproc`) and credential bootstrap.
  - VFS mount table and vnode allocation pools initialized.
  - Memory status jetsam snapshot buffer workaround.
  - Non-Apple silicon CTRR compatibility adaptation.
  - Thread call zone priming to eliminate early allocator lock contention.
  - Bounded root-device wait selftest in `IOFindBSDRoot()`.
  - Block device open error propagation in `bdevvp()`.
  - Automated +6s warm reboot to Fastboot upon reaching `[D51-TERMINAL]`.
* **Remaining Items**: None. Phase D1 acceptance gate fully passed.
* **Known Blockers**: None.
* **Dependencies**: Phase C.

---

### Phase D2 — Qualcomm MSM8996 UFS Controller Driver
* **Goal**: Implement a native IOKit controller driver for the Qualcomm Universal Flash Storage (UFS 2.0) host controller on MSM8996 (`0x00624000`).
* **Status**: **NOT STARTED**
* **Hardware Acceptance Criteria**: UFS host controller initialized, link startup completed, UFS PHY initialized, hardware interrupts routed via GICv3 (SPI 265), and SCSI/UFS NOP IN / REPORT LUNS command successfully executed on physical UFS storage.
* **Completed Items**: Hardware register addresses audited (`0x00624000`, size `0x2500`, PHY `0x00627000`, size `0x1000`).
* **Remaining Items**:
  - Implement `QualcommUFSController` IOKit driver class.
  - Configure Qualcomm MSM8996 UFS PHY and clock gating.
  - Map UFS MMIO aperture in ADT and kernel pmap.
  - Implement basic SCSI command execution engine.
  - Query LUN 0 / LUN 1 device geometry and serial number.
* **Known Blockers**: SMMU / IOMMU stage 1 translation bypass for DMA.
* **Dependencies**: Phase D1.

---

### Phase D3 — Block Device / GUID Partition Table (GPT)
* **Goal**: Publish physical `IOMedia` storage nubs in the IOKit registry and parse the disk GUID Partition Table (GPT).
* **Status**: **NOT STARTED**
* **Hardware Acceptance Criteria**: `IOBlockStorageDevice` publishes `disk0`; `IOMedia` represents physical disk partitions; GPT header and partition entries read from UFS sector 1–33.
* **Completed Items**: None.
* **Remaining Items**:
  - Implement `IOBlockStorageDriver` layer on top of `QualcommUFSController`.
  - Integrate Apple GPT / ApplePartitionScheme matching.
  - Expose partitions as `disk0s1`, `disk0s2`, etc.
  - Remove `sd0a` synthetic rootdev workaround and bounded wait loop.
* **Known Blockers**: None (standard IOKit storage architecture).
* **Dependencies**: Phase D2.

---

### Phase D4 — Real Root Filesystem Mount
* **Goal**: Mount an actual read-only root filesystem partition (APFS or HFS+) from physical UFS storage or ramdisk into VFS root vnode (`/`).
* **Status**: **NOT STARTED**
* **Hardware Acceptance Criteria**: `vfs_mountroot()` returns `0` (`KERN_SUCCESS`); `VFS_ROOT()` retrieves root directory vnode (`init_rootvnode != NULLVP`); `mountlist` shows `MNT_ROOTFS` active.
* **Completed Items**: None.
* **Remaining Items**:
  - Determine root filesystem format (HFS+ recommended for early bring-up simplicity, or ramdisk).
  - Prepare partition image containing minimal Darwin directory tree (`/sbin`, `/bin`, `/usr`, `/etc`, `/dev`).
  - Pass boot argument `rootdev=disk0sX` or `rd=disk0sX`.
  - Validate VFS directory lookup on `/`.
* **Known Blockers**: APFS encryption/container complexity (mitigated by using unencrypted HFS+ or ramdisk first).
* **Dependencies**: Phase D3.

---

### Phase E — PID 1 Bootstrap (`initproc` / launchd)
* **Goal**: Spawn the first Mach/BSD userspace process (`initproc` / PID 1) from the root filesystem.
* **Status**: **NOT STARTED**
* **Hardware Acceptance Criteria**: Kernel executes `bsd_utaskbootstrap()`; clones `initproc`; loads ARM64 Mach-O binary from `/sbin/launchd` via `execve()`; transitions to EL0 userspace.
* **Completed Items**: PAC signing verification bypass in `bsd/kern/kern_exec.c`.
* **Remaining Items**:
  - Provide statically linked ARM64 Darwin userspace binary.
  - Set up user address space, stack, and commpage in EL0.
  - Implement exception return to EL0 (`eret`).
  - Handle initial userspace system calls (`mach_trap` / BSD `syscall`).
* **Known Blockers**: Missing userspace libraries (libSystem / dyld) if dynamically linked.
* **Dependencies**: Phase D4.

---

### Phase F — Interactive Shell
* **Goal**: Establish an interactive command-line environment over the serial console.
* **Status**: **NOT STARTED**
* **Hardware Acceptance Criteria**: Interactive `/bin/sh` or micro-shell accepting keystrokes from Qualcomm UARTDM and displaying command output.
* **Completed Items**: None.
* **Remaining Items**:
  - TTY / devfs driver for `/dev/console` and `/dev/tty`.
  - Terse micro-shell or BSD sh compiled for arm64 without Apple PAC.
  - Basic POSIX utilities (`ls`, `cat`, `ps`, `uname`).
* **Known Blockers**: None once Phase E is achieved.
* **Dependencies**: Phase E.

---

### Phase G — Restore Deferred Subsystems
* **Goal**: Re-enable and canonicalize subsystems that were temporarily deferred during early bring-up.
* **Status**: **NOT STARTED**
* **Hardware Acceptance Criteria**: All 14 items in `docs/XZS_WORKAROUNDS.md` classified as `XZS-WORKAROUND` reviewed, tested, and restored to canonical upstream behavior.
* **Completed Items**: None.
* **Remaining Items**:
  - Restore canonical DTrace FBT and Profile providers.
  - Restore dynamic `memorystatus_jetsam_snapshot` allocation.
  - Restore TCP congestion control debug socket registration.
  - Cleanup cosmetic telemetry prefixes (`0x0x`).
* **Known Blockers**: None.
* **Dependencies**: Phase F.

---

### Phase H — Networking & Device Drivers
* **Goal**: Bring up network interfaces and hardware peripherals.
* **Status**: **NOT STARTED**
* **Hardware Acceptance Criteria**: `lo0` active; IP loopback ping functional; Qualcomm Wi-Fi (WCN3990 / ath10k) or USB gadget ethernet operational.
* **Completed Items**: None.
* **Remaining Items**:
  - Restore `loopattach()` and `lo0`.
  - Restore `ether_family_init()` and `gif_init()`.
  - Restore `skywalk_init()` with proper memory arena backing.
  - Restore TCP Fast Open with CoreCrypto AES provider.
  - USB controller driver (Synopsys DWC3 USB 3.0 at `0x06a00000`).
* **Known Blockers**: Qualcomm proprietary firmware blobs for Wi-Fi/Modem.
* **Dependencies**: Phase G.

---

### Phase I — Userspace & Platform Expansion
* **Goal**: Multi-process Darwin userspace environment.
* **Status**: **NOT STARTED**
* **Hardware Acceptance Criteria**: Multi-user execution, dynamic linking via `dyld`, networking services, and file management operating concurrently.
* **Dependencies**: Phase H.
