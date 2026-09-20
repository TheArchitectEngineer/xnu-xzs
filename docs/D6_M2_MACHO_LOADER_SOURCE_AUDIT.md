# D6-M2 Source Audit: Native XNU Exec & Mach-O Loader Architecture

**Project**: Native XNU on Sony Xperia XZs (MSM8996)  
**Milestone**: Phase D6-M2 — Minimal Mach-O Loader for `/sbin/launchd`  
**Base Commit**: `533932b` (Certified D6-M1 LKG)

---

## 1. Upstream Execution & Mach-O Parsing Architecture

### 1.1 Process & Task Bootstrap Context (D6-M1 Baseline)
- **Statement**: `PID1_CREATION_PATH = bsd_utaskbootstrap() -> cloneproc(TASK_NULL, NULL, kernproc, CLONEPROC_INITPROC) -> forkproc() [p_pid = 1, p_pptr = kernproc]`
  - **Classification**: `SOURCE-AUDITED FACT`
  - **Source References**: `src/xnu/bsd/kern/bsd_init.c:1577`, `src/xnu/bsd/kern/kern_fork.c:459`, `src/xnu/bsd/kern/kern_fork.c:1175`
- **Statement**: `TASK_CREATION_PATH = fork_create_child() -> task_create_internal(TASK_NULL, NULL, FALSE, FALSE, &child_task)`
  - **Classification**: `SOURCE-AUDITED FACT`
  - **Source References**: `src/xnu/bsd/kern/kern_fork.c:859`
- **Statement**: `THREAD_CREATION_PATH = fork_create_child() -> main_thread_create_waiting(child_task, ...)`
  - **Classification**: `SOURCE-AUDITED FACT`
  - **Source References**: `src/xnu/bsd/kern/kern_fork.c:980`

---

### 1.2 Upstream Exec Entry & Launchd Dispatch
- **Statement**: `UPSTREAM_EXEC_ENTRY = load_init_program(initproc) -> load_init_program_at_path(initproc, scratch, "/sbin/launchd") -> execve(p, &init_exec_args, retval) -> __mac_execve() -> exec_activate_image(imgp)`
  - **Classification**: `SOURCE-AUDITED FACT`
  - **Source References**:
    - `src/xnu/bsd/kern/kern_exec.c:7382` (`load_init_program`)
    - `src/xnu/bsd/kern/kern_exec.c:7257` (`load_init_program_at_path`)
    - `src/xnu/bsd/kern/kern_exec.c:3806` (`execve`)
    - `src/xnu/bsd/kern/kern_exec.c:4424` (`exec_activate_image`)
- **Statement**: `load_machfile_internal` does not exist in this XNU tree. The Mach-O activation hook is `exec_mach_imgact()` in `execsw[0]`.
  - **Classification**: `SOURCE-AUDITED FACT`
  - **Source References**: `src/xnu/bsd/kern/kern_exec.c:2327`, `src/xnu/bsd/kern/kern_exec.c:1321`

---

### 1.3 Mach-O Vnode Opening Path
- **Statement**: `MACHO_VNODE_OPEN_PATH = NDINIT(&nd, LOOKUP, OP_LOOKUP/OPEN, FOLLOW | LOCKLEAF, UIO_SYSSPACE, CAST_USER_ADDR_T(path), ctx) -> namei(&nd) -> nameidone(&nd) -> VNOP_OPEN(vp, FREAD, ctx)`
  - **Classification**: `SOURCE-AUDITED FACT`
  - **Source References**:
    - `src/xnu/bsd/kern/mach_loader.c:3910-3948` (`get_macho_vnode`)
    - `src/xnu/bsd/kern/kern_exec.c:2397-2409` (`exec_activate_image`)
    - `src/xnu/bsd/xzsfs/xzsfs_vnops.c:54` (`xzsfs_open`)

---

### 1.4 Mach-O Header Read Path
- **Statement**: `MACHO_HEADER_READ_PATH = vn_rdwr(UIO_READ, vp, (caddr_t)header, sizeof(struct mach_header_64), 0, UIO_SYSSPACE, IO_NODELOCKED, cred, &resid, p)`
  - **Classification**: `SOURCE-AUDITED FACT`
  - **Source References**:
    - `src/xnu/bsd/kern/mach_loader.c:3953-3957` (`get_macho_vnode`)
    - `src/xnu/bsd/kern/kern_exec.c:2441-2445` (`exec_activate_image`)

---

### 1.5 Mach-O Parsing Function
- **Statement**: `MACHO_PARSE_FUNCTION = parse_machfile(vp, map, thread, header, file_offset, macho_size, depth, aslr_offset, dyld_aslr_offset, result, binresult, imgp)`
  - **Classification**: `SOURCE-AUDITED FACT`
  - **Source References**: `src/xnu/bsd/kern/mach_loader.c:1048`
- **Statement**: In upstream XNU, `parse_machfile` explicitly documents:
  `If "map"==VM_MAP_NULL or "thread"==THREAD_NULL, do not make permanent VM modifications, just preflight the parse.`
  In this mode (`map == VM_MAP_NULL`), `load_segment` skips `map_segment()` and returns `LOAD_SUCCESS` after validating file offsets, alignment, and segment ranges.
  - **Classification**: `SOURCE-AUDITED FACT`
  - **Source References**: `src/xnu/bsd/kern/mach_loader.c:1042-1045`, `src/xnu/bsd/kern/mach_loader.c:2495-2497`

---

### 1.6 Load Result Structure
- **Statement**: `LOAD_RESULT_STRUCTURE = struct _load_result (load_result_t)` defined in `<bsd/kern/mach_loader.h:62>`
  - **Classification**: `SOURCE-AUDITED FACT`
  - **Source References**: `src/xnu/bsd/kern/mach_loader.h:62-117`
  - Key fields:
    - `user_addr_t mach_header`
    - `user_addr_t entry_point`
    - `user_addr_t user_stack`
    - `mach_vm_size_t user_stack_size`
    - `unsigned int needs_dynlinker : 1`
    - `unsigned int validentry : 1`
    - `mach_vm_address_t min_vm_addr`
    - `mach_vm_address_t max_vm_addr`

---

### 1.7 Entry Point Resolution Path
- **Statement**: `ENTRYPOINT_RESOLUTION_PATH = LC_UNIXTHREAD -> load_unixthread() -> load_threadentry() -> thread_entrypoint(thread, flavor, (thread_state_t)ts, size, entry_point)`
  - **Classification**: `SOURCE-AUDITED FACT`
  - **Source References**:
    - `src/xnu/bsd/kern/mach_loader.c:1523` (`LC_UNIXTHREAD` handler in `parse_machfile`)
    - `src/xnu/bsd/kern/mach_loader.c:2929` (`load_unixthread`)
    - `src/xnu/bsd/kern/mach_loader.c:3081` (`load_threadentry`)
    - `src/xnu/osfmk/arm64/status.c:2473` (`thread_entrypoint` for ARM64: extracts `state->pc` from `ARM_THREAD_STATE64`)
- **Statement**: `thread_entrypoint` on ARM64 ignores its first argument (`__unused thread_t thread`) and does not modify thread state when resolving the entry point. However, `load_unixthread` also invokes `load_threadstate` which would mutate registers if passed a non-NULL thread.
  - **Classification**: `SOURCE-AUDITED FACT`
  - **Source References**: `src/xnu/osfmk/arm64/status.c:2473`, `src/xnu/bsd/kern/mach_loader.c:2946`

---

### 1.8 Native Loader Cutoff & Inspection Wrapper
- **Statement**: `M2_NATIVE_LOADER_CUTOFF = map_segment() [calls vm_map_enter()] and load_threadstate() [installs registers on thread].`
  In `load_machfile()`, VM map creation begins with `pmap_create_options()` / `vm_map_create_options()` before any load commands are parsed.
  Therefore, calling full `load_machfile()` would violate the D6-M2 invariant (`USER_VM_SETUP_ATTEMPTED=no`).
  - **Classification**: `SOURCE-AUDITED FACT`
  - **Source References**: `src/xnu/bsd/kern/mach_loader.c:741-750`, `src/xnu/bsd/kern/mach_loader.c:2547`
- **Statement**: To satisfy the rule ("Prefer to reuse native XNU Mach-O parser structures and helpers without creating an independent parallel parser"), D6-M2 implements a read-only Mach-O inspection probe that reuses:
  - Native XNU vnode/read path (`namei`, `vnode_size`, `vnode_authorize`, `VNOP_OPEN`, `vn_rdwr`, `VNOP_CLOSE`, `vnode_put`)
  - Upstream Mach-O structures (`struct mach_header_64`, `struct load_command`, `struct segment_command_64`, `struct thread_command`, `arm_thread_state64_t`)
  - Upstream entry point extractor (`thread_entrypoint` from `<kern/thread.h>` / `osfmk/arm64/status.c`)
  - Upstream segment and load command validation rules (bounds checking, overflow guards, `SG_READ_ONLY`, protection flags)
  This cleanly decouples metadata validation from VM mapping and thread execution.
  - **Classification**: `SOURCE-AUDITED FACT`
  - **Source References**: `src/xnu/bsd/kern/mach_loader.c`, `src/xnu/osfmk/arm64/status.c`

---

## 2. Audit of `/sbin/launchd` on Sony Xperia XZs RootFS

From static analysis of `./rootfs/xzs-root/sbin/launchd`:
- File size: `16472` bytes
- Mach-O Magic: `0xfeedfacf` (`MH_MAGIC_64`)
- CPU Type: `0x0100000c` (`CPU_TYPE_ARM64`)
- CPU Subtype: `0x00000000` (`CPU_SUBTYPE_ARM64_ALL`)
- Filetype: `0x00000002` (`MH_EXECUTE`)
- Flags: `0x00000001` (`MH_NOUNDEFS`)
- Number of Load Commands: `7`
- Size of Load Commands: `648` bytes

### Load Commands Table:
| Index | Command | Size | Details |
| :--- | :--- | :--- | :--- |
| 0 | `LC_SEGMENT_64` | 72 | `__PAGEZERO`: vmaddr 0x0, vmsize 0x100000000, fileoff 0, filesize 0, prot --- |
| 1 | `LC_SEGMENT_64` | 152 | `__TEXT`: vmaddr 0x100000000, vmsize 0x4000, fileoff 0, filesize 16384, prot r-x, sect `__text` (addr 0x1000002f0, size 0x4b) |
| 2 | `LC_SEGMENT_64` | 72 | `__LINKEDIT`: vmaddr 0x100004000, vmsize 0x4000, fileoff 16384, filesize 88, prot r-- |
| 3 | `LC_SYMTAB` | 24 | symoff 16384, nsyms 3, stroff 16432, strsize 40 |
| 4 | `LC_UUID` | 24 | UUID `FCA095DF-966B-390E-9C57-DD172593F138` |
| 5 | `LC_SOURCE_VERSION` | 16 | version 0.0 |
| 6 | `LC_UNIXTHREAD` | 288 | flavor `ARM_THREAD_STATE64`, count 68, initial PC `0x1000002f0` |

### Derived Invariants:
- Dynamic linker commands (`LC_LOAD_DYLINKER`, `LC_LOAD_DYLIB`, etc.): **NONE** (`DYLD_REQUIRED=no`, `MACHO_STATIC_EXECUTABLE=yes`)
- Fixup commands (`LC_DYLD_INFO`, `LC_DYLD_CHAINED_FIXUPS`): **NONE** (`MACHO_REQUIRES_DYLD_FIXUPS=no`)
- Entry point `0x1000002f0` is strictly within `__TEXT` (`0x100000000` to `0x100004000`)
- `__TEXT` protection is `VM_PROT_READ | VM_PROT_EXECUTE` (`MACHO_INITIAL_PC_IN_EXEC_SEGMENT=yes`)
