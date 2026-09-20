# Phase D5-M5: Namespace Traversal + DevFS / Console Hardware Report

## 1. Required Execution & Telemetry Summary

```text
D5_M5_STATUS=COMPLETE
D5_M5_SEALED=yes

D5-M4_REGRESSION_PASS=yes

NAMEI_ROOT_PASS=yes
NAMEI_SBIN_LAUNCHD_PASS=yes
LAUNCHD_VNODE_TYPE=VREG
LAUNCHD_MODE=0755
LAUNCHD_FILEID=7
LAUNCHD_SIZE=16472

DEVFS_KERNEL_MOUNT_RETURN=0
NAMEI_DEV_PASS=yes
DEVFS_ROOT_VNODE=VDIR
DEVFS_ROOT_HAS_VROOT=yes
DEVFS_MOUNT_FSTYPE=devfs

NAMEI_DEV_CONSOLE_PASS=yes
DEV_CONSOLE_VNODE_TYPE=VCHR
DEV_CONSOLE_MAJOR=0
DEV_CONSOLE_MINOR=0

NAMESPACE_DEVFS_OVERLAY_VERIFIED=yes

ROOT_FS_TYPE=xzsfs
ROOT_FS_DEVICE=md0

PID1_STARTED=no
EXECVE_ATTEMPTED=no
EL0_ENTRY_ATTEMPTED=no

CMD24_COUNT=0
CMD25_COUNT=0
ZERO_STORAGE_WRITES=yes

D540_90_REACHED=yes
D540_91_REACHED=yes
D540_01_REACHED=yes

D5_COMPLETE=no
ROADMAP_ADVANCED_TO=D5-M6
```

---

## 2. Tested Artifact Hashes & Archive Record

All binary artifacts for the successful D5-M5 hardware run are permanently archived under `artifacts/archive/d5m5-pass/`:

| Artifact | Location | Size (Bytes) | SHA256 |
| :--- | :--- | :--- | :--- |
| **Mach-O Kernel** | `artifacts/archive/d5m5-pass/kernel.development.vmapple` | 21,862,160 | `24164d86549a6ea9902769005e82377a778531279f60f43b617fd1d65dec1f0a` |
| **Flattened Kernel** | `artifacts/archive/d5m5-pass/kernel.flat` | 23,019,520 | `1f7debff6174ede7223a7e17ca3cf192921822a7e88a04390a6429ddb3153a99` |
| **Combined Boot Image** | `artifacts/archive/d5m5-pass/xzs-xnu-boot.img` | 24,629,248 | `afa473266e27f2ad42fc297a896f8da2934953d47998b61019711bfdabc67896` |
| **Console Log** | `artifacts/logs/d5m5-pass/console.log` | 264,057 | `620ba06be52d87e07ebbf475659779dfbeaf4420803bf5f87b809a473b640cb4` |

---

## 3. Checkpoint Progression Audit

The extracted silicon log demonstrates complete, in-order execution of the D5-M4 regression prefix followed by all D5-M5 checkpoints:

### D5-M4 Regression Prefix
- `D530/00` through `D530/91`: 100% in-order PASS.
- Root mount of XZSFS on `md0` confirmed valid and stable.

### D5-M5 Main Sequence (`0xD540`)
1. `D540/00`: Phase D5-M5 enter (`xzsfs_d5m5_predevfs_probe`).
2. `D540/10`: `namei('/')` resolved global `rootvnode` (`rootvp == rootvnode`).
3. `D540/20`: `namei('/sbin/launchd')` returned `VREG` vnode.
4. `D540/21`: `VNOP_GETATTR` verified on `/sbin/launchd` (`va_type=VREG`, `va_mode=0755`, `va_fileid=7`, `va_data_size=16472`).
5. `D540/30`: Canonical `devfs_kernel_mount('/dev')` entered.
6. `D540/31`: `devfs_kernel_mount('/dev')` returned success (0).
7. `D540/40`: `namei('/dev')` crossed into devfs root vnode (`VDIR`, `devfs_mp->mnt_vtable->vfc_name == "devfs"`, `v_flag & VROOT`).
8. `D540/50`: `namei('/dev/console')` returned `VCHR` vnode.
9. `D540/51`: `vnode_specrdev(consolevp)` resolved `major=0, minor=0` (`cdev 0:0`).
10. `D540/60`: Namespace traversal and devfs overlay verified.
11. `D540/90`: Acceptance telemetry emitted.
12. `D540/91`: Phase D5-M5 complete and verified.
13. `D540/01`: Diagnostic terminal state before PID 1 (`delay(50000); xzs_spin_halt()`). Return time: +4s.

### DevFS Internal Sub-Trace
- `D541/10`, `D541/11`, `D541/20`, `D541/21`, `D541/30`: `kernel_mount` lifecycle.
- `D542/10`, `D542/20`, `D542/21`, `D542/30`, `D542/31`, `D542/40`, `D542/41`, `D542/50`: `mount_common` lifecycle.
- `D543/10`, `D543/20`, `D543/30`, `D543/31`, `D543/32`, `D543/33`, `D543/40`: `devfs_mount` lifecycle.

---

## 4. Root-Cause Analysis of DevFS Blocker & Workaround

In the un-bypassed code, `devfs_mount()` called `VM_KERNEL_ADDRHASH(devfs_mp_p)` to generate the filesystem ID (`f_fsid.val[0]`). On MSM8996 bring-up, the pointer-hashing implementation stalled execution indefinitely, leading to a 40-second watchdog bite.

Under `CONFIG_XZS_BRINGUP`, `mp->mnt_vfsstat.f_fsid.val[0]` and `sbp->f_fsid.val[0]` were replaced with a static non-pointer identifier `(int32_t)0x64657666` (`"devf"`). This allowed `devfs_mount()` to complete immediately, mount `/dev`, and expose `/dev/console`. The non-XZS Apple platform path remains completely unmodified.

---

## 5. Storage & EL0 Safety Verification

- **Storage writes**: `CMD24_COUNT=0`, `CMD25_COUNT=0`, `ZERO_STORAGE_WRITES=yes`. The physical eMMC was never written to.
- **PID 1 / EL0**: `PID1_STARTED=no`, `EXECVE_ATTEMPTED=no`, `EL0_ENTRY_ATTEMPTED=no`. The kernel halted at `D540/01` strictly prior to userspace bootstrap.
