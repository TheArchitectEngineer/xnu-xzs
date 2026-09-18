# XNU-XZS Source Modification & Architecture Audit (Phase D1 Freeze)

## 1. Upstream Baseline

* **Upstream Repository**: `https://github.com/apple-oss-distributions/xnu.git`
* **Upstream Tag / Release**: `xnu-12377.1.9` (macOS 15.0 Sequoia / Darwin 24.0.0)
* **Common Merge Base**: `f6217f891ac0bb64f3d375211650a4c1ff8ca1ea`
* **Target Hardware**: Sony Xperia XZs (G8231 / Tone Keyaki) — Qualcomm MSM8996 / Snapdragon 820
* **Audit Baseline**: Phase D1 Publication Freeze on branch `xzs-bringup`.

---

## 2. Classification Taxonomy

| Category | Definition | Repository Lifespan |
| :--- | :--- | :--- |
| **`XZS-PORT`** | Permanent hardware port implementation (UARTDM, GICv3, PSCI, secondary SMP trampolines, DTB/ADT, timers, memory apertures). | **Permanent** in `xzs-port` & `main` |
| **`XZS-COMPAT`** | Architectural compatibility adaptations for standard ARMv8.0-A non-Apple silicon (no-PAC, no CTRR/KTRR, headless PE, standalone corecrypto). | **Permanent** in `xzs-port` & `main` |
| **`XZS-WORKAROUND`** | Temporary early bring-up compromises (subsystem deferrals, allocator priming, sizing caps, error propagation) to reach acceptance milestones without blocking on missing drivers. | **Temporary** in `xzs-bringup` (tracked in `docs/XZS_WORKAROUNDS.md`) |
| **`XZS-DEBUG`** | Diagnostic telemetry and post-mortem hooks (pstore ramoops, early UART logging, crash register dumps, fastboot reboot). | **Configurable** via compile flags |
| **`XZS-SELFTEST`** | Synthetic hardware verification tests and bounded wait loops (bounded IOMedia wait, synthetic `sd0a` rootdev, 40k spinlock contention). | **Phase-Specific** behind `CONFIG_XZS_SELFTEST` |

---

## 3. Comprehensive File Inventory (51 XNU Files + Companion Subsystems)

### 3.1 `XZS-PORT` — Hardware Platform Implementation

| File | Subsystem | Description |
| :--- | :--- | :--- |
| `iokit/Kernel/arm/AppleARMSMP.cpp` | IOKit SMP | GICv3 SGI routing and inter-processor interrupt delivery (`PE_cpu_signal`) based on affinity levels (Aff1/Aff0). |
| `iokit/bsddev/IOKitBSDInit.cpp` | IOKit / BSD | Headless platform expert handling; `IOBSD` plane publishing. |
| `osfmk/arm/arm_init.c` | Platform Init | Platform entry points, early MMU configuration, early telemetry hooks, and boot argument propagation. |
| `osfmk/arm/pmap/pmap_data.h` | VM / Pmap | Memory mappings and I/O ranges for MSM8996 physical peripherals. |
| `osfmk/arm64/caches_asm.s` | Cache Management | ARMv8.0 Clean & Invalidate cache maintenance (`dc civac`) ensuring multi-core coherency to PoC. |
| `osfmk/arm64/machine_routines.c` | Architecture | CPU topology parsing (Cluster 0 Silver + Cluster 1 Gold), MPIDR decoding, core counts, and SGI targeting. |
| `osfmk/arm64/pcb.c` | Task / Thread | Context switch machine layer (`Switch_context`), active thread tracking across 4 cores. |
| `osfmk/arm64/sleh.c` | Interrupt Handling | GICv3 SGI (0..15) and PPI (timer) dispatch, reschedule AST routing (`ast_check`), and EOI/IAR register access. |
| `osfmk/arm64/start.s` | Low-Level Boot | Secondary CPU entry trampolines (`xzs_secondary_entry`), stack allocation, reentrant UART/pstore telemetry, watchdog bite trigger, and automated fastboot reset. |
| `osfmk/kern/ast.c` | Scheduler AST | Kernel preemption hook (`thread_preempted_in_kernel`) handling `AST_URGENT`. |
| `osfmk/kern/startup.c` | SMP Bootstrap | PSCI `CPU_ON` (`0xC4000003`) multi-core launch, `secondary_cpu_main` handoff, idle thread allocation, and processor registration into Mach `pset0`. |
| `pexpert/arm/pe_fiq.c` | GICv3 Driver | Full ARM GICv3 Distributor (`0x09bc0000`) and per-core Redistributor (`0x09c00000`) initialization, wake-up, and routing. |
| `pexpert/arm/pe_init.c` | Platform Expert | Platform identification for Tone Keyaki / MSM8996, device tree parsing, and boot parameter validation. |
| `pexpert/arm/pe_serial.c` | Serial Driver | Qualcomm BLSP2 UARTDM (0x075b0000) driver with FIFO management and early character I/O. |
| `pexpert/gen/pe_gen.c` | Boot Arguments | Command line argument parsing and device tree property retrieval. |
| `pexpert/pexpert/arm64/VMAPPLE.h` | Platform Defines | MSM8996 base addresses for GICD, GICR, UARTDM, MPM timer, and TLMM GPIO; defines `CONFIG_XZS_BRINGUP`. |

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
| `osfmk/arm64/machine_routines.c` | Architecture | `ml_unsafe_kernel_text_init`: Explicitly sets non-CTRR state (`_unsafe_kernel_text = false`, `_unsafe_kernel_text_initialized = true`) when Apple iBoot DT properties are absent. |
| `osfmk/arm64/machine_routines_asm.s` | Context Switch | Bypasses PAC signed thread state verification in `ml_check_signed_state`. |
| `osfmk/conf/files.arm64` | Build Config | Links XZS platform files and excludes Apple SEP/SPTM modules. |

---

### 3.3 `XZS-WORKAROUND` — Bring-up Subsystem Deferrals & Memory Adaptations

| File | Subsystem | Description | Target Retirement Phase |
| :--- | :--- | :--- | :--- |
| `bsd/kern/bsd_init.c` | BSD Core | Defers `skywalk_init()`, `dtrace_postinit()`, `loopattach()` (`lo0`), `gif_init()` (`gif0`), `ether_family_init()`. | Phase G & H |
| `bsd/kern/kern_memorystatus.c` | Memory Status | Static 64-byte aligned `memorystatus_jetsam_snapshot` buffer (capacity 2) to bypass early `kalloc_data` multi-page allocation stall. | Phase E |
| `bsd/dev/dtrace/fbt.c` | DTrace | Defers `fbt_init()` to eliminate probe table memory pressure. | Phase G |
| `bsd/dev/dtrace/profile_prvd.c` | DTrace | Defers `profile_init()` to eliminate background profiling timer load. | Phase G |
| `bsd/netinet/tcp_cache.c` | Networking | Caps `tcp_cache` size to 32 entries (instead of 1024) to prevent `zalloc_permanent` stall. | Phase H |
| `bsd/netinet/tcp_cc.c` | Networking | Defers `tcp_ccdbg_control_register()` socket control registration. | Phase H |
| `bsd/netinet/tcp_subr.c` | Networking | Defers TCP Fast Open (`tcp_fastopen = 0`); caps `tcp_tcbhashsize = 64`. | Phase H |
| `bsd/netinet6/ip6_input.c` | Networking | Guards `ip6_init_delayed()` against `lo_ifp == NULL` to avoid panic when `lo0` is deferred. | Phase H |
| `bsd/vfs/vfs_fsevents.c` | VFS | Caps `max_kfs_events = 8` (instead of 4096). | Phase G |
| `bsd/vfs/vfs_subr.c` | VFS | `bdevvp()`: Returns `ENODEV` (0x13) to `vfs_mountroot()` instead of panicking on open failure. | Phase D3 |
| `iokit/Kernel/IOStartIOKit.cpp` | IOKit Time | Sets `IOKitInitializeTime()` RTC timeout to 0 (non-blocking) until PMIC RTC driver exists. | Phase B |
| `iokit/bsddev/IOKitBSDInit.cpp` | IOKit BSD | Defers `matching->serialize(s)` debug logging to prevent `kmem_realloc_guard` allocation stall. | Phase G |
| `osfmk/kern/thread_call.c` | Thread Call | Primes `thread_call_zone` with 105 elements during early boot to eliminate `necp_client_init` lock contention. | Phase G |
| `osfmk/kern/zalloc.c` | Zone Allocator | Diagnostic tracing and wake-up instrumentation for thread-call zone expander. | Phase G |

---

### 3.4 `XZS-DEBUG` — Telemetry & Diagnostics

| File | Subsystem | Description |
| :--- | :--- | :--- |
| `osfmk/kern/debug.c` | Panic Hook | `panic_trap_to_debugger` redirecting kernel panics to early UART, persistent pstore ramoops, and warm reboot. |
| `osfmk/arm64/start.s` | Diagnostic I/O | `xzs_early_putc`, `xzs_early_puts`, `xzs_early_puthex64` writing to physical UART, DRAM buffer (`0x80060000`), and pstore ramoops (`0xa7fbe000` console, `0xa7f00000` dmesg). |
| `osfmk/arm64/sleh.c` | Crash Capture | Panic details dumper printing `pc`, `lr`, `esr`, `far`, and general-purpose registers on fatal exceptions. |

---

### 3.5 `XZS-SELFTEST` — Synthetic Verification Probes

| File | Subsystem | Description |
| :--- | :--- | :--- |
| `iokit/bsddev/IOKitBSDInit.cpp` | IOKit Matching | Bounded 1.0s wait loop (100 steps × 10ms via `CNTVCT_EL0`) polling `copyMatchingService("IOMedia")`, returning canonical `kIOReturnNotFound` (`0xe00002f0`) when no physical storage is attached. |
| `bsd/kern/bsd_init.c` | BSD Rootdev | `setconf()` fallback configuring synthetic rootdev `sd0a` (`major=6, minor=0`) when `IOFindBSDRoot()` returns `kIOReturnNotFound`, driving the VFS mountroot pipeline down to `bdevvp()`. |
| `osfmk/kern/startup.c` | Scheduler Tests | Synthetic worker thread dispatch, cross-cluster ring migration (CPU0->1->2->3->0), and 40,000-op concurrent `hw_lock` contention verification (`0x9c40`). |

---

### 3.6 Companion Subsystems & Tooling

* **`src/xzs-bootshim/`**: Standalone ARM64 EL1 bootshim that intercepts Sony ABOOT, parses Qualcomm DTB (`device/tone-keyaki.dtb`), builds ADT, constructs `boot_args`, and executes XNU `_start`.
* **`src/xzs-debug-dumper/`**: Standalone USB CDC-ACM and UART forensic payload for raw RAM dumping without host ADB.
* **`src/xzs-ram-test/`**: Minimal DRAM retention and pattern persistence verification binary.
* **`device/tone-keyaki.dtb`**: Qualcomm flattened device tree for Sony Xperia XZs (MSM8996).
* **`scripts/`**: Complete suite for building, auditing PAC (`check-no-pac.sh`), packaging (`package-boot.sh`), running hardware cycles, and extracting pstore ramoops (`run-and-extract.sh`).
