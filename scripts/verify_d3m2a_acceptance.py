#!/usr/bin/env python3
"""
verify_d3m2a_acceptance.py - Phase D3-M2A Automated Acceptance Verifier

Performs strict audit and byte-for-byte verification of the Primary GPT
Partition Entry Array captured by XNU against the independent physical TWRP oracle.
"""

import sys
import os
import re
import hashlib
import zlib

EXPECTED_SHA256 = "8249f2bfa98ec675bf66b6342e29a0fbf6ab0ccb716c789a172f1f46b1828dd3"
EXPECTED_CRC32 = 0x64EDE0F4
EXPECTED_BYTES = 16384
EXPECTED_SECTORS = 32

REQUIRED_TELEMETRY = [
    ("D3_BRANCH", "xzs-d3-gpt"),
    ("D3_BRANCH_BASE", "20cdf4a2c86f1b9eac7573479025ac618b505e84"),
    ("ORACLE_INDEPENDENCE", "yes"),
    ("GENERIC_SECTOR_PRIMITIVE", "yes"),
    ("LIVE_GEOMETRY_VALID", "yes"),
    ("LIVE_SEC_COUNT", "61071360"),
    ("LIVE_LAST_PHYSICAL_LBA", "61071359"),
    ("EXT_CSD_GEOMETRY_MATCH", "yes"),
    ("FRESH_PRIMARY_HEADER_VERIFIED", "yes"),
    ("PRIMARY_GPT_HEADER_VERIFIED", "yes"),
    ("ARRAY_GEOMETRY_DERIVED_FROM_HEADER", "yes"),
    ("ARRAY_BUFFER_CAPACITY", "16384"),
    ("RUNTIME_ARRAY_BYTES", "16384"),
    ("RUNTIME_ARRAY_SECTORS", "32"),
    ("ARRAY_FIRST_LBA", "2"),
    ("ARRAY_LAST_LBA", "33"),
    ("D3M2A_ARRAY_SECTOR_READ_COUNT", "32"),
    ("GPT_ARRAY_READ_ATTEMPTS", "32"),
    ("GPT_ARRAY_READ_SUCCESS", "32"),
    ("GPT_ARRAY_READ_BITMAP", "0xFFFFFFFF"),
    ("GPT_ARRAY_DUPLICATE_READS", "0"),
    ("GPT_ARRAY_MISSING_READS", "0"),
    ("GPT_ARRAY_OUT_OF_RANGE_READS", "0"),
    ("GPT_ARRAY_CRC_BYTE_COUNT", "16384"),
    ("GPT_ARRAY_CRC_COVERS_PADDING", "no"),
    ("ARRAY_POST_READ_CRC32_INITIAL", "0x64EDE0F4"),
    ("ARRAY_POST_VALIDATION_CRC32", "0x64EDE0F4"),
    ("GPT_PARTITION_ARRAY_BUFFER_MUTATED_AFTER_READ", "no"),
    ("GPT_PARTITION_ARRAY_CRC32_STORED", "0x64EDE0F4"),
    ("GPT_PARTITION_ARRAY_CRC32_CALCULATED", "0x64EDE0F4"),
    ("GPT_PARTITION_ARRAY_CRC32_MATCH", "yes"),
    ("GPT_PARTITION_ARRAY_CRC32_VERIFIED", "yes"),
    ("PARTITION_ARRAY_CONTENT_READ", "yes"),
    ("PARTITION_ENUMERATION_PERFORMED", "no"),
    ("PARTITION_ENTRIES_DECODED", "0"),
    ("BACKUP_GPT_READ", "no"),
    ("ZERO_LBA34_PLUS_READS", "yes"),
    ("ZERO_STORAGE_WRITES", "yes"),
    ("PRIMARY_GPT_PARTITION_ARRAY_VERIFIED", "yes"),
    ("D3_COMPLETE", "no"),
    ("ARRAY_DUMP_OCCURRED_AFTER_ALL_READS", "yes"),
    ("ARRAY_DUMP_CHUNK_COUNT", "32"),
    ("ARRAY_DUMP_BYTES_PER_CHUNK", "512"),
]

def main():
    log_file = sys.argv[1] if len(sys.argv) > 1 else "artifacts/logs/console-ramoops.log"
    oracle_file = "artifacts/oracles/mmcblk0_gpt_primary_entries.bin"
    output_bin = "artifacts/builds/d3m2a_primary_entries.bin"

    print("================================================================================")
    print("PHASE D3-M2A: PRIMARY GPT PARTITION ENTRY ARRAY ACCEPTANCE AUDIT")
    print("================================================================================")
    print(f"Target Log File:     {log_file}")
    print(f"Independent Oracle:  {oracle_file}")
    print(f"Output Reconstructed:{output_bin}")
    print("--------------------------------------------------------------------------------")

    if not os.path.exists(log_file):
        print(f"[FAIL] Log file not found: {log_file}")
        sys.exit(1)

    with open(log_file, "r", errors="ignore") as f:
        log_content = f.read()

    # Step 1: Check Telemetry Block
    start_marker = "=== D3M2A TELEMETRY START ==="
    end_marker = "=== D3M2A TELEMETRY END ==="

    if start_marker not in log_content or end_marker not in log_content:
        print("[FAIL] Missing D3M2A telemetry markers in log!")
        sys.exit(1)

    telemetry_block = log_content.split(start_marker)[1].split(end_marker)[0]

    # Parse telemetry key-values
    telemetry = {}
    for line in telemetry_block.splitlines():
        line = line.strip().replace("\r", "")
        if "=" in line and not line.startswith("GPT_ARRAY_CHUNK["):
            k, v = line.split("=", 1)
            telemetry[k.strip()] = v.strip()

    telemetry_pass = True
    print("\n1. AUDITING REQUIRED TELEMETRY KEYS:")
    for k, expected_v in REQUIRED_TELEMETRY:
        actual_v = telemetry.get(k)
        if actual_v is None:
            print(f"  [FAIL] Missing key: {k}")
            telemetry_pass = False
        elif actual_v.upper() != expected_v.upper():
            print(f"  [FAIL] Key {k}: expected '{expected_v}', got '{actual_v}'")
            telemetry_pass = False
        else:
            print(f"  [PASS] {k} = {actual_v}")

    if not telemetry_pass:
        print("\n[FAIL] Telemetry audit failed!")
        sys.exit(1)

    print("\n2. EXTRACTING & VALIDATING 32 HEX CHUNKS:")
    chunks = {}
    chunk_pattern = re.compile(r"GPT_ARRAY_CHUNK\[(\d{2})\]=([0-9a-fA-F]+)")
    for line in telemetry_block.splitlines():
        line = line.strip().replace("\r", "")
        m = chunk_pattern.match(line)
        if m:
            idx = int(m.group(1))
            hex_data = m.group(2)
            if idx in chunks:
                print(f"  [FAIL] Duplicate chunk index: {idx}")
                sys.exit(1)
            if len(hex_data) != 1024:
                print(f"  [FAIL] Chunk {idx} length is {len(hex_data)}, expected 1024 hex chars!")
                sys.exit(1)
            chunks[idx] = bytes.fromhex(hex_data)

    if len(chunks) != EXPECTED_SECTORS:
        print(f"  [FAIL] Expected {EXPECTED_SECTORS} chunks, found {len(chunks)}!")
        sys.exit(1)

    for i in range(EXPECTED_SECTORS):
        if i not in chunks:
            print(f"  [FAIL] Missing chunk {i:02d}!")
            sys.exit(1)

    print(f"  [PASS] Extracted exactly {len(chunks)} chunks (00..31) with valid hex content.")

    # Step 3: Reconstruct Binary Array
    os.makedirs(os.path.dirname(output_bin), exist_ok=True)
    reconstructed = b"".join(chunks[i] for i in range(EXPECTED_SECTORS))
    with open(output_bin, "wb") as f:
        f.write(reconstructed)

    print(f"\n3. RECONSTRUCTED BINARY AUDIT ({output_bin}):")
    print(f"  Byte size: {len(reconstructed)} (expected {EXPECTED_BYTES})")
    if len(reconstructed) != EXPECTED_BYTES:
        print(f"  [FAIL] Wrong size!")
        sys.exit(1)

    calc_sha256 = hashlib.sha256(reconstructed).hexdigest()
    calc_crc32 = zlib.crc32(reconstructed) & 0xFFFFFFFF

    print(f"  Calculated SHA-256: {calc_sha256}")
    print(f"  Expected   SHA-256: {EXPECTED_SHA256}")
    if calc_sha256 != EXPECTED_SHA256:
        print("  [FAIL] SHA-256 mismatch!")
        sys.exit(1)
    print("  [PASS] SHA-256 exact match!")

    print(f"  Calculated CRC32:   0x{calc_crc32:08X}")
    print(f"  Expected   CRC32:   0x{EXPECTED_CRC32:08X}")
    if calc_crc32 != EXPECTED_CRC32:
        print("  [FAIL] CRC32 mismatch!")
        sys.exit(1)
    print("  [PASS] CRC32 exact match!")

    # Step 4: Compare Against Independent Physical TWRP Oracle
    print("\n4. BYTE-FOR-BYTE COMPARISON AGAINST INDEPENDENT PHYSICAL ORACLE:")
    if not os.path.exists(oracle_file):
        print(f"  [FAIL] Oracle file not found: {oracle_file}")
        sys.exit(1)

    with open(oracle_file, "rb") as f:
        oracle_data = f.read()

    if len(oracle_data) != len(reconstructed):
        print(f"  [FAIL] Size difference: reconstructed={len(reconstructed)}, oracle={len(oracle_data)}")
        sys.exit(1)

    diff_count = 0
    for byte_i, (b_rec, b_orc) in enumerate(zip(reconstructed, oracle_data)):
        if b_rec != b_orc:
            if diff_count < 10:
                print(f"  [DIFF] Offset 0x{byte_i:04X}: reconstructed=0x{b_rec:02X}, oracle=0x{b_orc:02X}")
            diff_count += 1

    if diff_count > 0:
        print(f"  [FAIL] Found {diff_count} byte differences!")
        sys.exit(1)

    print("  [PASS] ZERO byte differences!")
    print("  D3M2A_ARRAY_BYTE_FOR_BYTE_MATCH: yes")

    print("\n================================================================================")
    print("PHASE D3-M2A ACCEPTANCE: ALL AUDITS PASSED [SILICON CERTIFIED]")
    print("================================================================================")

if __name__ == "__main__":
    main()
