# D4-M1 Persistent eMMC Runtime Verification

## Git Baseline

- **Milestone Branch:** `xzs-d4-block`
- **Base Commit:** `9395dbb540fbefc5221cd18d5f7f09a6c414e46f`
- **Main / xzs-port Head:** `9395dbb540fbefc5221cd18d5f7f09a6c414e46f`
- **Historical Tags:** `xzs-d2-storage-complete`, `xzs-d3-gpt-complete` (IMMUTABLE)
- **Target Hardware:** Sony Xperia XZs (`MSM8996` / Tone Keyaki / G8231 / Serial `BH905SX976`)
- **Storage Target:** Samsung BJNB4R 32GB eMMC 5.1 (`SDCC1` @ `0x07464900`)

## D3 Evidence Freeze

Phase D3 is complete, validated, and sealed on commit `b2d20c9bb6411904a0f87ee0b088ba0a2a99e579`.
The primary and backup GPT structures were verified byte-for-byte against independent host TWRP oracles:
- `artifacts/oracles/mmcblk0_lba1.bin` (Primary Header)
- `artifacts/oracles/mmcblk0_gpt_primary_entries.bin` (Primary Entries)
- `artifacts/oracles/mmcblk0_gpt_backup_header.bin` (Backup Header)
- `artifacts/oracles/mmcblk0_gpt_backup_entries.bin` (Backup Entries)

Before commencing D4-M1 silicon execution, dedicated single-sector oracle artifacts were frozen:
- `artifacts/oracles/d4m1_lba1.bin`: SHA-256 `e4b891b42fd57eb352ffbe0aa9098d04fe85f88e3425dcba529064cce72f862a` (CRC32: `0xBFDF741D`)
- `artifacts/oracles/d4m1_lba2.bin`: SHA-256 `614975343d7fd0d45cb2b61b17a8f23dcbc4f782b7972e18d2b70cabd8b31ee2`
- `artifacts/oracles/d4m1_lba33.bin`: SHA-256 `076a27c79e5ace2a3d47f9dd2e83e4ff6ea8872b3c2218f66c92b89b55f36560`
- `artifacts/oracles/d4m1_backup_header.bin`: SHA-256 `94434f5b0c7b8d547e2ef9ee6e3322ed1c82631310c172ab42a4878e4ec88aee` (CRC32: `0x03F02415`)

`D4M1_ORACLES_FROZEN_BEFORE_SILICON=yes`.

## D4 Runtime Context Design

The persistent runtime context is declared in `src/xnu/pexpert/pexpert/arm/xzs_sdhci.h` and instantiated statically in `src/xnu/pexpert/arm/xzs_sdhci.c`:

```c
typedef struct xzs_emmc_context {
    bool      initialized;

    uint16_t  rca;

    uint64_t  sector_count;
    uint64_t  last_physical_lba;

    uint32_t  sector_size;

    uint8_t   ext_csd_rev;

    uint32_t  initialization_count;
    uint32_t  initialization_reuse_count;
    uint32_t  controller_reset_count;

    uint64_t  successful_sector_reads;
    uint64_t  failed_sector_reads;
} xzs_emmc_context_t;

static xzs_emmc_context_t g_xzs_emmc_ctx;
```

Semantics:
- Fails closed: `initialized` is set to `true` strictly at the very end of successful card initialization and EXT_CSD derivation.
- No dynamic memory allocation (`kalloc` / `zalloc`).
- Pure hardware abstraction without premature OS entities (`bdevsw`, `cdevsw`, `devfs`, `IOService`).

## Persistent Initialization API

```c
int xzs_emmc_init_persistent(void);
```

- Programs SDC1 RCG2 clock to 400 kHz.
- Resets SDHCI host controller (`SDHCI_RESET_ALL`).
- Configures vendor and host power registers (1.8V bus).
- Executes card initialization sequence: CMD0 -> CMD1 polling -> CMD2 (ALL_SEND_CID) -> CMD3 (SET_RELATIVE_ADDR, RCA=2) -> CMD9 (SEND_CSD) -> CMD7 (SELECT_CARD) -> CMD8 (SEND_EXT_CSD).
- Derives live `sector_count` (`61071360`), `last_physical_lba` (`61071359`), `sector_size` (`512`), `ext_csd_rev` (`0x08`), and `rca` (`2`).

## Idempotency Test

The probe calls `xzs_emmc_init_persistent()` twice consecutively:
- Call #1: Performs full hardware initialization, sets `initialized = true`, `initialization_count = 1`, `controller_reset_count = 1`.
- Call #2: Detects `g_xzs_emmc_ctx.initialized == true`, increments `initialization_reuse_count = 1`, and returns `0` immediately without resetting the controller or re-issuing CMD0..CMD8.

Telemetry:
```text
INIT_CALL_COUNT: 2
INITIALIZATION_COUNT: 1
INITIALIZATION_REUSE_COUNT: 1
CONTROLLER_RESET_COUNT: 1
```

## Live Geometry

Live geometry values dynamically populated from hardware:
- `CONTEXT_RCA`: `2`
- `CONTEXT_SECTOR_SIZE`: `512` bytes
- `CONTEXT_SECTOR_COUNT`: `61071360`
- `CONTEXT_LAST_PHYSICAL_LBA`: `61071359`
- `CONTEXT_EXT_CSD_REV`: `0x08` (MMC v5.1)

## Hardware Command Counters

```text
CMD0_COUNT:                   1
CMD1_COUNT:                   2
CMD2_COUNT:                   1
CMD3_COUNT:                   1
CMD9_COUNT:                   1
CMD7_COUNT:                   1
CMD8_COUNT:                   1
CMD17_COUNT:                  4
CMD18_COUNT:                  0
CMD24_COUNT:                  0
CMD25_COUNT:                  0
ERASE_COUNT:                  0
DISCARD_COUNT:                0
READ_RETRY_COUNT:             0
RESET_BETWEEN_READS:          0
REINIT_BETWEEN_READS:         0
SUCCESSFUL_SECTOR_READS:      4
FAILED_SECTOR_READS:          0
```

## Multi-Sector Read API

```c
int xzs_emmc_read_blocks_sync(uint64_t start_lba, uint32_t count, void *buffer);
```

- Software loop issuing individual validated CMD17 read commands.
- Places sector `i` at `buffer + (i * 512)`.
- Reuses the live TRAN state established by `xzs_emmc_init_persistent()`.
- Verified using contiguous request `start_lba=1`, `count=2` into buffer `g_d4m1_lba12`.

## Request Bounds Validation

Before issuing any hardware command, `xzs_emmc_read_blocks_sync()` enforces:
1. `g_xzs_emmc_ctx.initialized == true`
2. `buffer != NULL`
3. `count > 0`
4. `start_lba < sector_count`
5. `(count - 1) <= (UINT64_MAX - start_lba)` (no overflow on addition)
6. `(start_lba + count - 1) < sector_count` (does not exceed disk boundaries)
7. `count <= (SIZE_MAX / 512)` (buffer pointer arithmetic safety)

Telemetry confirms:
- `REQUEST_RANGE_OVERFLOW_COUNT: 0`
- `REQUEST_BUFFER_SIZE_OVERFLOW_COUNT: 0`

## LBA1 Verification

- Read via: `xzs_emmc_read_blocks_sync(1, 2, buf)` (Sector 0 of multi-sector request)
- Generation: `LBA1_INIT_GENERATION: 1`
- R1 Raw: `0x00000900` (`READY_FOR_DATA=1`, `CURRENT_STATE=4` TRAN, `REJECT_BITS=0`)
- Stored Header CRC32: `0xBFDF741D`
- Calculated Header CRC32: `0xBFDF741D`
- SHA-256: `e4b891b42fd57eb352ffbe0aa9098d04fe85f88e3425dcba529064cce72f862a`
- Status: **EXACT MATCH** against frozen oracle

## LBA2 Verification

- Read via: `xzs_emmc_read_blocks_sync(1, 2, buf)` (Sector 1 of multi-sector request)
- Generation: `LBA2_INIT_GENERATION: 1`
- R1 Raw: `0x00000900` (`READY_FOR_DATA=1`, `CURRENT_STATE=4` TRAN, `REJECT_BITS=0`)
- SHA-256: `614975343d7fd0d45cb2b61b17a8f23dcbc4f782b7972e18d2b70cabd8b31ee2`
- Status: **EXACT MATCH** against frozen oracle

## LBA33 Verification

- Read via: `xzs_emmc_read_sector_sync(33, buf)` (Non-contiguous single-sector jump)
- Generation: `LBA33_INIT_GENERATION: 1`
- R1 Raw: `0x00000900` (`READY_FOR_DATA=1`, `CURRENT_STATE=4` TRAN, `REJECT_BITS=0`)
- SHA-256: `076a27c79e5ace2a3d47f9dd2e83e4ff6ea8872b3c2218f66c92b89b55f36560`
- Status: **EXACT MATCH** against frozen oracle

## Backup Header Verification

- Read via: `xzs_emmc_read_sector_sync(61071359, buf)` (Large seek to final physical disk LBA)
- Generation: `BACKUP_HEADER_INIT_GENERATION: 1`
- R1 Raw: `0x00000900` (`READY_FOR_DATA=1`, `CURRENT_STATE=4` TRAN, `REJECT_BITS=0`)
- Stored Header CRC32: `0x03F02415`
- Calculated Header CRC32: `0x03F02415`
- SHA-256: `94434f5b0c7b8d547e2ef9ee6e3322ed1c82631310c172ab42a4878e4ec88aee`
- Status: **EXACT MATCH** against frozen oracle

## Independent Oracle Comparisons

Reconstructed from `console-ramoops.log` via `scripts/verify_d4m1_acceptance.py`:

| Target Sector | LBA | Reconstructed Size | SHA-256 Hash | Oracle Status | CRC32 |
|---|---|---|---|---|---|
| Primary Header | 1 | 512 bytes | `e4b891b42fd57eb352ffbe0aa9098d04fe85f88e3425dcba529064cce72f862a` | MATCH | `0xBFDF741D` (VALID) |
| Primary Entry Slot 0..3 | 2 | 512 bytes | `614975343d7fd0d45cb2b61b17a8f23dcbc4f782b7972e18d2b70cabd8b31ee2` | MATCH | N/A |
| Primary Entry Slot 124..127 | 33 | 512 bytes | `076a27c79e5ace2a3d47f9dd2e83e4ff6ea8872b3c2218f66c92b89b55f36560` | MATCH | N/A |
| Backup Header | 61071359 | 512 bytes | `94434f5b0c7b8d547e2ef9ee6e3322ed1c82631310c172ab42a4878e4ec88aee` | MATCH | `0x03F02415` (VALID) |

All four target sectors show 100% byte-for-byte identity against independent host TWRP oracles.

## No-Reinitialization Proof

Direct evidence from hardware telemetry:
1. `INIT_CALL_COUNT = 2`
2. `INITIALIZATION_COUNT = 1`
3. `INITIALIZATION_REUSE_COUNT = 1`
4. `CONTROLLER_RESET_COUNT = 1`
5. `RESET_BETWEEN_READS = 0`
6. `REINIT_BETWEEN_READS = 0`
7. `LBA1_INIT_GENERATION = 1`
8. `LBA2_INIT_GENERATION = 1`
9. `LBA33_INIT_GENERATION = 1`
10. `BACKUP_HEADER_INIT_GENERATION = 1`
11. `ALL_TARGET_READ_READY_FOR_DATA = yes`
12. `ALL_TARGET_READ_R1_REJECT_BITS = 0`

The eMMC card remained continuously selected in TRAN state across all 4 non-contiguous reads without any intermediate controller reset, card reset, or re-negotiation.

## Safety Boundaries

- `READ_ONLY: yes`
- `CMD18_COUNT: 0`
- `CMD24_COUNT: 0`
- `CMD25_COUNT: 0`
- `ERASE_COUNT: 0`
- `DISCARD_COUNT: 0`
- `PARTITION_CONTENT_READS: 0`
- `FILESYSTEM_PROBES: 0`
- `ROOTFS_SELECTION_PERFORMED: no`
- `BDEVSW_IMPLEMENTED: no`
- `DEVFS_DISK0_PUBLISHED: no`
- `IOKIT_STORAGE_NUB_PUBLISHED: no`

## HARDWARE VERIFIED

- eMMC can be initialized once and reused for multiple physical reads.
- Four non-contiguous GPT metadata sectors read in one persistent session.
- Live geometry preserved across calls.
- Zero controller resets between data requests.
- Zero storage writes.

## XZS SOFTWARE VERIFIED

- Persistent context lifecycle (`xzs_emmc_context_t`, `g_xzs_emmc_ctx`).
- Idempotent initialization API (`xzs_emmc_init_persistent()`).
- 64-bit LBA API boundary (`xzs_emmc_read_sector_sync()`, `xzs_emmc_read_blocks_sync()`).
- Checked request range arithmetic.
- Software multi-sector CMD17 pipeline.
- Correct sequential destination buffer placement.

## NOT YET VERIFIED

- Concurrent callers / multi-threading.
- Controller synchronization / locking (`lck_mtx_t`).
- BSD `bdevsw` switch table integration.
- Strategy I/O routine (`d_strategy`) and `buf_t` handling.
- `devfs` disk nodes (`/dev/disk0`, `/dev/rdisk0`).
- Partition slice device registration (`disk0s1`..`disk0s55`).
- Filesystem mounting.

## D4-M1 Status

`D4-M1_COMPLETE = yes`
`PERSISTENT_EMMC_RUNTIME_VERIFIED = yes`
`MULTI_SECTOR_PIPELINE_VERIFIED = yes`
`D4_COMPLETE = no`

## Recommended D4-M2 Plan

1. Implement controller serialization lock (`lck_mtx_t` / `lck_grp_t`).
2. Design BSD `bdevsw` driver layer (`xzs_emmc_bdevsw`) with `d_open`, `d_close`, `d_strategy`, `d_psize`, `d_ioctl`.
3. Map `buf_t` I/O requests into `xzs_emmc_read_blocks_sync()`.
4. Register major block device number via `bdevsw_add()`.
5. Create raw and block device nodes (`/dev/disk0`, `/dev/rdisk0`) via `devfs_make_node()`.
6. Verify single-block and multi-block reads via BSD strategy layer on physical silicon.
