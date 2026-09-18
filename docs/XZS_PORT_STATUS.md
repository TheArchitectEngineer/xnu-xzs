# Sony Xperia XZs (MSM8996) — Port Status

This summary provides an executive overview of the project's technical status. It can be read in under 3 minutes.

---

## Current Milestone

```text
Phase D1 acceptance gate completed:
BSD/VFS bootstrap reaches root-device / block-storage boundary.
```

* **Target Device**: Sony Xperia XZs (Model G8231 / Platform Tone / Board Keyaki)
* **SoC**: Qualcomm Snapdragon 820 (MSM8996 Pro)
* **CPU Architecture**: Quad-core Qualcomm Kryo ARMv8.0-A (2x Silver + 2x Gold)
* **Active Development Branch**: `xzs-bringup`
* **Target Baseline Commit**: `27d000c`

---

## Highest Hardware-Verified Checkpoint

```text
[D50] ROOT DEVICE SELECTION ENTER
[D50-I0..I8] Canonical IOMedia discovery traversed (timeout=1.0s)
[D50b] IOFindBSDRoot returns canonical kIOReturnNotFound (0xe00002f0)
[D50c] Synthetic rootdev selected (sd0a: major=6, minor=0)
[D51] vfs_mountroot ENTER
[D51b] bdevvp(rootdev, ...) error=0x13 (ENODEV)
[D51-TERMINAL] cannot mount root, errno = 0x13
[XZS-BOOT] PHASE D1 TERMINAL CONDITION REACHED — WARM REBOOTING TO FASTBOOT
```

The kernel confirms that Mach SMP, BSD initialization, IOKit autoconfiguration, and VFS mountroot logic operate genuinely on physical silicon. The terminal error code `0x13` (`ENODEV`, decimal 19) proves that the VFS subsystem reached the storage layer and queried the BSD block device switch table (`bdevsw`), correctly failing because a physical storage controller driver has not yet been implemented.

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
- [x] **Automated Recovery**: Warm reboot back to Fastboot within +6 seconds via Qualcomm APCS watchdog bite upon reaching D1 terminal state.

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

- [ ] **Physical UFS Controller Driver**: No driver for Qualcomm MSM8996 UFS 2.0 (`0x00624000`).
- [ ] **Block Storage Devices**: No physical `IOMedia` or `disk0` published in IOKit registry.
- [ ] **Root Filesystem**: No APFS, HFS+, or ramdisk mounted at `/`.
- [ ] **Userspace Process**: No PID 1 (`launchd`), shell, or `/dev/console` interactive session.

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
