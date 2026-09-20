# Phase D5: Root Filesystem Architecture & Silicon Verification

This document records the definitive, hardware-verified architecture of **Phase D5 (Real Root Filesystem Mount)** on the Sony Xperia XZs (Qualcomm MSM8996).

---

## 1. Complete Architectural Stack

The final certified D5 runtime stack operates entirely within kernel mode (EL1), terminating strictly at the threshold of userspace execution:

```text
       ┌─────────────────────────────────────────────────────────────┐
       │             Qualcomm MSM8996 (Kryo ARM64 SoC)               │
       └──────────────────────────────┬──────────────────────────────┘
                                      │
                                      ▼
       ┌─────────────────────────────────────────────────────────────┐
       │                 Apple XNU Kernel Runtime (EL1)              │
       └──────────────────────────────┬──────────────────────────────┘
                                      │
                                      ▼
       ┌─────────────────────────────────────────────────────────────┐
       │              BSD Subsystem & VFS Core Framework             │
       └──────────────────────────────┬──────────────────────────────┘
                                      │
                                      ▼
       ┌─────────────────────────────────────────────────────────────┐
       │     RAMDisk Pseudo-Block Device (`md0`, major 2, minor 0)   │
       │     ADT `/chosen/memory-map/RAMDisk` -> `rd=md0` boot-arg   │
       └──────────────────────────────┬──────────────────────────────┘
                                      │
                                      ▼
       ┌─────────────────────────────────────────────────────────────┐
       │       XZSFS VFS Filesystem Driver (Read-Only Type 0x585A)   │
       │       Deterministic 128-byte header, SHA-256 metadata CRC   │
       └──────────────────────────────┬──────────────────────────────┘
                                      │
                                      ▼
       ┌─────────────────────────────────────────────────────────────┐
       │                   Real `mount_t` Structure                  │
       │                   Mounted at `/` with `MNT_RDONLY`          │
       └──────────────────────────────┬──────────────────────────────┘
                                      │
                                      ▼
       ┌─────────────────────────────────────────────────────────────┐
       │          Real Root Vnode (`VDIR` | `VROOT`, ID 1)           │
       └──────────────────────────────┬──────────────────────────────┘
                                      │
                                      ▼
       ┌─────────────────────────────────────────────────────────────┐
       │         Global `rootvnode` Published to Kernel Namespace    │
       └──────────────┬──────────────────────────────┬───────────────┘
                      │                              │
                      ▼                              ▼
  ┌─────────────────────────────────────┐  ┌─────────────────────────┐
  │  XZSFS Inode Lookup & Traversal     │  │  Kernel devfs Mount     │
  │  - `/sbin/launchd` (VREG, ID 7)     │  │  Mounted at `/dev`      │
  │  - `/bin/sh`       (VREG, ID 3)     │  │  `devfs_kernel_mount()` │
  └─────────────────────────────────────┘  └────────────┬────────────┘
                                                        │
                                                        ▼
                                           ┌─────────────────────────┐
                                           │ `/dev/console` (VCHR)   │
                                           │ Major 0, Minor 0        │
                                           └─────────────────────────┘
```

---

## 2. Canonical Telemetry & Runtime Invariants

The following invariants have been validated across physical runs on Sony Xperia XZs G8231 silicon:

```text
ROOTDEV_IS_MD0=yes

ROOT_FS_TYPE=xzsfs
ROOT_FS_DEVICE=md0

GLOBAL_ROOTVNODE_INSTALLED=yes

NAMEI_ROOT_PASS=yes
NAMEI_SBIN_LAUNCHD_PASS=yes
NAMEI_BIN_SH_PASS=yes

LAUNCHD_VNODE_TYPE=VREG
LAUNCHD_MODE=0755
LAUNCHD_FILEID=7
LAUNCHD_SIZE=16472

BIN_SH_VNODE_TYPE=VREG
BIN_SH_MODE=0755
BIN_SH_FILEID=3
BIN_SH_SIZE=16472

DEVFS_MOUNTED=yes
DEVFS_KERNEL_MOUNT_RETURN=0
NAMEI_DEV_PASS=yes
NAMEI_DEV_CONSOLE_PASS=yes

DEV_CONSOLE_VNODE_TYPE=VCHR
DEV_CONSOLE_MAJOR=0
DEV_CONSOLE_MINOR=0

NAMESPACE_DEVFS_OVERLAY_VERIFIED=yes

XZSFS_MOUNT_READ_ONLY=yes
ZERO_STORAGE_WRITES=yes

PID1_STARTED=no
EXECVE_ATTEMPTED=no
EL0_ENTRY_ATTEMPTED=no

CMD24_COUNT=0
CMD25_COUNT=0

ROADMAP_ADVANCED_TO=D6
```

---

## 3. Critical D5 Runtime Fixes & Classifications

During the bring-up and stabilization of Phase D5, three runtime blockers were analyzed, resolved, and verified on hardware. Their exact classification and rationale are recorded below:

### 1. 88 MiB Boot-Args Memory Contract Bound
- **Classification**: **PLATFORM FIX**
- **Location**: `src/xnu/osfmk/arm/arm_init.c`
- **Rationale**: The Qualcomm MSM8996 physical memory layout contains non-contiguous DRAM carveouts for modem, trustzone (TZ/QSEE), GPU, and camera subsystems. When `boot_args.memSize` and `memSizeActual` were unconstrained or reported the entire 3–4 GiB range, kernel zone allocators and pmap bootstrap attempted to allocate data structures exceeding available early contiguous physical frames, inducing runtime memory exhaustion or page faults. Bounding `memSize` and `memSizeActual` to 88 MiB (`0x05800000`) establishes an audited, deterministic memory envelope that guarantees safe bootstrap across early initialization.

### 2. `IOSecureBSDRoot` Platform Hook Bypass
- **Classification**: **BRING-UP WORKAROUND**
- **Location**: `src/xnu/bsd/kern/bsd_init.c`
- **Rationale**: Upstream macOS/iOS XNU invokes `IOSecureBSDRoot()` during root filesystem acquisition to query Apple SEP / Apple Image4 secure root manifest verification. On Qualcomm MSM8996 silicon, Apple security co-processors and platform policy drivers do not exist. Attempting to call the unbacked stub resulted in failed root acquisition. Under `#ifdef CONFIG_XZS_BRINGUP`, this call is safely bypassed to allow direct mount of the verified RAMDisk root filesystem.

### 3. devfs `VM_KERNEL_ADDRHASH` Workaround
- **Classification**: **XZS WORKAROUND**
- **Location**: `src/xnu/bsd/miscfs/devfs/devfs_vfsops.c`
- **Rationale**: During `devfs_mount()`, canonical XNU attempts to generate an address hash for the mount point structure via `VM_KERNEL_ADDRHASH(devfs_mp_p)` to harden pointer isolation. During single-core bootstrap prior to cryptographic entropy generator initialization, the underlying hashing loop entered an infinite stall. Under `#ifdef CONFIG_XZS_BRINGUP`, this is substituted with a static tag `0x64657666` (`'devf'`), enabling `devfs_kernel_mount("/dev")` to complete in less than 1 millisecond.

---

## 4. Hardware Regression Continuity Matrix

All milestones comprising Phase D5 have passed physical silicon gates and are permanently sealed:

| Milestone | Subsystem | Checkpoint | Invariant Proven | Silicon Status |
| :--- | :--- | :--- | :--- | :--- |
| **D5-M1** | XZSFS Image Format | Tooling Gate | 128B header, SHA256 metadata, determinism | **SEALED** |
| **D5-M2** | RAMDisk Block Transport | `0xD510` | ADT `/chosen/memory-map/RAMDisk` -> `md0` | **SEALED** |
| **D5-M3** | In-Kernel XZSFS Driver | `0xD520` | Native VFS operations, inode lookup, read, `EROFS` | **SEALED** |
| **D5-M4** | Real Root Mount Proof | `0xD530` | `vfs_mountroot()`, real `mount_t`, global `rootvnode` | **SEALED** |
| **D5-M5** | Namespace & devfs Console | `0xD540` | `namei(/)` -> rootvnode, `/dev/console` cdev 0:0 | **SEALED** |
| **D5-M6** | Final Seal & Continuity | `0xD550` | `namei(/bin/sh)` VREG, full regression, zero writes | **SEALED** |

---

## 5. Frozen D5 Evidence Footprint

- **Milestone Git Tag**: `xzs-d5-rootfs-complete` (Commit `789c68c4412a46a40b6c9982b45f8b75e3877350`)
- **Main Merge Commit**: `a47c13dbfa777c688849fb74bbd89a74aa96df1a`
- **Acceptance Verifier**: `scripts/verify_d5_final_acceptance.py` (100% PASS)
- **Primary Report**: `artifacts/reports/D5_M6_FINAL_SEAL_REPORT.md`
- **Archived Silicon Artifacts**: `artifacts/archive/d5-final-pass/`
- **Archived Silicon Console Logs**: `artifacts/logs/d5-final-pass/console.log`
