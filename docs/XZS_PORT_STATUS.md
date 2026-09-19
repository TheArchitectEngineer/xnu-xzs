# Sony Xperia XZs (MSM8996) — Port Status

This summary provides an executive overview of the project's technical status. It can be read in under 3 minutes.

---

## Current Milestone

```text
Phase D2 acceptance gate completed:
Physical eMMC storage bring-up verified on hardware.
Exactly one 512-byte sector read from LBA 1 matches independent TWRP oracle byte-for-byte.
Phase D3 (GPT Partition Discovery) is NEXT.
```

* **Target Device**: Sony Xperia XZs (Model G8231 / Platform Tone / Board Keyaki)
* **SoC**: Qualcomm Snapdragon 820 (MSM8996 Pro)
* **CPU Architecture**: Quad-core Qualcomm Kryo ARMv8.0-A (2x Silver + 2x Gold)
* **Storage Device**: Samsung BJNB4R 32GB eMMC 5.1 (`CID: 150100424a4e4234520fdac7c0381400`)
* **Active Development Branch**: `xzs-bringup`
* **Milestone Tag**: `xzs-d2-storage-complete`

---

## Highest Hardware-Verified Checkpoint

```text
[BREADCRUMB] CP=0x000000000000d3b0 ERR=0x0000000000000041 (CMD17 Issued: 0x113A, ARG: 0x00000001)
[BREADCRUMB] CP=0x000000000000d3b0 ERR=0x0000000000000042 (COMMAND_COMPLETE Latched, R1=0x00000900)
[BREADCRUMB] CP=0x000000000000d3b0 ERR=0x0000000000000050 (BUFFER_READ_READY Latched)
[BREADCRUMB] CP=0x000000000000d3b0 ERR=0x0000000000000052 (512 Bytes Captured from SDHCI_BUFFER)
[BREADCRUMB] CP=0x000000000000d3b0 ERR=0x0000000000000053 (TRANSFER_COMPLETE Latched)
[BREADCRUMB] CP=0x000000000000d3b0 ERR=0x0000000000000071 (BYTE_FOR_BYTE_MATCH: yes)
[BREADCRUMB] CP=0x000000000000d3b0 ERR=0x0000000000000080 (D2_STORAGE_COMPLETE: yes)
```

The kernel confirms that Mach SMP, BSD initialization, IOKit autoconfiguration, and Qualcomm SDCC1/SDHCI eMMC physical block read operate genuinely on physical silicon. The single-block physical sector payload (LBA 1, 512 bytes) was compared byte-for-byte against an independent TWRP Linux oracle (`/dev/block/mmcblk0`), confirming identical SHA-256 hash `e4b891b42fd57eb352ffbe0aa9098d04fe85f88e3425dcba529064cce72f862a` with zero byte differences.

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
- [x] **Automated Recovery**: Warm reboot back to Fastboot within +6 seconds via Qualcomm APCS watchdog bite upon reaching diagnostic terminal state.

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

- [ ] **GPT Partition Parser (D3)**: Primary and backup GUID Partition Table discovery and partition enumeration.
- [ ] **Block Storage Devices (D4)**: Physical `IOMedia` or `disk0` published in IOKit registry.
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
build -j8

./scripts/check-no-pac.sh
./scripts/package-boot.sh
```

### 2. Hardware Deployment
```bash
./scripts/run-and-extract.sh
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
Phase D2: Qualcomm MSM8996 UFS Physical Block Read
```
Implementing the `QualcommUFSController` driver to initialize UFS hardware, complete link startup, route GICv3 interrupt SPI 265, and issue SCSI/UFS read commands on physical internal flash storage.
