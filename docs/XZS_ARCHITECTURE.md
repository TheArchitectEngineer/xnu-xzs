# Sony Xperia XZs (MSM8996) — XNU Architecture & Execution Pipeline

## 1. High-Level Boot Pipeline

The boot process from cold power-on to userland executes through the following stages:

```text
[ Hardware Reset ]
       │
       ▼
[ Primary Bootloader (PBL) / SBL1 / QSEE / TrustZone ]
       │
       ▼
[ Sony Secondary Bootloader (S1 / LK / ABOOT) ]
       │  Loads Android-standard boot image: kernel (bootshim) + ramdisk + DTB
       ▼
[ xzs-bootshim (EL1 entry at 0x82000000) ]
       │  Parses Qualcomm DTB, detects MSM8996 hardware & DRAM topology
       │  Constructs Apple Device Tree (ADT) at 0x81810000
       │  Populates struct boot_args at 0x81800000
       │  Flattens & prepares Mach-O XNU kernel at 0x82200000
       ▼
[ XNU Physical Entry (_start in osfmk/arm64/start.s) ]
       │  Sets up exception vectors (LowExceptionVectorBase)
       │  Builds early bootstrap page tables (bootstrap_pagetables)
       │  Configures TCR_EL1, MAIR_EL1, TTBR0_EL1, TTBR1_EL1
       │  Enables MMU (SCTLR_EL1.M = 1) -> Transition to High KVA (0xfffffe0000000000)
       ▼
[ Kernel Early Bootstrap (arm_init in osfmk/arm/arm_init.c) ]
       │  Copies boot_args, initializes BootCpuData
       │  PE_init_platform: parses ADT, binds platform expert
       │  Configures system counter & timers (19.2 MHz / CNTPCT_EL0)
       │  Initializes Mach thread & processor bootstrap (cpu_bootstrap)
       │  Sorts kernel startup table (kernel_startup_initialize_upto)
       ▼
[ Pmap & VM Initialization (arm_vm_init in osfmk/arm64/arm_vm_init.c) ]
       │  Constructs kernel physical aperture and page allocator
       │  Initializes kernel zones (zinit) and submaps
       ▼
[ Platform & Interrupt Subsystem (PE_init_cpu) ]
       │  Initializes Qualcomm BLSP2 UARTDM serial console
       │  pe_init_fiq: Initializes ARM GICv3 Distributor (GICD)
       │  Wakes and binds CPU0 Redistributor (GICR) & CPU Interface
       ▼
[ Secondary CPU Bring-up (PSCI SMP in osfmk/kern/startup.c) ]
       │  For each secondary CPU (CPU1, CPU2, CPU3):
       │    Allocates per-CPU state (CpuDataEntries), stacks, and idle threads
       │    Issues PSCI CPU_ON (0xC4000003) via SMC #0
       │    Secondary core boots in start.s -> xzs_secondary_kva_entry
       │    Initializes per-core GICR, enables timer PPI & SGI
       │    Calls secondary_cpu_main -> joins Mach processor set (pset0)
       ▼
[ Full Mach SMP Scheduler Active ]
       │  CPU0, CPU1 (Kryo Silver) + CPU2, CPU3 (Kryo Gold) all online
       │  Idle threads running on all 4 cores
       │  SGI reschedule IPIs & kernel AST preemption operational
       ▼
[ BSD Subsystem Bootstrap (bsd_init in bsd/kern/bsd_init.c) ]
       │  Initializes Mach-to-BSD abstraction, proc structures, credentials
       │  Initializes VFS, mount tables, devfs
       │  Mounts root filesystem (RAM disk / UFS internal storage)
       ▼
[ Launchd / Userspace ]
```

---

## 2. Hardware Resource & Physical Memory Map

### 2.1 SoC & Core Topology
* **SoC**: Qualcomm Snapdragon 820 (MSM8996 Pro / MSM8996SG)
* **CPU Architecture**: ARMv8.0-A (64-bit), Dual-Cluster Qualcomm Kryo
  * **Cluster 0 (Power / Efficiency - Kryo Silver)**:
    * Core 0: MPIDR = `0x0000000080000000` (Aff1=0, Aff0=0) — Boot CPU (CPU0)
    * Core 1: MPIDR = `0x0000000080000001` (Aff1=0, Aff0=1) — Secondary CPU (CPU1)
  * **Cluster 1 (Performance - Kryo Gold)**:
    * Core 0: MPIDR = `0x0000000080000100` (Aff1=1, Aff0=0) — Secondary CPU (CPU2)
    * Core 1: MPIDR = `0x0000000080000101` (Aff1=1, Aff0=1) — Secondary CPU (CPU3)

### 2.2 MMIO Peripherals & Register Bases

| Peripheral | Physical Address | Size | Description |
| :--- | :--- | :--- | :--- |
| **BLSP2 UARTDM (UART2)** | `0x075b0000` | 4 KB | Serial console port (115200 8N1) |
| **GICv3 Distributor (GICD)** | `0x09bf0000` | 64 KB | Global interrupt distributor (`GICD_CTLR`, `GICD_ISENABLER`) |
| **GICv3 Redistributor (GICR) Base** | `0x09c00000` | 2 MB | Per-core redistributor frame array |
| **CPU0 Redistributor (RD_base)** | `0x09c00000` + offset | 128 KB | CPU0 control (`GICR_WAKER`) & SGI/PPI frame (`GICR_ISENABLER0`) |
| **CPU1 Redistributor (RD_base)** | `0x09c20000` + offset | 128 KB | CPU1 control & SGI/PPI frame |
| **CPU2 Redistributor (RD_base)** | `0x09c40000` + offset | 128 KB | CPU2 control & SGI/PPI frame |
| **CPU3 Redistributor (RD_base)** | `0x09c60000` + offset | 128 KB | CPU3 control & SGI/PPI frame |
| **MPM Timer** | `0x010b3000` | 4 KB | Qualcomm multi-processor sleep timer |
| **TLMM GPIO** | `0x01010000` | 256 KB | Top Level Mode Multiplexer / GPIO controller |

### 2.3 Persistent RAM & Diagnostics Memory

| Region | Physical Address | Size | Description |
| :--- | :--- | :--- | :--- |
| **Early DRAM Scratch Buffer** | `0x80060000` | 64 KB | Lockless ring buffer for early UART puts ("XZSD" signature) |
| **pstore console-ramoops** | `0xa7fbe000` | 256 KB | Persistent RAM console buffer ("DBGC" persistent_ram signature) |
| **pstore dmesg-ramoops** | `0xa7f00000` | 4 KB | Persistent RAM kernel oops/panic buffer |

---

## 3. Inter-Processor Communication (IPI) & Interrupt Architecture

* **Interrupt Controller**: ARM GICv3 in native Affinity Routing mode (`ICC_SRE_EL1.SRE = 1`).
* **Inter-Processor Interrupts (IPI)**:
  * Handled via GICv3 Software Generated Interrupts (SGI):
    * `SGI 0`: Maintenance / Cross-call
    * `SGI 1`: Reschedule / AST urgent signal (`SIGPast` / `SIGPdisabled`)
  * Target Addressing: Constructed via `ICC_SGI1R_EL1`:
    * Cluster 0 (Silver): `Aff1 = 0`, `TargetList = (1 << core_id)`
    * Cluster 1 (Gold): `Aff1 = 1`, `TargetList = (1 << core_id)`
* **Per-Core Timers**:
  * Driven by ARM Generic Physical Timer (`CNTP_TVAL_EL0`, `CNTP_CTL_EL0`).
  * Routed as PPI 30 (Physical Timer PPI) in `GICR_ISENABLER0`.
  * Frequency: 19.200 MHz, verified consistent across all 4 cores.

---

## 4. SMP Lifecycle State Machine

1. **Phase 1: Boot Core Initialization (CPU0)**:
   - Sets up `pset0` (processor set zero), allocates idle threads for all 4 cores via `idle_thread_create()`.
   - Transitions `processor_t` states: `PROCESSOR_OFF_LINE` -> `PROCESSOR_START`.
2. **Phase 2: Secondary Core Wakeup via PSCI**:
   - Invokes SMC #0 with `PSCI_0_2_FN64_CPU_ON` (`0xC4000003`), passing target MPIDR and secondary entry PA.
   - Target core boots in EL1, configures per-core MMU, caches, and stack pointer (`SP_EL0`).
3. **Phase 3: Hardware Interface Activation**:
   - Secondary core executes `pe_init_fiq()`, waking its dedicated `GICR` and configuring system registers.
   - Clears `DAIF` interrupt masks (enables IRQ/FIQ).
4. **Phase 4: Canonical Mach Scheduler Integration**:
   - Core enters `secondary_cpu_main(NULL)`.
   - Registers itself as running, joins runqueues, and enters the idle loop running `idle_thread`.
   - Participates in real thread dispatch, cross-cluster migration, and preemption.
