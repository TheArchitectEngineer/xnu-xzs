#!/usr/bin/env python3
"""
verify_d3m3_acceptance.py - Phase D3-M3 Acceptance Verifier

Performs automated parity comparison between XNU Backup GPT telemetry
and frozen independent host oracles.
Reconstructs artifacts/builds/d3m3_backup_header.bin and d3m3_backup_entries.bin
and proves byte-for-byte identity.
"""

import sys
import os
import re
import json
import hashlib

HOST_BACKUP_HEADER_ORACLE = "artifacts/oracles/mmcblk0_gpt_backup_header.bin"
HOST_BACKUP_ENTRIES_ORACLE = "artifacts/oracles/mmcblk0_gpt_backup_entries.bin"
HOST_BACKUP_MAP_JSON = "artifacts/oracles/gpt_backup_partition_map.json"

OUT_RECONSTRUCTED_HEADER = "artifacts/builds/d3m3_backup_header.bin"
OUT_RECONSTRUCTED_ENTRIES = "artifacts/builds/d3m3_backup_entries.bin"

REQUIRED_TELEMETRY = [
    ("D3_BRANCH", "xzs-d3-gpt"),
    ("D3_BRANCH_BASE", "20cdf4a2c86f1b9eac7573479025ac618b505e84"),
    ("ORACLE_INDEPENDENCE", "yes"),
    ("LIVE_SEC_COUNT_DERIVED_FROM_EXT_CSD", "yes"),
    ("LIVE_SEC_COUNT", "61071360"),
    ("LIVE_LAST_PHYSICAL_LBA", "61071359"),
    ("DEVICE_GEOMETRY_ORACLE_MATCH", "yes"),
    ("FRESH_PRIMARY_HEADER_VERIFIED", "yes"),
    ("PRIMARY_GPT_HEADER_VERIFIED", "yes"),
    ("FRESH_PRIMARY_ARRAY_CRC_VERIFIED", "yes"),
    ("PRIMARY_GPT_PARTITION_ARRAY_VERIFIED", "yes"),
    ("PRIMARY_GPT_PARTITION_MAP_VERIFIED", "yes"),
    ("PRIMARY_HEADER_LBA", "1"),
    ("PRIMARY_ARRAY_FIRST_LBA", "2"),
    ("PRIMARY_ARRAY_LAST_LBA", "33"),
    ("PRIMARY_ARRAY_BYTES", "16384"),
    ("PRIMARY_ARRAY_CRC32", "0x64EDE0F4"),
    ("BACKUP_GPT_HEADER_LBA", "61071359"),
    ("BACKUP_GPT_SIGNATURE_VALID", "yes"),
    ("BACKUP_GPT_HEADER_SIZE_STRUCTURALLY_VALID", "yes"),
    ("BACKUP_GPT_RESERVED_ZERO", "yes"),
    ("BACKUP_GPT_POST_HEADER_RESERVED_ZERO", "yes"),
    ("BACKUP_GPT_HEADER_CRC32_MATCH", "yes"),
    ("BACKUP_GPT_HEADER_VERIFIED", "yes"),
    ("GPT_HEADER_RECIPROCAL_LINKS_VALID", "yes"),
    ("PRIMARY_BACKUP_REVISION_MATCH", "yes"),
    ("PRIMARY_BACKUP_USABLE_RANGE_MATCH", "yes"),
    ("PRIMARY_BACKUP_DISK_GUID_MATCH", "yes"),
    ("PRIMARY_BACKUP_ENTRY_COUNT_MATCH", "yes"),
    ("PRIMARY_BACKUP_ENTRY_SIZE_MATCH", "yes"),
    ("PRIMARY_BACKUP_ARRAY_CRC_FIELD_MATCH", "yes"),
    ("XNU_PRIMARY_BACKUP_HEADER_RELATION_VALID", "yes"),
    ("BACKUP_ARRAY_FIRST_LBA", "61071327"),
    ("BACKUP_ARRAY_LAST_LBA", "61071358"),
    ("BACKUP_ARRAY_SECTORS", "32"),
    ("BACKUP_ARRAY_BYTES", "16384"),
    ("BACKUP_ARRAY_IMMEDIATELY_PRECEDES_HEADER", "yes"),
    ("BACKUP_ARRAY_BOUNDS_VALID", "yes"),
    ("BACKUP_ARRAY_READ_ATTEMPTS", "32"),
    ("BACKUP_ARRAY_READ_SUCCESS", "32"),
    ("BACKUP_ARRAY_DUPLICATE_READS", "0"),
    ("BACKUP_ARRAY_MISSING_READS", "0"),
    ("BACKUP_ARRAY_OUT_OF_RANGE_READS", "0"),
    ("BACKUP_GPT_PARTITION_ARRAY_CRC32_MATCH", "yes"),
    ("BACKUP_GPT_PARTITION_ARRAY_VERIFIED", "yes"),
    ("BACKUP_ARRAY_CRC32", "0x64EDE0F4"),
    ("XNU_PRIMARY_BACKUP_ARRAY_BYTE_MATCH", "yes"),
    ("BACKUP_GPT_ENTRY_SLOTS_PARSED", "128"),
    ("BACKUP_GPT_USED_ENTRY_COUNT", "55"),
    ("BACKUP_GPT_UNUSED_ENTRY_COUNT", "73"),
    ("KERNEL_USED_COUNT_HARDCODED", "no"),
    ("PRIMARY_BACKUP_USED_SLOT_INDICES_MATCH", "yes"),
    ("PRIMARY_BACKUP_ALL_USED_FIELDS_MATCH", "yes"),
    ("PRIMARY_BACKUP_PARTITION_MAP_MATCH", "yes"),
    ("AUTHORITATIVE_GPT_PARTITION_MAP_VERIFIED", "yes"),
    ("PRIMARY_BACKUP_GPT_CONSISTENT", "yes"),
    ("D3M3_GPT_SECTOR_READ_COUNT", "65"),
    ("PARTITION_CONTENT_READS", "0"),
    ("FILESYSTEM_PROBES", "0"),
    ("ROOTFS_SELECTION_PERFORMED", "no"),
    ("ZERO_STORAGE_WRITES", "yes"),
    ("D3_M1_COMPLETE", "yes"),
    ("D3_M2A_COMPLETE", "yes"),
    ("D3_M2B_COMPLETE", "yes"),
    ("D3_M3_COMPLETE", "yes"),
    ("D3_COMPLETE", "yes"),
    ("PARSED_MAP_STORAGE", "STATIC"),
]

def main():
    log_file = sys.argv[1] if len(sys.argv) > 1 else "artifacts/logs/console-ramoops.log"

    print("================================================================================")
    print("PHASE D3-M3: BACKUP GPT VERIFICATION & AUTHORITATIVE SEAL ACCEPTANCE AUDIT")
    print("================================================================================")
    print(f"Target Log File:               {log_file}")
    print(f"Host Backup Header Oracle:     {HOST_BACKUP_HEADER_ORACLE}")
    print(f"Host Backup Entries Oracle:    {HOST_BACKUP_ENTRIES_ORACLE}")
    print(f"Host Backup Partition Map:     {HOST_BACKUP_MAP_JSON}")
    print("--------------------------------------------------------------------------------")

    for req_file in [log_file, HOST_BACKUP_HEADER_ORACLE, HOST_BACKUP_ENTRIES_ORACLE, HOST_BACKUP_MAP_JSON]:
        if not os.path.exists(req_file):
            print(f"[FAIL] Required file not found: {req_file}")
            sys.exit(1)

    with open(log_file, "r", errors="ignore") as f:
        log_content = f.read()

    start_marker = "=== D3M3 TELEMETRY START ==="
    end_marker = "=== D3M3 TELEMETRY END ==="

    if start_marker not in log_content or end_marker not in log_content:
        print("[FAIL] Missing D3M3 telemetry markers in log!")
        sys.exit(1)

    telemetry_block = log_content.split(start_marker)[1].split(end_marker)[0]

    telemetry = {}
    xnu_entries = {}
    header_hex_rows = {}
    array_chunks = {}

    entry_pattern = re.compile(r"BACKUP_ENTRY\[(\d+)\].([A-Z_]+)=(.*)")
    hdr_pattern = re.compile(r"BACKUP_HEADER_HEX\[(\d+)\]=([0-9a-fA-F]+)")
    chunk_pattern = re.compile(r"BACKUP_ARRAY_CHUNK\[(\d+)\]=([0-9a-fA-F]+)")

    for line in telemetry_block.splitlines():
        line = line.strip().replace("\r", "")
        m_entry = entry_pattern.match(line)
        m_hdr = hdr_pattern.match(line)
        m_chunk = chunk_pattern.match(line)

        if m_entry:
            entry_idx = int(m_entry.group(1))
            field = m_entry.group(2)
            val = m_entry.group(3).strip()
            if entry_idx not in xnu_entries:
                xnu_entries[entry_idx] = {}
            xnu_entries[entry_idx][field] = val
        elif m_hdr:
            row_idx = int(m_hdr.group(1))
            header_hex_rows[row_idx] = bytes.fromhex(m_hdr.group(2).strip())
        elif m_chunk:
            chunk_idx = int(m_chunk.group(1))
            array_chunks[chunk_idx] = bytes.fromhex(m_chunk.group(2).strip())
        elif "=" in line:
            k, v = line.split("=", 1)
            telemetry[k.strip()] = v.strip()

    print("\n1. AUDITING REQUIRED TELEMETRY KEYS:")
    telemetry_pass = True
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

    print("\n2. RECONSTRUCTING & AUDITING BACKUP GPT HEADER:")
    if len(header_hex_rows) != 32:
        print(f"  [FAIL] Expected 32 header rows, got {len(header_hex_rows)}")
        sys.exit(1)

    reconstructed_header = bytearray()
    for row_idx in range(32):
        if row_idx not in header_hex_rows:
            print(f"  [FAIL] Missing header row {row_idx}")
            sys.exit(1)
        reconstructed_header.extend(header_hex_rows[row_idx])

    if len(reconstructed_header) != 512:
        print(f"  [FAIL] Reconstructed header size is {len(reconstructed_header)}, expected 512")
        sys.exit(1)

    with open(OUT_RECONSTRUCTED_HEADER, "wb") as f:
        f.write(reconstructed_header)

    with open(HOST_BACKUP_HEADER_ORACLE, "rb") as f:
        host_hdr_bytes = f.read()

    hdr_calc_sha256 = hashlib.sha256(reconstructed_header).hexdigest()
    hdr_host_sha256 = hashlib.sha256(host_hdr_bytes).hexdigest()

    print(f"  Reconstructed Header Size:   {len(reconstructed_header)} bytes")
    print(f"  Reconstructed Header SHA256: {hdr_calc_sha256}")
    print(f"  Host Oracle Header SHA256:   {hdr_host_sha256}")

    if hdr_calc_sha256 != hdr_host_sha256 or reconstructed_header != host_hdr_bytes:
        print("  [FAIL] Reconstructed Backup Header does not match host oracle!")
        sys.exit(1)

    print("  [PASS] D3M3_BACKUP_HEADER_BYTE_FOR_BYTE_MATCH: yes")
    print("  HOST_VS_XNU_BACKUP_HEADER_MATCH: yes")

    print("\n3. RECONSTRUCTING & AUDITING BACKUP PARTITION ENTRY ARRAY:")
    if len(array_chunks) != 32:
        print(f"  [FAIL] Expected 32 array chunks, got {len(array_chunks)}")
        sys.exit(1)

    reconstructed_array = bytearray()
    for c_idx in range(32):
        if c_idx not in array_chunks:
            print(f"  [FAIL] Missing chunk {c_idx}")
            sys.exit(1)
        reconstructed_array.extend(array_chunks[c_idx])

    if len(reconstructed_array) != 16384:
        print(f"  [FAIL] Reconstructed array size is {len(reconstructed_array)}, expected 16384")
        sys.exit(1)

    with open(OUT_RECONSTRUCTED_ENTRIES, "wb") as f:
        f.write(reconstructed_array)

    with open(HOST_BACKUP_ENTRIES_ORACLE, "rb") as f:
        host_array_bytes = f.read()

    arr_calc_sha256 = hashlib.sha256(reconstructed_array).hexdigest()
    arr_host_sha256 = hashlib.sha256(host_array_bytes).hexdigest()

    print(f"  Reconstructed Array Size:    {len(reconstructed_array)} bytes")
    print(f"  Reconstructed Array SHA256:  {arr_calc_sha256}")
    print(f"  Host Oracle Array SHA256:    {arr_host_sha256}")

    if arr_calc_sha256 != arr_host_sha256 or reconstructed_array != host_array_bytes:
        print("  [FAIL] Reconstructed Backup Array does not match host oracle!")
        sys.exit(1)

    print("  [PASS] D3M3_BACKUP_ARRAY_BYTE_FOR_BYTE_MATCH: yes")
    print("  HOST_VS_XNU_BACKUP_ARRAY_MATCH: yes")

    print("\n4. FIELD-BY-FIELD ACCURACY AUDIT ACROSS ALL 55 BACKUP PARTITIONS:")
    with open(HOST_BACKUP_MAP_JSON, "r") as f:
        oracle_map_data = json.load(f)

    oracle_used = oracle_map_data["used_partitions"]
    if len(xnu_entries) != len(oracle_used):
        print(f"  [FAIL] Used count mismatch: XNU={len(xnu_entries)}, Oracle={len(oracle_used)}")
        sys.exit(1)

    xnu_by_slot = {}
    for idx, e in xnu_entries.items():
        slot = int(e.get("SLOT", "-1"))
        xnu_by_slot[slot] = e

    oracle_by_slot = {e["slot_index"]: e for e in oracle_used}
    oracle_slots = sorted(list(oracle_by_slot.keys()))
    xnu_slots = sorted(list(xnu_by_slot.keys()))

    if xnu_slots != oracle_slots:
        print("  [FAIL] Used slot indices mismatch!")
        sys.exit(1)

    field_diffs = 0
    for slot in oracle_slots:
        o = oracle_by_slot[slot]
        x = xnu_by_slot[slot]

        if x.get("TYPE_GUID_RAW", "").lower() != o["type_guid_raw_hex"].lower():
            field_diffs += 1
        if x.get("TYPE_GUID", "").lower() != o["type_guid_formatted"].lower():
            field_diffs += 1
        if x.get("UNIQUE_GUID_RAW", "").lower() != o["unique_guid_raw_hex"].lower():
            field_diffs += 1
        if x.get("UNIQUE_GUID", "").lower() != o["unique_guid_formatted"].lower():
            field_diffs += 1
        if int(x.get("START_LBA", "-1")) != o["start_lba"]:
            field_diffs += 1
        if int(x.get("END_LBA", "-1")) != o["end_lba"]:
            field_diffs += 1
        if int(x.get("SECTORS", "-1")) != o["sector_count"]:
            field_diffs += 1
        if int(x.get("SIZE_BYTES", "-1")) != o["size_bytes"]:
            field_diffs += 1
        if x.get("ATTR", "").lower() != o["attributes_raw_hex"].lower():
            field_diffs += 1
        if x.get("NAME_RAW", "").lower() != o["name_raw_hex"].lower():
            field_diffs += 1
        if x.get("NAME", "") != o["name_canonical"]:
            field_diffs += 1

    if field_diffs > 0:
        print(f"  [FAIL] Found {field_diffs} field differences in backup map!")
        sys.exit(1)

    print(f"  [PASS] ZERO field differences across all {len(oracle_slots)} used backup partitions!")
    print("  HOST_VS_XNU_BACKUP_PARTITION_MAP_MATCH: yes")

    print("\n================================================================================")
    print("PHASE D3-M3 ACCEPTANCE: ALL AUDITS PASSED [AUTHORITATIVE SEAL CERTIFIED]")
    print("================================================================================")

if __name__ == "__main__":
    main()
