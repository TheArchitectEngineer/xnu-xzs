# Xperia XZs / Qualcomm MSM8996 Port Roadmap

This document outlines the technical architecture roadmap for bringing up Apple XNU natively on Sony Xperia XZs (Qualcomm MSM8996).

Progress is strictly gated by physical hardware verification. Speculative percentages and estimated completion dates (ETAs) are deliberately omitted.

---

## 1. Executive Status Summary

| Phase | Description | Current Status |
| :--- | :--- | :---: |
| **Phase A** | Native kernel entry (Bootshim, ADT, MMU, High KVA) | **COMPLETE** |
| **Phase B** | Platform bring-up (UARTDM, GICv3, Timer, Pmap, VM) | **COMPLETE** |
| **Phase C** | SMP / Mach scheduler (PSCI, 4 Kryo cores, IPI, AST, Preemption) | **COMPLETE** |
| **Phase D1** | BSD / VFS bootstrap to root-storage boundary | **COMPLETE** |
| **Phase D2** | Physical eMMC storage bring-up (SDCC1, CMD0..CMD17, PIO) | **COMPLETE** |
| **Phase D3** | GUID Partition Table (GPT) discovery & partition enumeration | **COMPLETE** |
| **Phase D4** | Block-storage driver integration (`bdevsw` / `disk0`) | **COMPLETE** |
| **Phase D5** | Real root filesystem mount (HFS+ / APFS / ramdisk) | **NOT STARTED** |
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
* **Status**: **COMPLETE**
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
* **Status**: **COMPLETE**
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

### Phase D2 — Physical eMMC Storage Bring-up
* **Goal**: Drive Qualcomm MSM8996 SDCC1 / SDHCI v5 host controller (`0x07464900`) and establish physical data transfers with the Samsung BJNB4R 32GB eMMC 5.1 device.
* **Status**: **COMPLETE**
* **Hardware Acceptance Criteria**: SDC1 clock configured at 400 kHz; host controller reset; MMC full bus initialization sequence completed (CMD0 -> CMD1 -> CMD2 -> CMD3 -> CMD9 -> CMD7 -> CMD8 -> CMD17); single-block physical sector read (LBA 1) matches independent TWRP oracle byte-for-byte (`e4b891b42fd57eb352ffbe0aa9098d04fe85f88e3425dcba529064cce72f862a`).
* **Completed Milestones**:
  - **D2-M1 (SDC1 Audit & Identity Oracle)**: ✅ Probed host version (`0x4902`), capabilities (`0x742dc8b2`), HC mode (`0x00000001`).
  - **D2-M2 (Clock & Reset Replay)**: ✅ 400-kHz RCG configuration (`F(400000, P_XO, 12, 1, 4)`), `SDHCI_RESET_ALL` cleared in 10 µs.
  - **D2-M3 (Host Power & Card Clock)**: ✅ 1.8V bus power (`0x0B`), internal/card clock active (`0x0007`), timeout `0x0F`.
  - **D2-M4A (CMD0 Go Idle)**: ✅ Issued `CMD0` (`0x0000`), hardware execution verified.
  - **D2-M4B/C (CMD1 Card Power-Up)**: ✅ Polled `CMD1` until `CARD_READY=yes`, `FINAL_OCR=0xC0FF8080` (Sector Mode).
  - **D2-M4D-A (CMD2 CID Identification)**: ✅ `CID_MATCH=yes` (`150100424a4e4234520fdac7c0381400`, Samsung BJNB4R).
  - **D2-M4D-B (CMD3 RCA Assignment)**: ✅ Assigned `RCA = 2`, verified transition to STBY state (`R1 = 0x00000500`).
  - **D2-M4D-C (CMD9 CSD Capture)**: ✅ `CSD_MATCH=yes` (`d02701320f5903fff6dbffef8e404000`).
  - **D2-M4D-D (CMD7 Card Selection)**: ✅ Issued `CMD7` addressed to RCA 2, card selected (`CARD_SELECTION_CONFIRMED=yes`).
  - **D2-M4E (CMD8 EXT_CSD Read)**: ✅ First physical 512-byte data transfer via PIO, `EXT_CSD_REV=0x08`, `SEC_COUNT=61071360`, `TRAN` directly observed (`R1 = 0x00000900`).
  - **D2-M5 (CMD17 Physical LBA 1 Read)**: ✅ Single sector read at `LBA = 1`, 512 bytes captured from `SDHCI_BUFFER`, 100% byte-for-byte match with TWRP disk oracle (`cmp -l` exit 0).
  - **D2 Final Gate**: `D2_STORAGE_COMPLETE = yes`.
* **Remaining Items**: None. Phase D2 sealed.
* **Dependencies**: Phase D1.

---

### Phase D3 — GUID Partition Table (GPT) Discovery
* **Goal**: Parse primary and backup GUID Partition Tables (GPT) from the physical eMMC user area and enumerate partitions.
* **Status**: **COMPLETE**
* **Detailed Milestone Execution & Verification**:
  - **D3-M1 (Primary GPT Header)**:
    - Fresh physical LBA 1 read & parse; verified `"EFI PART"` signature, revision `0x00010000`, size 92, and calculated CRC32 `0xBFDF741D` matching stored header field.
  - **D3-M2A (Primary Partition Entry Array)**:
    - Sequential 32 single-block PIO `CMD17` reads of LBA 2..33 (16,384 bytes); calculated CRC32 `0x64EDE0F4` matching header; 100% byte-for-byte match against independent TWRP oracle.
  - **D3-M2B (Primary Partition Map)**:
    - Decoded all 128 slots into static kernel storage; validated 55 used entries (`PartitionTypeGUID != 0`) and 73 unused entries; validated extents within usable bounds `[34, 61071326]`, arithmetic overflow safety, pairwise non-overlap across all 55 used partitions, unique GUID validity, and deterministic canonical name decoding.
  - **D3-M3 (Backup GPT & Authoritative Seal)**:
    - Dynamically read and verified Backup GPT Header at sector 61071359 (CRC32 `0x03F02415`); confirmed reciprocal links (`PRIMARY.MyLBA == BACKUP.AlternateLBA`, `PRIMARY.AlternateLBA == BACKUP.MyLBA`); derived Backup Array geometry (LBAs 61071327..61071358); verified Backup Array CRC32 (`0x64EDE0F4`); confirmed 100% byte-for-byte and map-for-map cross-validation with Primary GPT.
    - Verified `AUTHORITATIVE_GPT_PARTITION_MAP_VERIFIED = yes` and `PRIMARY_BACKUP_GPT_CONSISTENT = yes`.
* **Hardware Acceptance Criteria**:
  - Primary and backup GPT headers verified on silicon with dynamic CRCs.
  - Reciprocal links between primary and backup verified.
  - Exact 16,384-byte array equality between primary and backup.
  - 55 valid, non-overlapping partitions identified with exact sector ranges.
  - Zero partition content reads, zero filesystem probes, zero rootfs selection, zero storage writes.
* **Explicit Invariants & Preserved Semantics**:
  - `GPT_NAME_PAIRED_A_B_ENTRIES_OBSERVED = yes`
  - `A_B_BOOT_SLOT_SEMANTICS = NOT_ESTABLISHED`
  - `FILESYSTEMS = NOT_PROBED`
  - `ROOTFS = NOT_SELECTED`
* **Remaining Items**: None. Phase D3 is COMPLETE and SEALED. Phase D4 is NEXT.
* **Dependencies**: Phase D2.

---

### Phase D4 — Block Storage Driver & BSD Integration
* **Goal**: Transition eMMC hardware operations from diagnostic probes into a persistent, reusable block storage runtime, implement BSD block device switch (`bdevsw`), devfs whole-disk node (`/dev/disk0`), runtime GPT partition map loaded via block layer, devfs partition slice devices (`/dev/disk0s1`..`disk0s55`), IOKit BSD root discovery bridge (`XZSeMMCStorageNub`), and real block vnode acquisition (`bdevvp`).
* **Architecture**: `XZS_HYBRID_IOKIT_BDEVSW_STORAGE` (BSD `bdevsw` switch table backed by persistent eMMC runtime with lightweight IOKit media nub discovery).
* **Status**: **COMPLETE**
* **Detailed Milestone Execution & Verification**:
  - **D4-M1 (Persistent eMMC Runtime Context & Multi-Sector Pipeline)**: ✅ **COMPLETE**
    - Established static persistent runtime context (`xzs_emmc_context_t`, `g_xzs_emmc_ctx`) with fail-closed semantics.
    - Implemented idempotent persistent initialization (`xzs_emmc_init_persistent()`): `INIT_CALL_COUNT=2`, `INITIALIZATION_COUNT=1`, `INITIALIZATION_REUSE_COUNT=1`, `CONTROLLER_RESET_COUNT=1`.
    - Implemented 64-bit checked single-sector primitive (`xzs_emmc_read_sector_sync()`) with checked narrowing.
    - Implemented multi-sector synchronous software pipeline (`xzs_emmc_read_blocks_sync()`) with 7-point overflow and range bounds validation.
    - Verified on physical hardware: 4 non-contiguous reads (`LBA 1`, `LBA 2`, `LBA 33`, Backup Header `LBA 61071359`) across a single persistent session with zero intermediate controller resets.
    - 100% byte-for-byte exact identity against independent frozen TWRP oracles. Both primary and backup GPT header CRCs dynamically verified on read buffers.
    - `PERSISTENT_EMMC_RUNTIME_VERIFIED = yes`, `MULTI_SECTOR_PIPELINE_VERIFIED = yes`.
  - **D4-M2 (BSD Block Device Switch Layer)**: ✅ **COMPLETE**
    - Implemented controller serialization lock (`lck_mtx_t g_xzs_emmc_mtx`).
    - Implemented BSD `bdevsw` switch table (`xzs_bdev_open`, `xzs_bdev_close`, `xzs_bdev_strategy`, `xzs_bdev_psize`, `xzs_bdev_ioctl`).
    - Registered major block device dynamically via `bdevsw_add(-1, &g_xzs_bdevsw)` at `BSD_POST_VFSINIT` (allocated major 1).
    - Integrated genuine in-tree `buf_t` handling (`buf_alloc(NULL)`, `buf_reset()`, `buf_map()`, `buf_biowait()`, `buf_free()`).
    - Verified 512B LBA1 read (CRC32 `0xD3A34BC1`, SHA256 `e4b891b42fd57eb352ffbe0aa9098d04fe85f88e3425dcba529064cce72f862a`, 100% match).
    - Verified 1024B LBA1..2 multi-sector read (CRC32 `0xA21C1724`, SHA256 `4a161d7ec294bc215b8dd22989a4f87f1c501fac3250acf66e5e2a4085ece8d0`, 100% match).
    - Verified synthetic failure rejection: out-of-range -> `EINVAL`, misaligned -> `EINVAL`, write -> `EROFS`, with zero physical commands issued.
    - Verified geometry query ioctls and `d_psize` (61,071,360 512-byte blocks).
    - `BSD_BLOCK_STRATEGY_VERIFIED = yes`, `BDEVSW_IMPLEMENTED = yes`, `CONTROLLER_SERIALIZATION_ENABLED = yes`.
  - **D4-M3 (Whole-Disk devfs Publication)**: ✅ **COMPLETE**
    - Created `/dev/disk0` via `devfs_make_node()`, preserved handle `g_xzs_disk0_devfs_handle`.
    - `DEVFS_DISK0_PUBLISHED = yes`, `DEVFS_RDISK0_PUBLISHED = no`.
    - `WHOLE_DISK_DEVFS_IDENTITY_VERIFIED = yes`.
    - Write open rejected with `EROFS` (`DISK0_WRITE_OPEN_REJECTED = yes`).
  - **D4-M4 (Runtime GPT & Partition Slices)**: ✅ **COMPLETE**
    - Initialized runtime GPT map through block layer (`d_strategy` reads of LBA 1 and LBA 2..33).
    - Header CRC32 (`0xBFDF741D`) and Array CRC32 (`0x64EDE0F4`) verified.
    - Parsed all 128 slots: 55 used entries, matching D3 canonical evidence.
    - Derived minor mapping: `minor 0 = whole disk`, `minor N = Nth used GPT entry`.
    - Published all 55 partitions as `/dev/disk0s1`..`disk0s55`, stored handles.
    - Verified slice translation on literal `"boot"` entry (slot 29, minor 30, FirstLBA 208896, LastLBA 339967):
      - First sector (slice block 0): CRC32 `0x7756D106` matching independent oracle (`SLICE_FIRST_SECTOR_BYTE_MATCH = yes`).
      - Last sector (slice block 131071): CRC32 `0xB2AA7578` matching independent oracle (`SLICE_LAST_SECTOR_BYTE_MATCH = yes`).
      - One-past-end read (slice block 131072) rejected with `EINVAL`, `resid=512`, `SLICE_OUT_OF_RANGE_CMD17_DELTA = 0`.
    - `PARTITION_SLICES_PUBLISHED = yes`, `PUBLISHED_SLICE_COUNT = 55`.
  - **D4-M5 (IOKit BSD Root Discovery Bridge)**: ✅ **COMPLETE**
    - Implemented C++ `XZSeMMCStorageNub : public IOService` in `src/xnu/iokit/bsddev/xzs_storage_nub.cpp`.
    - Published canonical properties: `kIOBSDNameKey = "disk0"`, `kIOBSDMajorKey = 1`, `kIOBSDMinorKey = 0`.
    - Verified discovery via `IOBSDNameMatching("disk0")`: `IOKIT_BSD_IDENTITY_DISCOVERABLE = yes`.
    - Global `rootdev` NOT mutated (`GLOBAL_ROOTDEV_MUTATED = no`, `ROOTFS_SELECTION_PERFORMED = no`).
  - **D4-M6 (Real Block Vnode Acquisition & D4 Seal)**: ✅ **COMPLETE**
    - Block vnode acquired via `bdevvp(makedev(1, 0), &vp)`, `VNOP_OPEN(FREAD)` succeeded.
    - Controlled read of LBA 1 via `buf_bread()`: CRC32 `0xD3A34BC1` (100% byte match).
    - Vnode cleanly released via audited API `vnode_close(vp, FREAD, vfs_context_kernel())`.
    - `BDEVVP_ACQUISITION_VERIFIED = yes`, `BDEVVP_LBA1_BYTE_MATCH = yes`.
    - `D4_COMPLETE = yes`.
* **Dependencies**: Phase D3. Phase D5 is NEXT.

---

### Phase D5 — Real Root Filesystem Mount
* **Goal**: Mount an actual read-only root filesystem partition (APFS, HFS+, or ramdisk) into VFS root vnode (`/`).
* **Status**: **NOT STARTED**
* **Hardware Acceptance Criteria**: `vfs_mountroot()` returns `KERN_SUCCESS`; `VFS_ROOT()` retrieves root directory vnode (`init_rootvnode != NULLVP`); `mountlist` shows `MNT_ROOTFS` active.
* **Dependencies**: Phase D4.
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
