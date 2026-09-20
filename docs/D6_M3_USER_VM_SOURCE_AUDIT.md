# Phase D6-M3 Source Audit: PID1 User VM + Initial Stack

**Date:** 2026-09-20
**Target Architecture:** ARM64 (Qualcomm MSM8996 / Sony Xperia XZs)
**Milestone:** D6-M3 (PID1 User VM + Initial Stack)
**Baseline Commit:** `c3a8e48` (D6-M2 certified LKG)

---

## 1. Executive Summary

This document presents a comprehensive source-code audit of native XNU virtual memory (VM), Mach-O segment loading, cross-map access primitives, user stack allocation, Darwin initial argument layout, and thread register state preparation paths for Phase D6-M3.

The objective of D6-M3 is to construct a fully valid userspace execution environment for PID 1 (`initproc`) without entering EL0 or starting execution:
1. Preserve and validate the existing PID 1 task and `vm_map` instantiated during native XNU process creation.
2. Establish the 4 GiB `__PAGEZERO` inaccessible guard region (`0x0 .. 0x100000000`) and verify zero overlapping mappings in `[0x0, 0x100000000)`.
3. Dynamically derive `__TEXT` mapping parameters from the Mach-O `LC_SEGMENT_64` header (`vmaddr`, `vmsize`, `fileoff`, `filesize`, `initprot`, `maxprot`).
4. Allocate `__TEXT`, load file-backed bytes from `/sbin/launchd`, verify content integrity via CRC32 and `memcmp`, verify zero-fill on any padding tail, and finalize protection to `RX` (W^X strictly enforced, zero unexpected RWX).
5. Resolve `__LINKEDIT` policy based on static executable metadata (runtime mapping not required).
6. Create a dedicated user stack (`RW`, `NX`, 128 KiB) rooted at `USRSTACK64` (`0x16FE00000ULL`), validated against task map bounds and page alignment.
7. Construct the Darwin initial argument stack frame (`argc=1`, `argv[0]="/sbin/launchd"`, `envp[0]=NULL`, `apple[0]=NULL`) with a 16-byte aligned user stack pointer (`initial_sp = 0x16FDFFB0ULL`).
8. Install ARM64 user register state (`PC = 0x1000002f0`, `SP = initial_sp`, `CPSR = PSR64_USER64_DEFAULT`) while keeping the thread and task strictly suspended.
9. Perform a read-locked kernel-side VM map audit asserting 0 unexpected RWX regions and verifying all permissions.
10. Enforce an airtight execution barrier: thread and task remain suspended; zero EL0 entry; hard stop before D6-M4.

---

## 2. Source-Audited Native XNU Paths & Findings

### 2.1 PID 1 Task & Map Identity
- **Source Paths:** `cloneproc()` ([kern_fork.c:726](file:///Users/lechaukha12/Desktop/xnu-xzs/src/xnu/bsd/kern/kern_fork.c#L726)), `fork_create_child()` ([kern_fork.c:442](file:///Users/lechaukha12/Desktop/xnu-xzs/src/xnu/bsd/kern/kern_fork.c#L442)), `task_create_internal()` ([task.c:1600-1618](file:///Users/lechaukha12/Desktop/xnu-xzs/src/xnu/osfmk/kern/task.c#L1600-L1618)), `vm_map_setup()` ([vm_map.c:9185](file:///Users/lechaukha12/Desktop/xnu-xzs/src/xnu/osfmk/vm/vm_map.c#L9185)).
- **Audit Findings:**
  - `cloneproc(TASK_NULL, NULL, kernproc, CLONEPROC_INITPROC)` already created PID 1 (`initproc`), its Mach task `t`, its main thread `th`, and its address space map `map = t->map`.
  - In `task_create_internal()`, `pmap_create_options(ledger, 0, PMAP_CREATE_64BIT)` instantiated a brand-new ARM64 hardware pmap, and `vm_map_create_options(pmap, VM_MIN_ADDRESS, VM_MAX_ADDRESS, VM_MAP_CREATE_PAGEABLE)` created a fresh pageable user VM map.
  - In `vm_map_setup(map, task)` ([vm_map.c:9185](file:///Users/lechaukha12/Desktop/xnu-xzs/src/xnu/osfmk/vm/vm_map.c#L9185)):
    `assert(!map->owning_task); map->owning_task = task;`
    Calling `vm_map_setup()` when `map->owning_task` is already non-NULL will trigger an assertion failure. Therefore, D6-M3 checks:
    If `map->owning_task == t`, the map is already setup. If `map->owning_task == TASK_NULL`, `vm_map_setup(map, t)` binds it.
  - Existing task and map are preserved intact:
    `PID1_EXISTING_MAP_STATE=valid_clean_user_map`
    `PID1_MAP_REINITIALIZATION_REQUIRED=no`
    `PID1_EXISTING_MAP_PRESERVED=yes`
- **Classification:** `SOURCE-AUDITED FACT`

### 2.2 Dynamic Mach-O Parameter Derivation
- **Source Paths:** `xzs_d6m2_macho_probe()` ([mach_loader.c:4099](file:///Users/lechaukha12/Desktop/xnu-xzs/src/xnu/bsd/kern/mach_loader.c#L4099)), `struct segment_command_64` ([mach-o/loader.h](file:///Users/lechaukha12/Desktop/xnu-xzs/src/xnu/EXTERNAL_HEADERS/mach-o/loader.h)).
- **Audit Findings:**
  - All segment mapping parameters are derived dynamically by scanning `LC_SEGMENT_64` load commands from `/sbin/launchd`:
    - `__TEXT`: `vmaddr`, `vmsize`, `fileoff`, `filesize`, `initprot`, `maxprot`.
    - `__PAGEZERO`: `vmaddr`, `vmsize`.
    - `__LINKEDIT`: `vmaddr`, `vmsize`, `fileoff`, `filesize`, `initprot`, `maxprot`.
  - No segment sizes or file offsets are hardcoded into the mapping logic.
  - In `/sbin/launchd` on XZSFS:
    - `__PAGEZERO`: vmaddr=0x0, vmsize=0x100000000 (4 GiB), fileoff=0, filesize=0.
    - `__TEXT`: vmaddr=0x100000000, vmsize=0x4000 (16 KiB), fileoff=0, filesize=16384 (0x4000), initprot=r-x, maxprot=r-x.
    - `__LINKEDIT`: vmaddr=0x100004000, vmsize=0x4000, fileoff=16384, filesize=88, initprot=r--, maxprot=r--.
- **Classification:** `SOURCE-AUDITED FACT`

### 2.3 `__PAGEZERO` Reservation & Guard Verification
- **Source Paths:** `load_segment()` ([mach_loader.c:2368-2415](file:///Users/lechaukha12/Desktop/xnu-xzs/src/xnu/bsd/kern/mach_loader.c#L2368-L2415)), `vm_map_raise_min_offset()` ([vm_map.c:22014](file:///Users/lechaukha12/Desktop/xnu-xzs/src/xnu/osfmk/vm/vm_map.c#L22014)), `vm_map_has_hard_pagezero()` ([vm_map.c:21954](file:///Users/lechaukha12/Desktop/xnu-xzs/src/xnu/osfmk/vm/vm_map.c#L21954)), `vm_commit_pagezero_status()` ([vm_map.c:23290](file:///Users/lechaukha12/Desktop/xnu-xzs/src/xnu/osfmk/vm/vm_map.c#L23290)).
- **Audit Findings:**
  - Upstream XNU enforces `__PAGEZERO` by raising the map's minimum offset via `vm_map_raise_min_offset(map, 0x100000000ULL)`.
  - This sets `map->min_offset = 0x100000000ULL` and `map->holes_list->start = 0x100000000ULL`.
  - Zero bytes are allocated or copied for `__PAGEZERO`.
  - Guard validation independently verifies under `vm_map_lock_read(map)`:
    1. `map->min_offset >= 0x100000000ULL` (`vm_map_has_hard_pagezero` returns TRUE).
    2. Zero `vm_map_entry_t` exist with `vme_start < 0x100000000ULL` (`USER_PAGEZERO_OVERLAPPING_MAPPING_COUNT=0`).
    3. `USER_PAGEZERO_GUARD_VALID=yes`.
- **Classification:** `SOURCE-AUDITED FACT`

### 2.4 Cross-Map Access Primitives (Kernel to Suspended User VM)
- **Source Paths:** `vm_map_write_user()` ([vm_map.c:20023](file:///Users/lechaukha12/Desktop/xnu-xzs/src/xnu/osfmk/vm/vm_map.c#L20023)), `vm_map_read_user()` ([vm_map.c:20084](file:///Users/lechaukha12/Desktop/xnu-xzs/src/xnu/osfmk/vm/vm_map.c#L20084)), `osfmk/vm/vm_map_xnu.h:799-806`.
- **Audit Findings:**
  - `vm_map_write_user(map, src_p, dst_addr, size)` safely copies trusted kernel data into the target user map by calling `PMAP_SWITCH_USER(current_thread(), map, cpu_number())`, executing `copyout()`, and cleanly restoring via `vm_map_switch_back()`.
  - `vm_map_read_user(map, src_addr, dst_p, size)` performs the reverse operation using `copyin()`.
  - Both primitives operate cleanly on a suspended task/map without altering thread scheduling state or leaving temporary TTBR mutations behind.
  - Signposts:
    `USER_MAP_WRITE_PRIMITIVE=vm_map_write_user`
    `USER_MAP_READ_PRIMITIVE=vm_map_read_user`
    `CROSS_MAP_ACCESS_SOURCE_AUDITED=yes`
- **Classification:** `SOURCE-AUDITED FACT`

### 2.5 Segment Content Verification & W^X Enforcement
- **Source Paths:** `mach_vm_allocate_kernel()` ([vm_kern.c:1298](file:///Users/lechaukha12/Desktop/xnu-xzs/src/xnu/osfmk/vm/vm_kern.c#L1298)), `vm_map_protect()` ([vm_map.c:5799](file:///Users/lechaukha12/Desktop/xnu-xzs/src/xnu/osfmk/vm/vm_map.c#L5799)), `crc32()` ([libkern/libkern/zlib.h:1340](file:///Users/lechaukha12/Desktop/xnu-xzs/src/xnu/libkern/libkern/zlib.h#L1340)).
- **Audit Findings:**
  - `mach_vm_allocate_kernel(map, &text_addr, text_vmsize, VM_MAP_KERNEL_FLAGS_FIXED())` allocates the virtual range with `VM_PROT_READ | VM_PROT_WRITE` (`cur_protection`) and `VM_PROT_ALL` (`max_protection`).
  - Exactly `text_filesize` bytes are read from `/sbin/launchd` via `vn_rdwr()` into a kernel buffer.
  - `expected_crc32 = crc32(0, kbuf, text_filesize)`.
  - Bytes are written to userspace via `vm_map_write_user()`.
  - Bytes are read back from userspace via `vm_map_read_user()` into a verification buffer.
  - `mapped_crc32 = crc32(0, vbuf, text_filesize)`.
  - Assert `expected_crc32 == mapped_crc32` and `memcmp(kbuf, vbuf, text_filesize) == 0` -> `USER_TEXT_FILE_BYTES_VERIFIED=yes`.
  - Any tail bytes `text_vmsize - text_filesize` are verified to be zero -> `USER_TEXT_ZEROFILL_VALID=yes`.
  - The first `vm_map_protect(..., FALSE, text_initprot)` call removes write permission and establishes the Mach-O-requested current protection `RX`.
  - The second `vm_map_protect(..., TRUE, text_maxprot)` call seals maximum protection to the Mach-O `maxprot` value `RX`, preventing later write re-enablement.
  - Map entry protection is independently read back under lock, asserting current protection `RX`, maximum protection `RX`, and no write permission.
  - `TEXT_WX_TRANSITION_USED=yes`, `USER_TEXT_WRITABLE_AFTER_FINALIZE=no`.
- **Classification:** `SOURCE-AUDITED FACT`

### 2.6 `__LINKEDIT` Policy Resolution
- **Source Paths:** `load_segment()` ([mach_loader.c:2519](file:///Users/lechaukha12/Desktop/xnu-xzs/src/xnu/bsd/kern/mach_loader.c#L2519)), D6-M2 loader metadata.
- **Audit Findings:**
  - D6-M2 established: `MACHO_STATIC_EXECUTABLE=yes`, `DYLD_REQUIRED=no`, `DYNAMIC_LIBRARY_DEPENDENCY_COUNT=0`, `MACHO_REQUIRES_UNSUPPORTED_FIXUPS=no`.
  - `__LINKEDIT` in this binary contains only link-editor metadata (symbol table, string table, code signature header) totaling 88 bytes.
  - Upstream XNU maps `__LINKEDIT` primarily for dynamic linkers (`dyld`). Static binaries executing directly from `0x1000002f0` do not require `__LINKEDIT` at runtime.
  - Policy: `USER_LINKEDIT_RUNTIME_REQUIRED=no`, `USER_LINKEDIT_MAPPED=no`, `USER_LINKEDIT_PROTECTION=NONE`.
- **Classification:** `SOURCE-AUDITED FACT`

### 2.7 Darwin Initial Stack ABI & User Stack Design
- **Source Paths:** [bsd/arm/vmparam.h:17](file:///Users/lechaukha12/Desktop/xnu-xzs/src/xnu/bsd/arm/vmparam.h#L17), `create_unix_stack()` ([kern_exec.c:7183](file:///Users/lechaukha12/Desktop/xnu-xzs/src/xnu/bsd/kern/kern_exec.c#L7183)), `exec_copyout_strings()` ([kern_exec.c:5792-5948](file:///Users/lechaukha12/Desktop/xnu-xzs/src/xnu/bsd/kern/kern_exec.c#L5792-L5948)).
- **Audit Findings:**
  - `DARWIN_INITIAL_STACK_ABI_SOURCE_AUDITED=yes`.
  - Stack top is `USRSTACK64 = 0x000000016FE00000ULL`.
  - Stacks grow downwards. Proposed 128 KiB (`0x20000`) stack range:
    `[0x000000016FDE0000ULL, 0x000000016FE00000ULL)`.
  - Range validation against task map:
    - `0x16FDE0000 >= map->min_offset` (`0x100000000ULL`) -> PASS.
    - `0x16FE00000 <= map->max_offset` (`0x7FFFFFFFF000ULL`) -> PASS.
    - `0x16FDE0000 % 16384 == 0` (16 KiB page aligned) -> PASS.
    - `0x16FE00000 % 16384 == 0` (16 KiB page aligned) -> PASS.
  - Protection: `VM_PROT_READ | VM_PROT_WRITE` (`RW`), `NX` (never executable).
  - Darwin initial stack frame construction:
    - String area at `0x16FDFFE0ULL`: `"/sbin/launchd\0"` (14 bytes + 18 bytes zero padding = 32 bytes).
    - `argc` area at `0x16FDFFB0ULL` (one pointer-sized slot): `argc = 1`.
    - Pointer area starts immediately at `0x16FDFFB8ULL`, matching `exec_copyout_strings()`:
      - `argv[0] = 0x16FDFFE0ULL`
      - `argv[1] = NULL` (0)
      - `envp[0] = NULL` (0)
      - `apple[0] = NULL` (0)
    - The remaining 8 bytes before the string area are zero alignment padding.
    - Initial SP: `initial_sp = 0x16FDFFB0ULL`.
    - Stack alignment: `initial_sp & 0xF == 0` (16-byte aligned, required by ARM64 hardware).
- **Classification:** `SOURCE-AUDITED FACT`

### 2.8 Thread Register Preparation & Suspend Invariants
- **Source Paths:** `machine_thread_state_initialize()` ([status.c:2066](file:///Users/lechaukha12/Desktop/xnu-xzs/src/xnu/osfmk/arm64/status.c#L2066)), `thread_setentrypoint()` ([status.c:2447](file:///Users/lechaukha12/Desktop/xnu-xzs/src/xnu/osfmk/arm64/status.c#L2447)), `thread_setuserstack()` ([status.c:2410](file:///Users/lechaukha12/Desktop/xnu-xzs/src/xnu/osfmk/arm64/status.c#L2410)), `get_user_regs()` ([status.c:2158](file:///Users/lechaukha12/Desktop/xnu-xzs/src/xnu/osfmk/arm64/status.c#L2158)).
- **Audit Findings:**
  - `machine_thread_state_initialize(th)` zeroes thread user context and initializes `cpsr = PSR64_USER64_DEFAULT`.
  - `thread_setentrypoint(th, 0x1000002f0ULL)` sets `sv->ss_64.pc = 0x1000002f0ULL`.
  - `thread_setuserstack(th, initial_sp)` sets `sv->ss_64.sp = initial_sp`.
  - These routines modify only saved registers in `thread->machine.upcb`. They do NOT resume threads or touch scheduling run queues.
  - Classification:
    `PID1_REGISTER_STATE_READY=yes`
    `PID1_REGISTER_STATE_INSTALLED=yes`
  - Suspend state preservation:
    - At D6-M3 entry: record `initial_th_suspend = th->suspend_count` and `initial_t_suspend = t->suspend_count`.
    - No additional suspend operations are added.
    - At D6-M3 exit: assert `th->suspend_count == initial_th_suspend` and `t->suspend_count == initial_t_suspend`.
    - `PID1_THREAD_REMAINS_SUSPENDED=yes`
    - `PID1_TASK_REMAINS_SUSPENDED=yes`
    - `PID1_SUSPEND_STATE_UNINTENTIONALLY_CHANGED=no`
- **Classification:** `SOURCE-AUDITED FACT`

### 2.9 Read-Locked Kernel-Side VM Map Audit
- **Source Paths:** `vm_map_lock_read()` ([vm_map_xnu.h:651](file:///Users/lechaukha12/Desktop/xnu-xzs/src/xnu/osfmk/vm/vm_map_xnu.h#L651)), `vm_map_unlock_read()` ([vm_map_xnu.h:659](file:///Users/lechaukha12/Desktop/xnu-xzs/src/xnu/osfmk/vm/vm_map_xnu.h#L659)).
- **Audit Findings:**
  - Iterating `vm_map_entry_t` without holding `vm_map_lock_read(map)` violates XNU locking hierarchy.
  - The audit acquires `vm_map_lock_read(map)`, verifies:
    1. Zero entries below `0x100000000ULL` (`USER_PAGEZERO_OVERLAPPING_MAPPING_COUNT=0`).
    2. `__TEXT` entry is present: `vme_start == 0x100000000ULL`, `vme_end == 0x100004000ULL`, `protection == (VM_PROT_READ | VM_PROT_EXECUTE)`.
    3. User stack entry is present: `vme_start == 0x16FDE0000ULL`, `vme_end == 0x16FE00000ULL`, `protection == (VM_PROT_READ | VM_PROT_WRITE)`.
    4. Zero entries have `(protection & (VM_PROT_WRITE | VM_PROT_EXECUTE)) == (VM_PROT_WRITE | VM_PROT_EXECUTE)` (`PID1_VM_UNEXPECTED_RWX_COUNT=0`).
  - Map read lock is released via `vm_map_unlock_read(map)` before telemetry emission or spin halt.
  - `VM_MAP_AUDIT_LOCKING_VALID=yes`.
- **Classification:** `SOURCE-AUDITED FACT`

---

## 3. Required Architectural Signposts

```text
PID1_VM_CREATION_PATH=task_create_internal -> vm_map_create_options (reusing initproc task->map)
PID1_EXISTING_MAP_STATE=valid_clean_user_map
PID1_MAP_REINITIALIZATION_REQUIRED=no
PID1_EXISTING_MAP_PRESERVED=yes

SEGMENT_MAPPING_PATH=vm_map_raise_min_offset (__PAGEZERO) + mach_vm_allocate_kernel (__TEXT) + vm_map_write_user
USER_MAP_WRITE_PRIMITIVE=vm_map_write_user
USER_MAP_READ_PRIMITIVE=vm_map_read_user
CROSS_MAP_ACCESS_SOURCE_AUDITED=yes

VM_PROTECTION_PATH=vm_map_protect(current=text_initprot) + vm_map_protect(max=text_maxprot)
TEXT_WX_TRANSITION_USED=yes
USER_TEXT_WRITABLE_AFTER_FINALIZE=no

STACK_CREATION_PATH=mach_vm_allocate_kernel(map, stack_base, stack_size, FIXED) + vm_map_write_user
DARWIN_INITIAL_STACK_ABI_SOURCE_AUDITED=yes

USER_REGISTER_PREPARATION_PATH=machine_thread_state_initialize + thread_setentrypoint + thread_setuserstack
PID1_REGISTER_STATE_READY=yes
PID1_REGISTER_STATE_INSTALLED=yes

PID1_THREAD_REMAINS_SUSPENDED=yes
PID1_TASK_REMAINS_SUSPENDED=yes
PID1_SUSPEND_STATE_UNINTENTIONALLY_CHANGED=no

VM_MAP_AUDIT_LOCKING_VALID=yes
EL0_HANDOFF_BOUNDARY=CLOSED (zero EL0 entry, terminal diagnostic spin halt at D620/01)
```

---

## 4. Execution Cutoff Definition

```text
M3_NATIVE_EXEC_CUTOFF=thread_setuserstack_and_register_preparation_complete
```

All kernel objects, virtual memory structures, hardware page tables, stack frames, and thread saved registers are fully populated and validated from kernel space. Execution stops dead before activating threads or scheduling PID 1.
