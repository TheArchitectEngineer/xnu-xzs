# XZSFS Vnode Lifetime and Locking Model

**Status:** COMPLETE & FROZEN  
**Phase:** D5-M3 (XZSFS Read-Only VFS Driver)  
**Target:** Sony Xperia XZs (`MSM8996` / Tone Keyaki / G8231 / Serial `BH905SX976`)  
**Kernel Tree:** XNU Darwin 24 (Apple Silicon / ARM64 VMAPPLE base ported to Kryo)

---

## 1. Executive Summary & Required Classifications

This document details the lifetime, reference counting, and locking model for vnodes, in-memory filesystem nodes (`struct xzsfs_node`), mount structures (`struct xzsfs_mount`), and underlying block device vnodes (`devvp`) within the XZSFS driver.

### Required Audit Classifications:
```text
VNODE_IOCOUNT_MODEL_AUDITED=yes
VNODE_RECLAIM_MODEL_AUDITED=yes
MOUNT_LIFETIME_AUDITED=yes
DEVVP_REFERENCE_MODEL_AUDITED=yes
```

---

## 2. In-Memory Data Structures

### 2.1 Mount Structure: `struct xzsfs_mount`
```c
struct xzsfs_mount {
    mount_t             mp;             /* Associated VFS mount structure */
    vnode_t             devvp;          /* Backing block device vnode (e.g. md0) */
    
    struct xzsfs_superblock sb;         /* Parsed and validated superblock */
    
    uint32_t            object_count;   /* Number of objects in table */
    struct xzsfs_node  *nodes;          /* Array of in-memory node structures */
    char               *string_table;   /* In-memory copy of validated string table */
    
    vnode_t             root_vp;        /* Cached root directory vnode */
    
    lck_grp_t          *lck_grp;        /* Driver lock group */
    lck_mtx_t           lock;           /* Mutex protecting mount and node attachments */
};
```

### 2.2 Node Structure: `struct xzsfs_node`
```c
struct xzsfs_node {
    uint32_t            object_id;      /* 1-based unique identifier */
    uint32_t            parent_id;      /* Parent directory identifier */
    
    enum vtype          vtype;          /* VDIR or VREG */
    uint32_t            mode;           /* POSIX permissions (e.g. 0755, 0644) */
    uint32_t            uid;            /* Owner user ID (0) */
    uint32_t            gid;            /* Owner group ID (0) */
    
    uint64_t            size;           /* File logical size (0 for directories) */
    uint64_t            data_offset;    /* Payload offset from image start */
    
    const char         *name;           /* Pointer to NUL-terminated name in string table */
    uint32_t            name_len;       /* Name length excluding NUL */
    
    vnode_t             vnode;          /* Pointer to active vnode (or NULL if reclaimed) */
    struct xzsfs_mount *xmp;            /* Pointer back to owning mount structure */
};
```

---

## 3. Vnode Lifetime and Reference Counting Model

In XNU, vnodes are governed by two distinct reference counters:
- `v_iocount`: Represents active in-flight I/O operations or references within the kernel.
- `v_usecount`: Represents open file table references or long-term attachments.

### 3.1 Vnode Creation & Attachment
1. **Creation Trigger**:
   - `VFS_ROOT`: When VFS requests the root vnode.
   - `VNOP_LOOKUP`: When a child component is resolved.
   - `VFS_VGET`: When an object is queried by `ino64_t`.
2. **Locking**:
   - Caller acquires `lck_mtx_lock(&xmp->lock)`.
   - Checks if `node->vnode != NULL`.
     - If non-NULL: invokes `vnode_get(node->vnode)`. If successful, unlocks and returns the existing vnode with incremented `iocount`.
     - If NULL (or `vnode_get` indicates reclaimed): constructs `struct vnode_fsparam` and calls `vnode_create(VNCREATE_FLAVOR, VCREATESIZE, &vnfs_param, &node->vnode)`.
3. **Attachment**:
   - `vnode_create` initializes the vnode, links `v_data = node`, and sets initial `iocount = 1`.
   - `node->vnode` is stored under `xmp->lock`.
   - The returned `vnode_t` has `iocount == 1`.
4. **Iocount Ownership**:
   - The function that called `vnode_create` or `vnode_get` holds the `iocount`.
   - The caller must eventually release this `iocount` using `vnode_put(vp)`.

### 3.2 Inactive Transition (`VNOP_INACTIVE`)
- In XNU, when all open handles close and `v_usecount` drops to 0, VFS invokes `VNOP_INACTIVE`.
- For XZSFS, since there are no dirty pages or uncommitted metadata, `xzsfs_inactive` performs no I/O.
- Returns `0` (clean drop, or delegates to `nop_inactive`). The vnode remains cached or transitions to the free list.

### 3.3 Reclaim Transition (`VNOP_RECLAIM`)
- When VFS decides to reuse or tear down the vnode (e.g. during memory pressure or unmount `vflush`), it calls `VNOP_RECLAIM`.
- **Reclaim Sequence**:
  1. `xzsfs_reclaim(struct vnop_reclaim_args *ap)` is entered.
  2. Extracts `vp = ap->a_vp` and `node = (struct xzsfs_node *)vnode_fsnode(vp)`.
  3. If `node != NULL`:
     - Acquires `lck_mtx_lock(&node->xmp->lock)`.
     - Detaches association: `node->vnode = NULL`.
     - Clears vnode private pointer: `vnode_clearfsnode(vp)`.
     - Releases `lck_mtx_unlock(&node->xmp->lock)`.
  4. Note: Because `struct xzsfs_node` is preallocated in `xmp->nodes` array for the lifetime of the mount, the `node` memory itself is NOT freed here; only the vnode linkage is severed.
  5. Returns `0`. VFS is now free to destroy or reallocate the `vnode_t`.

---

## 4. Mount Lifetime and Teardown Ownership

### 4.1 Mount Activation
1. In `xzsfs_mount(mp, devvp, data, context)`:
   - Holds reference to `devvp` (`vnode_get(devvp)` or provided by VFS).
   - Validates superblock and metadata tables via block reads on `devvp`.
   - Allocates `struct xzsfs_mount` with `Z_WAITOK | Z_ZERO`.
   - Pre-allocates `object_count * sizeof(struct xzsfs_node)` array for in-memory node representations.
   - Copies validated string table into kernel memory.
   - Initializes `lck_mtx_init(&xmp->lock, &xzsfs_lck_grp, LCK_ATTR_NULL)`.
   - Associates `mp->mnt_data = (void *)xmp`.

### 4.2 Mount Teardown (`xzsfs_unmount`)
1. In `xzsfs_unmount(mp, mntflags, context)`:
   - Sets flags `vflush_flags = (mntflags & MNT_FORCE) ? FORCECLOSE : 0`.
   - Calls `vflush(mp, NULL, vflush_flags)` to forcibly reclaim all active vnodes. This guarantees `VNOP_RECLAIM` runs on every allocated vnode, severing all `node->vnode` references.
   - Drops cached `root_vp` if held.
   - Deallocates `xmp->string_table`.
   - Deallocates `xmp->nodes`.
   - Destroys mutex: `lck_mtx_destroy(&xmp->lock, &xzsfs_lck_grp)`.
   - Drops `devvp` reference: `vnode_put(xmp->devvp)`.
   - Frees `xmp` structure.
   - Sets `mp->mnt_data = NULL`.
   - Returns `0`.

---

## 5. Device Vnode (`devvp`) Reference Ownership Model

1. **Acquisition**:
   - For block device mounts, `devvp` is resolved via `bdevvp(rootdev)` in BSD initialization or passed to `vfs_mount`.
   - `bdevvp()` returns a specfs block vnode with an acquired reference (`iocount = 1`).
2. **Usage**:
   - All block reads via `buf_bread(devvp, blkno, 512, NOCRED, &bp)` operate on `devvp`.
   - The buffer cache manages buffer lifetimes (`buf_brelse(bp)`).
3. **Release**:
   - `devvp` remains held by `xzsfs_mount` until `vfs_unmount`, at which point `vnode_put(devvp)` (and/or `vnode_rele(devvp)`) is called, returning its refcount to baseline.
