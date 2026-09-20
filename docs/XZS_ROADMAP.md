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
| **Phase D5** | Real root filesystem mount (RAMDisk XZSFS) | **COMPLETE / SEALED** |
| **Phase D6** | PID 1 / First EL0 userspace (`initproc` / launchd) | **IN PROGRESS — D6-M1..M5 COMPLETE / SEALED; D6-M6 NEXT** |
| **Phase D7** | Interactive serial shell (`/bin/sh` or micro-shell) | **NOT STARTED** |
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
* **Goal**: Mount an actual read-only root filesystem (RAMDisk XZSFS) into VFS root vnode (`/`).
* **Status**: **COMPLETE / SEALED — ALL D5 MILESTONES (M1-M6) COMPLETE ON HARDWARE; D6 NEXT**
  - **D5-M1 (Format Freeze & Tooling)**: **COMPLETE** (XZSFS v1 on-disk format frozen, `mkxzsfs.py` generator, `verify_xzsfs.py` independent verifier, static ARM64 Mach-O binaries verified, deterministic rootfs image built).
  - **D5-M2 (RAMDisk Block Transport)**: **COMPLETE / SEALED** (ADT `/chosen/memory-map/RAMDisk` -> `rd=md0` -> `mdevadd`; immutable logical image CRC32 `0x131e9191`).
  - **D5-M3 (Kernel XZSFS Driver)**: **COMPLETE / SEALED** (native read-only XZSFS VFS driver; superblock, metadata CRC, object graph, lookup, read, readdir, getattr, and `EROFS` behavior verified). Runtime regression gate passed on two consecutive hardware runs through `D520/90`.
  - **D5-M4 (Silicon Root Mount Proof)**: **COMPLETE / SEALED** (`vfs_mountroot()` -> `xzsfs_mount()` -> real root vnode -> `VFS_ROOT()` -> real VNOP dispatch -> global `rootvnode`). Full `D530/00` through `D530/91` and terminal `D530/01` sequence verified on Xperia XZs G8231 hardware.
  - **D5-M5 (Namespace + devfs/console)**: **COMPLETE / SEALED** (Pathname resolution of `/` and `/sbin/launchd` via `namei()`, `devfs_kernel_mount("/dev")`, crossing into devfs root vnode, `/dev/console` resolution to cdev 0:0, and full telemetry verification).
  - **D5-M6 (Final D5 Seal)**: **COMPLETE / SEALED** (Namespace resolution of `/bin/sh` to fileid 3, full D5 regression continuity across M1-M6 verified on hardware, zero storage writes, hard boundary preserved before PID 1).
* **D5 Final Hardware Evidence**:
  - Tested boot image SHA-256: `18f1f3ade838d859f5e4a3eb0bede5953cd4a0b46c008d97033ce8e06b1d5fe9`.
  - Checkpoint sequence: `D530/00`..`91` -> `D540/00`..`01` -> `D550/00`..`91` and terminal `D550/01` (return time: +4s).
  - `scripts/verify_d5_final_acceptance.py`: **100% PASS** for all D530, D540, D550 checkpoints and all canonical telemetry invariants.
  - Pathname lookups: `namei("/")` returned global `rootvnode`; `namei("/sbin/launchd")` returned `VREG` (fileid 7, size 16472, mode 0755); `namei("/bin/sh")` returned `VREG` (fileid 3, size 16472, mode 0755); `namei("/dev")` crossed into devfs root vnode (`VDIR`, `devfs`); `namei("/dev/console")` returned `VCHR` (major 0, minor 0).
  - Hard boundary preserved: `PID1_STARTED=no`, `EXECVE_ATTEMPTED=no`, `EL0_ENTRY_ATTEMPTED=no`.
  - Storage remained read-only: `CMD24_COUNT=0`, `CMD25_COUNT=0`, `ZERO_STORAGE_WRITES=yes`.
* **Hardware Acceptance Criteria**: **SATISFIED FOR PHASE D5**. Full read-only rootfs mount, namespace traversal, devfs overlay, and console vnode verified on silicon.
* **Known Blockers**: None remaining for Phase D5.
* **Dependencies**: Phase D4.

---

### Phase D6 — PID 1 / First EL0 Userspace
* **Goal**: Bootstrap the first Mach/BSD userspace process (`initproc` / PID 1) from the root filesystem and transition from EL1 to EL0.
* **Status**: **IN PROGRESS — D6-M1 THROUGH D6-M5 COMPLETE / SEALED ON HARDWARE; D6-M6 NOT STARTED**
* **Milestones**:
  - **D6-M1 (PID1 Skeleton)**: **COMPLETE / SEALED** (Created and validated BSD `initproc` (PID 1, PPID 0), Mach task (non-kernel), Mach thread, and embedded uthread; zero userspace execution; full D5 regression prefix).
  - **D6-M2 (Minimal Mach-O Loader)**: **COMPLETE / SEALED** (Opened `/sbin/launchd`, validated ARM64 Mach-O header/load commands, enumerated segments, resolved entry PC `0x1000002f0`; verified static/no-dyld contract; no VM mapping; zero EL0 entry).
  - **D6-M3 (User VM + Initial Stack)**: **COMPLETE / SEALED** (Mapped and content-verified `__TEXT`, finalized current and maximum protection to RX, preserved hard PAGEZERO, left `__LINKEDIT` unmapped, constructed a native Darwin initial frame on an RW/NX stack, installed and read back PC/SP, and verified zero unexpected RWX mappings while PID1 remained suspended).
  - **D6-M4 (First EL0 Transition)**: **COMPLETE / SEALED** (Released PID1 task/thread holds, verified full return-to-user path `D630/33`..`37` -> `eret`, executed canonical 5-instruction `/sbin/launchd` EL0 sequence on hardware, captured canonical `svc #0x80` before dispatch with full register signature; `FIRST_REAL_EL0_INSTRUCTION_HARDWARE_VERIFIED=yes`, `CANONICAL_SVC64_SIGNATURE_HARDWARE_VERIFIED=yes`).
  - **D6-M5 (First Syscall Round-Trip)**: **COMPLETE / SEALED** (Routed canonical `svc #0x80`, `x16=4` through normal `handle_svc()` and `unix_syscall()` dispatch to the real `sysent[4]` `write()` handler; hardware verified deterministic `EBADF=9` error ABI, normal return to EL0, and a post-return instruction signature at `ELR=0x100000310`).
  - **D6-M6 (Minimal Stable PID1 Runtime)**: **NOT STARTED** (Execute minimal deterministic `/sbin/launchd` userspace runtime loop/exit).
  - **D6-M7 (Final D6 Seal)**: **NOT STARTED** (Regression verification, independent verifier, evidence archive, and roadmap seal).
* **D6-M1 Hardware Evidence**:
  - Boot image SHA-256: `bd64531a83390b8c1cfd57efc8bfe712923a53c92d854a2e6bdfc3e0795c6f83`.
  - Checkpoint sequence: `D530/00`..`91` -> `D540/00`..`01` -> `D550/00`..`01` -> `D600/00`..`91` and terminal `D600/01` (return time: +4s).
  - `scripts/verify_d6m1_acceptance.py`: **100% PASS** for all D530, D540, D550, and D600 checkpoints and all canonical telemetry invariants.
  - Process objects verified on silicon: `PID1_PROCESS_CREATED=yes`, `PID1_PROC_PID=1`, `PID1_PROC_PPID=0`, `PID1_TASK_CREATED=yes`, `PID1_TASK_IS_KERNEL_TASK=no`, `PID1_THREAD_CREATED=yes`, `PID1_UTHREAD_CREATED=yes`.
  - Boundaries strictly preserved: `PID1_STARTED=no`, `EXECVE_ATTEMPTED=no`, `MACHO_LOAD_ATTEMPTED=no`, `USER_VM_SETUP_ATTEMPTED=no`, `EL0_ENTRY_ATTEMPTED=no`.
* **D6-M2 Hardware Evidence**:
  - Boot image SHA-256: `dfac7b14e0700a432ecb9fce95050838f9af21ed69f13edb5a46e6fd845a2f5d`.
  - Checkpoint sequence: `D530/00`..`91` -> `D540/00`..`01` -> `D550/00`..`01` -> `D600/00`..`91` -> `D600/01` -> `D610/00`..`91` and terminal `D610/01` (return time: +4s).
  - `scripts/verify_d6m2_acceptance.py`: **100% PASS** across all regression checkpoints and D6-M2 load commands/segments/entrypoint telemetry invariants.
  - Validation results on silicon: `LAUNCHD_OPENED=yes`, `MACHO_HEADER_VALID=yes`, `MACHO_CPU_ARM64=yes`, `MACHO_FILETYPE_EXECUTE=yes`, `MACHO_LOAD_COMMAND_COUNT=7`, `MACHO_SEGMENT_COUNT=3`, `MACHO_LOADABLE_SEGMENT_COUNT=2`, `MACHO_PAGEZERO_PRESENT=yes`, `MACHO_TEXT_PRESENT=yes`, `MACHO_ENTRY_COMMAND=LC_UNIXTHREAD`, `MACHO_THREAD_FLAVOR=ARM_THREAD_STATE64`, `MACHO_INITIAL_PC=0x1000002f0`, `MACHO_INITIAL_PC_IN_EXEC_SEGMENT=yes`, `MACHO_ENTRY_SEGMENT=__TEXT`, `MACHO_ENTRY_SEGMENT_INITPROT=r-x`, `MACHO_STATIC_EXECUTABLE=yes`, `DYLD_REQUIRED=no`, `DYNAMIC_LIBRARY_DEPENDENCY_COUNT=0`, `MACHO_REQUIRES_UNSUPPORTED_FIXUPS=no`.
  - Boundaries strictly preserved: `USER_VM_SETUP_ATTEMPTED=no`, `USER_SEGMENTS_MAPPED=no`, `USER_STACK_SETUP_ATTEMPTED=no`, `PID1_STARTED=no`, `EL0_ENTRY_ATTEMPTED=no`.
* **D6-M3 Hardware Evidence**:
  - Tested source commit: `ab019a3128659b097fd7ef5dd8a9e6f7a541e0b7`.
  - Boot image SHA-256: `ba26b737ca32426176282bc6a1099e05bb9717ac5bf3a635df9aeee5ae6c8056`.
  - Checkpoint sequence: `D620/00`, `/10`, `/20`, `/30`, `/31`, `/32`, `/40`, `/41`, `/50`, `/51`, `/52`, `/60`, `/61`, `/70`, `/71`, `/72`, `/90`, `/91`, `/01` (PASS; return to Fastboot at +7s).
  - `scripts/verify_d6m3_acceptance.py`: **PASS**, exit status 0, including full D5/D6-M1/D6-M2 regression prefix.
  - `__TEXT`: `vmaddr=0x100000000`, `vmsize=0x4000`, `fileoff=0`, `filesize=0x4000`, current `RX`, maximum `RX`, CRC32 `0x0a100b37`, writable after finalize `no`.
  - Stack: `[0x16fde0000, 0x16fe00000)`, `RW`, `NX`; native initial frame with `argc=1`, `argv[0]=/sbin/launchd`; SP `0x16fdfffb0` is 16-byte aligned.
  - Saved register state: PC `0x1000002f0`, SP `0x16fdfffb0`; PID1 task/thread remained suspended; EL0 not attempted.
  - Final map audit: PAGEZERO overlap count 0, unexpected RWX count 0, `__LINKEDIT` unmapped.
* **D6-M4 Hardware Evidence**:
  - Tested source commit: `4bff94a46a01558746a04097d94c12ac51c27c2d`.
  - Boot image SHA-256: `0a49fd191645b829fe356e777af84ef7009be875c076ee7f03c61c8f4815a0a0`.
  - Checkpoint sequence: `D630/00`..`91` and terminal `D630/01` (PASS; automated return to Fastboot at +4s).
  - `scripts/verify_d6m4_acceptance.py`: **100% PASS**, exit status 0, including full D5/D6-M1/D6-M2/D6-M3 regression prefix and D6-M4 acceptance assertions.
  - Three-state descriptor transition verified: `0x00600000843a4ac3` (BEFORE) -> `0x00600000843a4ec3` (AFTER_NATIVE_AF) -> `0x00200000843a4ec3` (FINAL). `NATIVE_AF_DELTA_ONLY_AF=yes`, `XZS_EXEC_DELTA_ONLY_UXN=yes`, `TOTAL_BOOTSTRAP_DELTA_AF_AND_UXN=yes`, `UNRELATED_PTE_BITS_UNCHANGED=yes`.
  - Hardware-verified first EL0 execution: `FIRST_REAL_EL0_INSTRUCTION_HARDWARE_VERIFIED=yes`, `CANONICAL_SVC64_SIGNATURE_HARDWARE_VERIFIED=yes`.
  - Register state at `svc #0x80`: `ESR_EL1=0x56000080` (SVC64, imm=0x80), `ELR_EL1=0x100000304`, `SPSR_EL1=0x0`, `SP_EL0=0x16fdfffb0`, `x0=1`, `x1=0x100000320`, `x2=0x1a`, `x16=4` (SYS_write).
  - Boundaries strictly preserved: `SYSCALL_DISPATCH_REACHED=no`, `FIRST_SYSCALL_ROUNDTRIP_COMPLETE=no`.
* **D6-M5 Hardware Evidence**:
  - Hardware-tested source commit: `6669437fbe9d4c9a5f33bb87fc1a443a4346b703`.
  - Boot image SHA-256: `87d2b6c609f3b7348e347d53db7de6655eef849dffe51054c71ba2887098ccac`.
  - Raw console log SHA-256: `dd084c9e3901b86fb4513fc0aadafc3dd7d4ef88917134cd05b09adb49f51747`.
  - Checkpoint sequence: `D640/00`, `/10`, `/20`, `/30`, `/40`, `/50`, `/60`, `/70`, `/90`, `/91`, `/01` (PASS; automated return to Fastboot at +7s).
  - `scripts/verify_d6m5_acceptance.py`: **PASS**, including full D5/D6-M1/D6-M2/D6-M3 prefix and D6-M4 regression mode.
  - Real path: `sleh_synchronous` -> `handle_svc` -> `unix_syscall` -> `sysent[4]` -> `write` -> `arm_prepare_syscall_return` -> EL0.
  - Hardware return: `SYSCALL_RETURN_VALUE=9`, `SYSCALL_RETURN_ERROR=EBADF`, carry set. PID1 fd 1 is not yet bound to `/dev/console`.
  - Post-return proof: second SVC at `ELR=0x100000310`, `x0=0`, `x16=1`, carry set; `POST_SYSCALL_EL0_INSTRUCTION_EXECUTED=yes`.
* **Hardware Acceptance Criteria**: **SATISFIED THROUGH D6-M5**. First real BSD syscall dispatcher/handler/return round-trip and subsequent EL0 execution verified on physical silicon.
* **Known Blockers**: None for D6-M5. D6-M6 (Minimal Stable PID1 Runtime) is not started.
* **Dependencies**: Phase D5 (COMPLETE / SEALED), Phase D6-M1 through D6-M5 (COMPLETE / SEALED).

---

### Phase D7 — Interactive Serial Shell
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
