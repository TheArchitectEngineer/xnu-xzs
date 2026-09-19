#!/usr/bin/env python3
"""
host_slice_oracle.py — Derive test partition geometry and verify slice oracles

Reads artifacts/oracles/gpt_primary_partition_map.json, finds the partition
with name_canonical == "boot", derives its slot, first_lba, last_lba, sector_count,
and generates/validates slice oracle metadata.
"""

import json
import sys
import zlib
import hashlib
from pathlib import Path

MAP_JSON = Path("artifacts/oracles/gpt_primary_partition_map.json")
ORACLE_DIR = Path("artifacts/oracles")

def main():
    if not MAP_JSON.exists():
        print(f"Error: {MAP_JSON} does not exist", file=sys.stderr)
        sys.exit(1)

    with open(MAP_JSON, "r") as f:
        data = json.load(f)

    used_parts = data.get("used_partitions", [])
    boot_entry = None

    for p in used_parts:
        if p.get("name_canonical") == "boot":
            boot_entry = p
            break

    if not boot_entry:
        print("FATAL: Literal 'boot' partition not found in D3 authoritative map!", file=sys.stderr)
        sys.exit(1)

    slot_index = boot_entry["slot_index"]
    start_lba = boot_entry["start_lba"]
    end_lba = boot_entry["end_lba"]
    sector_count = boot_entry["sector_count"]

    # Derive minor mapping: 1-based index among used partitions in slot order
    used_slots = sorted([p["slot_index"] for p in used_parts])
    try:
        derived_minor = used_slots.index(slot_index) + 1
    except ValueError:
        print("FATAL: slot_index not in used_slots!", file=sys.stderr)
        sys.exit(1)

    print("=== DERIVED SLICE TEST PARTITION METADATA ===")
    print(f"SLICE_TEST_NAME:           boot")
    print(f"SLICE_TEST_GPT_SLOT:       {slot_index}")
    print(f"SLICE_TEST_MINOR:          {derived_minor}")
    print(f"SLICE_TEST_FIRST_LBA:      {start_lba}")
    print(f"SLICE_TEST_LAST_LBA:       {end_lba}")
    print(f"SLICE_TEST_SECTOR_COUNT:   {sector_count}")
    print(f"TOTAL_USED_PARTITIONS:     {len(used_parts)}")

    # Check if oracles exist
    first_path = ORACLE_DIR / "slice_test_first.bin"
    last_path = ORACLE_DIR / "slice_test_last.bin"

    if first_path.exists():
        first_data = first_path.read_bytes()
        first_sha256 = hashlib.sha256(first_data).hexdigest()
        first_crc32 = zlib.crc32(first_data) & 0xFFFFFFFF
        print(f"FIRST_SECTOR_BYTES:        {len(first_data)}")
        print(f"FIRST_SECTOR_SHA256:       {first_sha256}")
        print(f"FIRST_SECTOR_CRC32:        0x{first_crc32:08X}")

    if last_path.exists():
        last_data = last_path.read_bytes()
        last_sha256 = hashlib.sha256(last_data).hexdigest()
        last_crc32 = zlib.crc32(last_data) & 0xFFFFFFFF
        print(f"LAST_SECTOR_BYTES:         {len(last_data)}")
        print(f"LAST_SECTOR_SHA256:        {last_sha256}")
        print(f"LAST_SECTOR_CRC32:         0x{last_crc32:08X}")

if __name__ == "__main__":
    main()
