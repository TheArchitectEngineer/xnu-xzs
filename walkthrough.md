# Phase D3-M2A Silicon Verification Walkthrough

## Milestone Objective
Phase D3-M2A: Read the 16,384-byte Primary GPT Partition Entry Array (LBA 2..33) from the physical Samsung BJNB4R eMMC storage using sequential `CMD17` single-block PIO transfers, compute the exact UEFI/IEEE 802.3 CRC32 checksum, verify it matches the Primary GPT Header's `PartitionEntryArrayCRC32` field, prove buffer immutability post-read, serialize the array as 32 hex chunks, and verify byte-for-byte identity against an independent host TWRP oracle.

---

## Changes Made
1. **Generic Physical Sector Reader (`xzs_emmc_read_sector_pio`):**
   - Refactored `xzs_emmc_read_sector_pio(uint32_t lba, uint64_t validated_sector_count, uint8_t out[512])` in `src/xnu/pexpert/arm/xzs_sdhci.c` and declared in `src/xnu/pexpert/pexpert/arm/xzs_sdhci.h`.
   - Fails closed if `validated_sector_count == 0` or `(uint64_t)lba >= validated_sector_count`.
   - Removed the milestone-specific `lba >= 2` boundary restriction.
2. **Phase D3-M2A Probe Routine (`xzs_sdhci_phase_d3m2a_probe`):**
   - Breadcrumb trace in namespace `CP = 0xD3D0` (`0x00` to `0x01`).
   - Hardware initialization replay from cold reset through CMD0, CMD1 (polling until ready), CMD2, CMD3 (RCA=2), CMD9, CMD7, and CMD8 (`EXT_CSD`).
   - Extracted live geometry: `live_sec_count = 61071360`, `live_last_physical_lba = 61071359`.
   - Fresh physical LBA 1 read & validation: "EFI PART", revision `0x00010000`, size 92, Header CRC32 `0xBFDF741D` (exact match).
   - Dynamic array geometry extracted: `128 * 128 = 16384` bytes, 32 sectors, bounds LBA 2..33.
   - Buffer capacity: static aligned 16 KiB buffer `g_xzs_gpt_primary_entries`.
   - Compact PIO read loop: 32 individual `CMD17` reads with ZERO console/UART dumping between transfers.
   - Audit read sequence proof: `GPT_ARRAY_READ_BITMAP = 0xFFFFFFFF`, 32 attempts, 32 successes, 0 duplicates, 0 missing, 0 out-of-range.
   - Exact CRC32 validation: computed `crc32(0, array, 16384)` yielding `0x64EDE0F4`, exact match with header field.
   - Immutability diagnostic: `CRC_A` (post-read) == `CRC_B` (pre-serialization) -> `GPT_PARTITION_ARRAY_BUFFER_MUTATED_AFTER_READ = no`.
   - Deterministic post-read chunk serialization: `GPT_ARRAY_CHUNK[00..31]` (512 bytes hex each).
   - Strict HARD STOP: zero partition decoding/enumeration, zero reads of LBA 34+, zero backup GPT reads, zero writes, warm reset to fastboot.
3. **Hooked in BSD Boot Pipeline:**
   - Hooked `xzs_sdhci_phase_d3m2a_probe()` in `src/xnu/bsd/kern/bsd_init.c`.
4. **Verification Tools & Independent Oracles:**
   - Captured physical oracle directly from eMMC via TWRP: `artifacts/oracles/mmcblk0_gpt_primary_entries.bin`.
   - Created `scripts/verify_d3m2a_acceptance.py` to parse ramoops log, reconstruct binary, and run automated acceptance checks.

---

## Silicon Validation Results

```text
================================================================================
PHASE D3-M2A: PRIMARY GPT PARTITION ENTRY ARRAY ACCEPTANCE AUDIT
================================================================================
Target Log File:     artifacts/logs/console-ramoops.log
Independent Oracle:  artifacts/oracles/mmcblk0_gpt_primary_entries.bin
Output Reconstructed:artifacts/builds/d3m2a_primary_entries.bin
--------------------------------------------------------------------------------

1. AUDITING REQUIRED TELEMETRY KEYS:
  [PASS] All 43 telemetry keys verified

2. EXTRACTING & VALIDATING 32 HEX CHUNKS:
  [PASS] Extracted exactly 32 chunks (00..31) with valid hex content.

3. RECONSTRUCTED BINARY AUDIT (artifacts/builds/d3m2a_primary_entries.bin):
  Byte size: 16384 (expected 16384)
  Calculated SHA-256: 8249f2bfa98ec675bf66b6342e29a0fbf6ab0ccb716c789a172f1f46b1828dd3
  Expected   SHA-256: 8249f2bfa98ec675bf66b6342e29a0fbf6ab0ccb716c789a172f1f46b1828dd3
  [PASS] SHA-256 exact match!
  Calculated CRC32:   0x64EDE0F4
  Expected   CRC32:   0x64EDE0F4
  [PASS] CRC32 exact match!

4. BYTE-FOR-BYTE COMPARISON AGAINST INDEPENDENT PHYSICAL ORACLE:
  [PASS] ZERO byte differences!
  D3M2A_ARRAY_BYTE_FOR_BYTE_MATCH: yes

================================================================================
PHASE D3-M2A ACCEPTANCE: ALL AUDITS PASSED [SILICON CERTIFIED]
================================================================================
```

Direct binary checks:
```bash
cmp artifacts/builds/d3m2a_primary_entries.bin artifacts/oracles/mmcblk0_gpt_primary_entries.bin
# Exit code 0 (identical)

sha256sum artifacts/builds/d3m2a_primary_entries.bin artifacts/oracles/mmcblk0_gpt_primary_entries.bin
8249f2bfa98ec675bf66b6342e29a0fbf6ab0ccb716c789a172f1f46b1828dd3  artifacts/builds/d3m2a_primary_entries.bin
8249f2bfa98ec675bf66b6342e29a0fbf6ab0ccb716c789a172f1f46b1828dd3  artifacts/oracles/mmcblk0_gpt_primary_entries.bin
```

---

## Artifacts Produced
- `artifacts/reports/D3_M2A_ARRAY_READ_REPORT.md`: Comprehensive formal milestone report.
- `artifacts/oracles/mmcblk0_gpt_primary_entries.bin`: Physical 16-KiB TWRP oracle.
- `artifacts/builds/d3m2a_primary_entries.bin`: Exact 16-KiB binary reconstructed from XNU silicon execution.
- `scripts/verify_d3m2a_acceptance.py`: Automated acceptance verifier script.
