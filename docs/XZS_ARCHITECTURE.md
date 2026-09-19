# Sony Xperia XZs (MSM8996) — Architecture & Execution Pipeline

This document details the complete architectural execution flow of Apple XNU running on Qualcomm Snapdragon 820 (MSM8996), from primary bootloader power-on through the BSD/VFS storage boundary and future userspace.

---

## 1. End-to-End Boot Architecture

```text
+-------------------------------------------------------------------------+
|                  Hardware Reset (Qualcomm MSM8996)                      |
+-------------------------------------------------------------------------+
                                     │
                                     ▼
+-------------------------------------------------------------------------+
|      Primary Bootloader (PBL) / SBL1 / QSEE TrustZone / ABOOT           |
|      - Sony S1 Fastboot loader initializes LPDDR4 DRAM and USB          |
|      - Loads Android-format boot image containing bootshim + DTB        |
+-------------------------------------------------------------------------+
                                     │
                                     ▼  (EL1 Non-Secure Entry at 0x82000000)
+-------------------------------------------------------------------------+
|                 xzs-bootshim (src/xzs-bootshim/)                        |
|  [XZS-PORT]                                                             |
|  - Parses Qualcomm Flattened Device Tree (DTB) from ABOOT               |
|  - Probes LPDDR4 memory banks & carveouts (modem, TZ, camera, GPU)      |
|  - Builds Apple Device Tree (ADT) in memory at 0x81810000               |
|  - Populates Apple boot_args struct at 0x81800000                       |
|  - Cleans D-cache to Point of Coherency (PoC)                           |
|  - Jumps directly to XNU entry point (_start at 0x82200000)             |
+-------------------------------------------------------------------------+
                                     │
                                     ▼  (Low-Level Assembly Bootstrap)
+-------------------------------------------------------------------------+
|              Apple XNU Low-Level Entry (osfmk/arm64/start.s)            |
|  [XZS-PORT / XZS-COMPAT]                                                |
|  - Installs early exception vectors (LowExceptionVectorBase)            |
|  - Builds bootstrap page tables (16KB granule, TTBR0 physical identity) |
|  - Sets TCR_EL1 (0x226511a511), MAIR_EL1 (0x0c0804ff00bb44ff)           |
|  - Enables MMU (SCTLR_EL1.M = 1) -> Jumps to High KVA (0xfffffe0...)    |
|  - Initializes early UARTDM and persistent RAM (pstore ramoops)         |
+-------------------------------------------------------------------------+
                                     │
                                     ▼  (High KVA C Runtime)
+-------------------------------------------------------------------------+
|               Platform Initialization (osfmk/arm/arm_init.c)            |
|  [XZS-PORT]                                                             |
|  - Copies boot_args and binds BootCpuData                               |
|  - PE_init_platform: parses ADT, registers platform expert              |
|  - Starts ARM Generic Physical Timer (19.2 MHz / CNTPCT_EL0)            |
|  - Configures CPU topology (Cluster 0 Silver + Cluster 1 Gold)          |
+-------------------------------------------------------------------------+
                                     │
                                     ▼
+-------------------------------------------------------------------------+
|          Virtual Memory & Allocator Bootstrap (arm_vm_init)             |
|  [XZS-PORT / XZS-WORKAROUND]                                            |
|  - Builds kernel physical aperture (pmap) and zone allocators (zalloc)   |
|  - Maps MMIO devices and pstore RAM (0xa6000000-0xa7ffffff)             |
|  - thread_call zone priming (allocates 105 elements to avoid stalls)    |
+-------------------------------------------------------------------------+
                                     │
                                     ▼
+-------------------------------------------------------------------------+
|        Platform Peripherals & GICv3 (pexpert/arm/pe_fiq.c)              |
|  [XZS-PORT]                                                             |
|  - Maps Qualcomm BLSP2 UARTDM at 0x075b0000 (115200 8N1)               |
|  - Maps & enables ARM GICv3 Distributor (0x09bc0000)                   |
|  - Wakes CPU0 Redistributor (0x09c00000) & sets ICC_SRE_EL1.SRE = 1    |
+-------------------------------------------------------------------------+
                                     │
                                     ▼
+-------------------------------------------------------------------------+
|           Multi-Core SMP Bring-up via PSCI (osfmk/kern/startup.c)       |
|  [XZS-PORT / CANONICAL MACH]                                            |
|  - For secondary cores (CPU1, CPU2, CPU3):                              |
|    * Issues standard ARM PSCI CPU_ON (0xC4000003) via SMC #0            |
|    * Secondary core boots in start.s -> xzs_secondary_kva_entry         |
|    * Wakes dedicated per-core GICR frame (0x09c20000, 0x09c40000, etc.) |
|    * Enables timer PPIs (27/30) & SGI 1 reschedule interrupt             |
|    * Enters secondary_cpu_main -> joins Mach processor set (pset0)      |
+-------------------------------------------------------------------------+
                                     │
                                     ▼
+-------------------------------------------------------------------------+
|             Full Mach SMP Scheduler Active (4/4 Cores)                  |
|  - 4 Cores running idle_threads across Kryo Silver & Gold clusters      |
|  - Reschedule IPIs delivered via GICv3 SGI 1                            |
|  - AST urgent preemption operational (thread_preempted_in_kernel)       |
|  - Cache coherency & exclusive monitors verified (40k lock contention)  |
+-------------------------------------------------------------------------+
                                     │
                                     ▼
+-------------------------------------------------------------------------+
|                  BSD Subsystem Bootstrap (bsd_init)                     |
|  [XZS-PORT / CANONICAL BSD]                                             |
|  - Initializes BSD credentials, proc0, kernproc, sysctl MIB             |
|  - Allocates VFS mount table and vnode allocation pools                 |
|  - Applies memorystatus static jetsam buffer workaround                 |
|  - Defers non-storage subsystems (Skywalk, lo0, gif0, ether, DTrace)   |
+-------------------------------------------------------------------------+
                                     │
                                     ▼
+-------------------------------------------------------------------------+
|             IOKit Autoconfiguration (bsd_autoconf / IOKitBSDInit)       |
|  [XZS-PORT / CANONICAL IOKIT]                                           |
|  - Publishes IOBSD plane resource                                       |
|  - Traverses IOKit registry matching for boot devices                   |
+-------------------------------------------------------------------------+
                                     │
                                     ▼
+-------------------------------------------------------------------------+
|          Root Device Discovery Boundary (IOFindBSDRoot)                 |
|  [XZS-SELFTEST / BOUNDARY GATE]                                         |
|  - Traverses /chosen and builds serviceMatching("IOMedia")             |
|  - Defers debug serialization (avoids kmem_realloc_guard stall)         |
|  - Bounded 1.0s wait loop polls copyMatchingService("IOMedia")          |
|  - Returns canonical kIOReturnNotFound (0xe00002f0) on missing storage  |
|  - setconf() assigns synthetic rootdev "sd0a" (major=6, minor=0)        |
+-------------------------------------------------------------------------+
                                     │
                                     ▼
+-------------------------------------------------------------------------+
|              VFS Root Mount Boundary (vfs_mountroot)                    |
|  [XZS-WORKAROUND / HARDWARE TERMINAL GATE]                             |
|  - Calls bdevvp(rootdev, &rootvp)                                       |
|  - VNOP_OPEN queries bdevsw[6], returns canonical ENODEV (0x13)         |
|  - bdevvp returns error without panicking                               |
|  - [D51-TERMINAL] logged: cannot mount root, errno = 0x13               |
|  - Automated warm reboot to Fastboot triggered via xzs_spin_halt()      |
+-------------------------------------------------------------------------+
                                     │
                                     ▼  [FUTURE PHASES]
+-------------------------------------------------------------------------+
|  Phase D2: Physical eMMC Storage Bring-up (SDCC1 / CMD17 LBA 1) [DONE]   |
|  Phase D3: GUID Partition Table (GPT) Discovery & Enumeration  [DONE]   |
|  Phase D4: Block Storage Integration (disk0 / bdevvp)           [DONE]   |
|  Phase D5: Real Root Filesystem Mount (HFS+ / APFS / ramdisk)   [NEXT]   |
|  Phase E:  PID 1 Userspace Exec (/sbin/launchd)                         |
|  Phase F:  Interactive Serial Console Shell (/bin/sh)                   |
+-------------------------------------------------------------------------+
```

---

## 2. Upstream XNU vs. Xperia XZs Adaptation Boundary

| Layer | Canonical Apple XNU Behavior | Xperia XZs / MSM8996 Adaptation | Classification |
| :--- | :--- | :--- | :--- |
| **Boot ABI** | Expects Apple iBoot structures, DeviceTree binary at boot, and Secure Boot certificates. | Bootshim translates Qualcomm DTB into Apple Device Tree (ADT) and populates `struct boot_args`. | `XZS-PORT` |
| **CPU Architecture** | Apple Silicon (Firestorm/Icestorm, etc.) with Apple PAC (arm64e) and CTRR/KTRR hardware registers. | Qualcomm Kryo ARMv8.0-A without PAC; built with `-mno-ptrauth`; CTRR presence check handled cleanly in `machine_routines.c`. | `XZS-COMPAT` |
| **Interrupt Controller** | Apple Interrupt Controller (AIC). | Standard ARM GICv3 Distributor (`0x09bc0000`) and Redistributors (`0x09c00000`) in system register mode. | `XZS-PORT` |
| **Multi-Core Boot** | Apple proprietary CPU start registers. | Standard ARM PSCI v1.0 `CPU_ON` SMC call (`0xC4000003`). | `XZS-PORT` |
| **Serial Diagnostics** | Apple SART / SPURT serial hardware. | Qualcomm BLSP2 UARTDM at `0x075b0000` (115200 8N1). | `XZS-PORT` |
| **Post-Mortem Logging** | Apple NVRAM / Panic Log registers. | Persistent DRAM buffer (`0x80060000`) and pstore ramoops (`0xa7fbe000` console, `0xa7f00000` dmesg). | `XZS-PORT` |
| **Root Device Discovery** | Apple ACPI / IOPlatformExpert nub matching `AppleARMPERoot`. | Traverses canonical IOKit matching for `IOMedia` with a bounded 1.0s wait loop; falls back to synthetic `sd0a`. | `XZS-SELFTEST` |
| **Block Device Open** | Panics immediately with `panic("bdevvp failed: open")` if open fails. | Propagates `ENODEV` (0x13) to `vfs_mountroot()` to log terminal telemetry and trigger clean automated reset. | `XZS-WORKAROUND` |

---

## 3. Hardware Resource & Physical Memory Map

### 3.1 SoC & Core Topology
* **SoC**: Qualcomm Snapdragon 820 (MSM8996 Pro / MSM8996SG)
* **CPU Architecture**: ARMv8.0-A (64-bit), Dual-Cluster Qualcomm Kryo
  * **Cluster 0 (Power / Efficiency - Kryo Silver)**:
    * Core 0: MPIDR = `0x0000000080000000` (Aff1=0, Aff0=0) — Boot CPU (CPU0)
    * Core 1: MPIDR = `0x0000000080000001` (Aff1=0, Aff0=1) — Secondary CPU (CPU1)
  * **Cluster 1 (Performance - Kryo Gold)**:
    * Core 0: MPIDR = `0x0000000080000100` (Aff1=1, Aff0=0) — Secondary CPU (CPU2)
    * Core 1: MPIDR = `0x0000000080000101` (Aff1=1, Aff0=1) — Secondary CPU (CPU3)

### 3.2 Audited MMIO Peripherals & Physical Bases

| Peripheral | Physical Base | Size | Description | Source File Reference |
| :--- | :--- | :--- | :--- | :--- |
| **BLSP2 UARTDM (UART2)** | `0x075b0000` | 4 KB | Serial console port (115200 8N1) | `pexpert/arm/pe_serial.c` |
| **GICv3 Distributor (GICD)** | `0x09bc0000` | 64 KB | Global interrupt distributor | `pexpert/arm/pe_fiq.c`, `adt.c` |
| **GICv3 Redistributor (GICR) Array**| `0x09c00000` | 1 MB | Per-core redistributor frame array | `pexpert/arm/pe_fiq.c`, `adt.c` |
| - CPU0 Redistributor | `0x09c00000` | 128 KB | CPU0 control (`GICR_WAKER`) & SGI frame | `pe_fiq.c` |
| - CPU1 Redistributor | `0x09c20000` | 128 KB | CPU1 control & SGI frame | `pe_fiq.c` |
| - CPU2 Redistributor | `0x09c40000` | 128 KB | CPU2 control & SGI frame | `pe_fiq.c` |
| - CPU3 Redistributor | `0x09c60000` | 128 KB | CPU3 control & SGI frame | `pe_fiq.c` |
| **APCS Hardware Watchdog** | `0x09830000` | 4 KB | APCS WDT (`+0x04 RST, +0x08 EN, +0x14 BITE`) | `osfmk/arm64/start.s` |
| **MPM PS_HOLD** | `0x004ab000` | 4 KB | Power manager pull-down register | `osfmk/arm64/start.s` |
| **MPM Sleep Timer** | `0x010b3000` | 4 KB | Qualcomm multi-processor sleep timer | `adt.c` |
| **Qualcomm SDCC1 / SDHCI Controller** | `0x07464900` | 4 KB | SDC1 eMMC 5.1 Host Controller (Samsung BJNB4R) | `xzs_sdhci.c` (Phase D2) |
| **Qualcomm SDCC1 Core Vendor Spec** | `0x07464A00` | 4 KB | SDCC1 vendor-specific register aperture | `xzs_sdhci.c` (Phase D2) |

### 3.3 Diagnostic Memory & Persistent SRAM

| Region | Physical Base | Size | Memory Type / Attributes | Purpose |
| :--- | :--- | :--- | :--- | :--- |
| **IMEM SRAM Restart Reason** | `0x066bf65c` | 4 B | On-chip SRAM (retained across warm boot) | Pre-seeded with `0x77665500` (Fastboot reboot reason) |
| **IMEM SRAM Breadcrumb** | `0x066bf660` | 32 B | On-chip SRAM | Checkpoint ID, errno, fault ESR/ELR (`XZSD` magic) |
| **DRAM Scratch Ring Buffer** | `0x80060000` | 64 KB | Normal WBWA RAM | Lockless ring buffer for early UART puts |
| **DRAM Breadcrumb Mirror** | `0x80060020` | 32 B | Normal WBWA RAM | Checkpoint mirror in physical DRAM |
| **pstore dmesg-ramoops** | `0xa7f00000` | 4 KB | Normal WBWA RAM (`0xa6000000` aperture) | Linux/TWRP ramoops panic zone 0 (`DBGC` magic) |
| **pstore console-ramoops** | `0xa7fbe000` | 256 KB | Normal WBWA RAM (`0xa6000000` aperture) | Persistent console log buffer (`DBGC` magic) |

---

## 4. SMP Inter-Processor Interrupt (IPI) Architecture

* **Routing Scheme**: ARM GICv3 Software Generated Interrupts (SGIs) via system register `ICC_SGI1R_EL1`.
* **SGI Allocations**:
  * `SGI 0`: Maintenance / cross-call.
  * `SGI 1`: Reschedule / AST urgent signal (`SIGPast` / `SIGPdisabled`).
* **Target Encoding**:
  * Cluster 0 (Kryo Silver, CPU0/1): `Aff1 = 0`, `TargetList = (1 << core_id)`.
  * Cluster 1 (Kryo Gold, CPU2/3): `Aff1 = 1`, `TargetList = (1 << core_id)`.
* **Architectural Timers**: Driven by ARM Generic Timers routed in `GICR_ISENABLER0` at 19.200 MHz across all cores: INTID 27 (Virtual Timer `CNTV_*`, primary decrementer) and INTID 30 (Physical Timer `CNTP_*`, fallback).
