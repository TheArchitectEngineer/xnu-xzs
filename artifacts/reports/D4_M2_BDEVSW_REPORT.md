# D4-M2 BSD Block Device Switch Verification

## Git Baseline

- **Milestone Branch:** `xzs-d4-block`
- **Base Commit:** `3913108f82e266420eec15607e39df748ffa134d`
- **Certified Checkpoint Commit:** `3913108f82e266420eec15607e39df748ffa134d`
- **Target Device:** Sony Xperia XZs (`MSM8996` / Tone Keyaki / G8231 / Serial `BH905SX976`)
- **Target Controller:** Qualcomm SDC1 (`0x07464900`)
- **Storage Target:** Samsung BJNB4R 32GB eMMC 5.1

## Exact `bdevsw` Source Audit

Before implementing the driver, the exact local XNU `struct bdevsw` layout was audited from `src/xnu/bsd/sys/conf.h` (lines 158–166):

```c
struct bdevsw {
	open_close_fcn_t        *d_open;
	open_close_fcn_t        *d_close;
	strategy_fcn_t          *d_strategy;
	ioctl_fcn_t             *d_ioctl;
	dump_fcn_t              *d_dump;
	psize_fcn_t             *d_psize;
	int                     d_type;
};
```

Function types:
- `d_open_t / d_close_t`: `int (dev_t dev, int flags, int devtype, struct proc *p)`
- `d_strategy_t`: `void (struct buf *bp)`
- `d_ioctl_t`: `int (dev_t dev, u_long cmd, caddr_t data, int fflag, struct proc *p)`
- `d_dump_t`: `int (void)` (implemented with `eno_dump`)
- `d_psize_t`: `int (dev_t dev)`
- `d_type`: `D_DISK` (`2`)

Reference Driver: `src/xnu/bsd/dev/memdev.c`.

## Block Driver Registration & Dynamic Major Allocation

The block driver is registered dynamically through BSD's canonical registration API:
```c
g_xzs_bdev_major = bdevsw_add(-1, &g_xzs_bdevsw);
```
- **Registration Callsite:** `src/xnu/bsd/kern/bsd_init.c:733`
- **Registration Phase:** `BSD_POST_VFSINIT` (immediately following `vfsinit()` and `bsd_bufferinit()`, guaranteeing `iobufqueue` is fully ready for `buf_alloc()`).
- **Dynamic Major Allocated:** `1` (positive, non-conflicting dynamic slot).
- `BDEV_MAJOR_DYNAMIC=yes`
- `BDEVSW_REGISTER_COUNT=1`

## Persistent Storage Integration

Storage ownership is cleanly tied to the block device switch:
- `xzs_bdev_open()` ensures the persistent hardware runtime exists:
  - If `!g_xzs_emmc_ctx.initialized`, invokes `xzs_emmc_init_persistent()`.
  - On subsequent calls, reuses the existing initialized context without resetting controller or card.
- `d_strategy()` **never** initializes or resets storage (`STRATEGY_REINITIALIZES_STORAGE=no`).
- `INITIALIZATION_COUNT=1`
- `STRATEGY_REINITIALIZATION_COUNT=0`

## Controller Serialization

To enforce the invariant that only one hardware command/transfer is in flight at any given moment, a dedicated kernel mutex is introduced:
- Group: `lck_grp_alloc_init("xzs_emmc", LCK_GRP_ATTR_NULL)`
- Mutex: `lck_mtx_init(&g_xzs_emmc_mtx, g_xzs_emmc_lck_grp, LCK_ATTR_NULL)`
- Lifecycle: Initialized once at driver registration time (`CONTROLLER_LOCK_INITIALIZED=yes`).
- Critical Section: Held strictly across `xzs_emmc_read_blocks_sync()`. Mutex is released **before** `buf_unmap()` and `buf_biodone()`.
- Telemetry: `CONTROLLER_LOCK_ACQUIRE_COUNT=2`, `MAX_COMMANDS_IN_FLIGHT=1`.

## `d_open` / `d_close`

- **`d_open` Semantics:**
  - Minor check: Only minor `0` permitted (`minor != 0` -> `ENXIO`).
  - Read-only policy: `flags & FWRITE` -> `EROFS`.
  - Read validation: `!(flags & FREAD)` -> `EINVAL`.
  - Hardware bringup: Ensures persistent eMMC context is initialized.
  - Silicon telemetry:
    - `READ_ONLY_OPEN_SUCCESS=yes`
    - `WRITE_OPEN_REJECTED=yes`
    - `WRITE_OPEN_ERRNO=EROFS`
- **`d_close` Semantics:**
  - Pure logical close (`CLOSE_RESETS_STORAGE=no`).
  - Does not power off eMMC, reset controller, issue CMD0, or destroy context.

## `d_strategy`, `buf_t` Mapping & Block-to-LBA Translation

- **Buffer Mapping:**
  - Implemented via canonical `buf_map(bp, &io_addr)`.
  - Automatically handles both cluster and standard iobufs without assuming internal layout.
  - Paired with `buf_unmap(bp)`.
- **Block Translation:**
  - `BSD_BLOCK_UNIT_BYTES=512`
  - `BSD_BLOCK_TO_EMMC_LBA_RULE: start_lba = (uint64_t)buf_blkno(bp)`.
  - Minor 0 maps linearly to physical eMMC user partition LBA.
- **Enforcement:**
  - Must have `B_READ` flag. Write requests rejected with `EROFS` before touching hardware.
  - Request size must be positive multiple of 512.
  - Overflow and bounds checked against live `g_xzs_emmc_ctx.sector_count`.
  - Residual reporting: Full size on entry, set to `0` on success via `buf_setresid(bp, 0)`.

## `d_ioctl` & `d_psize`

- **`d_ioctl`:**
  - `DKIOCGETBLOCKSIZE`: Returns `512` (`g_xzs_emmc_ctx.sector_size`).
  - `DKIOCGETBLOCKCOUNT`: Returns `61071360` (`g_xzs_emmc_ctx.sector_count`).
  - `DKIOCISWRITABLE`: Returns `0` (not writable).
  - Unrecognized ioctls return `ENOTTY`.
- **`d_psize`:**
  - Returns `61071360` (`g_xzs_emmc_ctx.sector_count`), matching local XNU contract of 512-byte blocks (`D_PSIZE_UNIT=512_BYTE_BLOCKS`).
  - Corresponds to 31,268,536,320 bytes total capacity.

## LBA1 Strategy Test (512 Bytes)

- Real `buf_t` allocated via `buf_alloc(NULL)`.
- Configured: `B_READ`, `blkno=1`, `count=512`, `size=512`, `b_dev=makedev(major, 0)`.
- Dispatched through registered `bdevsw[1].d_strategy(bp1)`.
- Completion verified via `buf_biowait(bp1)`.
- Result: `err=0`, `resid=0`.
- Oracle verification:
  - Computed CRC32: `0xD3A34BC1` (exact match)
  - Computed SHA256: `e4b891b42fd57eb352ffbe0aa9098d04fe85f88e3425dcba529064cce72f862a` (exact match against `artifacts/oracles/d4m1_lba1.bin`).
- Freed cleanly via `buf_free(bp1)`.

## 2-Sector Strategy Test (LBA1..2, 1024 Bytes)

- Real `buf_t` allocated via `buf_alloc(NULL)`.
- Configured: `B_READ`, `blkno=1`, `count=1024`, `size=1024`, `b_dev=makedev(major, 0)`.
- Dispatched through registered `bdevsw[1].d_strategy(bp2)`.
- Hardware execution: 2 sequential CMD17 reads (`LBA 1`, `LBA 2`).
- Completion verified via `buf_biowait(bp2)`.
- Result: `err=0`, `resid=0`.
- Oracle verification:
  - Computed CRC32: `0xA21C1724` (exact match)
  - Computed SHA256: `4a161d7ec294bc215b8dd22989a4f87f1c501fac3250acf66e5e2a4085ece8d0` (exact match against `artifacts/oracles/d4m2_lba12.bin`).
- Freed cleanly via `buf_free(bp2)`.

## Failure Injection Tests

All failure injection tests were submitted via legitimate `buf_t` buffers to `d_strategy()`:

1. **Out-of-Range Extent:**
   - Request: `start_lba = 61071360`, `count = 512`
   - Result: Rejected before hardware with `EINVAL`, `resid = 512`.
   - `OUT_OF_RANGE_CMD17_COUNT=0`.
2. **Misaligned Request Size:**
   - Request: `start_lba = 1`, `count = 513`
   - Result: Rejected before hardware with `EINVAL`, `resid = 513`.
   - `MISALIGNED_CMD17_COUNT=0`.
3. **Write Request Rejection:**
   - Request: `B_WRITE`, `start_lba = 1`, `count = 512`
   - Result: Rejected before hardware with `EROFS`, `resid = 512`.
   - `WRITE_TEST_CMD24_COUNT=0`
   - `WRITE_TEST_CMD25_COUNT=0`

## Independent Oracle Comparison

```text
Target  Requested  Transferred  CRC32       SHA-256                                                           Oracle Parity
---------------------------------------------------------------------------------------------------------------------------
LBA1    512 B      512 B        0xD3A34BC1  e4b891b42fd57eb352ffbe0aa9098d04fe85f88e3425dcba529064cce72f862a  100% MATCH
LBA1..2 1024 B     1024 B       0xA21C1724  4a161d7ec294bc215b8dd22989a4f87f1c501fac3250acf66e5e2a4085ece8d0  100% MATCH
```

## Persistent Lifecycle Proof

- `INITIALIZATION_COUNT: 1`
- `INITIALIZATION_REUSE_COUNT: 1`
- `CONTROLLER_RESET_COUNT: 1`
- `STRATEGY_REINITIALIZATION_COUNT: 0`
- `RESET_BETWEEN_STRATEGY_REQUESTS: 0`
- `BDEV_STRATEGY_CALL_COUNT: 5` (2 data reads + 3 rejection tests)
- `BDEV_STRATEGY_READ_COUNT: 2`
- `STRATEGY_TOTAL_SECTORS_READ: 3`
- `CMD17_COUNT: 3`

## Safety Boundaries

- `READ_ONLY=yes`
- `ZERO_STORAGE_WRITES=yes`
- `CMD18_COUNT=0`
- `CMD24_COUNT=0`
- `CMD25_COUNT=0`
- `ERASE_COUNT=0`
- `DISCARD_COUNT=0`
- `DEVFS_DISK0_PUBLISHED=no`
- `DEVFS_NODE_COUNT_CREATED=0`
- `IOKIT_STORAGE_NUB_PUBLISHED=no`
- `PARTITION_SLICES_PUBLISHED=no`
- `IOFIND_BSD_ROOT_INTEGRATION=no`
- `FILESYSTEM_PROBES=0`
- `ROOTFS_SELECTION_PERFORMED=no`

---

## HARDWARE VERIFIED

- Real BSD block strategy path (`bdevsw[major].d_strategy`) successfully drives physical eMMC CMD17 read operations.
- 512-byte strategy read of LBA1 matches independent physical oracle bit-for-bit.
- 1024-byte multi-sector strategy request correctly sequences across physical sector boundaries with exact byte parity.
- Persistent eMMC controller and card remain initialized and unperturbed across consecutive strategy requests.

## XZS SOFTWARE VERIFIED

- Dynamic BSD `bdevsw` registration via `bdevsw_add(-1, ...)` at `BSD_POST_VFSINIT`.
- Read-only `d_open` allowing `FREAD` and rejecting `FWRITE` with `EROFS`.
- Safe `d_close` preserving hardware runtime.
- Mutex-based controller serialization (`lck_mtx_t`) enforcing single in-flight hardware command.
- In-tree `buf_t` handling via `buf_alloc(NULL)`, `buf_reset()`, `buf_map()`, `buf_biowait()`, and `buf_free()`.
- Geometry reporting via `d_ioctl` (`DKIOCGETBLOCKSIZE`, `DKIOCGETBLOCKCOUNT`, `DKIOCISWRITABLE`) and `d_psize`.
- Immediate software rejection of out-of-range, misaligned, and write requests with zero physical command generation.

## NOT YET VERIFIED

- Character device / raw node (`cdevsw`).
- Devfs node publication (`/dev/disk0`, `/dev/rdisk0`).
- Partition slice device creation from GPT partition entries.
- IOKit storage nub integration (`BSD Name`, `BSD Major`, `BSD Minor`).
- VFS root device discovery (`IOFindBSDRoot`) and filesystem mounting.

---

## D4-M2 Status

```text
D4-M1_COMPLETE=yes
D4-M2_COMPLETE=yes

PERSISTENT_EMMC_RUNTIME_VERIFIED=yes
BSD_BLOCK_STRATEGY_VERIFIED=yes

BDEVSW_IMPLEMENTED=yes
CONTROLLER_SERIALIZATION_ENABLED=yes

DEVFS_DISK0_PUBLISHED=no
PARTITION_SLICES_PUBLISHED=no
IOKIT_STORAGE_NUB_PUBLISHED=no

D4_COMPLETE=no
```

## Recommended D4-M3 Plan

Now that dynamic `bdevsw` registration, mutex-protected strategy dispatch, and legitimate `buf_t` handling are hardware-proven:
1. Implement read-only character device (`cdevsw`) companion for raw disk operations (`/dev/rdisk0`).
2. Audit `devfs_make_node()` to publish `/dev/disk0` and `/dev/rdisk0`.
3. Introduce partition slice minor decoding based on the verified GPT partition table (e.g. `disk0s1` .. `disk0sN`).
4. Validate user/kernel character and block reads through devfs nodes while strictly maintaining read-only invariants.
