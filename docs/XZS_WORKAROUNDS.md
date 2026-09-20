# Xperia XZs / MSM8996 Phase D1 Workaround & Compatibility Matrix

This document provides an exhaustive inventory of all architectural compatibility shims (`XZS-COMPAT`), temporary bring-up workarounds (`XZS-WORKAROUND`), and synthetic verification probes (`XZS-SELFTEST`) active in the repository on branch `xzs-bringup`.

Source code in this repository is the single source of truth.

---

## 1. Classification Taxonomy

To ensure engineering rigor and prevent technical debt conflation, every non-upstream path is strictly categorized into one of three classifications:

| Category | Definition | Lifespan & Target Branch |
| :--- | :--- | :--- |
| **`XZS-COMPAT`** | Permanent architectural compatibility adaptations for standard ARMv8.0-A non-Apple silicon (e.g. absent Apple PAC, absent Apple CTRR/KTRR, absent Secure Enclave, standard GICv3 system registers, standard ARM PSCI). | **Permanent**: Merged into `xzs-port` and clean upstream ports. |
| **`XZS-WORKAROUND`** | Temporary bring-up compromises designed to bypass subsystems, defer non-essential daemons, or prime allocators to avoid deadlock, starvation, or missing driver blocking before reaching current milestones. | **Temporary**: Maintained in `xzs-bringup`; must be systematically retired once prerequisites exist. |
| **`XZS-SELFTEST`** | Synthetic hardware verification instrumentation and bounded test harnesses designed to prove milestone attainment empirically on physical hardware without relying on complete downstream subsystems. | **Phase-Specific**: Retired or gated behind diagnostic test macros once real drivers take over. |

---

## 2. Comprehensive Workaround Matrix (17 Active Items)

### 1. TCP Fast Open Defer
* **Name**: `tcp_tfo_init_defer`
* **File / Function**: [`src/xnu/bsd/netinet/tcp_subr.c`](../src/xnu/bsd/netinet/tcp_subr.c#L496-L510) — `tcp_tfo_init()`
* **Reason**: During early bootstrap prior to `corecrypto.kext` registration, the AES crypto provider (`g_crypto_funcs->ccaes_cbc_encrypt`) is unavailable. Attempting canonical initialization crashes or hangs on null function pointers.
* **Classification**: `XZS-WORKAROUND`
* **What canonical behavior is being bypassed**: Generation of random AES-128 key via `read_frandom()` and crypto context setup via `aes_encrypt_key128()`.
* **Why acceptable for current Phase**: Phase D1 scope is bounded by BSD/VFS root-device selection and mountroot. TCP Fast Open is an optimization for inbound/outbound TCP connections and is unreferenced by VFS.
* **Dependency**: Registration of CoreCrypto AES provider.
* **Removal condition**: `corecrypto` registered and functional.
* **Future phase where it must be revisited**: Phase H (Networking).
* **Hardware evidence**: Boot reaches and logs `[tcp_tfo_init] [XZS-WORKAROUND] crypto provider unavailable; deferring TCP Fast Open`.

---

### 2. Skywalk Subsystem Defer
* **Name**: `skywalk_init_defer`
* **File / Function**: [`src/xnu/bsd/kern/bsd_init.c`](../src/xnu/bsd/kern/bsd_init.c#L815-L833) — `bsd_init()`, [`src/xnu/bsd/skywalk/core/skywalk.c`](../src/xnu/bsd/skywalk/core/skywalk.c#L516-L539)
* **Reason**: Skywalk requires complex memory arena allocations, nexus providers, and kernel threads. During early bootstrap before VM pageout daemon is operational, `sk_alloc_data()` triggers `zone_expand_locked` stalls on `kalloc.data.2048`.
* **Classification**: `XZS-WORKAROUND`
* **What canonical behavior is being bypassed**: `skywalk_init()` full memory arena configuration, nexus channel initialization, and selftests.
* **Why acceptable for current Phase**: Skywalk is the modern userspace packet networking infrastructure. It has zero interaction with block device discovery, VFS mount tables, or root filesystem bootstrap.
* **Dependency**: Full VM pageout lifecycle and networking nexus drivers.
* **Removal condition**: VM paging and network device drivers active.
* **Future phase where it must be revisited**: Phase G (Restore deferred subsystems) / Phase H (Networking).
* **Hardware evidence**: Verified checkpoint `[XZS-BOOT] [D42-5] skywalk_init DEFERRED (XZS-WORKAROUND)`.

---

### 3. Thread Call Zone Priming & Reserve
* **Name**: `thread_call_zone_priming`
* **File / Function**: [`src/xnu/osfmk/kern/thread_call.c`](../src/xnu/osfmk/kern/thread_call.c#L559-L610) — `thread_call_initialize()`, [`src/xnu/osfmk/kern/zalloc.c`](../src/xnu/osfmk/kern/zalloc.c#L4917-L4950)
* **Reason**: Initial `thread_call_zone` allocation provides 99 elements. Early kernel initialization consumes exactly 99 elements up to `necp_client_init()`. Entering NECP triggers 3 consecutive synchronous thread-call allocations while holding zone locks, causing deadlocks or allocator stalls.
* **Classification**: `XZS-WORKAROUND`
* **What canonical behavior is being bypassed**: Asynchronous on-demand zone expansion via `zone_expand_async`.
* **Why acceptable for current Phase**: Warms up and expands the zone synchronously on CPU 0 during early single-threaded boot (`STARTUP_SUB_THREAD_CALL`), provisioning 131 free elements and preventing all expander contention.
* **Dependency**: None (self-contained zone allocation).
* **Removal condition**: Full scheduler workqueue and async zone expander threads fully operational.
* **Future phase where it must be revisited**: Phase G (Subsystem restore).
* **Hardware evidence**: Hardware logs confirm clean progression through `necp_init` (`[D42-6]` -> `[D42-7]`) without lock contention.

---

### 4. Memorystatus Jetsam Static Bring-up Buffer
* **Name**: `memorystatus_static_jetsam_buffer`
* **File / Function**: [`src/xnu/bsd/kern/kern_memorystatus.c`](../src/xnu/bsd/kern/kern_memorystatus.c#L2160-L2190) — `memorystatus_init()`
* **Reason**: Canonical XNU dynamically allocates `memorystatus_jetsam_snapshot` sized for `maxproc` (typically hundreds of KB) via `kalloc_data`. Prior to root mount and pageout initialization, multi-page dynamic allocations can stall on page acquisition.
* **Classification**: `XZS-WORKAROUND`
* **What canonical behavior is being bypassed**: Dynamic sizing of jetsam snapshot table based on `maxproc`.
* **Why acceptable for current Phase**: Provides a 64-byte aligned static buffer with `memorystatus_jetsam_snapshot_max = 2`. All consumers respect the `max` field. Memorystatus initialization completes cleanly without memory allocation stalls.
* **Dependency**: Dynamic VM memory expansion.
* **Removal condition**: Root filesystem mounted, swap/pageout daemon running, and full userspace process table active.
* **Future phase where it must be revisited**: Phase E (Userspace bootstrap / launchd).
* **Hardware evidence**: Hardware logs confirm `[MEMSTAT-JS-STATIC] [XZS-WORKAROUND] static buf ptr=... max=0x2`.

---

### 5. Non-Apple Silicon CTRR Hardware Compatibility
* **Name**: `ctrr_absence_compat`
* **File / Function**: [`src/xnu/osfmk/arm64/machine_routines.c`](../src/xnu/osfmk/arm64/machine_routines.c#L3159-L3173) — `ml_unsafe_kernel_text_init()`
* **Reason**: Qualcomm Snapdragon 820 lacks Apple Configurable Text Read-Only Region (CTRR) / Kernel Text Read-Only Region (KTRR) registers and Apple iBoot device tree properties (`kernel-ctrr-to-be-enabled`). Upstream XNU treats missing DT properties as uninitialized state.
* **Classification**: `XZS-COMPAT`
* **What canonical behavior is being bypassed**: Querying Apple-proprietary iBoot device tree CTRR properties.
* **Why acceptable for current Phase**: Explicitly marks `_unsafe_kernel_text = false` and `_unsafe_kernel_text_initialized = true`, ensuring standard ARMv8 page tables protect kernel text without panicking.
* **Dependency**: Standard ARMv8 MMU page table protections.
* **Removal condition**: Permanent on standard ARMv8 hardware.
* **Future phase where it must be revisited**: Permanent architecture adaptation.
* **Hardware evidence**: Kernel boots cleanly past `STARTUP_SUB_TUNABLES` with write-protected kernel text.

---

### 6. DTrace Function Boundary Tracing (FBT) Defer
* **Name**: `dtrace_fbt_defer`
* **File / Function**: [`src/xnu/bsd/dev/dtrace/fbt.c`](../src/xnu/bsd/dev/dtrace/fbt.c#L621-L631) — `fbt_init()`
* **Reason**: FBT probes every kernel function boundary, constructing massive probe tables via dynamic memory allocation and patching instruction streams.
* **Classification**: `XZS-WORKAROUND`
* **What canonical behavior is being bypassed**: `cdevsw_add(FBT_MAJOR, ...)` and kernel-wide function boundary instrumentation.
* **Why acceptable for current Phase**: DTrace FBT is purely a kernel debugging / profiling facility for userspace; it is entirely orthogonal to VFS root mounting.
* **Dependency**: Full memory capacity and userspace DTrace toolchain.
* **Removal condition**: Stable multi-megabyte kernel heap and userspace debugging requirements.
* **Future phase where it must be revisited**: Phase G (Restore deferred subsystems).
* **Hardware evidence**: Verified log `[XZS-BOOT] [XZS-WORKAROUND] fbt_init DEFERRED`.

---

### 7. DTrace Profile Provider Defer
* **Name**: `dtrace_profile_prvd_defer`
* **File / Function**: [`src/xnu/bsd/dev/dtrace/profile_prvd.c`](../src/xnu/bsd/dev/dtrace/profile_prvd.c#L707-L716) — `profile_init()`
* **Reason**: Profile provider registers high-frequency sampling timers across all cores and allocates profiling buffers.
* **Classification**: `XZS-WORKAROUND`
* **What canonical behavior is being bypassed**: `cdevsw_add(PROFILE_MAJOR, ...)` and timer-based profiling registration.
* **Why acceptable for current Phase**: Profiling is not required for rootfs discovery and introduces unnecessary timer interrupt load during early bootstrap.
* **Dependency**: DTrace core infrastructure.
* **Removal condition**: Stable userspace and profiling tools integration.
* **Future phase where it must be revisited**: Phase G (Restore deferred subsystems).
* **Hardware evidence**: Verified log `[XZS-BOOT] [XZS-WORKAROUND] profile_init DEFERRED`.

---

### 8. IOPOLLED_COREFILE Kernel Crashdump Defer
* **Name**: `iopolled_corefile_defer`
* **File / Function**: [`src/xnu/iokit/bsddev/IOKitBSDInit.cpp`](../src/xnu/iokit/bsddev/IOKitBSDInit.cpp#L78-L126) — `IOKitBSDInit()`
* **Reason**: `IOPOLLED_COREFILE` attempts to allocate 150MB–1GB polled disk space for kernel core dumps and schedules background thread calls (`IOOpenPolledCoreFile`).
* **Classification**: `XZS-WORKAROUND`
* **What canonical behavior is being bypassed**: Background thread allocation for polled disk core dump file reservation.
* **Why acceptable for current Phase**: Physical storage does not exist yet; allocating polled corefiles to non-existent disk devices would fail or block.
* **Dependency**: Operational block storage driver (UFS) and mounted root filesystem.
* **Removal condition**: UFS driver operational and panic partition configured.
* **Future phase where it must be revisited**: Phase D4 / Phase G.
* **Hardware evidence**: `IOKitBSDInit()` completes without attempting polled corefile allocation.

---

### 9. DTrace Post-Init Defer
* **Name**: `dtrace_postinit_defer`
* **File / Function**: [`src/xnu/bsd/kern/bsd_init.c`](../src/xnu/bsd/kern/bsd_init.c#L890-L903) — `bsd_init()`
* **Reason**: `dtrace_postinit()` registers kernel symbols with DTrace and attempts to process kext symbols (`OSKextRegisterKextsWithDTrace`).
* **Classification**: `XZS-WORKAROUND`
* **What canonical behavior is being bypassed**: Symbol table walking and kext DTrace registration.
* **Why acceptable for current Phase**: No external kexts exist in Phase D1; symbol registration is not needed for VFS bootstrap.
* **Dependency**: DTrace subsystem initialization.
* **Removal condition**: Full DTrace enabling in userspace.
* **Future phase where it must be revisited**: Phase G.
* **Hardware evidence**: Verified log `[XZS-BOOT] [XZS-WORKAROUND] dtrace_postinit DEFERRED`.

---

### 10. Loopback Interface (lo0) Defer
* **Name**: `loopattach_defer`
* **File / Function**: [`src/xnu/bsd/kern/bsd_init.c`](../src/xnu/bsd/kern/bsd_init.c#L914-L928) — `bsd_init()`
* **Reason**: `loopattach()` creates `lo0`, attaches BPF, launches multicast listener threads (IGMP/MLD), and spawns interface worker threads that wait on thread calls.
* **Classification**: `XZS-WORKAROUND`
* **What canonical behavior is being bypassed**: Canonical `lo0` interface creation and protocol binding.
* **Why acceptable for current Phase**: Root filesystem discovery and mounting over block devices does not use IP loopback.
* **Dependency**: Operational network stack and thread scheduling.
* **Removal condition**: Phase H (Networking bring-up).
* **Future phase where it must be revisited**: Phase H (Networking).
* **Hardware evidence**: Verified log `[XZS-BOOT] [D46] loopattach ENTER` followed by `[XZS-WORKAROUND] loopattach/lo0 DEFERRED`.

---

### 11. IPv6 Tunnel Interface (gif0) Defer
* **Name**: `gif_init_defer`
* **File / Function**: [`src/xnu/bsd/kern/bsd_init.c`](../src/xnu/bsd/kern/bsd_init.c#L930-L942) — `bsd_init()`
* **Reason**: `gif_init()` creates generic tunnel interface `gif0` via `ifnet_attach()`, which depends on `lo0` and spawns interface management threads.
* **Classification**: `XZS-WORKAROUND`
* **What canonical behavior is being bypassed**: `gif0` tunnel interface attachment.
* **Why acceptable for current Phase**: Virtual network tunnels are irrelevant to root storage boundary.
* **Dependency**: Network stack and `lo0`.
* **Removal condition**: Phase H (Networking).
* **Future phase where it must be revisited**: Phase H (Networking).
* **Hardware evidence**: Verified log `[XZS-BOOT] [XZS-WORKAROUND] gif_init/gif0 DEFERRED`.

---

### 12. Ethernet Family Defer
* **Name**: `ether_family_init_defer`
* **File / Function**: [`src/xnu/bsd/kern/bsd_init.c`](../src/xnu/bsd/kern/bsd_init.c#L956-L971) — `bsd_init()`
* **Reason**: `ether_family_init()` registers VLAN, BOND, bridge, and fake ethernet (`if_fake_init`). Fake ethernet directly requires Skywalk nexus providers which are deferred.
* **Classification**: `XZS-WORKAROUND`
* **What canonical behavior is being bypassed**: Data link interface layer (DLIL) ethernet family registrations.
* **Why acceptable for current Phase**: Block storage devices (UFS) do not belong to the ethernet family.
* **Dependency**: Skywalk subsystem and physical/virtual network interfaces.
* **Removal condition**: Network bring-up.
* **Future phase where it must be revisited**: Phase H (Networking).
* **Hardware evidence**: Verified log `[XZS-BOOT] [D47] ether_family_init ENTER` -> `[XZS-WORKAROUND] ether_family_init DEFERRED` -> `[D47a] ether_family_init DONE`.

---

### 13. IPv6 Delayed Init Loopback Guard
* **Name**: `ip6_init_delayed_lo0_guard`
* **File / Function**: [`src/xnu/bsd/netinet6/ip6_input.c`](../src/xnu/bsd/netinet6/ip6_input.c#L518-L524) — `ip6_init_delayed()`
* **Reason**: Canonical `ip6_init_delayed()` calls `in6_ifattach_prelim(lo_ifp)`. Because `loopattach()` is deferred in Phase D1, `lo_ifp` is `NULL`. Passing `NULL` triggers an immediate kernel assertion panic (`VERIFY(ifp != NULL)`).
* **Classification**: `XZS-WORKAROUND`
* **What canonical behavior is being bypassed**: Preliminary IPv6 address attachment to `lo0`.
* **Why acceptable for current Phase**: Prevents fatal panic when `lo0` is deferred; safely returns early.
* **Dependency**: `lo0` loopback interface creation.
* **Removal condition**: Restoring canonical `loopattach()`.
* **Future phase where it must be revisited**: Phase H (Networking).
* **Hardware evidence**: Hardware logs confirm `[XZS-BOOT] [XZS-WORKAROUND] ip6_init_delayed skipped: lo0 deferred` without crashing.

---

### 14. IOFindBSDRoot Matching Debug Serialization Defer
* **Name**: `iofindbsdroot_debug_serialize_defer`
* **File / Function**: [`src/xnu/iokit/bsddev/IOKitBSDInit.cpp`](../src/xnu/iokit/bsddev/IOKitBSDInit.cpp#L898-L921) — `IOFindBSDRoot()`
* **Reason**: Canonical code executes `OSSerialize *s = OSSerialize::withCapacity(5); matching->serialize(s); IOLog("Waiting on %s\n", s->text()); s->release();`. `OSSerialize::ensureCapacity` calls `kmem_realloc_guard` on the early kernel heap, triggering a non-deterministic page allocation stall or hang under warm-reset conditions.
* **Classification**: `XZS-WORKAROUND`
* **What canonical behavior is being bypassed**: Formatting and printing the XML debug representation of the matching dictionary to the console log.
* **Why acceptable for current Phase**: The serialization block is 100% cosmetic debug output. It does NOT participate in service matching, provider discovery, timeout handling, or root selection. Deferring it eliminates the allocation stall while preserving the exact canonical `matching` dictionary.
* **Dependency**: None.
* **Removal condition**: VM heap reallocator robustness verified under all reset conditions.
* **Future phase where it must be revisited**: Phase G (Subsystem cleanup).
* **Hardware evidence**: Resolves the baseline blocker; hardware logs confirm `[D50-I5] serialize matching ENTER` -> `[XZS-WORKAROUND] IOFindBSDRoot matching serialization/logging DEFERRED` -> `[D50-I5a] serialize matching RETURN / DEFERRED`.

---

### 15. Bounded Root-Device Wait Loop
* **Name**: `bounded_iomedia_wait`
* **File / Function**: [`src/xnu/iokit/bsddev/IOKitBSDInit.cpp`](../src/xnu/iokit/bsddev/IOKitBSDInit.cpp#L937-L973) — `IOFindBSDRoot()`
* **Reason**: Canonical `IOService::waitForMatchingService(matching)` blocks indefinitely waiting for a storage controller driver to publish an `IOMedia` nub. On physical hardware without a UFS driver, blocking forever triggers the APCS hardware watchdog bite (10s–30s) and forces an un-instrumented reboot.
* **Classification**: `XZS-SELFTEST`
* **What canonical behavior is being bypassed**: Unbounded infinite thread sleeping in `waitForMatchingService`.
* **Why acceptable for current Phase**: Polling with a strict 1.0s timeout (100 steps × 10ms via `CNTVCT_EL0`) proves empirically that canonical matching ran, no physical media was published, and gracefully returns `kIOReturnNotFound` (`0xe00002f0`) without starving the CPU or requiring artificial watchdog resets during the wait.
* **Dependency**: Implementation of Qualcomm UFS controller driver publishing `IOMedia`.
* **Removal condition**: UFS driver registered and functional.
* **Future phase where it must be revisited**: Phase D2 / Phase D3.
* **Hardware evidence**: Verified hardware log `[D50-I7] canonical root-service wait ENTER` -> `[XZS-SELFTEST] bounded root-device wait (timeout=1.0s)...` -> `[D50-I8] no matching physical root service` -> returns canonical `kIOReturnNotFound`.

---

### 16. Synthetic Root Device Fallback (`sd0a`)
* **Name**: `synthetic_rootdev_fallback`
* **File / Function**: [`src/xnu/bsd/kern/bsd_init.c`](../src/xnu/bsd/kern/bsd_init.c#L1302-L1317) — `setconf()`
* **Reason**: When `IOFindBSDRoot()` returns `kIOReturnNotFound` (because UFS physical driver is not yet written), canonical XNU would halt or prompt for a device. To verify the VFS mountroot pipeline down to the block device layer, a synthetic BSD block device handle must be configured.
* **Classification**: `XZS-SELFTEST`
* **What canonical behavior is being bypassed**: Requiring a real physical disk handle from IOKit device matching.
* **Why acceptable for current Phase**: Assigns `rootdev = makedev(6, 0)` and `rootdevice = "sd0a"`. This allows the kernel to enter `vfs_mountroot()`, instantiate a vnode for the root device, and probe the BSD block device switch table (`bdevsw`).
* **Dependency**: Real block device nub from IOKit (e.g. `disk0s1`).
* **Removal condition**: Real storage driver implemented.
* **Future phase where it must be revisited**: Phase D3 / Phase D4.
* **Hardware evidence**: Verified hardware log `[XZS-BOOT] [D50c] synthetic rootdev selected (XZS-WORKAROUND / SELFTEST)` -> `[D50d] rootdev major=0x6 minor=0x0 rootdevice=sd0a`.

---

### 17. Block Device Vnode Open (`bdevvp`) Error Propagation
* **Name**: `bdevvp_error_propagation`
* **File / Function**: [`src/xnu/bsd/vfs/vfs_subr.c`](../src/xnu/bsd/vfs/vfs_subr.c#L2169-L2187) — `bdevvp()`
* **Reason**: Upstream XNU invokes `panic("bdevvp failed: open")` immediately if `VNOP_OPEN` on the block device vnode fails.
* **Classification**: `XZS-WORKAROUND`
* **What canonical behavior is being bypassed**: Immediate kernel panic upon block device open failure.
* **Why acceptable for current Phase**: When probing the synthetic `sd0a` device, `bdevsw[6]` has no driver attached, returning `ENODEV` (`0x13`). By returning this error to `vfs_mountroot()` instead of panicking, the exact failure code is propagated and logged at the VFS terminal boundary, proving that the VFS subsystem reached the storage layer cleanly.
* **Dependency**: Real block storage driver in `bdevsw`.
* **Removal condition**: Physical UFS driver registered in `bdevsw`.
* **Future phase where it must be revisited**: Phase D3 / Phase D4.
* **Hardware evidence**: Verified hardware log `[XZS-BOOT] [D51b] bdevvp(rootdev, ...) error=0x13` -> returned to `bsd_init` as `[D51-TERMINAL] cannot mount root, errno = 0x13`.

---

### 18. Synthetic PID1 Console Descriptor Bootstrap
* **Name**: `pid1_console_stdio_bootstrap`
* **File / Function**: [`src/xnu/bsd/kern/mach_loader.c`](../src/xnu/bsd/kern/mach_loader.c) — `xzs_d6m6_setup_console_stdio()`
* **Reason**: The project-specific PID1 is constructed directly from `kernproc` and does not yet run the normal userspace launchd initialization that opens `/dev/console`; the inherited fd 0/1/2 slots are therefore empty.
* **Classification**: `XZS-WORKAROUND`
* **What canonical behavior is being bypassed**: Userspace init opening and assigning its own standard descriptors.
* **Why acceptable for current Phase**: Uses unmodified native `open1()`, VFS, fileproc, vnode, and device mechanisms with PID1's thread/credential context. It does not special-case `read()` or `write()` and gives PID1 ordinary descriptor ownership/cleanup semantics.
* **Dependency**: A fuller PID1 capable of issuing `open()`/`dup2()` before starting the shell.
* **Removal condition**: PID1 or `/bin/sh` establishes fd 0/1/2 itself through verified userspace syscalls.
* **Future phase where it must be revisited**: D7 shell hardening.
* **Hardware evidence**: Pending D6-M6 physical verification.

---

## 3. Summary of Workaround Distribution

```text
Total Active Items: 18
├── XZS-COMPAT:     1 (CTRR absence handling)
├── XZS-WORKAROUND: 15 (Subsystem defers, sizing caps, priming, error propagation, PID1 stdio bootstrap)
└── XZS-SELFTEST:   2 (Bounded IOMedia wait, synthetic sd0a rootdev)
```

No additional workarounds may be introduced without documenting them in this matrix and classifying them according to the taxonomy above.
