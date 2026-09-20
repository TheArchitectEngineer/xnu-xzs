# XZSFS (Xperia XZs File System) Specification — Version 1

**Status:** FROZEN  
**Target:** Sony Xperia XZs (`MSM8996` / Tone Keyaki / G8231 / Serial `BH905SX976`)  
**Phase:** D5-M1 (Host Tooling & Format Definition)  
**Byte Order:** Little-Endian (LE)  
**Default Sector Size:** 512 bytes  
**Data Alignment:** 512 bytes (`XZSFS_DATA_ALIGNMENT = 512`)  

---

## 1. Overview & Architectural Scope

XZSFS Version 1 is a minimal, deterministic, read-only on-disk filesystem designed exclusively for Apple XNU early bootstrap on the Sony Xperia XZs.

### Core Design Principles:
1. **Ultra-Simple & Deterministic**: Linear layout with fixed metadata tables. Zero b-trees, zero free-space bitmaps, zero journaling, zero dynamic extents.
2. **Read-Only**: Strictly enforces read-only semantics. All mutation operations (`create`, `mkdir`, `remove`, `rename`, `write`, `truncate`) fail with `EROFS`.
3. **Bounded & Fail-Closed**: All offsets, lengths, and object IDs are strictly bounds-checked. Overlapping file extents, out-of-bounds tables, cycles in the directory graph, or mismatched checksums cause immediate rejection.
4. **512-Byte Sector Friendly**: All tables, headers, and data payloads are aligned to 512-byte sectors, making XZSFS directly suitable for both physical eMMC block devices (`/dev/disk0sN`) and memory-backed RAMDisks (`/dev/md0`).
5. **No Host Leakage**: File timestamps, host UIDs/GIDs, and host directory layout quirks are completely abstracted. Re-running the generator with identical input trees produces 100% byte-identical images.

---

## 2. On-Disk Layout

An XZSFS v1 image consists of four contiguous sections:

```text
+-------------------------------------------------------------------------+
| Sector 0: Superblock (512 bytes)                                        |
+-------------------------------------------------------------------------+
| Sectors 1..M: Object Table (fixed 64-byte entries, 8 objects/sector)     |
+-------------------------------------------------------------------------+
| Sectors M+1..N: String Table (packed NUL-terminated strings, padded)    |
+-------------------------------------------------------------------------+
| Sectors N+1..EOF: Aligned Data Payloads (512-byte aligned file extents) |
+-------------------------------------------------------------------------+
```

---

## 3. Superblock Specification

The superblock occupies Sector 0 (offset `0x0000`, length 512 bytes). All fields are little-endian.

| Byte Offset | Field Name | Type | Description |
| :--- | :--- | :--- | :--- |
| `0x0000 - 0x0003` | `magic` | `uint32_t` | Magic signature: `0x5346535A` (ASCII `'XZSF'`) |
| `0x0004 - 0x0007` | `format_version` | `uint32_t` | Format version: `1` |
| `0x0008 - 0x000B` | `header_size` | `uint32_t` | Size of superblock: `512` bytes |
| `0x000C - 0x000F` | `sector_size` | `uint32_t` | Sector size: `512` bytes |
| `0x0010 - 0x0017` | `image_size_bytes` | `uint64_t` | Total size of filesystem image in bytes |
| `0x0018 - 0x001B` | `object_count` | `uint32_t` | Total number of objects in Object Table |
| `0x001C - 0x001F` | `root_object_id` | `uint32_t` | Object ID of root directory (`/`): always `1` |
| `0x0020 - 0x0027` | `object_table_offset` | `uint64_t` | Byte offset of Object Table from start of image (`512`) |
| `0x0028 - 0x002F` | `object_table_size` | `uint64_t` | Byte size of Object Table (`object_count * 64`) |
| `0x0030 - 0x0037` | `string_table_offset` | `uint64_t` | Byte offset of String Table from start of image (512-byte aligned) |
| `0x0038 - 0x003F` | `string_table_size` | `uint64_t` | Byte size of String Table (padded to 512 bytes) |
| `0x0040 - 0x0047` | `data_offset` | `uint64_t` | Byte offset of Data Payload area (512-byte aligned) |
| `0x0048 - 0x004F` | `data_size` | `uint64_t` | Total byte size of data payloads (including padding) |
| `0x0050 - 0x0053` | `metadata_crc32` | `uint32_t` | IEEE 802.3 CRC32 of metadata area (see §6) |
| `0x0054 - 0x0057` | `reserved` | `uint32_t` | Reserved (must be `0`) |
| `0x0058 - 0x01FF` | `padding` | `uint8_t[424]` | Zero padding to 512-byte boundary |

---

## 4. Object Table Entry Specification

Each object (directory or regular file) is described by a fixed 64-byte entry. Exactly 8 entries fit in one 512-byte sector.

| Byte Offset | Field Name | Type | Description |
| :--- | :--- | :--- | :--- |
| `0x00 - 0x03` | `object_id` | `uint32_t` | Unique object identifier (1-based: `1 .. object_count`) |
| `0x04 - 0x07` | `parent_id` | `uint32_t` | Object ID of parent directory (for root `/`, `parent_id == 1`) |
| `0x08 - 0x0B` | `type` | `uint32_t` | Object type: `1` = Directory (`XZSFS_TYPE_DIR`), `2` = Regular File (`XZSFS_TYPE_REG`) |
| `0x0C - 0x0F` | `mode` | `uint32_t` | POSIX permissions (e.g. `0755` for dirs/executables, `0644` for files) |
| `0x10 - 0x13` | `uid` | `uint32_t` | Owner user ID (always `0` / root) |
| `0x14 - 0x17` | `gid` | `uint32_t` | Owner group ID (always `0` / wheel) |
| `0x18 - 0x1B` | `name_offset` | `uint32_t` | Byte offset within String Table for filename |
| `0x1C - 0x1F` | `name_length` | `uint32_t` | Length of filename in bytes (excluding terminating NUL) |
| `0x20 - 0x27` | `data_offset` | `uint64_t` | Byte offset of payload from image start (`0` for directories) |
| `0x28 - 0x2F` | `data_length` | `uint64_t` | File size in bytes (`0` for directories) |
| `0x30 - 0x37` | `reserved1` | `uint64_t` | Reserved (must be `0`) |
| `0x38 - 0x3F` | `reserved2` | `uint64_t` | Reserved (must be `0`) |

### Object Type Constants:
```c
#define XZSFS_TYPE_DIR  1   /* Directory */
#define XZSFS_TYPE_REG  2   /* Regular file */
```

---

## 5. String Table Specification

1. The string table contains NUL-terminated ASCII strings for all object names.
2. For the root directory (`/`), `name_offset = 0`, `name_length = 0`, string is `""` (single NUL byte).
3. All other filenames must be non-empty, ASCII-only (`[a-zA-Z0-9._-]`), and must NOT contain `/` or NUL.
4. Entries `.` and `..` are **never stored** in the string table or object table. Directory hierarchy is resolved entirely via `parent_id`.
5. The string table is padded with NUL bytes to the next 512-byte boundary.

---

## 6. Checksum Policy

Metadata integrity is protected by `metadata_crc32` in the superblock:
- **Algorithm**: Standard IEEE 802.3 CRC32 (polynomial `0xEDB88320`, initial value `0xFFFFFFFF`, final XOR `0xFFFFFFFF`).
- **Calculation Range**:
  1. Superblock bytes `0x0000 - 0x01FF` (512 bytes), with `metadata_crc32` (bytes `0x0050 - 0x0053`) set to `0`.
  2. The entire Object Table (bytes `object_table_offset` through `object_table_offset + object_table_size - 1`).
  3. The entire String Table (bytes `string_table_offset` through `string_table_offset + string_table_size - 1`).
- File data payloads are **not** included in `metadata_crc32` to allow fast kernel mount validation without reading the entire storage area.
- Host artifact verification uses standard SHA-256 over the entire `.img` file.

---

## 7. Path Traversal & Lookup Rules

Path resolution in XZSFS v1 is exact and deterministic:
1. **Root Directory**: Object ID `1` always represents `/`.
2. **Lookup Operation** `lookup(parent_id, name)`:
   - Scan Object Table for an entry where `entry.parent_id == parent_id`.
   - Compare string table slice at `entry.name_offset` of length `entry.name_length` with `name`.
   - If match found, return `entry.object_id`.
   - If no entry matches, return `ENOENT`.
3. **No Ambiguity**: Duplicate filenames under the same parent directory are strictly forbidden and rejected at build and verification time.
4. **Acyclic Hierarchy**: Any cycle in `parent_id` (e.g. `A -> B -> A`) is invalid and rejected.

---

## 8. Alignment & Size Invariants

1. `header_size == 512`.
2. `object_table_offset == 512`.
3. `string_table_offset == (object_table_offset + object_table_size + 511) & ~511`.
4. `data_offset == (string_table_offset + string_table_size + 511) & ~511`.
5. For every regular file:
   - `data_offset % 512 == 0`.
   - `data_offset >= sb.data_offset`.
   - `data_offset + data_length <= sb.image_size_bytes`.
6. For every directory:
   - `data_offset == 0`.
   - `data_length == 0`.
7. `image_size_bytes % 512 == 0`.

---

## 9. Read-Only Semantics & Error Mapping

| Operation | XZSFS Behavior | Error Code |
| :--- | :--- | :--- |
| `VNOP_CREATE` | Prohibited | `EROFS` |
| `VNOP_MKDIR` | Prohibited | `EROFS` |
| `VNOP_REMOVE` | Prohibited | `EROFS` |
| `VNOP_RMDIR` | Prohibited | `EROFS` |
| `VNOP_RENAME` | Prohibited | `EROFS` |
| `VNOP_WRITE` | Prohibited | `EROFS` |
| `VNOP_TRUNCATE` | Prohibited | `EROFS` |
| `VNOP_SETATTR` | Prohibited | `EROFS` |
| `VNOP_LINK` | Prohibited | `EROFS` |
| `VNOP_SYMLINK` | Prohibited | `ENOTSUP` (no symlinks in v1) |
| `VNOP_READ` | Supported for regular files | `0` (or `EINVAL` on bad offset) |
| `VNOP_LOOKUP` | Supported for directories | `0` (or `ENOENT`) |
| `VNOP_READDIR` | Supported for directories | `0` |
| `VNOP_GETATTR` | Supported for all objects | `0` |
