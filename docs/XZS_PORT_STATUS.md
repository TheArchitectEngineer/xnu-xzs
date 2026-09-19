# Sony Xperia XZs (MSM8996) — Port Status

This summary provides an executive overview of the project's technical status. It can be read in under 3 minutes.

---

## Current Milestone

```text
Phase D4-M2 acceptance gate completed:
BSD bdevsw read-only block device integration verified on physical hardware.
Real BSD buf_t requests driven through bdevsw[1].d_strategy() with controller mutex serialization.
100% byte-for-byte oracle match on 512B LBA1 and 1024B LBA1..2 requests.
Dynamic major allocated (1), read-only open/strategy policy enforced, zero storage writes.
BSD_BLOCK_STRATEGY_VERIFIED = yes.
BDEVSW_IMPLEMENTED = yes.
CONTROLLER_SERIALIZATION_ENABLED = yes.
Phase D4-M3 (Character Device & devfs /dev/disk0 Integration) is NEXT.
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
[BREADCRUMB] CP=0x000000000000d410 ERR=0x0000000000000000 (Enter D4-M2)
[BREADCRUMB] CP=0x000000000000d410 ERR=0x0000000000000010 (Git Baseline)
[BREADCRUMB] CP=0x000000000000d410 ERR=0x0000000000000020 (bdevsw Layout Audited)
[BREADCRUMB] CP=0x000000000000d410 ERR=0x0000000000000030 (Controller Mutex Initialized)
[BREADCRUMB] CP=0x000000000000d410 ERR=0x0000000000000021 (Dynamic Major Allocated: 1)
[BREADCRUMB] CP=0x000000000000d410 ERR=0x0000000000000031 (bdevsw Switch Registered)
[BREADCRUMB] CP=0x000000000000d410 ERR=0x0000000000000040 (Read-Only Open Pass)
[BREADCRUMB] CP=0x000000000000d410 ERR=0x0000000000000041 (Write Open Rejected: EROFS)
[BREADCRUMB] CP=0x000000000000d410 ERR=0x0000000000000050 (Strategy LBA1 Begin)
[BREADCRUMB] CP=0x000000000000d410 ERR=0x0000000000000051 (Strategy LBA1 Completed)
[BREADCRUMB] CP=0x000000000000d410 ERR=0x0000000000000052 (LBA1 512B Oracle Pass)
[BREADCRUMB] CP=0x000000000000d410 ERR=0x0000000000000060 (Strategy Multi-Sector Read Begin)
[BREADCRUMB] CP=0x000000000000d410 ERR=0x0000000000000061 (Strategy Multi-Sector Completed)
[BREADCRUMB] CP=0x000000000000d410 ERR=0x0000000000000062 (1024B Oracle Pass)
[BREADCRUMB] CP=0x000000000000d410 ERR=0x0000000000000070 (Out-of-Range Rejection Pass: EINVAL)
[BREADCRUMB] CP=0x000000000000d410 ERR=0x0000000000000071 (Misaligned Rejection Pass: EINVAL)
[BREADCRUMB] CP=0x000000000000d410 ERR=0x0000000000000072 (Write Strategy Rejection Pass: EROFS)
[BREADCRUMB] CP=0x000000000000d410 ERR=0x0000000000000080 (ioctl / psize / close Pass)
[BREADCRUMB] CP=0x000000000000d410 ERR=0x0000000000000081 (Persistent Lifecycle Counters Pass)
[BREADCRUMB] CP=0x000000000000d410 ERR=0x0000000000000090 (Phase D4-M2 Complete)
[BREADCRUMB] CP=0x000000000000d410 ERR=0x0000000000000001 (Terminal Warm Reset to Fastboot)
```

The kernel confirms that Mach SMP, BSD initialization, IOKit autoconfiguration, physical eMMC persistent runtime lifecycle, dynamic `bdevsw` registration, mutex serialization, and legitimate `buf_t` strategy I/O operate genuinely on physical silicon. The block driver (`bdevsw[1]`) processes 512-byte and 1024-byte buffer transfers directly to eMMC CMD17 with 100% byte parity against independent physical oracles.

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
- [x] **Persistent eMMC Runtime Context & Multi-Sector Pipeline (D4-M1)**: `xzs_emmc_context_t` lifecycle, idempotent initialization (`INIT_CALL_COUNT=2`, `INITIALIZATION_COUNT=1`, `INITIALIZATION_REUSE_COUNT=1`, `CONTROLLER_RESET_COUNT=1`), 64-bit multi-sector pipeline (`xzs_emmc_read_blocks_sync`), checked request ranges, zero resets between reads, 100% byte-for-byte oracle match on 4 non-contiguous sectors.
- [x] **BSD `bdevsw` Read-Only Block Device Layer (D4-M2)**: Dynamic major registration via `bdevsw_add()`, controller mutex serialization (`lck_mtx_t`), `d_open`/`d_close`/`d_strategy`/`d_ioctl`/`d_psize` handlers, genuine `buf_t` mapping and biowait completion, 512B and 1024B strategy reads matching physical oracles with 100% parity, synthetic error rejection (out-of-range, misaligned, write) issuing zero physical commands.
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

- [ ] **Character Device & devfs Disk Nodes (D4-M3)**: `cdevsw`, `/dev/disk0`, `/dev/rdisk0`.
- [ ] **Partition Slice Devices (D4-M4)**: Devfs slice nodes (`disk0s1`..`disk0s55`) backed by authoritative GPT partition map.
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
python3 scripts/verify_d4m2_acceptance.py artifacts/logs/xnu-console-extracted.log
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
Phase D4-M3: Character Device Companion & devfs Node Registration (/dev/disk0, /dev/rdisk0)
```
Implement `cdevsw`, register dynamic character major via `cdevsw_add_with_bdev()`, publish `/dev/disk0` and `/dev/rdisk0` via `devfs_make_node()`, and verify user/kernel character and block reads while strictly preserving read-only safety invariants.
