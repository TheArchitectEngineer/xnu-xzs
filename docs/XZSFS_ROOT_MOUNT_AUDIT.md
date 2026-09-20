# XZSFS Root Mount & Root Vnode Source Audit

**Status:** COMPLETE & SEALED  
**Phase:** D5-M4 (Real XZSFS Mount + Root Vnode)  
**Target:** Sony Xperia XZs (`MSM8996` / Tone Keyaki / G8231 / Serial `BH905SX976`)  
**Kernel Tree:** XNU Darwin 24 (Apple Silicon / ARM64 VMAPPLE base ported to Kryo)

---

## 1. Executive Summary & Required Classifications

This audit document establishes the exact native XNU call chain, structures, reference counting, and filesystem selection mechanism for mounting the root filesystem and installing the global root vnode (`rootvnode`). No fake structures or bypasses are used; all integration occurs through XNU's standard VFS subsystems.

### Required Audit Classifications:
```text
ROOT_MOUNT_CALL_CHAIN_SOURCE_AUDITED=yes
ROOTVNODE_INSTALL_PATH_SOURCE_AUDITED=yes
MOUNT_PRIVATE_STATE_API_AUDITED=yes
ROOT_VNODE_IOCOUNT_LIFETIME_AUDITED=yes
ROOT_FS_TYPE_SELECTION_MECHANISM=vfsconf_iteration_vfc_canmountroot
```

---

## 2. Complete Root Mount Call Chain

The complete audited call sequence in this kernel source tree is:

```text
bsd_init() [bsd/kern/bsd_init.c:1108]
 ↓
vfs_mountroot() [bsd/vfs/vfs_subr.c:1203]
 ↓
bdevvp(rootdev, &rootvp) [bsd/vfs/vfs_subr.c:1240]
   - Resolves specfs block device vnode for md0
   - Acquires temporary iocount and usecount on rootvp
 ↓
vfsconf list traversal:
   for (vfsp = vfsconf; vfsp; vfsp = vfsp->vfc_next) [bsd/vfs/vfs_subr.c:1256]
   - Checks: vfsp->vfc_mountroot != NULL || ISSET(vfsp->vfc_vfsflags, VFC_VFSCANMOUNTROOT)
   - xzsfs registers VFS_TBLCANMOUNTROOT (0x20000) in vfs_fsadd() -> sets VFC_VFSCANMOUNTROOT
 ↓
vfs_rootmountalloc_internal(vfsp, "root_device") [bsd/vfs/vfs_subr.c:1262]
   - Allocates mount_t structure (zalloc Z_WAITOK | Z_ZERO)
   - Initializes mount locks, mutexes, and default I/O parameters
   - mp->mnt_devvp = rootvp;
 ↓
VFS_MOUNT(mp, rootvp, 0, ctx) [bsd/vfs/vfs_subr.c:1268]
   - Dispatches via mp->mnt_op->vfs_mount -> xzsfs_mount(mp, devvp, data, ctx)
 ↓
xzsfs_mount() [bsd/xzsfs/xzsfs_vfsops.c:20]
   - Re-reads and validates superblock, metadata CRC, and object graph over devvp via buf_bread()
   - Allocates mount-private struct xzsfs_mount
   - Retains devvp reference (vnode_ref)
   - Initializes in-memory node table (xmp->nodes[])
   - Calls xzsfs_get_vnode() to create real root vnode (object_id=1, VDIR)
   - Attaches mount-private data via vfs_setfsprivate(mp, xmp)
   - Initializes filesystem identifier via vfs_getnewfsid(mp)
   - Sets mount flags: MNT_LOCAL | MNT_RDONLY
   - Returns 0
 ↓
vfs_mountroot() post-mount setup [bsd/vfs/vfs_subr.c:1286-1363]
   - mp->mnt_devvp->v_specflags |= SI_MOUNTEDON;
   - vfs_unbusy(mp);
   - mount_list_add(mp);
   - vfs_init_io_attributes(rootvp, mp);
   - VFS_START(mp, 0, ctx);
   - vnode_put(rootvp); (drops temporary iocount from bdevvp, keeping usecount on mnt_devvp)
   - returns 0 to bsd_init()
 ↓
bsd_init() rootvnode installation [bsd/kern/bsd_init.c:1140-1153]
   - mountlist.tqh_first->mnt_flag |= MNT_ROOTFS;
   - VFS_ROOT(mountlist.tqh_first, &init_rootvnode, vfs_context_kernel());
     - Dispatches via mp->mnt_op->vfs_root -> xzsfs_root() -> returns root vnode with iocount=1
   - (void)vnode_ref(init_rootvnode); (increments usecount=1)
   - (void)vnode_put(init_rootvnode); (decrements iocount=0)
   - lck_rw_lock_exclusive(&rootvnode_rw_lock);
   - set_rootvnode(init_rootvnode); [bsd/kern/bsd_init.c:426]
     - init_rootvnode->v_flag |= VROOT;
     - rootvnode = init_rootvnode;
     - kernproc->p_fd.fd_cdir = init_rootvnode;
     - new_mount->mnt_flag |= MNT_ROOTFS;
   - lck_rw_unlock_exclusive(&rootvnode_rw_lock);
   - init_rootvnode = NULLVP;
```

---

## 3. Filesystem Selection Mechanism

```text
ROOT_FS_TYPE_SELECTION_MECHANISM=vfsconf_iteration_vfc_canmountroot
```

XNU's `vfs_mountroot()` does not hardcode filesystem types. Instead, it iterates through all registered filesystems in the global linked list `vfsconf` (`vfsconf_iteration`):
1. Any filesystem that declares capability to mount the root volume sets `VFS_TBLCANMOUNTROOT` in its `struct vfs_fsentry.vfe_flags` during registration via `vfs_fsadd()`.
2. `vfs_fsadd()` translates this flag to `VFC_VFSCANMOUNTROOT` on the allocated `struct vfstable` entry.
3. When `vfs_mountroot()` loops through `vfsconf`, any entry with `VFC_VFSCANMOUNTROOT` is evaluated by invoking its mount operation against `rootvp`.
4. `xzsfs_mount` inspects `rootvp` (`md0`). If the superblock magic (`0x5346535A`), version (`1`), and metadata CRC match, it accepts the mount and returns `0`.
5. This requires zero hardcoded filesystem checks in generic VFS.

---

## 4. Vnode Lifetime, Iocount & Usecount Model

### 4.1 Root Vnode Reference Invariants
1. **Creation**:
   - `xzsfs_get_vnode(mp, &xmp->nodes[0], &root_vp)` sets up `struct vnode_fsparam` and calls `vnode_create()`.
   - `vnode_create()` initializes the vnode with `v_iocount = 1`, `v_usecount = 0`.
   - `node->vnode = root_vp` is cached under `xmp->lock`.
2. **VFS_ROOT Dispatch**:
   - `VFS_ROOT(mp, &init_rootvnode, ctx)` calls `xzsfs_root()`.
   - `xzsfs_root()` calls `xzsfs_get_vnode()`, which finds `node->vnode != NULL` and calls `vnode_get(node->vnode)`, incrementing `v_iocount` to 2.
   - Returned `init_rootvnode` has `v_iocount = 2` (or 1 after mount release), `v_usecount = 0`.
3. **Installation in `set_rootvnode`**:
   - `(void)vnode_ref(init_rootvnode)` increments `v_usecount = 1`.
   - `(void)vnode_put(init_rootvnode)` decrements `v_iocount`.
   - `set_rootvnode()` locks `rootvnode_rw_lock` and assigns `rootvnode = init_rootvnode`.
   - Baseline stable state: `v_usecount = 1`, `v_iocount = 0`.
4. **Reclaim Protection**:
   - Because `v_usecount >= 1` (held by `rootvnode` and `kernproc->p_fd.fd_cdir`), XNU VFS will never reclaim or recycle the root vnode during normal system execution.
   - `XZSFS_ROOT_VNODE_CREATE_COUNT = 1`, `XZSFS_ROOT_VNODE_RECLAIM_COUNT = 0`.

---

## 5. Mount-Private State & Devvp Ownership

```text
MOUNT_PRIVATE_STATE_API_AUDITED=yes
```

1. **Mount-Private Data**:
   - Allocated with `kalloc_type(struct xzsfs_mount, Z_WAITOK | Z_ZERO)`.
   - Bound to mount via `vfs_setfsprivate(mp, xmp)`.
   - Retrieved via `(struct xzsfs_mount *)vfs_fsprivate(mp)`.
2. **Backing Block Device Vnode (`devvp` / `md0`)**:
   - Passed to `xzsfs_mount(mp, devvp, ...)` from `vfs_mountroot()`.
   - `xzsfs_mount()` calls `vnode_ref(devvp)` to establish long-term mount ownership.
   - `mp->mnt_devvp` holds the device reference in VFS core.
   - On unmount (`xzsfs_unmount`), `vnode_rele(xmp->devvp)` releases this reference.
3. **Error Unwind Protocol**:
   - If superblock or metadata validation fails during mount:
     - Mutex destroyed via `lck_mtx_destroy(&xmp->lock, xzsfs_lck_grp)`.
     - Mount-private memory freed via `kfree_type(struct xzsfs_mount, xmp)`.
     - `devvp` reference dropped.
     - `vfs_setfsprivate(mp, NULL)`.
     - No dangling references or leaks.
