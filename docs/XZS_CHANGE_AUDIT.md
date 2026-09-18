# XNU-XZS Source Modification & Architecture Audit

## 1. Upstream Baseline

* **Upstream Repository**: `https://github.com/apple-oss-distributions/xnu.git`
* **Upstream Tag / Release**: `xnu-12377.1.9` (macOS 15.0 Sequoia / Darwin 24.0.0)
* **Common Merge Base**: `f6217f891ac0bb64f3d375211650a4c1ff8ca1ea`
* **Target Hardware**: Sony Xperia XZs (G8231 / Tone Keyaki) — Qualcomm MSM8996 / Snapdragon 820

---

## 2. Classification Taxonomy

Every change in the XNU codebase for Xperia XZs is categorized into one of five functional classifications:

| Category | Purpose | Long-term Status |
| :--- | :--- | :--- |
| **`XZS-PORT`** | Permanent hardware port implementation (UARTDM, GICv3, PSCI, secondary SMP trampolines, DTB/ADT, timers). | **Permanent** in `xzs-port` |
| **`XZS-COMPAT`** | Architectural compatibility adaptations for standard ARMv8.0-A non-Apple silicon (no-PAC, no CTRR/KTRR, headless PE, standalone corecrypto). | **Permanent** in `xzs-port` |
| **`XZS-WORKAROUND`** | Temporary architecture debt / early bring-up constraints (e.g. `invalid_tte[0] = boot_tte[0]`, 88MB low DRAM, timer-seeded PRNG). | **Temporary** (tracked in `XZS_TECHNICAL_DEBT.md`) |
| **`XZS-DEBUG`** | Diagnostic telemetry and post-mortem hooks (pstore ramoops, early UART logging, crash register dumps, fastboot reboot). | **Configurable** via compile flags |
| **`XZS-SELFTEST`** | Synthetic hardware verification tests (worker dispatch, ring migration, 40k hw_lock contention, spinlock/TLB test). | **Optional** behind `CONFIG_XZS_SELFTEST` |

---

## 3. Detailed File Inventory (46 Files)

### 3.1 `XZS-PORT` — Hardware Platform Implementation

| File | Subsystem | Description |
| :--- | :--- | :--- |
| `iokit/Kernel/arm/AppleARMSMP.cpp` | IOKit SMP | GICv3 SGI routing and inter-processor interrupt delivery (`PE_cpu_signal`) based on affinity levels (Aff1/Aff0). |
| `iokit/bsddev/IOKitBSDInit.cpp` | IOKit / BSD | Headless platform expert handling to bypass Apple ACPI/IOPlatformExpert device discovery during rootfs bootstrap. |
| `osfmk/arm/arm_init.c` | Platform Init | Platform entry points, early MMU configuration, early telemetry hooks, and boot argument propagation. |
| `osfmk/arm/pmap/pmap_data.h` | VM / Pmap | Memory mappings and I/O ranges for MSM8996 physical peripherals. |
| `osfmk/arm64/caches_asm.s` | Cache Management | ARMv8.0 Clean & Invalidate cache maintenance (`dc civac`) ensuring multi-core coherency. |
| `osfmk/arm64/machine_routines.c` | Architecture | CPU topology parsing (Cluster 0 Silver + Cluster 1 Gold), MPIDR decoding, core counts, and SGI targeting. |
| `osfmk/arm64/pcb.c` | Task / Thread | Context switch machine layer (`Switch_context`), active thread tracking across 4 cores. |
| `osfmk/arm64/sleh.c` | Interrupt Handling | GICv3 SGI (0..15) and PPI (timer) dispatch, reschedule AST routing (`ast_check`), and EOI/IAR register access. |
| `osfmk/arm64/start.s` | Low-Level Boot | Secondary CPU entry trampolines (`xzs_secondary_entry`, `xzs_secondary_kva_entry`), stack allocation, and reentrant early UART/pstore telemetry. |
| `osfmk/kern/ast.c` | Scheduler AST | Kernel preemption hook (`thread_preempted_in_kernel`) handling `AST_URGENT`. |
| `osfmk/kern/startup.c` | SMP Bootstrap | PSCI `CPU_ON` calls, `secondary_cpu_main` handoff, idle thread allocation, and processor registration into Mach `pset0`. |
| `pexpert/arm/pe_fiq.c` | GICv3 Driver | Full ARM GICv3 Distributor (`GICD`) and per-core Redistributor (`GICR`) initialization, wake-up, and routing. |
| `pexpert/arm/pe_init.c` | Platform Expert | Platform identification for Tone Keyaki / MSM8996, device tree parsing, and boot parameter validation. |
| `pexpert/arm/pe_serial.c` | Serial Driver | Qualcomm BLSP2 UARTDM (0x075b0000) driver with FIFO management and early character I/O. |
| `pexpert/gen/pe_gen.c` | Boot Arguments | Command line argument parsing and device tree property retrieval. |
| `pexpert/pexpert/arm64/VMAPPLE.h` | Platform Defines | MSM8996 base addresses for GICD, GICR, UARTDM, MPM timer, and TLMM GPIO. |

---

### 3.2 `XZS-COMPAT` — ARMv8.0 Non-Apple Silicon Compatibility

| File | Subsystem | Description |
| :--- | :--- | :--- |
| `EXTERNAL_HEADERS/AppleFeatures.h` | SDK Headers | Macro definitions for Apple-specific features absent in open-source SDKs. |
| `EXTERNAL_HEADERS/CodeSignature/Entitlements.h` | Security | Entitlement structures for kernel code signature validation. |
| `EXTERNAL_HEADERS/CoreEntitlements/V2/API.h` | Security | CoreEntitlements V2 API stubs for non-Apple environments. |
| `EXTERNAL_HEADERS/CoreEntitlements/V2/Kernel.h` | Security | Kernel-internal entitlement parsing utilities. |
| `EXTERNAL_HEADERS/TrustCache/API.h` | Security | TrustCache stubs allowing unsigned kernel modules and test binaries. |
| `EXTERNAL_HEADERS/arm64/ppl/sart.h` | Security / PPL | Stubs for Apple SART (Secure Access Restricted Target). |
| `EXTERNAL_HEADERS/arm64/ppl/uat.h` | Security / PPL | Stubs for Apple UAT (Unified Address Translation). |
| `EXTERNAL_HEADERS/arm64/tunables/tunables.s` | Architecture | Tunable registers assembly stubs. |
| `EXTERNAL_HEADERS/iBoot/boot_args_abi.h` | Boot ABI | Bootloader argument structure definition for compatibility with `xzs-bootshim`. |
| `EXTERNAL_HEADERS/os/firehose_buffer_private.h` | Logging | Firehose trace buffer memory layouts. |
| `bsd/dev/arm64/sysctl.c` | Sysctl | Bypasses queries to Apple-proprietary CPU registers (`KTRR`, `CTRR`, `APL_HID`). |
| `bsd/kern/kern_exec.c` | Exec | Disables mandatory PAC pointer signing verification during binary load. |
| `bsd/kern/lockdown_mode.c` | Security | Bypasses Apple Lockdown Mode hardware checks. |
| `bsd/sys/make_symbol_aliasing.sh` | Build Scripts | Toolchain alias generator updated for modern open-source LLVM/Clang. |
| `libkern/crypto/register_crypto.c` | CoreCrypto | Software crypto registration fallback when Apple Secure Enclave Processor (SEP) is not present. |
| `makedefs/MakeInc.def` | Build System | Sets target architecture `-march=armv8-a`, disables PAC (`-mno-ptrauth`), sets `BUILD_LTO=0` and `BOUND_CHECKS=0`. |
| `osfmk/arm/commpage/commpage_asm.s` | Commpage | Removes Apple PAC signing instructions (`pacia`, `autia`) from shared userspace commpage. |
| `osfmk/arm64/amcc_rorgn_stubs.c` | Memory Controller | Stubs for Apple Memory Controller read-only regions (CTRR). |
| `osfmk/arm64/arm64_hypercall.c` | Virtualization | Hypercall stubs for bare-metal Qualcomm execution (EL1 without EL2 hypervisor). |
| `osfmk/arm64/locore.s` | Low-Level Entry | Exception entry points adjusted to bypass PAC authentication. |
| `osfmk/arm64/machine_routines_asm.s` | Context Switch | Bypasses PAC signed thread state verification in `ml_check_signed_state`. |
| `osfmk/conf/files.arm64` | Build Config | Links XZS platform files and excludes Apple SEP/SPTM modules. |

---

### 3.3 `XZS-WORKAROUND` — Temporary Architecture Debt

| File | Subsystem | Description | Resolution Plan |
| :--- | :--- | :--- | :--- |
| `osfmk/arm/pmap/pmap.c` | Memory / Pmap | Relaxed physical address bounds for low 88MB DRAM window. | Expand to full 4GB physical memory map once high DRAM ranges verified. |
| `osfmk/arm64/arm_vm_init.c` | VM Init | `invalid_tte[0] = boot_tte[0]` preserving early physical identity mapping. | Transition to isolated identity table unmapped after bootstrap. |
| `osfmk/prng/prng_random.c` | PRNG | Software random generation seeded by hardware timer counter (`CNTPCT_EL0`). | Connect Qualcomm hardware True Random Number Generator (TRNG) driver. |
| `osfmk/vm/vm_kern.c` | VM Submaps | Reduced submap sizes to fit within the initial 88MB memory limit. | Dynamically size VM submaps based on full memory size from ADT. |
| `osfmk/vm/vm_pageout.c` | VM Pageout | Throttled background pageout daemon for memory-constrained bootstrap. | Restore canonical pageout parameters once rootfs is mounted. |
| `osfmk/vm/vm_reclaim.c` | VM Reclaim | Zone reclaim thresholds tuned for low physical memory. | Scale with total RAM. |
| `osfmk/vm/vm_user.c` | VM User | User address space allocation bounds adjusted for early bring-up. | Revert to standard 64-bit user address space. |

---

### 3.4 `XZS-DEBUG` — Telemetry & Diagnostics

| File | Subsystem | Description |
| :--- | :--- | :--- |
| `osfmk/kern/debug.c` | Panic Hook | `panic_trap_to_debugger` redirecting kernel panics to early UART, persistent pstore ramoops, and warm reboot. |
| `osfmk/arm64/start.s` | Diagnostic I/O | `xzs_early_putc`, `xzs_early_puts`, `xzs_early_puthex64` writing to physical UART, DRAM buffer (0x80060000), and pstore ramoops (0xa7fbe000). |
| `osfmk/arm64/sleh.c` | Crash Capture | Panic details dumper printing `pc`, `lr`, `esr`, `far`, and general-purpose registers `x0..x3, sp, fp` on fatal exceptions. |

---

### 3.5 `XZS-SELFTEST` — Synthetic Verification

| File | Subsystem | Description |
| :--- | :--- | :--- |
| `osfmk/kern/startup.c` | Scheduler Tests | Synthetic Thread A/B/C tests, worker thread dispatch, cross-cluster ring migration (0->1->2->3->0), and 40,000-op concurrent `hw_lock` contention. Gated behind `CONFIG_XZS_SELFTEST`. |
