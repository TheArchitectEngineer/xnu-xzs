#!/usr/bin/env python3
"""
verify_d3m2b_acceptance.py - Phase D3-M2B Acceptance Verifier

Performs automated parity comparison between XNU Primary GPT Partition Map telemetry
and the frozen independent host oracle (artifacts/oracles/gpt_primary_partition_map.json).
"""

import sys
import os
import re
import json
import hashlib

ORACLE_JSON = "artifacts/oracles/gpt_primary_partition_map.json"

REQUIRED_TELEMETRY = [
    ("D3_BRANCH", "xzs-d3-gpt"),
    ("D3_BRANCH_BASE", "20cdf4a2c86f1b9eac7573479025ac618b505e84"),
    ("ORACLE_INDEPENDENCE", "yes"),
    ("LIVE_SEC_COUNT_DERIVED_FROM_EXT_CSD", "yes"),
    ("LIVE_SEC_COUNT", "61071360"),
    ("DEVICE_GEOMETRY_ORACLE_MATCH", "yes"),
    ("FRESH_PRIMARY_HEADER_VERIFIED", "yes"),
    ("PRIMARY_GPT_HEADER_VERIFIED", "yes"),
    ("FRESH_PRIMARY_ARRAY_CRC_VERIFIED", "yes"),
    ("PRIMARY_GPT_PARTITION_ARRAY_VERIFIED", "yes"),
    ("GPT_PARTITION_ARRAY_CRC32_VERIFIED", "yes"),
    ("KERNEL_GPT_CRC_ORACLE_HARDCODED", "no"),
    ("ENTRY_COUNT_DERIVED_FROM_HEADER", "yes"),
    ("ENTRY_SIZE_DERIVED_FROM_HEADER", "yes"),
    ("RUNTIME_NUM_ENTRIES", "128"),
    ("RUNTIME_ENTRY_SIZE", "128"),
    ("PARTITION_ENTRY_BOUNDS_CHECKS", "yes"),
    ("ENTRY_RESERVED_TAIL_BYTES_PER_SLOT", "0"),
    ("PARTITION_PARSER_ENTERED", "yes"),
    ("GPT_ENTRY_SLOTS_PARSED", "128"),
    ("KERNEL_USED_COUNT_HARDCODED", "no"),
    ("USED_UNIQUE_GUID_ZERO_COUNT", "0"),
    ("DUPLICATE_UNIQUE_GUID_COUNT", "0"),
    ("UNIQUE_PARTITION_GUIDS_VALID", "yes"),
    ("INVALID_USED_EXTENT_COUNT", "0"),
    ("PARTITION_SIZE_ARITHMETIC_OVERFLOW_COUNT", "0"),
    ("OVERLAPPING_PARTITION_PAIR_COUNT", "0"),
    ("PRIMARY_PARTITION_EXTENTS_NON_OVERLAPPING", "yes"),
    ("ALL_USED_ENTRIES_WITHIN_USABLE_RANGE", "yes"),
    ("D3M2B_GPT_SECTOR_READ_COUNT", "33"),
    ("LBA34_PLUS_READS", "0"),
    ("PARTITION_CONTENT_READS", "0"),
    ("FILESYSTEM_PROBES", "0"),
    ("ROOTFS_SELECTION_PERFORMED", "no"),
    ("BACKUP_GPT_READ", "no"),
    ("ZERO_STORAGE_WRITES", "yes"),
    ("PRIMARY_GPT_PARTITION_MAP_VERIFIED", "yes"),
    ("D3_COMPLETE", "no"),
    ("PARSED_MAP_STORAGE", "STATIC"),
]

def main():
    log_file = sys.argv[1] if len(sys.argv) > 1 else "artifacts/logs/console-ramoops.log"

    print("================================================================================")
    print("PHASE D3-M2B: PRIMARY GPT PARTITION MAP ACCEPTANCE AUDIT")
    print("================================================================================")
    print(f"Target Log File:        {log_file}")
    print(f"Frozen Host Oracle JSON:{ORACLE_JSON}")
    print("--------------------------------------------------------------------------------")

    if not os.path.exists(log_file):
        print(f"[FAIL] Log file not found: {log_file}")
        sys.exit(1)

    if not os.path.exists(ORACLE_JSON):
        print(f"[FAIL] Frozen host oracle JSON not found: {ORACLE_JSON}")
        sys.exit(1)

    with open(ORACLE_JSON, "r") as f:
        oracle_data = json.load(f)

    oracle_used = oracle_data["used_partitions"]
    oracle_metadata = oracle_data["metadata"]

    with open(log_file, "r", errors="ignore") as f:
        log_content = f.read()

    start_marker = "=== D3M2B TELEMETRY START ==="
    end_marker = "=== D3M2B TELEMETRY END ==="

    if start_marker not in log_content or end_marker not in log_content:
        print("[FAIL] Missing D3M2B telemetry markers in log!")
        sys.exit(1)

    telemetry_block = log_content.split(start_marker)[1].split(end_marker)[0]

    telemetry = {}
    xnu_entries = {}
    entry_pattern = re.compile(r"GPT_ENTRY\[(\d+)\].([A-Z_]+)=(.*)")

    for line in telemetry_block.splitlines():
        line = line.strip().replace("\r", "")
        m = entry_pattern.match(line)
        if m:
            entry_idx = int(m.group(1))
            field = m.group(2)
            val = m.group(3).strip()
            if entry_idx not in xnu_entries:
                xnu_entries[entry_idx] = {}
            xnu_entries[entry_idx][field] = val
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

    # Dynamic used/unused count checks
    xnu_used_count = int(telemetry.get("GPT_USED_ENTRY_COUNT", "-1"))
    xnu_unused_count = int(telemetry.get("GPT_UNUSED_ENTRY_COUNT", "-1"))
    host_used_count = oracle_metadata["host_used_entry_count"]
    host_unused_count = oracle_metadata["host_unused_entry_count"]

    if xnu_used_count != host_used_count:
        print(f"  [FAIL] Used count mismatch: XNU={xnu_used_count}, Host={host_used_count}")
        telemetry_pass = False
    else:
        print(f"  [PASS] GPT_USED_ENTRY_COUNT = {xnu_used_count} (matches host oracle)")

    if xnu_unused_count != host_unused_count:
        print(f"  [FAIL] Unused count mismatch: XNU={xnu_unused_count}, Host={host_unused_count}")
        telemetry_pass = False
    else:
        print(f"  [PASS] GPT_UNUSED_ENTRY_COUNT = {xnu_unused_count} (matches host oracle)")
        print("  HOST_USED_COUNT_ORACLE_DERIVED: yes")

    if not telemetry_pass:
        print("\n[FAIL] Telemetry audit failed!")
        sys.exit(1)

    print("\n2. PARSED ENTRY COUNT & SLOT IDENTITY PARITY:")
    if len(xnu_entries) != len(oracle_used):
        print(f"  [FAIL] Count mismatch: XNU has {len(xnu_entries)} records, Oracle has {len(oracle_used)}")
        sys.exit(1)

    print(f"  [PASS] Exactly {len(xnu_entries)} used partition records extracted from XNU telemetry.")

    # Match entries by actual GPT slot index
    xnu_by_slot = {}
    for idx, e in xnu_entries.items():
        slot = int(e.get("SLOT", "-1"))
        if slot in xnu_by_slot:
            print(f"  [FAIL] Duplicate slot in XNU entries: {slot}")
            sys.exit(1)
        xnu_by_slot[slot] = e

    oracle_by_slot = {e["slot_index"]: e for e in oracle_used}

    xnu_slots = sorted(list(xnu_by_slot.keys()))
    oracle_slots = sorted(list(oracle_by_slot.keys()))

    if xnu_slots != oracle_slots:
        print(f"  [FAIL] Slot indices mismatch!")
        print(f"  XNU slots:    {xnu_slots}")
        print(f"  Oracle slots: {oracle_slots}")
        sys.exit(1)

    print(f"  [PASS] Used slot indices match exactly: {xnu_slots}")
    print("  HOST_VS_XNU_USED_SLOT_INDICES_MATCH: yes")

    print("\n3. FIELD-BY-FIELD ACCURACY AUDIT ACROSS ALL 55 PARTITIONS:")
    field_diffs = 0

    for slot in oracle_slots:
        o = oracle_by_slot[slot]
        x = xnu_by_slot[slot]

        # 1. Type GUID raw & formatted
        if x.get("TYPE_GUID_RAW", "").lower() != o["type_guid_raw_hex"].lower():
            print(f"  [DIFF Slot {slot:02d}] TYPE_GUID_RAW: XNU={x.get('TYPE_GUID_RAW')} Oracle={o['type_guid_raw_hex']}")
            field_diffs += 1
        if x.get("TYPE_GUID", "").lower() != o["type_guid_formatted"].lower():
            print(f"  [DIFF Slot {slot:02d}] TYPE_GUID: XNU={x.get('TYPE_GUID')} Oracle={o['type_guid_formatted']}")
            field_diffs += 1

        # 2. Unique GUID raw & formatted
        if x.get("UNIQUE_GUID_RAW", "").lower() != o["unique_guid_raw_hex"].lower():
            print(f"  [DIFF Slot {slot:02d}] UNIQUE_GUID_RAW: XNU={x.get('UNIQUE_GUID_RAW')} Oracle={o['unique_guid_raw_hex']}")
            field_diffs += 1
        if x.get("UNIQUE_GUID", "").lower() != o["unique_guid_formatted"].lower():
            print(f"  [DIFF Slot {slot:02d}] UNIQUE_GUID: XNU={x.get('UNIQUE_GUID')} Oracle={o['unique_guid_formatted']}")
            field_diffs += 1

        # 3. Start & End LBA
        if int(x.get("START_LBA", "-1")) != o["start_lba"]:
            print(f"  [DIFF Slot {slot:02d}] START_LBA: XNU={x.get('START_LBA')} Oracle={o['start_lba']}")
            field_diffs += 1
        if int(x.get("END_LBA", "-1")) != o["end_lba"]:
            print(f"  [DIFF Slot {slot:02d}] END_LBA: XNU={x.get('END_LBA')} Oracle={o['end_lba']}")
            field_diffs += 1

        # 4. Sector count & size bytes
        if int(x.get("SECTORS", "-1")) != o["sector_count"]:
            print(f"  [DIFF Slot {slot:02d}] SECTORS: XNU={x.get('SECTORS')} Oracle={o['sector_count']}")
            field_diffs += 1
        if int(x.get("SIZE_BYTES", "-1")) != o["size_bytes"]:
            print(f"  [DIFF Slot {slot:02d}] SIZE_BYTES: XNU={x.get('SIZE_BYTES')} Oracle={o['size_bytes']}")
            field_diffs += 1

        # 5. Attributes raw
        if x.get("ATTR", "").lower() != o["attributes_raw_hex"].lower():
            print(f"  [DIFF Slot {slot:02d}] ATTR: XNU={x.get('ATTR')} Oracle={o['attributes_raw_hex']}")
            field_diffs += 1

        # 6. Partition name raw hex & canonical
        if x.get("NAME_RAW", "").lower() != o["name_raw_hex"].lower():
            print(f"  [DIFF Slot {slot:02d}] NAME_RAW: XNU={x.get('NAME_RAW')} Oracle={o['name_raw_hex']}")
            field_diffs += 1
        if x.get("NAME", "") != o["name_canonical"]:
            print(f"  [DIFF Slot {slot:02d}] NAME: XNU={x.get('NAME')} Oracle={o['name_canonical']}")
            field_diffs += 1

    if field_diffs > 0:
        print(f"\n[FAIL] Found {field_diffs} field differences between XNU and Host Oracle!")
        sys.exit(1)

    print(f"  [PASS] ZERO field differences across all {len(oracle_slots)} used partitions!")
    print("  HOST_VS_XNU_PARTITION_NAME_RAW_MATCH: yes")
    print("  HOST_VS_XNU_PARTITION_NAME_CANONICAL_MATCH: yes")
    print("  HOST_VS_XNU_ALL_USED_FIELDS_MATCH: yes")
    print("  HOST_VS_XNU_PRIMARY_PARTITION_MAP_MATCH: yes")

    print("\n================================================================================")
    print("PHASE D3-M2B ACCEPTANCE: ALL AUDITS PASSED [SILICON CERTIFIED]")
    print("================================================================================")

if __name__ == "__main__":
    main()
