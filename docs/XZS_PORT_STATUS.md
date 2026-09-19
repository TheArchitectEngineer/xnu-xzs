# Sony Xperia XZs (MSM8996) — Port Status

This summary provides an executive overview of the project's technical status. It can be read in under 3 minutes.

---

## Current Milestone

```text
Phase D4-M1 acceptance gate completed:
Persistent eMMC runtime context & multi-sector read pipeline verified on hardware.
One-time hardware initialization reused across multiple non-contiguous reads.
Zero controller resets between reads; 100% byte-for-byte match on 4 target sectors.
PERSISTENT_EMMC_RUNTIME_VERIFIED = yes.
MULTI_SECTOR_PIPELINE_VERIFIED = yes.
Phase D4-M2 (BSD bdevsw Block Device Layer) is NEXT.
```

* **Target Device**: Sony Xperia XZs (Model G8231 / Platform Tone / Board Keyaki)
* **SoC**: Qualcomm Snapdragon 820 (MSM8996 Pro)
* **CPU Architecture**: Quad-core Qualcomm Kryo ARMv8.0-A (2x Silver + 2x Gold)
* **Storage Device**: Samsung BJNB4R 32GB eMMC 5.1 (`CID: 150100424a4e4234520fdac7c0381400`)
* **Active Branches**: `main` (integrated), `xzs-port` (synchronized), `xzs-d4-block` (active D4 milestone)
* **Milestone Tag**: `xzs-d3-gpt-complete` (D3 sealed)

---

## Highest Hardware-Verified Checkpoint

```text
[BREADCRUMB] CP=0x000000000000d400 ERR=0x0000000000000000 (Enter D4-M1)
[BREADCRUMB] CP=0x000000000000d400 ERR=0x0000000000000010 (Git Baseline)
[BREADCRUMB] CP=0x000000000000d400 ERR=0x0000000000000020 (Independent Sector Oracles Frozen)
[BREADCRUMB] CP=0x000000000000d400 ERR=0x0000000000000030 (Persistent Init Call #1 Begin)
[BREADCRUMB] CP=0x000000000000d400 ERR=0x0000000000000031 (Persistent Init Complete)
[BREADCRUMB] CP=0x000000000000d400 ERR=0x0000000000000032 (Context Geometry Derived Live)
[BREADCRUMB] CP=0x000000000000d400 ERR=0x0000000000000033 (Persistent Init Call #2 Context Reused)
[BREADCRUMB] CP=0x000000000000d400 ERR=0x0000000000000040 (Multi-Sector Read LBA 1..2 Begin)
[BREADCRUMB] CP=0x000000000000d400 ERR=0x0000000000000041 (LBA 1 Byte & Header CRC32 Verified: 0xBFDF741D)
[BREADCRUMB] CP=0x000000000000d400 ERR=0x0000000000000050 (LBA 2 Generation Recorded)
[BREADCRUMB] CP=0x000000000000d400 ERR=0x0000000000000051 (LBA 2 Verified)
[BREADCRUMB] CP=0x000000000000d400 ERR=0x0000000000000060 (LBA 33 Single-Sector Read Begin)
[BREADCRUMB] CP=0x000000000000d400 ERR=0x0000000000000061 (LBA 33 Verified)
[BREADCRUMB] CP=0x000000000000d400 ERR=0x0000000000000070 (Backup Header LBA 61071359 Read Begin)
[BREADCRUMB] CP=0x000000000000d400 ERR=0x0000000000000071 (Backup Header Byte & CRC32 Verified: 0x03F02415)
[BREADCRUMB] CP=0x000000000000d400 ERR=0x0000000000000080 (Lifecycle Counters Verified: 1 Init / 1 Reuse / 4 Reads)
[BREADCRUMB] CP=0x000000000000d400 ERR=0x0000000000000081 (All Four Sector Oracles Match: 100% Byte Identity)
[BREADCRUMB] CP=0x000000000000d400 ERR=0x0000000000000090 (Phase D4-M1 Complete)
[BREADCRUMB] CP=0x000000000000d400 ERR=0x0000000000000001 (Diagnostic Terminal Warm Reset to Fastboot)
```

The kernel confirms that Mach SMP, BSD initialization, IOKit autoconfiguration, physical eMMC persistent runtime lifecycle, and multi-sector pipelined block transfers operate genuinely on physical silicon. The eMMC controller is initialized once and kept in operational TRAN state. Multiple non-contiguous sector reads (LBA 1, LBA 2, LBA 33, and Backup Header LBA 61071359) execute across the persistent context with zero intermediate controller resets and 100% byte-for-byte oracle match.

---

## What Works (Hardware Verified)

- [x] **Sony S1 Bootloader Handoff**: Android boot image loading via fastboot.
- [x] **xzs-bootshim**: Qualcomm DTB parsing, Apple Device Tree (ADT) creation at `0x81810000`, `boot_args` population at `0x81800000`.
- [x] **Low-Level ARM64 MMU**: TCR/MAIR configuration, 16KB granule, transition to High KVA (`0xfffffe0000000000`).
- [x] **Qualcomm BLSP2 UARTDM**: Serial logging at 115200 8N1 at `0x075b0000`.
- [x] **Persistent RAM / Pstore Ramoops**: Ring buffer at `0x80060000`, console at `0xa7fbe000` (256KB), dmesg at `0xa7f00000` (4KB).
- [x] **ARM GICv3**: Distributor (`0x09bc0000`) and per-core Redistributors (`0x09c00000` array) in native system register mode.
- [x] **ARM Generic Timers**: PPI 27 (Virtual) & PPI 30 (Physical) firing reliably across all cores at 19.2 MHz.
- [x] **ARM PSCI v1.0 Multi-Core**: SMC `CPU_ON` (`0xC4000003`) bringing all 4 Kryo cores online.
- [x] **Mach SMP Scheduler**: All 4 cores in processor set `pset0`, running `idle_thread`, servicing reschedule IPIs via SGI 1, handling AST urgent preemption.
- [x] **Multi-Core Cache Coherency**: Inner Shareable WBWA memory, 40,000-op atomic lock contention verified (`0x9c40`).
- [x] **BSD Subsystem Bootstrap**: Process 0 (`kernproc`), credentials, zones, domains, sysctl tree.
- [x] **VFS Core Framework**: Mount lists, vnode pools (`vnodes=263168`), devfs bootstrap.
- [x] **IOKit Autoconfiguration**: `IOKitBSDInit` publishing `IOBSD` plane to BSD.
- [x] **Physical eMMC Storage Bring-up (D2)**: SDCC1 clock (400 kHz), controlled reset, power-up, CMD0..CMD17, 512-byte PIO sector read, TWRP oracle byte-for-byte match.
- [x] **GUID Partition Table Discovery & Seal (D3)**: Primary & Backup GPT Header CRC32 verified, 16-KiB entry array verified, 55 partitions enumerated, reciprocal links confirmed, 100% byte-for-byte and map-for-map match. `AUTHORITATIVE_GPT_PARTITION_MAP_VERIFIED = yes`.
- [x] **Persistent eMMC Runtime Context & Multi-Sector Pipeline (D4-M1)**: `xzs_emmc_context_t` lifecycle, idempotent initialization (`INIT_CALL_COUNT=2`, `INITIALIZATION_COUNT=1`, `INITIALIZATION_REUSE_COUNT=1`, `CONTROLLER_RESET_COUNT=1`), 64-bit multi-sector pipeline (`xzs_emmc_read_blocks_sync`), checked request ranges, zero resets between reads, 100% byte-for-byte oracle match on 4 non-contiguous sectors (LBA 1, 2, 33, 61071359).
- [x] **Automated Recovery**: Warm reboot back to Fastboot within +5 seconds via Qualcomm APCS watchdog bite upon reaching diagnostic terminal state.

---

## What Is Deferred (Temporary Bring-up Workarounds)

The following non-essential subsystems are temporarily deferred to eliminate allocator and lock contention before the storage boundary:
* **Skywalk** (`skywalk_init`): Userspace networking memory arenas deferred.
* **Loopback & Tunnels** (`lo0` / `gif0`): Virtual network interfaces deferred.
* **Ethernet Family** (`ether_family_init`): DLIL ethernet registration deferred.
* **TCP Fast Open** (`tcp_fastopen = 0`): Deferred pending CoreCrypto AES registration.
* **DTrace Providers** (`fbt_init`, `profile_init`, `dtrace_postinit`): Tracing probes deferred.
* **Polled Corefiles** (`IOPOLLED_COREFILE`): Crashdump allocation deferred until storage exists.

*(Full matrix: see [`docs/XZS_WORKAROUNDS.md`](XZS_WORKAROUNDS.md))*

---

## What Does Not Exist Yet

- [ ] **BSD Block Device Switch (D4-M2)**: `struct bdevsw`, `bdevsw_add()`, `d_strategy()`, `/dev/disk0`, `/dev/rdisk0`.
- [ ] **Partition Slice Devices (D4-M3)**: Devfs slice nodes (`disk0s1`..`disk0s55`) backed by authoritative GPT partition map.
- [ ] **Root Filesystem (D5)**: No APFS, HFS+, or ramdisk mounted at `/`.
- [ ] **Userspace Process (Phase E)**: No PID 1 (`launchd`), shell, or `/dev/console` interactive session.

---

## How to Reproduce

### 1. Build Pipeline
```bash
DEVELOPER_DIR=/Applications/Xcode-beta.app/Contents/Developer \
make -C src/xnu \
KERNEL_CONFIGS=DEVELOPMENT \
ARCH_CONFIGS=ARM64 \
MACHINE_CONFIGS=VMAPPLE \
RC_DARWIN_KERNEL_VERSION=24.0.0 \
build -j$(sysctl -n hw.ncpu)

./scripts/check-no-pac.sh src/xnu/BUILD/obj/DEVELOPMENT_ARM64_VMAPPLE/kernel.development.vmapple
./scripts/package-boot.sh
```

### 2. Hardware Deployment & Telemetry Verification
```bash
./scripts/run-and-extract.sh
python3 scripts/verify_d4m1_acceptance.py
```

### 3. Cold-Reset Note
> [!IMPORTANT]
> **Cold-Reset Procedure Required for Determinism**:
> To guarantee reproducible D1 terminal execution, always cold-reset the phone before testing:
> 1. Hold `Power + Volume Up` until the phone vibrates 3 times (full power cut).
> 2. Hold `Volume Down` and insert USB cable to enter Fastboot (blue LED).
> 3. Run `./scripts/run-and-extract.sh`.

---

## Next Technical Boundary

```text
Phase D4-M2: BSD bdevsw Block Device Layer & devfs Registration
```
Implement controller serialization lock (`lck_mtx_t`), BSD `bdevsw` switch table (`d_open`, `d_close`, `d_strategy`, `d_psize`, `d_ioctl`), integrate buffer cache strategy I/O into `xzs_emmc_read_blocks_sync()`, and create `/dev/disk0` / `/dev/rdisk0` nodes via `devfs_make_node()`.
