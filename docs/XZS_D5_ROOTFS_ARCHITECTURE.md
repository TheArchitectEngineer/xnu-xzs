# Xperia XZs XNU Phase D5 RootFS Architecture

This document specifies the technical architecture, filesystem inventory, runtime mechanisms, and execution roadmap for **Phase D5: Real Root Filesystem Mount** on the Sony Xperia XZs (`MSM8996` / Tone Keyaki / G8231 / Serial `BH905SX976`).

> [!IMPORTANT]
> **Architectural Boundary**: This document represents design, architecture, and source-level audit only. In accordance with strict phase discipline, **zero D5 implementation source code changes** have been made.

---

## 1. Certified D4 Baseline

Phase D4 is complete and sealed:
- **Git Checkpoint**: Commit `08545927ef153fc957e504a5a06d2c981c58de16` on `main` (synchronized with `xzs-port`).
- **Milestone Tag**: `xzs-d4-block-storage-complete`.
- **Historical D4 Branch**: `xzs-d4-block` at `1371952114b68cdb7fe7f3dcc74de3ffa7f8d4df`.
- **Hardware-Certified Storage State**:
  - Persistent eMMC runtime context verified (`g_xzs_emmc_ctx`, single initialization).
  - BSD `bdevsw` block device driver active with dynamic major (`1`).
  - Whole-disk devfs node `/dev/disk0` published (61,071,360 512-byte blocks).
  - Runtime GPT partition map loaded via block layer (`d_strategy`).
  - 55 partition slice devices published (`/dev/disk0s1` .. `/dev/disk0s55`).
  - C++ IOKit storage nub (`XZSeMMCStorageNub`) published to registry and discoverable via `IOBSDNameMatching("disk0")`.
  - Real block vnode acquisition via `bdevvp()` verified with 100% byte parity on LBA 1.
  - Zero storage writes (`CMD24=0`, `CMD25=0`, `ERASE=0`, `DISCARD=0`).
  - Global `rootdev` NOT mutated (`GLOBAL_ROOTDEV_MUTATED=no`).
  - Zero filesystem probes or rootfs selection performed (`FILESYSTEM_PROBES=0`, `ROOTFS_SELECTION_PERFORMED=no`).

---

## 2. Current BSD/VFS Boot State

In Phase D4, the kernel executed the D4 diagnostic probe from `bsd_init.c` at `BSD_POST_VFSINIT` (line 735). Upon successful verification of D4-M1 through D4-M6, the probe triggered an automated warm reboot to Fastboot via `xzs_spin_halt()`.

Consequently, in the D4 test configuration:
- `NORMAL_BOOT_REACHES_ROOT_DISCOVERY = no`
- `NORMAL_BOOT_REACHES_VFS_MOUNTROOT = no`

When the D4 diagnostic halt is removed, normal boot execution continues through `bsd_init()`:
1. `bsd_autoconf()` (line 848) initializes IOKit autoconfiguration and calls `IOKitBSDInit()`.
2. `setconf()` (line 1051) performs root device discovery via `IOFindBSDRoot()`.
3. `vfs_mountroot()` (line 1060) attempts to mount the root filesystem.
4. In Phase D1 (prior to storage bring-up), `IOFindBSDRoot()` timed out after 1.0s and fell back to synthetic `rootdev = makedev(6, 0)` ("sd0a"). `bdevvp(rootdev, &rootvp)` failed with `ENODEV` (0x13) because `bdevsw[6]` was unpopulated, halting at `[D51-TERMINAL]`.

---

## 3. Root Mount Call Chain

The exact execution chain in this XNU tree is:

```text
bsd_init()                                    [src/xnu/bsd/kern/bsd_init.c:1044]
  │
  ├──> setconf()                              [src/xnu/bsd/kern/bsd_init.c:1309]
  │      │
  │      └──> IOFindBSDRoot()                 [src/xnu/iokit/bsddev/IOKitBSDInit.cpp:676]
  │             ├──> Parse boot-args: "rd=", "rootdev="
  │             ├──> If "rd=md*":
  │             │      Check /chosen/memory-map for "RAMDisk"
  │             │      mdevadd(-1, base >> 12, size >> 12, 0)
  │             │      *root = mdevlookup(unit) -> dev_t
  │             ├──> If "rd=disk0*":
  │             │      matching = IOBSDNameMatching(name)
  │             │      Match XZSeMMCStorageNub
  │             │      *root = makedev(major, minor)
  │             └──> If no rd:
  │                    matching = serviceMatching("IOMedia", "Apple_HFS")
  │                    Wait for matching IOMedia
  │
  ├──> vfs_mountroot()                        [src/xnu/bsd/vfs/vfs_subr.c:1203]
  │      │
  │      ├──> bdevvp(rootdev, &rootvp)        [src/xnu/bsd/vfs/vfs_subr.c:1240]
  │      │      Create VBLK vnode for rootdev, VNOP_OPEN(FREAD)
  │      │
  │      ├──> Iterate vfsconf table           [src/xnu/bsd/vfs/vfs_subr.c:1256]
  │      │      For each vfsp in vfsconf:
  │      │        if (!vfsp->vfc_mountroot && !ISSET(vfc_vfsflags, VFC_VFSCANMOUNTROOT)) continue;
  │      │        mp = vfs_rootmountalloc_internal(vfsp, "root_device");
  │      │        error = (*vfsp->vfc_mountroot)(mp, rootvp, ctx) OR VFS_MOUNT(mp, rootvp, 0, ctx);
  │      │        if (!error) break;
  │      │
  │      └──> If no filesystem matched: return ENODEV (0x13)
  │
  ├──> Post-Mountroot Setup                   [src/xnu/bsd/kern/bsd_init.c:1092]
  │      mountlist.tqh_first->mnt_flag |= MNT_ROOTFS;
  │      VFS_ROOT(mountlist.tqh_first, &init_rootvnode, vfs_context_kernel());
  │      set_rootvnode(init_rootvnode);
  │
  ├──> devfs_kernel_mount("/dev")             [src/xnu/bsd/kern/bsd_init.c:1178]
  │      Mount devfs at /dev on root filesystem
  │
  ├──> bsd_utaskbootstrap()                   [src/xnu/bsd/kern/bsd_init.c:1224]
  │      Clone process 0 to create process 1 (PID 1)
  │
  └──> load_init_program(p)                   [src/xnu/bsd/kern/kern_exec.c:7382]
         Executes "/sbin/launchd" (or launchd.$LAUNCHDSUFFIX)
```

**Required Symbols:**
- `ROOT_MOUNT_ENTRYPOINT = vfs_mountroot` (`src/xnu/bsd/vfs/vfs_subr.c:1203`)
- `ROOT_DEVICE_SELECTION_FUNCTION = setconf` (`src/xnu/bsd/kern/bsd_init.c:1309`) -> `IOFindBSDRoot` (`src/xnu/iokit/bsddev/IOKitBSDInit.cpp:676`)
- `ROOT_BLOCK_VNODE_FUNCTION = bdevvp` (`src/xnu/bsd/vfs/vfs_subr.c:1240`)
- `ROOT_FS_MOUNT_DISPATCH_FUNCTION = vfs_mountroot` (iterates `vfsconf` list via `vfc_mountroot`)

---

## 4. Filesystem Source Inventory

A comprehensive audit of the `src/xnu` repository for filesystem implementations reveals:

| Filesystem | Source Path | VFS Registration | Mount Function | Read Support | Write Support | Root-Mount Capable | Build Inclusion |
| :--- | :--- | :--- | :--- | :---: | :---: | :---: | :---: |
| **devfs** | `bsd/miscfs/devfs/` | `devfs_vfsops` | `devfs_mount` | YES | YES | **NO** (`vfc_mountroot = NULL`) | BUILT & LINKED |
| **mockfs** | `bsd/miscfs/mockfs/` | `mockfs_vfsops` | `mockfs_mountroot` | YES | NO | **YES** (`vfc_mountroot` set) | PRESENT (Config gated) |
| **nullfs** | `bsd/miscfs/nullfs/` | `nullfs_vfsops` | `nullfs_mount` | YES | YES | **NO** (`vfc_mountroot = NULL`) | BUILT & LINKED |
| **bindfs** | `bsd/miscfs/bindfs/` | `bindfs_vfsops` | `bindfs_mount` | YES | YES | **NO** (`vfc_mountroot = NULL`) | BUILT & LINKED |
| **routefs**| `bsd/miscfs/routefs/`| `routefs_vfsops` | `routefs_mount` | YES | NO | **NO** (`vfc_mountroot = NULL`) | BUILT & LINKED |
| **hfs** | *NOT IN TREE* | N/A | N/A | N/A | N/A | N/A | NOT PRESENT |
| **apfs** | *NOT IN TREE* | N/A | N/A | N/A | N/A | N/A | NOT PRESENT |
| **ext2/3/4**| *NOT IN TREE* | N/A | N/A | N/A | N/A | N/A | NOT PRESENT |
| **f2fs** | *NOT IN TREE* | N/A | N/A | N/A | N/A | N/A | NOT PRESENT |
| **msdos** | *NOT IN TREE* | N/A | N/A | N/A | N/A | N/A | NOT PRESENT |
| **exfat** | *NOT IN TREE* | N/A | N/A | N/A | N/A | N/A | NOT PRESENT |
| **ramfs** | *NOT IN TREE* | N/A | N/A | N/A | N/A | N/A | NOT PRESENT |
| **tmpfs** | *NOT IN TREE* | N/A | N/A | N/A | N/A | N/A | NOT PRESENT |

**Mandatory Inventory Keys:**
```text
HFS_PRESENT=no
APFS_PRESENT=no
EXT4_PRESENT=no
F2FS_PRESENT=no
MSDOS_PRESENT=no
MOCKFS_PRESENT=yes
RAMFS_PRESENT=no
```

> [!NOTE]
> In canonical macOS/Darwin, on-disk filesystems (HFS+, APFS, FAT, exFAT) are implemented in external kernel extensions (`hfs.kext`, `apfs.kext`, `msdosfs.kext`) loaded by the bootloader/kernelcache. The core open-source XNU repository contains only virtual and pseudo filesystems.

---

## 5. Filesystem Runtime/Link Inventory

Inspection of the compiled `kernel.development.vmapple` binary via `nm`:
- **`devfs`**: `devfs_vfsops`, `devfs_mount`, `devfs_lookup` are **LINKED**.
- **`mockfs`**: **NOT LINKED** in current VMAPPLE configuration (`MOCKFS` config option was not enabled).
- **`apfs`**: Only helper lock stubs (`kern_apfs_reflock_*`) and sysctl (`sysctl_apfsprebootuuid`) are linked. **NO APFS filesystem driver exists**.
- **`hfs`**: Only policy stubs (`iopolicysys_vfs_hfs_case_sensitivity`) are linked. **NO HFS filesystem driver exists**.

```text
FILESYSTEM_SOURCE_PRESENT != FILESYSTEM_RUNTIME_AVAILABLE
```

Currently, the runtime kernel contains **ZERO** disk filesystem drivers capable of mounting a root filesystem (`vfc_mountroot`).

---

## 6. RAMDisk Support

Source audit of `src/xnu/bsd/dev/memdev.c` and `src/xnu/iokit/bsddev/IOKitBSDInit.cpp`:
- **In-Tree Driver**: `memdev.c` implements a complete RAM disk driver providing both `bdevsw` (`mdevbdevsw`) and `cdevsw` (`mdevcdevsw`).
- **Devices**: Exposes `/dev/md0` through `/dev/md15`.
- **Kernel API**: `dev_t mdevadd(int devid, uint64_t base, unsigned int size, int phys)` initializes a memory device with base page address and page count.
- **Device Tree Interface**: `IOFindBSDRoot()` checks `/chosen/memory-map` for the `"RAMDisk"` property:
  ```cpp
  data = (OSData *)regEntry->getProperty("RAMDisk");
  ramdParms = (uintptr_t *)data->getBytesNoCopy();
  mdevadd(-1, ml_static_ptovirt(ramdParms[0]) >> 12, ramdParms[1] >> 12, 0);
  ```
- **Boot-Arg Selection**: `rd=md0` or `rootdev=md0` instructs `IOFindBSDRoot()` to return `*root = mdevlookup(0)`.
- **Buffer Cache Support**: `mdevstrategy()` fully supports standard `buf_t` read requests.
- **Ioctl Support**: Implements `DKIOCGETMEMDEVINFO` (`dk_memdev_info_t`), reporting `mi_base`, `mi_size`, `mi_mdev=1`, `mi_phys`.

```text
RAMDISK_ROOT_SUPPORTED=yes
```

---

## 7. Imageboot / root-dmg Support

Source audit of `src/xnu/bsd/kern/imageboot.c` and `src/xnu/iokit/bsddev/DINetBootHook.cpp`:
- **Kernel Component**: `imageboot.c` is compiled and linked in `kernel.development.vmapple`.
- **Disk Image Attach**: Relies on `di_root_ramfile_buf()` and `di_load_controller()`.
- **External Dependency**: `di_load_controller()` queries IOKit matching for class `"IOHDIXController"`:
  ```cpp
  matchDictionary = IOService::serviceMatching("IOHDIXController");
  ```
- **Finding**: `IOHDIXController` is part of Apple's closed-source `DiskImages.kext`. It does **NOT** exist in `src/xnu`.
- **Result**: Without `IOHDIXController`, `imageboot` fails with `kIOReturnNotFound` (0xe00002bc).

```text
IMAGEBOOT_PRESENT=yes
ROOT_DMG_BOOTARG_SUPPORTED=yes (code present)
IMAGEBOOT_FILESYSTEM_REQUIREMENT=IOHDIXController (DiskImages.kext) + APFS/HFS kext
IMAGEBOOT_FUNCTIONAL_STANDALONE=no
```

---

## 8. Android Partition Filesystem Survey

Inspection of Sony Xperia XZs physical eMMC partitions from authoritative recovery and pstore logs:

| Partition | GPT Slot | BSD Slice | Size | Filesystem | Encrypted? | TWRP Mountable? |
| :--- | :---: | :---: | :---: | :---: | :---: | :---: |
| `boot` | 29 | `disk0s30` | 64 MB | Android Boot Image (`ANDROID!`) | NO | N/A (raw image) |
| `cache` | 42 | `disk0s43` | 256 MB | `ext4` | NO | YES |
| `FOTAKernel` | 47 | `disk0s48` | 64 MB | Android Boot Image (TWRP) | NO | N/A (raw image) |
| `oem` | 52 | `disk0s53` | 400 MB | `ext4` | NO | YES |
| `userdata` | 53 | `disk0s54` | 21.3 GB | `ext4` (feature `0x2000` = FBE) | **YES** | Requires decryption |
| `system` | 54 | `disk0s55` | 7.35 GB | `ext4` (read-only) | NO | YES |

**Comparison with XNU:**
- Android partitions use `ext4`.
- Current XNU has **0 lines of ext4 code**.
- Porting an ext4 filesystem driver into XNU is out of scope and high-risk.

```text
ANDROID_SYSTEM_FS_SUPPORTED_BY_CURRENT_XNU=no
```

---

## 9. Root Device Selection Mechanisms

| Mechanism | Description | In-Tree Support | XZS Compatibility |
| :--- | :--- | :---: | :---: |
| **A. Fixed `rootdev`** | Hardcode `rootdev = makedev(...)` in `setconf()` | YES | Compatible, but inflexible |
| **B. Boot-arg `rd=disk0sN`** | Matched via `IOBSDNameMatching` to `XZSeMMCStorageNub` | YES | **100% Compatible** (D4-M5 verified) |
| **C. Boot-arg `rd=md0`** | Matched to `/dev/md0` via `mdevlookup(0)` | YES | **100% Compatible** (RAMDisk) |
| **D. IOFindBSDRoot Default** | Matches `IOMedia` with `Content="Apple_HFS"` | YES | Requires `Apple_HFS` property |
| **E. Imageboot / root-dmg** | Attaches DMG via `IOHDIXController` | NO | Requires missing `DiskImages.kext` |

---

## 10. Mach-O Exec Requirements

Source audit of `src/xnu/bsd/kern/mach_loader.c`:
1. **Dynamic Executables (`MH_DYLDLINK`)**:
   - Require `LC_LOAD_DYLINKER` pointing to `/usr/lib/dyld`.
   - If `dlp == NULL`, `load_machfile()` returns `LOAD_FAILURE`.
   - Dyld requires `libSystem.dylib` and dyld shared cache.
2. **Static Executables (NO `MH_DYLDLINK`, NO `LC_LOAD_DYLINKER`)**:
   - `mach_loader.c:1153-1156`:
     ```c
     #if !(DEVELOPMENT || DEBUG)
         return LOAD_FAILURE;
     #endif
     ```
     In `DEVELOPMENT` kernels (which we build), **static executables are explicitly allowed**!
   - Must use `LC_UNIXTHREAD` (load command `0x5`) to set register state (`entry_point`, `user_stack`, PC, SP).
   - Must **NOT** use `LC_MAIN` (load command `0x80000028`), because `LC_MAIN` sets `result->needs_dynlinker = TRUE` (the kernel expects dyld to jump to `entryoff`).
   - System calls are made directly via `svc #0x80` with standard ARM64 Darwin syscall calling convention (syscall number in `x16`, arguments in `x0..x5`, return in `x0`).

```text
STATIC_MACHO_USERSPACE_FEASIBLE=yes
```

---

## 11. PID 1 Requirements

Source audit of `src/xnu/bsd/kern/kern_exec.c:7342`:
- `load_init_program()` attempts to load executables in the following exact order:
  1. `/usr/appleinternal/sbin/launchd.$LAUNCHDSUFFIX` (if `launchdsuffix` boot-arg set)
  2. `/usr/appleinternal/sbin/launchd.development` (in DEVELOPMENT kernels)
  3. `/sbin/launchd`
- There is **no fallback to `/sbin/init` or `/bin/sh`**.
- Therefore, the root filesystem **MUST** contain an executable at `/sbin/launchd` (or `/sbin/launchd.development`).

```text
DEFAULT_INIT_PATH=/sbin/launchd
INIT_FALLBACK_PATHS=/usr/appleinternal/sbin/launchd.development
```

---

## 12. Console Requirements

Source audit of `src/xnu/bsd/miscfs/devfs/devfs_vfsops.c` and `src/xnu/bsd/dev/arm/cons.c`:
- `devfs_init()` creates:
  - `/dev/console` (cdev major 0, minor 0)
  - `/dev/tty` (cdev major 2, minor 0)
- `cdevsw[0]` is `{ cnopen, cnclose, cnread, cnwrite, cnioctl, ... }`.
- `cnread` and `cnwrite` route directly to `km_tty[0]` and `cons_ops`.
- `cons_ops` is backed by `pe_serial.c` (Qualcomm BLSP2 UARTDM at `0x075b0000` @ 115200 8N1).
- Therefore, userspace `/dev/console` is **fully functional** as soon as `devfs` is mounted at `/dev`.

```text
USERSPACE_CONSOLE_PATH=/dev/console
USERSPACE_UART_CONSOLE_FEASIBLE=yes
DEVFS_MOUNTED_SEPARATELY=yes (mounted at /dev by bsd_init immediately after mountroot)
```

---

## 13. Audit of `mockfs`

Source audit of `src/xnu/bsd/miscfs/mockfs/`:
- **Purpose**: "Boot from an executable". Fake filesystem designed for single-binary testing.
- **Structure**:
  - Memory-backed tree with exactly 3 nodes:
    1. `MOCKFS_ROOT`: Directory node representing `/`.
    2. `MOCKFS_DEV`: Directory node representing `/dev` (mountpoint for devfs).
    3. `MOCKFS_FILE`: Regular file node representing the root device as an executable.
- **Lookup Behavior** (`mockfs_lookup`):
  - `"sbin"` -> resolves to `/`
  - `"dev"` -> resolves to `MOCKFS_DEV` (`/dev`)
  - `"launchd"` -> resolves to `MOCKFS_FILE`
  - **ALL OTHER NAMES RETURN `ENOENT`**!
- **User I/O**:
  - `VNOP_OPEN` is **NOT IMPLEMENTED**.
  - `mockfs_read()` only supports kernel `cluster_read()` during `load_machfile()`.
  - Regular file creation, deletion, modification: **NOT SUPPORTED**.
- **Can it serve as rootfs?**:
  - For running a single monolithic `launchd` binary: **YES**.
  - For a general rootfs with multiple binaries (`/bin/sh`, `/bin/ls`), configuration files, or scripts: **NO**.

```text
MOCKFS_ROOTFS_USABLE=no (for general multi-file rootfs and interactive shell)
MOCKFS_ROOTFS_USABLE=yes (strictly as a single-binary bootstrap test vehicle)
```

---

## 14. Candidate RootFS Architectures

| Dimension | Candidate 1: `mockfs` on RAMDisk | Candidate 2: Custom Read-Only FS (`xzsfs`) on RAMDisk | Candidate 3: Physical eMMC `ext4` Port |
| :--- | :--- | :--- | :--- |
| **Backing Device** | `/dev/md0` (RAMDisk) | `/dev/md0` (RAMDisk) | `/dev/disk0s55` (system) |
| **eMMC Mutation** | **ZERO WRITES** | **ZERO WRITES** | ZERO WRITES (read-only) |
| **Filesystem Driver** | In-tree `mockfs` (needs config enable) | Minimal in-tree VFS driver (~350 LOC) | Full `ext4` port (~5,000+ LOC) |
| **Directory Support** | Only `/` and `/dev` | Full directory tree (`/bin`, `/sbin`, etc.) | Full directory tree |
| **File Count** | Exactly 1 binary (`launchd`) | Multiple binaries (`launchd`, `sh`, etc.) | Full Android filesystem |
| **User I/O (`open/read`)**| NO (`VNOP_OPEN` missing) | YES | YES |
| **Shell Support** | NO | **YES** | YES |
| **Complexity / Risk** | Very Low / Zero flash risk | Low / Zero flash risk | Extremely High / Regressions |
| **Time to First Shell** | Fast (but shell impossible) | **Fastest viable path to shell** | Very Slow |

---

## 15. Recommended RootFS Strategy

```text
D5_RECOMMENDED_ROOTFS_STRATEGY = RAMDISK_XZSFS
```

**Architecture Rationale:**
1. **Zero Flash Risk**: Running from a RAMDisk completely honors the `ZERO_STORAGE_WRITES=yes` invariant. The phone's eMMC is untouched.
2. **Fastboot Deployment**: The RAMDisk image is bundled into the Android boot image (`xzs-xnu-boot.img`) and loaded into DRAM by `fastboot boot`, making development 100% deterministic and safe.
3. **Multi-File Capability**: Unlike `mockfs` (which can never run a shell because it only exposes `launchd`), a minimal clean read-only VFS driver (`xzsfs`) provides a real directory tree (`/sbin/launchd`, `/bin/sh`, `/dev/console`), enabling both PID 1 and Phase F (interactive shell).
4. **Minimal Footprint**: An `xzsfs` image format can be as simple as an uncompressed flat archive (header, directory entries, file extents) that maps directly to memory pages, requiring only ~350 lines of clean C in `src/xnu/bsd/miscfs/xzsfs/`.

---

## 16. Proposed Phase D5 Milestone Breakdown

```text
D5-M1: RootFS Host Tooling & Static Userspace Generator
       - Host script (mkxzsfs.py) to generate minimal xzsfs image.
       - Minimal static ARM64 Mach-O binaries: /sbin/launchd and /bin/sh (using LC_UNIXTHREAD).
       - Image contains: /, /dev, /sbin/launchd, /bin/sh.

D5-M2: RAMDisk Device Integration (/dev/md0)
       - Bootshim / chosen node populates /chosen/memory-map/RAMDisk.
       - IOFindBSDRoot() discovers RAMDisk, calls mdevadd(), binds rd=md0.
       - bdevvp(rootdev, &rootvp) acquires block vnode for /dev/md0.

D5-M3: Minimal Read-Only VFS Driver (xzsfs)
       - Implements xzsfs_mountroot, xzsfs_vfsops, xzsfs_vnops.
       - Registered in vfsconf with VFC_VFSCANMOUNTROOT.
       - Read-only lookup, getattr, read, readdir via buffer cache.

D5-M4: Real VFS Root Mount & Root Vnode Acquisition
       - vfs_mountroot() successfully mounts /dev/md0 via xzsfs.
       - VFS_ROOT() returns non-NULL rootvnode.
       - devfs mounted at /dev.

D5-M5: Path Lookup & Exec Preparation
       - Kernel namei lookup of "/" and "/sbin/launchd" succeeds.
       - Lookup of "/dev/console" succeeds.

D5-M6: Phase D5 Documentation Seal & Baseline Integration
       - Full silicon verification telemetry.
       - Commit, merge to main, tag xzs-d5-rootfs-complete.
```

---

## 17. First D5 Implementation Milestone & Silicon Acceptance Test

```text
D5_FIRST_IMPLEMENTATION_MILESTONE = D5-M1_ROOTFS_TOOLING_AND_RAMDISK_INTEGRATION
```

**First Silicon Acceptance Test (D5-M4):**
```text
D5_FIRST_SILICON_ACCEPTANCE_TEST:
1. Boot kernel with embedded RAMDisk image via fastboot boot.
2. IOFindBSDRoot() selects /dev/md0 (rd=md0).
3. bdevvp(rootdev, &rootvp) succeeds (error = 0).
4. vfs_mountroot() mounts xzsfs root filesystem (error = 0).
5. VFS_ROOT() returns valid rootvnode != NULLVP.
6. Namei lookup of "/" succeeds.
7. Namei lookup of "/sbin/launchd" succeeds.
8. devfs_kernel_mount("/dev") succeeds.
9. Diagnostic halt BEFORE execve/PID 1.
```

---

## 18. Risks / Unknowns

1. **Static Mach-O Compatibility**:
   - Must use `LC_UNIXTHREAD` rather than `LC_MAIN` to avoid triggering dyld expectation in `load_machfile()`.
   - Toolchain must produce clean ARM64 Mach-O without Apple PAC signatures (`-mno-ptrauth`).
2. **Page Size & VM Alignment**:
   - XNU ARM64 user VM map uses 16KB page shift (`SIXTEENK_PAGE_SHIFT`).
   - Binaries in rootfs must have segments aligned to 16KB boundaries (`0x4000`).
3. **RAM Size Constraints**:
   - Xperia XZs has 4GB LPDDR4 DRAM.
   - A minimal rootfs containing launchd and a micro-shell is < 2 MB, using < 0.05% of available RAM.

---

## 19. Deferred Work

- Read-write filesystems (Phase D5 is strictly read-only).
- Physical eMMC rootfs repartitioning (deferred to maintain zero flash mutation).
- Dynamic linking / `dyld` / `libSystem.dylib` (Phase E uses static Mach-O first).
- Multi-user authentication / PAM / accounts (single root user).

---

## 20. Milestone Status

- **D5-M1 (Format Freeze & Host Tooling)**: COMPLETE (2026-09-19)
  - `XZSFS_FORMAT_V1_FROZEN=yes` (`docs/XZSFS_FORMAT_V1.md`)
  - `XZSFS_IMAGE_GENERATOR_COMPLETE=yes` (`scripts/mkxzsfs.py`)
  - `XZSFS_HOST_VERIFIER_COMPLETE=yes` (`scripts/verify_xzsfs.py`)
  - `XZSFS_DETERMINISTIC_BUILD=yes`
  - `XZSFS_HOST_STRUCTURAL_VERIFY=yes`
  - `XZSFS_HOST_PAYLOAD_VERIFY=yes`
  - `XZSFS_IMAGE_SIZE=35840`
  - `XZS_INIT_MACHO_STATIC_FORMAT_VERIFIED=yes`
  - `XZS_SH_MACHO_STATIC_FORMAT_VERIFIED=yes`
  - `D5_KERNEL_IMPLEMENTATION_STARTED=no`
  - `D5_SILICON_EXECUTION_STARTED=no`
- **D5-M2 (RAMDisk Block Transport)**: PENDING
- **D5-M3 (Kernel XZSFS VFS Parser)**: PENDING
- **D5-M4 (Silicon Root Mount Proof)**: PENDING