# D6-M1: Native XNU PID 1 Process / Task / Thread Source Audit

This document records the empirical source-level audit of the native Apple XNU process bootstrap path for Phase D6-M1 on the Sony Xperia XZs (Qualcomm MSM8996).

---

## 1. Executive Findings Summary

```text
PID1_CREATION_PATH=bsd_init() -> bsd_utaskbootstrap() -> cloneproc(CLONEPROC_INITPROC) -> forkproc()
TASK_CREATION_PATH=cloneproc() -> fork_create_child() -> task_create_internal()
THREAD_CREATION_PATH=fork_create_child() -> main_thread_create_waiting() -> thread_create_internal()
PROC_TASK_LINKAGE=proc_set_task(child_proc, child_task) + set_bsdtask_info(child_task, child_proc)
THREAD_UTHREAD_LINKAGE=thread_create_internal() -> uthread_init() [contiguous offset ((uintptr_t)th + sizeof(struct thread))]
```

---

## 2. Detailed Source Walkthrough & Classifications

### A. Process 0 / Kernel Process Bootstrap
- **Location**: `src/xnu/bsd/kern/bsd_init.c:496-576`
- **Audit Findings**:
  - `kernproc` is statically allocated and initialized in early `bsd_init()`.
  - `set_bsdtask_info(kernel_task, (void *)kernproc)` links the Mach `kernel_task` to `kernproc`.
  - `kernproc->p_pptr = kernproc` (parent is itself).
  - `kernproc->p_pid = 0`.
  - `kernproc->p_stat = SRUN`.
  - `fdt_init(kernproc)` sets up the file descriptor table for process 0.
- **Classification**: `SOURCE-AUDITED FACT`

### B. Bootstrap Entrypoint: `bsd_utaskbootstrap()`
- **Location**: `src/xnu/bsd/kern/bsd_init.c:1459-1496`
- **Audit Findings**:
  - Called directly from `bsd_init()` (line 1324) following `devfs_kernel_mount("/dev")` and `siginit(kernproc)`.
  - Calls `thread = cloneproc(TASK_NULL, NULL, kernproc, CLONEPROC_INITPROC)`.
  - Queries `initproc = proc_find(1)` to obtain a referenced pointer to the newly created process.
  - Calls `zalloc_first_proc_made()`.
  - Explicitly drops parent transition locks via `proc_signalend(initproc, 0)` and `proc_transend(initproc, 0)`.
  - Upstream then calls `vm_map_setup(get_task_map(task), task)`, `ipc_task_enable(task)`, and `task_clear_return_wait(task, TCRW_CLEAR_ALL_WAIT)`.
  - In D6-M1, userspace VM mapping and clearing of return wait must NOT be executed.
- **Classification**: `SOURCE-AUDITED FACT`

### C. Process Creation & PID Assignment: `forkproc()`
- **Location**: `src/xnu/bsd/kern/kern_fork.c:920-985`
- **Audit Findings**:
  - Allocates `child_proc` from `proc_task_zone` using `zalloc_flags(proc_task_zone, Z_WAITOK | Z_ZERO)`.
  - Allocates `child_proc->p_stats` from `proc_stats_zone`.
  - Inherits signal acts from parent: `child_proc->p_sigacts = parent_proc->p_sigacts`.
  - Allocates file descriptor table via `fdt_init(child_proc)`.
  - Acquires `proc_list_lock()`.
  - Assigns PID: `lastpid` starts at 0; when `forkproc()` runs for the first child, `pid = ++lastpid` evaluates to `1`.
  - Sets `child_proc->p_pid = 1`.
  - Sets `proc_ro_data.p_uniqueid = ++nextuniqueid` (evaluates to 1).
  - Returns allocated and initialized `child_proc`.
- **Classification**: `SOURCE-AUDITED FACT`

### D. Task Creation & Process-Task Linkage: `fork_create_child()`
- **Location**: `src/xnu/bsd/kern/kern_fork.c:441-534`
- **Audit Findings**:
  - For `parent_task == TASK_NULL` (bootstrap case), `inherit_memory` is `FALSE`.
  - Calls `task_create_internal(parent_task, proc_ro, parent_coalitions, inherit_memory, is_64bit_addr, is_64bit_data, TF_NONE, TF_NONE, ...)` to allocate a new Mach task (`child_task`).
  - Links proc to task: `proc_set_task(child_proc, child_task)`.
  - Links task to proc: `set_bsdtask_info(child_task, child_proc)`.
  - Verifies that `child_task != kernel_task`.
- **Classification**: `SOURCE-AUDITED FACT`

### E. Thread Creation, Parking & Uthread Linkage
- **Location**: `src/xnu/bsd/kern/kern_fork.c:513-528`, `src/xnu/osfmk/kern/thread.c:1357-1367`, `src/xnu/osfmk/kern/bsd_kern.c:180-190`
- **Audit Findings**:
  - `fork_create_child()` calls `main_thread_create_waiting(child_task, (thread_continue_t)task_wait_to_return, task_get_return_wait_event(child_task), &child_thread)`.
  - `main_thread_create_waiting()` invokes `thread_create_internal()`.
  - Inside `thread_create_internal()`, under `#ifdef MACH_BSD`, `uthread_init(parent_task, get_bsdthread_info(new_thread), ...)` is invoked.
  - `get_bsdthread_info(th)` computes `(struct uthread *)((uintptr_t)th + sizeof(struct thread))`. The `uthread` structure is allocated contiguously immediately following `struct thread`.
  - `get_machthread(uth)` performs the inverse calculation: `(struct thread *)((uintptr_t)uth - sizeof(struct thread))`.
  - The thread is marked `THREAD_TAG_MAINTHREAD`.
  - The thread begins parked/blocked, waiting on the event `task_get_return_wait_event(child_task)`. Unless `task_clear_return_wait()` is called, this thread cannot run or reach userspace.
- **Classification**: `SOURCE-AUDITED FACT`

### F. Parent-Child Relationship & Process Visibility
- **Location**: `src/xnu/bsd/kern/kern_fork.c:741-746`
- **Audit Findings**:
  - `pinsertchild(parent_proc, child_proc, in_exec)` inserts `child_proc` into `parent_proc`'s child queue and sets `child_proc->p_pptr = parent_proc`.
  - For `initproc`, `parent_proc` is `kernproc` (PID 0).
  - Therefore:
    - `PID1_PROC_PID = 1`
    - `PID1_PROC_PPID = 0`
  - `child_proc->p_stat` is set to `SRUN`.
- **Classification**: `SOURCE-AUDITED FACT`

### G. Execution Boundary for D6-M1 vs Subsequent Milestones
- In D6-M1, `bsd_utaskbootstrap()` must create these objects and then **STOP**.
- Specifically:
  - `vm_map_setup()` is omitted in D6-M1 (deferred to D6-M3).
  - `task_clear_return_wait()` is omitted in D6-M1 (deferred to EL0 transition phase).
  - `thread_suspend(thread)` is called as a fail-safe to guarantee the thread cannot be scheduled to EL0.
  - `bsdinit_task()` and `load_init_program()` are NOT invoked.
- **Classification**: `INFERENCE/HYPOTHESIS` (validated by architectural design constraints).
