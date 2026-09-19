# Phase D3-M2B Silicon Verification Walkthrough

## Milestone Objective
Phase D3-M2B: Re-read and integrity-verify the Primary GPT Header (LBA 1) and 16-KiB Entry Array (LBA 2..33) directly from the onboard Samsung BJNB4R eMMC storage on the physical Sony Xperia XZs (`MSM8996`), decode all 128 GPT partition slots into static kernel structures, validate extents, bounds, arithmetic safety, pairwise non-overlap, and unique GUIDs, deterministically decode canonical UTF-16LE names and preserve 72-byte raw hex, emit full telemetry, and verify 100% parity against an independent frozen host oracle JSON with zero differences.

---

## Changes Made
1. **Independent Host GPT Map Oracle (`scripts/host_gpt_entries_oracle.py`):**
   - Consumed independent binary captures `artifacts/oracles/mmcblk0_lba1.bin` and `artifacts/oracles/mmcblk0_gpt_primary_entries.bin`.
   - Derived all geometry and bounds from LBA 1 oracle (`FirstUsableLBA=34`, `LastUsableLBA=61071326`, `NumberOfPartitionEntries=128`, `SizeOfPartitionEntry=128`, `PartitionEntryArrayCRC32=0x64EDE0F4`).
   - Verified array CRC32 (`0x64EDE0F4`) and SHA-256 (`8249f2bfa98ec...`).
   - Generated and frozen machine-readable partition map JSON ahead of silicon evaluation:
     - File: `artifacts/oracles/gpt_primary_partition_map.json`
     - SHA-256: `13ee706f681f640b26b7a02350a27c7bc1e6ad4a7b95378f7c398b240b6b09e5`
     - Host Used Count: 55, Host Unused Count: 73.

2. **Automated Offline Acceptance Verifier (`scripts/verify_d3m2b_acceptance.py`):**
   - Extracts all D3-M2B telemetry keys from `console-ramoops.log`.
   - Reconstructs used entries from `GPT_ENTRY[xx].*` records.
   - Matches records by original `slot_index` (`0..54`).
   - Audits all 12 fields per used partition against the frozen oracle JSON.

3. **Phase D3-M2B XNU Probe Implementation (`xzs_sdhci_phase_d3m2b_probe`):**
   - Implemented in `src/xnu/pexpert/arm/xzs_sdhci.c` and declared in `src/xnu/pexpert/pexpert/arm/xzs_sdhci.h`.
   - Hooked into BSD initialization path in `src/xnu/bsd/kern/bsd_init.c`.
   - Static map storage: `static xzs_gpt_partition_entry_t g_xzs_gpt_parsed_entries[128]` (zero kernel stack allocation).
   - Breadcrumb sequence in namespace `CP = 0xD3E0` (`0x00` -> `0x01`).
   - Integrity gates:
     - Replay fresh hardware init, extract live geometry (`live_sec_count = 61071360`).
     - Fresh LBA 1 read & Header CRC32 validation (`0xBFDF741D`).
     - Derive dynamic array geometry (`runtime_num_entries = 128`, `runtime_entry_size = 128`, `runtime_array_bytes = 16384`).
     - Fresh LBA 2..33 read & Array CRC32 validation (`0x64EDE0F4`).
     - Loop over `runtime_num_entries` with per-slot bounds validation.
   - USED / UNUSED classification: slot is UNUSED iff `PartitionTypeGUID == all_zero`.
   - Strict validation:
     - Unique GUID non-zero and pairwise unique.
     - Extents within `[gpt_first_usable_lba, gpt_last_usable_lba]` and `< live_sec_count`.
     - Overflow-safe sector and byte arithmetic.
     - Pairwise extent non-overlap across all 55 used entries: `A.start <= B.end && B.start <= A.end`.
   - Deterministic string encoding:
     - Canonical name: ASCII for `0x0020..0x007E`, `\uXXXX` otherwise, terminated at `0x0000`.
     - Preserved raw hex for 72-byte name and 16-byte type/unique GUIDs.
     - Formatted GUIDs: `Data1 (LE32) - Data2 (LE16) - Data3 (LE16) - Data4[0..1] - Data4[2..7]`.
   - Hard Safety Boundary: Zero reads beyond LBA 33, zero partition content reads, zero filesystem probes, zero rootfs selection, zero backup GPT reads, zero storage writes, warm reset to fastboot.

---

## Silicon Validation Results

```text
================================================================================
PHASE D3-M2B: PRIMARY GPT PARTITION MAP ACCEPTANCE AUDIT
================================================================================
Target Log File:        artifacts/logs/console-ramoops.log
Frozen Host Oracle JSON:artifacts/oracles/gpt_primary_partition_map.json
--------------------------------------------------------------------------------

1. AUDITING REQUIRED TELEMETRY KEYS:
  [PASS] D3_BRANCH = xzs-d3-gpt
  [PASS] D3_BRANCH_BASE = 20cdf4a2c86f1b9eac7573479025ac618b505e84
  [PASS] ORACLE_INDEPENDENCE = yes
  [PASS] LIVE_SEC_COUNT_DERIVED_FROM_EXT_CSD = yes
  [PASS] LIVE_SEC_COUNT = 61071360
  [PASS] DEVICE_GEOMETRY_ORACLE_MATCH = yes
  [PASS] FRESH_PRIMARY_HEADER_VERIFIED = yes
  [PASS] PRIMARY_GPT_HEADER_VERIFIED = yes
  [PASS] FRESH_PRIMARY_ARRAY_CRC_VERIFIED = yes
  [PASS] PRIMARY_GPT_PARTITION_ARRAY_VERIFIED = yes
  [PASS] GPT_PARTITION_ARRAY_CRC32_VERIFIED = yes
  [PASS] KERNEL_GPT_CRC_ORACLE_HARDCODED = no
  [PASS] ENTRY_COUNT_DERIVED_FROM_HEADER = yes
  [PASS] ENTRY_SIZE_DERIVED_FROM_HEADER = yes
  [PASS] RUNTIME_NUM_ENTRIES = 128
  [PASS] RUNTIME_ENTRY_SIZE = 128
  [PASS] PARTITION_ENTRY_BOUNDS_CHECKS = yes
  [PASS] ENTRY_RESERVED_TAIL_BYTES_PER_SLOT = 0
  [PASS] PARTITION_PARSER_ENTERED = yes
  [PASS] GPT_ENTRY_SLOTS_PARSED = 128
  [PASS] KERNEL_USED_COUNT_HARDCODED = no
  [PASS] USED_UNIQUE_GUID_ZERO_COUNT = 0
  [PASS] DUPLICATE_UNIQUE_GUID_COUNT = 0
  [PASS] UNIQUE_PARTITION_GUIDS_VALID = yes
  [PASS] INVALID_USED_EXTENT_COUNT = 0
  [PASS] PARTITION_SIZE_ARITHMETIC_OVERFLOW_COUNT = 0
  [PASS] OVERLAPPING_PARTITION_PAIR_COUNT = 0
  [PASS] PRIMARY_PARTITION_EXTENTS_NON_OVERLAPPING = yes
  [PASS] ALL_USED_ENTRIES_WITHIN_USABLE_RANGE = yes
  [PASS] D3M2B_GPT_SECTOR_READ_COUNT = 33
  [PASS] LBA34_PLUS_READS = 0
  [PASS] PARTITION_CONTENT_READS = 0
  [PASS] FILESYSTEM_PROBES = 0
  [PASS] ROOTFS_SELECTION_PERFORMED = no
  [PASS] BACKUP_GPT_READ = no
  [PASS] ZERO_STORAGE_WRITES = yes
  [PASS] PRIMARY_GPT_PARTITION_MAP_VERIFIED = yes
  [PASS] D3_COMPLETE = no
  [PASS] PARSED_MAP_STORAGE = STATIC
  [PASS] GPT_USED_ENTRY_COUNT = 55 (matches host oracle)
  [PASS] GPT_UNUSED_ENTRY_COUNT = 73 (matches host oracle)
  HOST_USED_COUNT_ORACLE_DERIVED: yes

2. PARSED ENTRY COUNT & SLOT IDENTITY PARITY:
  [PASS] Exactly 55 used partition records extracted from XNU telemetry.
  [PASS] Used slot indices match exactly: [0, 1, 2, ..., 54]
  HOST_VS_XNU_USED_SLOT_INDICES_MATCH: yes

3. FIELD-BY-FIELD ACCURACY AUDIT ACROSS ALL 55 PARTITIONS:
  [PASS] ZERO field differences across all 55 used partitions!
  HOST_VS_XNU_PARTITION_NAME_RAW_MATCH: yes
  HOST_VS_XNU_PARTITION_NAME_CANONICAL_MATCH: yes
  HOST_VS_XNU_ALL_USED_FIELDS_MATCH: yes
  HOST_VS_XNU_PRIMARY_PARTITION_MAP_MATCH: yes

================================================================================
PHASE D3-M2B ACCEPTANCE: ALL AUDITS PASSED [SILICON CERTIFIED]
================================================================================
```

---

## Artifacts Produced
- `artifacts/reports/D3_M2B_PARTITION_MAP_REPORT.md`: Comprehensive formal milestone report with complete 55-partition table.
- `artifacts/oracles/gpt_primary_partition_map.json`: Frozen independent host oracle (SHA-256: `13ee706f681f640b...`).
- `scripts/host_gpt_entries_oracle.py`: Host oracle generation and bounds derivation script.
- `scripts/verify_d3m2b_acceptance.py`: Automated acceptance verifier script.
