# Sony Xperia XZs (MSM8996) — XNU Port Status

## Current Milestone

* **Device**: Sony Xperia XZs (G8231 / Tone Keyaki / Serial `BH905SX976`)
* **SoC**: Qualcomm Snapdragon 820 (MSM8996 Pro)
* **Architecture**: Quad-core ARMv8.0-A Qualcomm Kryo (2x Silver @ 1.59 GHz + 2x Gold @ 2.15 GHz)
* **Execution Level**: EL1 (booted natively via S1 ABOOT fastboot)
* **Kernel Git Tag**: `v1.0-4core-mach-smp-verified` (Commit `8a0a42d`)
* **Current Clean Development Branch**: `xzs-port`
* **Status**: **4-Core Full Mach SMP Scheduler 100% Verified on Physical Hardware**

---

## High-Level Goal Progress

| Milestone | Objective | Status | Verification Evidence |
| :--- | :--- | :---: | :--- |
| **Mục tiêu A** | XNU native execution on Qualcomm MSM8996 | **DONE ✅** | Bootshim, ADT, MMU, High KVA, UARTDM, Platform Expert |
| **Mục tiêu B** | 4/4 CPU low-level SMP bring-up | **DONE ✅** | PSCI `CPU_ON`, per-core GICR, timer PPI, SGI IPI, shared coherency |
| **Mục tiêu C** | **Full Mach SMP Scheduler Integration** | **DONE ✅** | **All 4 cores in `pset0`, idle_threads, worker dispatch, ring migration, 40k lock contention** |
| **Mục tiêu D** | BSD Subsystem, VFS, Rootfs & Userspace | **NEXT ⏳** | Scheduled next: RAM disk / UFS storage & `bsd_init` |

---

## Detailed Verified Checkpoints

- [x] **S1 boot image**: Accepted and loaded by Sony ABOOT bootloader.
- [x] **xzs-bootshim**: Parses Qualcomm DTB, builds ADT, constructs `boot_args` at `0x81800000`.
- [x] **Exception vectors**: Installed at `LowExceptionVectorBase` with clean high-KVA traps.
- [x] **Page Tables & MMU**: `TCR_EL1 = 0x226511a511`, `MAIR_EL1 = 0x0c0804ff00bb44ff`, `SCTLR_EL1 = 0x10c01805` (MMU, I-cache, D-cache enabled).
- [x] **arm_init()**: Platform expert initialized, timers configured (19.2 MHz), CPU topology parsed (`ml_parse_cpu_topology`).
- [x] **Pmap & VM runtime**: Kernel physical aperture active, zones, kmem, and submaps initialized.
- [x] **Qualcomm BLSP2 UARTDM**: Driver operational (115200 8N1) at `0x075b0000`.
- [x] **ARM GICv3**: Distributor (`GICD`) and per-core Redistributors (`GICR`) active on all 4 cores with native system register interface (`ICC_SRE_EL1.SRE = 1`).
- [x] **ARM Generic Physical Timer**: PPI 30 firing reliably across all cores.
- [x] **PSCI v1.0**: `CPU_ON` (0xC4000003), `CPU_OFF` (0x84000002), and `AFFINITY_INFO` (0xC4000004) verified via SMC #0.
- [x] **Multi-Core Cache Coherency**: Inner Shareable WBWA memory, exclusive monitors (`ldxr`/`stxr`), hardware spinlocks verified.
- [x] **Canonical Mach Secondary Lifecycle**:
  - CPU0: Cluster 0 Core 0 (Kryo Silver) — Boot Core
  - CPU1: Cluster 0 Core 1 (Kryo Silver) — Handed off to `secondary_cpu_main`
  - CPU2: Cluster 1 Core 0 (Kryo Gold) — Handed off to `secondary_cpu_main`
  - CPU3: Cluster 1 Core 1 (Kryo Gold) — Handed off to `secondary_cpu_main`
- [x] **Mach Scheduler `pset0`**: All 4 processors registered, running `idle_thread`, participating in runqueues.
- [x] **Reschedule IPI & Preemption**: SGI 1 delivering `SIGPast`, invoking `ast_check()`, taking `AST_URGENT` in kernel mode, executing `thread_preempted_in_kernel`.
- [x] **Cross-Cluster Thread Migration**: Single thread successfully hopped across cores: CPU0 -> CPU1 -> CPU2 -> CPU3 -> CPU0.
- [x] **Concurrent Lock Contention**: 40,000 atomic operations completed concurrently across all 4 cores with zero race conditions (`g_xzs_contended_counter = 0x9c40`).

---

## Active Repository Branches

* **`main`**: Clean Apple upstream-compatible baseline (`xnu-12377.1.9`).
* **`xzs-bringup`**: Historical bring-up laboratory containing raw bring-up tests, verbose checkpoints, and early diagnostic hooks (anchored at `8a0a42d`).
* **`xzs-port`**: Production clean port branch for ongoing development (Phase D: BSD, VFS, userspace).

---

## Current Blockers

* **None for Mach SMP**. Full 4-core SMP scheduler is 100% complete and verified on hardware.
* **Next Focus (Phase D)**: BSD Subsystem initialization (`bsd_init`) and RAM-disk rootfs driver.
