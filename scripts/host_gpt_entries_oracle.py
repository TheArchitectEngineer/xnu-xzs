#!/usr/bin/env python3
"""
host_gpt_entries_oracle.py - Phase D3-M2B Independent Host GPT Partition Map Oracle

Consumes:
  - artifacts/oracles/mmcblk0_lba1.bin
  - artifacts/oracles/mmcblk0_gpt_primary_entries.bin

Flow:
  1. Load & verify SHA256 of mmcblk0_lba1.bin
  2. Parse LBA1 header and verify Header CRC32
  3. Derive bounds (FirstUsableLBA, LastUsableLBA) and entry array geometry
     (NumberOfPartitionEntries, SizeOfPartitionEntry, PartitionEntryArrayCRC32)
  4. Load & verify SHA256 of mmcblk0_gpt_primary_entries.bin
  5. Compute and verify array CRC32 against LBA1-derived CRC32
  6. Decode all partition entry slots
  7. Classify USED iff PartitionTypeGUID != all_zero
  8. Decode raw/formatted GUIDs, extents, attributes, raw name hex, canonical name
  9. Perform pairwise Unique GUID uniqueness and extent overlap audits
  10. Output artifacts/oracles/gpt_primary_partition_map.json
"""

import os
import sys
import struct
import zlib
import hashlib
import json

LBA1_PATH = "artifacts/oracles/mmcblk0_lba1.bin"
ARRAY_PATH = "artifacts/oracles/mmcblk0_gpt_primary_entries.bin"
OUTPUT_JSON = "artifacts/oracles/gpt_primary_partition_map.json"

EXPECTED_LBA1_SHA256 = "e4b891b42fd57eb352ffbe0aa9098d04fe85f88e3425dcba529064cce72f862a"
EXPECTED_ARRAY_SHA256 = "8249f2bfa98ec675bf66b6342e29a0fbf6ab0ccb716c789a172f1f46b1828dd3"

def format_guid(b: bytes) -> str:
    """Format 16-byte GUID using standard mixed-endian representation."""
    d1 = struct.unpack('<I', b[0:4])[0]
    d2 = struct.unpack('<H', b[4:6])[0]
    d3 = struct.unpack('<H', b[6:8])[0]
    d4 = b[8:16]
    return f"{d1:08x}-{d2:04x}-{d3:04x}-{d4[0]:02x}{d4[1]:02x}-{d4[2:].hex()}"

def decode_canonical_name(name_bytes: bytes) -> str:
    """
    Decode 72-byte / 36 UTF-16 code unit GPT PartitionName deterministically.
    - U+0000: terminate visible name
    - U+0020..U+007E: emit ASCII character
    - otherwise: emit \\uXXXX for each UTF-16 code unit
    """
    out = []
    for i in range(0, len(name_bytes), 2):
        code_unit = struct.unpack('<H', name_bytes[i:i+2])[0]
        if code_unit == 0x0000:
            break
        if 0x0020 <= code_unit <= 0x007E:
            out.append(chr(code_unit))
        else:
            out.append(f"\\u{code_unit:04x}")
    return "".join(out)

def main():
    print("================================================================================")
    print("PHASE D3-M2B: INDEPENDENT HOST PRIMARY GPT PARTITION MAP ORACLE")
    print("================================================================================")

    # 1. Load & Verify LBA1 Oracle
    if not os.path.exists(LBA1_PATH):
        print(f"[FAIL] LBA1 oracle not found at {LBA1_PATH}")
        sys.exit(1)

    with open(LBA1_PATH, "rb") as f:
        lba1_bytes = f.read()

    if len(lba1_bytes) != 512:
        print(f"[FAIL] LBA1 oracle size is {len(lba1_bytes)}, expected 512")
        sys.exit(1)

    lba1_sha256 = hashlib.sha256(lba1_bytes).hexdigest()
    if lba1_sha256 != EXPECTED_LBA1_SHA256:
        print(f"[FAIL] LBA1 SHA256 mismatch: {lba1_sha256}")
        sys.exit(1)

    # Parse LBA1 Header
    sig = lba1_bytes[0:8]
    if sig != b"EFI PART":
        print(f"[FAIL] Invalid GPT signature in LBA1 oracle: {sig}")
        sys.exit(1)

    revision = struct.unpack('<I', lba1_bytes[8:12])[0]
    header_size = struct.unpack('<I', lba1_bytes[12:16])[0]
    stored_header_crc = struct.unpack('<I', lba1_bytes[16:20])[0]

    # Calculate Header CRC32 (zeroing CRC field in scratch)
    scratch = bytearray(lba1_bytes[:header_size])
    scratch[16:20] = b"\x00\x00\x00\x00"
    calc_header_crc = zlib.crc32(scratch) & 0xFFFFFFFF

    if calc_header_crc != stored_header_crc:
        print(f"[FAIL] LBA1 Header CRC32 mismatch: calc=0x{calc_header_crc:08X}, stored=0x{stored_header_crc:08X}")
        sys.exit(1)

    # Derive fields from LBA1 oracle
    my_lba = struct.unpack('<Q', lba1_bytes[24:32])[0]
    alternate_lba = struct.unpack('<Q', lba1_bytes[32:40])[0]
    first_usable_lba = struct.unpack('<Q', lba1_bytes[40:48])[0]
    last_usable_lba = struct.unpack('<Q', lba1_bytes[48:56])[0]
    disk_guid_raw = lba1_bytes[56:72]
    partition_entry_lba = struct.unpack('<Q', lba1_bytes[72:80])[0]
    num_partition_entries = struct.unpack('<I', lba1_bytes[80:84])[0]
    size_of_partition_entry = struct.unpack('<I', lba1_bytes[84:88])[0]
    stored_array_crc = struct.unpack('<I', lba1_bytes[88:92])[0]

    print("1. DERIVED FROM LBA1 ORACLE:")
    print(f"  HOST_BOUNDS_DERIVED_FROM_LBA1_ORACLE:        yes")
    print(f"  HOST_ARRAY_GEOMETRY_DERIVED_FROM_LBA1_ORACLE:yes")
    print(f"  FirstUsableLBA:                             {first_usable_lba}")
    print(f"  LastUsableLBA:                              {last_usable_lba}")
    print(f"  NumberOfPartitionEntries:                   {num_partition_entries}")
    print(f"  SizeOfPartitionEntry:                       {size_of_partition_entry}")
    print(f"  PartitionEntryLBA:                          {partition_entry_lba}")
    print(f"  PartitionEntryArrayCRC32 (stored in LBA1):  0x{stored_array_crc:08X}")

    # 2. Load & Verify 16-KiB Array Oracle
    if not os.path.exists(ARRAY_PATH):
        print(f"[FAIL] Array oracle not found at {ARRAY_PATH}")
        sys.exit(1)

    with open(ARRAY_PATH, "rb") as f:
        array_bytes = f.read()

    expected_array_bytes = num_partition_entries * size_of_partition_entry
    if len(array_bytes) != expected_array_bytes:
        print(f"[FAIL] Array oracle size {len(array_bytes)} does not match header derived size {expected_array_bytes}")
        sys.exit(1)

    array_sha256 = hashlib.sha256(array_bytes).hexdigest()
    if array_sha256 != EXPECTED_ARRAY_SHA256:
        print(f"[FAIL] Array oracle SHA256 mismatch: {array_sha256}")
        sys.exit(1)

    calc_array_crc = zlib.crc32(array_bytes) & 0xFFFFFFFF
    if calc_array_crc != stored_array_crc:
        print(f"[FAIL] Array oracle CRC32 (0x{calc_array_crc:08X}) does not match LBA1 stored CRC32 (0x{stored_array_crc:08X})")
        sys.exit(1)

    print("\n2. ARRAY INTEGRITY VERIFICATION:")
    print(f"  HOST_ARRAY_CRC_VERIFIED_BEFORE_ENTRY_PARSE:  yes")
    print(f"  Array bytes:                                {len(array_bytes)}")
    print(f"  Array SHA256:                               {array_sha256}")
    print(f"  Array CRC32:                                0x{calc_array_crc:08X}")

    # 3. Decode all partition slots
    slots = []
    used_entries = []
    unused_count = 0
    zero_unique_count = 0
    unique_guid_set = set()
    dup_unique_count = 0
    invalid_extent_count = 0
    arith_overflow_count = 0

    all_zero_guid = b"\x00" * 16

    for slot_i in range(num_partition_entries):
        offset = slot_i * size_of_partition_entry
        slot_bytes = array_bytes[offset : offset + size_of_partition_entry]

        type_guid_raw = slot_bytes[0:16]
        is_used = (type_guid_raw != all_zero_guid)

        if not is_used:
            unused_count += 1
            slots.append({
                "slot_index": slot_i,
                "used": False
            })
            continue

        unique_guid_raw = slot_bytes[16:32]
        if unique_guid_raw == all_zero_guid:
            zero_unique_count += 1

        if unique_guid_raw in unique_guid_set:
            dup_unique_count += 1
        unique_guid_set.add(unique_guid_raw)

        start_lba, end_lba, attrs = struct.unpack('<QQQ', slot_bytes[32:56])

        # Extent bounds check
        if not (start_lba <= end_lba and start_lba >= first_usable_lba and end_lba <= last_usable_lba):
            invalid_extent_count += 1

        # Sector count and size bytes calculation
        sector_count = (end_lba - start_lba + 1) if end_lba >= start_lba else 0
        if sector_count > (0xFFFFFFFFFFFFFFFF // 512):
            arith_overflow_count += 1
            size_bytes = 0
        else:
            size_bytes = sector_count * 512

        # Partition name (72 bytes)
        name_raw = slot_bytes[56:128]
        name_raw_hex = name_raw.hex()
        name_canonical = decode_canonical_name(name_raw)

        entry_record = {
            "slot_index": slot_i,
            "used": True,
            "type_guid_raw_hex": type_guid_raw.hex(),
            "type_guid_formatted": format_guid(type_guid_raw),
            "unique_guid_raw_hex": unique_guid_raw.hex(),
            "unique_guid_formatted": format_guid(unique_guid_raw),
            "start_lba": start_lba,
            "end_lba": end_lba,
            "sector_count": sector_count,
            "size_bytes": size_bytes,
            "attributes_raw_hex": f"0x{attrs:016x}",
            "name_raw_hex": name_raw_hex,
            "name_canonical": name_canonical
        }
        slots.append(entry_record)
        used_entries.append(entry_record)

    # 4. Pairwise Overlap Audit
    overlap_pairs = []
    for i in range(len(used_entries)):
        for j in range(i + 1, len(used_entries)):
            a = used_entries[i]
            b = used_entries[j]
            if a["start_lba"] <= b["end_lba"] and b["start_lba"] <= a["end_lba"]:
                overlap_pairs.append({
                    "entry_a_slot": a["slot_index"],
                    "entry_a_name": a["name_canonical"],
                    "entry_a_range": [a["start_lba"], a["end_lba"]],
                    "entry_b_slot": b["slot_index"],
                    "entry_b_name": b["name_canonical"],
                    "entry_b_range": [b["start_lba"], b["end_lba"]],
                })

    print("\n3. PARSER STRUCTURAL AUDIT:")
    print(f"  HOST_ENTRY_SLOTS:                           {num_partition_entries}")
    print(f"  HOST_USED_ENTRY_COUNT:                      {len(used_entries)}")
    print(f"  HOST_UNUSED_ENTRY_COUNT:                    {unused_count}")
    print(f"  USED_UNIQUE_GUID_ZERO_COUNT:                {zero_unique_count}")
    print(f"  DUPLICATE_UNIQUE_GUID_COUNT:                {dup_unique_count}")
    print(f"  INVALID_USED_EXTENT_COUNT:                  {invalid_extent_count}")
    print(f"  PARTITION_SIZE_ARITHMETIC_OVERFLOW_COUNT:   {arith_overflow_count}")
    print(f"  OVERLAPPING_PARTITION_PAIR_COUNT:           {len(overlap_pairs)}")

    if zero_unique_count != 0 or dup_unique_count != 0 or invalid_extent_count != 0 or len(overlap_pairs) != 0:
        print("[FAIL] Structural validation failed!")
        sys.exit(1)

    # 5. Build Oracle Document & Write JSON
    oracle_doc = {
        "metadata": {
            "source_lba1": LBA1_PATH,
            "source_lba1_sha256": lba1_sha256,
            "source_array": ARRAY_PATH,
            "source_array_sha256": array_sha256,
            "derived_first_usable_lba": first_usable_lba,
            "derived_last_usable_lba": last_usable_lba,
            "derived_num_partition_entries": num_partition_entries,
            "derived_size_of_partition_entry": size_of_partition_entry,
            "derived_partition_array_crc32": f"0x{stored_array_crc:08X}",
            "host_used_entry_count": len(used_entries),
            "host_unused_entry_count": unused_count,
            "used_slot_indices": [e["slot_index"] for e in used_entries]
        },
        "used_partitions": used_entries
    }

    os.makedirs(os.path.dirname(OUTPUT_JSON), exist_ok=True)
    json_bytes = json.dumps(oracle_doc, indent=2).encode('utf-8')
    with open(OUTPUT_JSON, "wb") as f:
        f.write(json_bytes)

    json_sha256 = hashlib.sha256(json_bytes).hexdigest()
    print(f"\n4. FROZEN HOST ORACLE ARTIFACT:")
    print(f"  Artifact Path:   {OUTPUT_JSON}")
    print(f"  Byte Size:       {len(json_bytes)} bytes")
    print(f"  SHA-256:         {json_sha256}")
    print(f"  HOST_PARTITION_MAP_JSON_SHA256={json_sha256}")
    print(f"  HOST_PARTITION_MAP_GENERATED_BEFORE_SILICON_EVALUATION=yes")
    print(f"  HOST_PARTITION_MAP_ORACLE_INDEPENDENT=yes")
    print("================================================================================")
    print("HOST PRIMARY GPT PARTITION MAP ORACLE GENERATED SUCCESSFULLY")
    print("================================================================================")

if __name__ == "__main__":
    main()
