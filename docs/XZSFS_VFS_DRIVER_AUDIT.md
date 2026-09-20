# XZSFS VFS Driver Source Audit

**Status:** COMPLETE & FROZEN  
**Phase:** D5-M3 (XZSFS Read-Only VFS Driver)  
**Target:** Sony Xperia XZs (`MSM8996` / Tone Keyaki / G8231 / Serial `BH905SX976`)  
**Kernel Tree:** XNU Darwin 24 (Apple Silicon / ARM64 VMAPPLE base ported to Kryo)

---

## 1. Executive Summary & Required Classifications

This document provides the exact source audit of XNU's Virtual File System (VFS) and vnode operation contracts as implemented in this specific kernel source tree. No interfaces were assumed or copied from external macOS releases.

### Required Audit Classifications:
```text
VFS_REGISTRATION_API_SOURCE_AUDITED=yes
VNODE_CREATION_API_SOURCE_AUDITED=yes
VNODE_OP_TABLE_SOURCE_AUDITED=yes
REFERENCE_FILESYSTEM_SELECTED=mockfs
XZSFS_VFS_MOUNT_CONTRACT_AUDITED=yes
REAL_VNODE_REQUIRES_VALID_MOUNT_T=yes
VNODE_DEFAULT_HANDLER_SYMBOLS_SOURCE_AUDITED=yes
```

---

## 2. Reference Filesystem Selection: `mockfs`

After auditing `mockfs` (`src/xnu/bsd/miscfs/mockfs/`), `devfs` (`src/xnu/bsd/miscfs/devfs/`), `nullfs` (`src/xnu/bsd/miscfs/nullfs/`), and `bindfs` (`src/xnu/bsd/miscfs/bindfs/`), **`mockfs`** was selected as the primary architectural reference for XZSFS.

### Justification:
1. **Lightweight & Self-Contained**: `mockfs` comprises only 5 files (`mockfs.h`, `mockfs_fsnode.h`, `mockfs_fsnode.c`, `mockfs_vfsops.c`, `mockfs_vnops.c`) implementing a read-only filesystem over a backing device.
2. **Standard VFS Registration Pattern**: Demonstrates clean usage of `struct vfsops`, `struct vnodeopv_desc`, and `vnodeopv_entry_desc` tables.
3. **Clean Vnode Creation Contract**: Uses `vnode_create(VNCREATE_FLAVOR, VCREATESIZE, &vnfs_param, &fsnp->vp)` with explicit `vnode_fsnode()` attachment and mutex synchronization.
4. **Vnode Reclaim & Inactive Model**: Demonstrates proper lifecycle teardown where `mockfs_reclaim` clears the `fsnode` association via `vnode_clearfsnode(vp)` under lock.
5. **Direct Applicability**: Like `mockfs`, XZSFS is a structured read-only image backed by a block device (`devvp` / `md0`). However, unlike `mockfs` which only creates 3 hardcoded synthetic nodes, XZSFS features a dynamic, fully compliant object table and string table parser validating an arbitrary frozen tree.

---

## 3. VFS Registration API Contract

### 3.1 `vfs_fsadd` and `vfstable_t`
In `src/xnu/bsd/vfs/kpi_vfs.c`:
```c
int vfs_fsadd(struct vfs_fsentry *vfe, vfstable_t *handle);
```
- **Functionality**: Dynamically registers a filesystem driver into XNU's global filesystem table (`vfsconf` / `vfstable`).
- **Input (`struct vfs_fsentry`)**:
  - `vfe_vfsops`: Pointer to filesystem's `struct vfsops`.
  - `vfe_vopcnt`: Number of `vnodeopv_desc` vectors being registered (1 for regular/dir vnodes).
  - `vfe_opvdescs`: Array of pointers to `struct vnodeopv_desc`, NULL-terminated.
  - `vfe_fstypenum`: Historic filesystem type number (omitted via `VFS_TBLNOTYPENUM`).
  - `vfe_fsname`: ASCII string name (`"xzsfs"`, max `MFSNAMELEN` = 16 bytes).
  - `vfe_flags`: Capability bitmask (`VFS_TBLTHREADSAFE | VFS_TBLFSNODELOCK | VFS_TBLNOTYPENUM | VFS_TBL64BITREADY | VFS_TBLLOCALVOL`).
- **Operation Vector Setup**:
  - `vfs_fsadd` allocates contiguous memory for `vfe_vopcnt * vfs_opv_numops` function pointers.
  - Populates `*vfe_opvdescs[i]->opv_desc_vector_p` with the allocated vector.
  - Populates each operation offset from `opve_descp->opve_impl`.
  - Automatically replaces any unfilled routine slots with `opv_desc_vector[VOFFSET(vnop_default)]`.
- **Global Insertion**:
  - Calls `vfstable_add(newvfstbl)` which acquires `mount_list_lock()` and inserts into the first available slot in `vfsconf[]`.
  - Invokes `vfsops->vfs_init(&vfsc)` if defined.
  - Returns opaque handle `vfstable_t`.

---

## 4. VFS Operations (`struct vfsops`) and Mount Signature

Defined in `src/xnu/bsd/sys/mount.h`:
```c
struct vfsops {
    int  (*vfs_mount)(struct mount *mp, vnode_t devvp, user_addr_t data, vfs_context_t context);
    int  (*vfs_start)(struct mount *mp, int flags, vfs_context_t context);
    int  (*vfs_unmount)(struct mount *mp, int mntflags, vfs_context_t context);
    int  (*vfs_root)(struct mount *mp, struct vnode **vpp, vfs_context_t context);
    int  (*vfs_quotactl)(struct mount *mp, int cmds, uid_t uid, caddr_t arg, vfs_context_t context);
    int  (*vfs_getattr)(struct mount *mp, struct vfs_attr *vfa, vfs_context_t context);
    int  (*vfs_sync)(struct mount *mp, int waitfor, vfs_context_t context);
    int  (*vfs_vget)(struct mount *mp, ino64_t ino, struct vnode **vpp, vfs_context_t context);
    int  (*vfs_fhtovp)(...);
    int  (*vfs_vptofh)(...);
    int  (*vfs_init)(struct vfsconf *vfsc);
    int  (*vfs_sysctl)(...);
    int  (*vfs_setattr)(...);
    int  (*vfs_ioctl)(...);
    int  (*vfs_vget_snapdir)(...);
    int  (*vfs_unmount_preflight)(...);
};
```

### 4.1 Audited Mount Signature (`vfs_mount`)
The exact signature for `vfs_mount` in this kernel is:
```c
int xzsfs_mount(struct mount *mp, vnode_t devvp, user_addr_t data, vfs_context_t context);
```
- For D5-M3, `xzsfs_mount` is fully prepared and bound to `.vfs_mount` in `xzsfs_vfsops`.
- It validates superblock and metadata on `devvp`, allocates `struct xzsfs_mount`, and initializes the mount structure.
- **HARD STOP**: D5-M3 strictly avoids invoking `xzsfs_mount` as the real root mount (`VFS_MOUNTROOT_CALLED=no`, `ROOT_VNODE_INSTALLED=no`). The real root mount transition is deferred to D5-M4.

---

## 5. Vnode Creation vs `mount_t` Audit & Strategy Selection

### 5.1 Source Audit of `vnode_create_internal`
In `src/xnu/bsd/vfs/vfs_subr.c`:
1. Line 7098: `if (param->vnfs_mp->mnt_ioflags & MNT_IOFLAGS_IOSCHED_SUPPORTED) ...`
2. Line 7124: `vnode_resolver_create(param->vnfs_mp, vp, tinfo, FALSE)`
3. Line 7215: `if (param->vnfs_mp) { ... insmntque(vp, param->vnfs_mp); }`
4. `insmntque(vp, mp)`: Calls `mount_lock_spin(mp)` and inserts the vnode into `mp->mnt_vnodelist`.

**Conclusion**:
```text
REAL_VNODE_REQUIRES_VALID_MOUNT_T=yes
```
Creating a real `vnode_t` via `vnode_create()` strictly requires an active, initialized, and kernel-registered `mount_t` structure. Fabricating a dummy or uninitialized `mount_t` is unsafe and would corrupt kernel mount lists or trigger a kernel panic.

### 5.2 Strategy Selection: Strategy B
```text
Strategy B: D5-M3 exercises parser/node/read/readdir/getattr core helpers,
           registers the real VFS/VNOP tables via vfs_fsadd(),
           but defers actual vnode-dispatch execution to D5-M4/M5.
```
- Avoids fabricating dummy `mount_t` instances.
- Exercises 100% of the block I/O, parsing, lookup, payload extraction, readdir formatting, getattr formatting, and read-only rejection logic directly against `devvp`.
- Registers `"xzsfs"` with `vfs_fsadd()`, validating the operation vector tables and registration count.
- Strictly preserves the hard boundary: `VFS_MOUNTROOT_CALLED=no`, `XZSFS_ROOT_MOUNT_ATTEMPTED=no`, `ROOT_VNODE_INSTALLED=no`.

---

## 6. Vnode Fallback Symbols & Operation Vector Audit

Every fallback routine listed in `xzsfs_vnodeop_entries` has been confirmed against `src/xnu/bsd/`:

| Operation | Handler Symbol | Source Declaration | Source Implementation | Return Value |
| :--- | :--- | :--- | :--- | :--- |
| Default | `vn_default_error` | `sys/vnode.h:892` | `bsd/vfs/vfs_init.c:136` | `ENOTSUP` |
| `vnop_fsync` | `nop_fsync` | `vfs/vfs_support.h:106` | `bsd/vfs/vfs_support.c:482` | `0` |
| `vnop_inactive`| `nop_inactive` | `vfs/vfs_support.h:136` | `bsd/vfs/vfs_support.c:599` | `0` |
| `vnop_ioctl` | `err_ioctl` | `vfs/vfs_support.h:92` | `bsd/vfs/vfs_support.c:349` | `ENOTSUP` |
| `vnop_select`| `err_select` | `vfs/vfs_support.h:95` | `bsd/vfs/vfs_support.c:380` | `ENOTSUP` |
| `vnop_mmap` | `err_mmap` | `vfs/vfs_support.h:104` | `bsd/vfs/vfs_support.c:462` | `ENOTSUP` |
| `vnop_pathconf`| `err_pathconf` | `vfs/vfs_support.h:147` | `bsd/vfs/vfs_support.c:748` | `ENOTSUP` |
| `vnop_advlock`| `err_advlock` | `vfs/vfs_support.h:150` | `bsd/vfs/vfs_support.c:777` | `ENOTSUP` |
| `vnop_bwrite` | `err_bwrite` | `vfs/vfs_support.h:157` | `bsd/vfs/vfs_support.c:805` | `ENOTSUP` |
| `vnop_pageout`| `err_pageout` | `vfs/vfs_support.h:163` | `bsd/vfs/vfs_support.c:837` | `ENOTSUP` |
| `vnop_copyfile`| `err_copyfile` | `vfs/vfs_support.h:169` | `bsd/vfs/vfs_support.c:859` | `ENOTSUP` |
| `vnop_blktooff`| `err_blktooff` | `vfs/vfs_support.h:172` | `bsd/vfs/vfs_support.c:879` | `ENOTSUP` |
| `vnop_offtoblk`| `err_offtoblk` | `vfs/vfs_support.h:175` | `bsd/vfs/vfs_support.c:900` | `ENOTSUP` |
| `vnop_blockmap`| `err_blockmap` | `vfs/vfs_support.h:178` | `bsd/vfs/vfs_support.c:921` | `ENOTSUP` |

### 6.1 Mutation Operations Policy (`EROFS`)
Unlike `err_create`, `err_write`, etc., which return `ENOTSUP`, XZSFS implements an explicit `xzsfs_rofs_err()` handler:
```c
int xzsfs_rofs_err(void *ap) {
    return EROFS;
}
```
Bound to: `create`, `write`, `mkdir`, `rmdir`, `remove`, `rename`, `link`, `symlink`, `setattr`.

Classification:
```text
VNODE_DEFAULT_HANDLER_SYMBOLS_SOURCE_AUDITED=yes
```

---

## 7. Block I/O Layer Contract (`buf_bread`)

XZSFS never reads `0x81700000` directly. All block reads are routed through `devvp`:
```text
xzsfs_core_read / xzsfs_parser
    ↓
devvp (BSD spec block vnode for md0)
    ↓
buf_bread(devvp, blkno, 512, NOCRED, &bp)
    ↓
spec_strategy()
    ↓
mdevstrategy()
```
Buffer handling rules:
1. Every successful `buf_bread()` MUST be paired with `buf_brelse(bp)`.
2. Access data payload via `buf_dataptr(bp)`.
3. Sector calculation:
   `blkno = disk_offset / 512`
   `offset_in_sector = disk_offset % 512`
   `bytes_to_copy = min(bytes_needed, 512 - offset_in_sector)`
