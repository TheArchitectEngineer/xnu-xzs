# Phase D3 GPT Partition Discovery — Complete Verification Walkthrough

## Milestone Objective
Complete Phase D3 of the native Apple XNU kernel port on Sony Xperia XZs (`MSM8996` / Tone Keyaki / G8231 / `BH905SX976`), verifying the Primary GPT Header, Primary Partition Entry Array, Primary Partition Map, Backup GPT Header, Backup Partition Entry Array, and achieving cross-validated authoritative partition map certification on physical silicon with 100% parity against independent TWRP host oracles.

---

## 1. D3 Milestone Progression

| Milestone | Scope | Silicon Checkpoint | Oracle Hash | Status |
|:---|:---|:---|:---|:---:|
| **D3-M1** | Primary GPT Header (LBA 1) verification & CRC32 | `CP=0xD3C0` / `0x01` | Header CRC `0xBFDF741D` | **PASS** |
| **D3-M2A** | Primary Entry Array (LBA 2..33, 16 KiB) read & CRC32 | `CP=0xD3D0` / `0x01` | SHA256 `8249f2bfa9...` | **PASS** |
| **D3-M2B** | Primary Partition Map decode, extents & non-overlap | `CP=0xD3E0` / `0x01` | 55 used / 73 unused | **PASS** |
| **D3-M3** | Backup GPT Header/Array read & Authoritative Seal | `CP=0xD3F0` / `0x01` | 100% reciprocal parity | **PASS** |

---

## 2. Changes Made in Phase D3-M3
1. **Independent Host Backup GPT Oracle (`scripts/host_gpt_backup_oracle.py`):**
   - Derived physical geometry via TWRP (`blockdev --getsz /dev/block/mmcblk0` -> 61,071,360 sectors).
   - Captured Backup GPT Header from sector 61071359 (`artifacts/oracles/mmcblk0_gpt_backup_header.bin`, SHA-256 `94434f5b0c7b...`).
   - Verified Backup Header CRC32 (`0x03F02415`).
   - Verified reciprocal link semantics between Primary LBA 1 and Backup Header.
   - Dynamically derived Backup Entry Array geometry (LBAs 61071327..61071358, 32 sectors, 16,384 bytes).
   - Captured Backup Entry Array (`artifacts/oracles/mmcblk0_gpt_backup_entries.bin`, SHA-256 `8249f2bfa9...`).
   - Verified byte-for-byte identity between Primary and Backup arrays (`cmp` exit 0).
   - Generated Backup Partition Map JSON (`artifacts/oracles/gpt_backup_partition_map.json`) and verified zero differences against Primary Partition Map JSON.

2. **Phase D3-M3 XNU Kernel Implementation (`xzs_sdhci_phase_d3m3_probe`):**
   - Implemented in `src/xnu/pexpert/arm/xzs_sdhci.c`, declared in `src/xnu/pexpert/pexpert/arm/xzs_sdhci.h`, and hooked into `src/xnu/bsd/kern/bsd_init.c`.
   - Unified entry array parser `xzs_parse_gpt_entry_array()` shared between Primary and Backup passes.
   - Static map and array allocations in BSS/data (zero kernel stack allocation).
   - Breadcrumb sequence in namespace `CP = 0xD3F0` (`0x00` -> `0x01`).
   - Replay fresh hardware initialization through CMD0..CMD8.
   - Re-verify Primary Header (LBA 1) and Primary Array (LBA 2..33).
   - Read Backup Header dynamically from `Primary.AlternateLBA` (LBA 61071359).
   - Verify Backup Header CRC32 (`0x03F02415`) and reciprocal relationship.
   - Read Backup Array dynamically from derived sectors (LBAs 61071327..61071358).
   - Verify Backup Array CRC32 (`0x64EDE0F4`).
   - Compare Primary and Backup raw arrays in memory (exact byte match).
   - Parse Backup Map into static records and verify 100% field equality with Primary Map.
   - Assert `AUTHORITATIVE_GPT_PARTITION_MAP_VERIFIED=yes` and `PRIMARY_BACKUP_GPT_CONSISTENT=yes`.
   - Serialize Backup Header (`BACKUP_HEADER_HEX[00..31]`), Backup Array (`BACKUP_ARRAY_CHUNK[00..31]`), and 55 `BACKUP_ENTRY[xx]` records.
   - Hard Safety Boundary: zero partition content reads, zero filesystem probes, zero rootfs selection, zero storage writes, clean warm reset to fastboot.

3. **Acceptance Verification Script (`scripts/verify_d3m3_acceptance.py`):**
   - Extracts telemetry from `console-ramoops.log`.
   - Reconstructs `artifacts/builds/d3m3_backup_header.bin` and verifies against host oracle (`cmp` exit 0).
   - Reconstructs `artifacts/builds/d3m3_backup_entries.bin` and verifies against host oracle (`cmp` exit 0).
   - Audits field-by-field parity across all 55 used backup partitions against `gpt_backup_partition_map.json`.

---

## 3. Silicon Validation Results

```text
================================================================================
PHASE D3-M3: BACKUP GPT VERIFICATION & AUTHORITATIVE SEAL ACCEPTANCE AUDIT
================================================================================
Target Log File:               artifacts/logs/console-ramoops.log
Host Backup Header Oracle:     artifacts/oracles/mmcblk0_gpt_backup_header.bin
Host Backup Entries Oracle:    artifacts/oracles/mmcblk0_gpt_backup_entries.bin
Host Backup Partition Map:     artifacts/oracles/gpt_backup_partition_map.json
--------------------------------------------------------------------------------

1. AUDITING REQUIRED TELEMETRY KEYS:
  [PASS] All 69 required telemetry keys verified

2. RECONSTRUCTING & AUDITING BACKUP GPT HEADER:
  Reconstructed Header Size:   512 bytes
  Reconstructed Header SHA256: 94434f5b0c7b8d547e2ef9ee6e3322ed1c82631310c172ab42a4878e4ec88aee
  Host Oracle Header SHA256:   94434f5b0c7b8d547e2ef9ee6e3322ed1c82631310c172ab42a4878e4ec88aee
  [PASS] D3M3_BACKUP_HEADER_BYTE_FOR_BYTE_MATCH: yes
  HOST_VS_XNU_BACKUP_HEADER_MATCH: yes

3. RECONSTRUCTING & AUDITING BACKUP PARTITION ENTRY ARRAY:
  Reconstructed Array Size:    16384 bytes
  Reconstructed Array SHA256:  8249f2bfa98ec675bf66b6342e29a0fbf6ab0ccb716c789a172f1f46b1828dd3
  Host Oracle Array SHA256:    8249f2bfa98ec675bf66b6342e29a0fbf6ab0ccb716c789a172f1f46b1828dd3
  [PASS] D3M3_BACKUP_ARRAY_BYTE_FOR_BYTE_MATCH: yes
  HOST_VS_XNU_BACKUP_ARRAY_MATCH: yes

4. FIELD-BY-FIELD ACCURACY AUDIT ACROSS ALL 55 BACKUP PARTITIONS:
  [PASS] ZERO field differences across all 55 used backup partitions!
  HOST_VS_XNU_BACKUP_PARTITION_MAP_MATCH: yes

================================================================================
PHASE D3-M3 ACCEPTANCE: ALL AUDITS PASSED [AUTHORITATIVE SEAL CERTIFIED]
================================================================================
```

---

## 4. Authoritative Partition Map Classification
- **Primary / Backup Consistency:** Certified 100% consistent across all 128 slots.
- **Used Partitions:** Exactly 55 partitions identified spanning LBA 34 to 61067263.
- **Unused Partitions:** 73 zero-GUID slots.
- **GPT Name-Paired Entries:** Name pairs with `_a` / `_b` suffixes observed in GPT metadata (`GPT_NAME_PAIRED_A_B_ENTRIES_OBSERVED=yes`). Functional A/B boot slot semantics remain unestablished (`A_B_BOOT_SLOT_SEMANTICS=NOT_ESTABLISHED`).
- **Filesystem Content Boundary:** Zero filesystem superblocks probed (`FILESYSTEMS=NOT_PROBED`), zero rootfs selected (`ROOTFS=NOT_SELECTED`), zero partition data read (`PARTITION_CONTENT_READS=0`), zero writes performed (`ZERO_STORAGE_WRITES=yes`).
- **Phase D3 Status:** COMPLETE. Phase D4 is NEXT.
