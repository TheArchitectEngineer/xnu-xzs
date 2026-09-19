#!/usr/bin/env python3
"""
host_gpt_backup_oracle.py - Phase D3-M3 Independent Host Backup GPT Oracle

Parses and validates the Backup GPT Header and Entry Array directly from host captures.
Checks reciprocal link semantics against the Primary GPT Header oracle.
Parses Backup Partition Array into artifacts/oracles/gpt_backup_partition_map.json
and verifies exact parity with Primary Partition Map.
"""

import sys
import os
import struct
import zlib
import hashlib
import json

LBA1_PATH = "artifacts/oracles/mmcblk0_lba1.bin"
BACKUP_HEADER_PATH = "artifacts/oracles/mmcblk0_gpt_backup_header.bin"
BACKUP_ENTRIES_PATH = "artifacts/oracles/mmcblk0_gpt_backup_entries.bin"
PRIMARY_ENTRIES_PATH = "artifacts/oracles/mmcblk0_gpt_primary_entries.bin"

PRIMARY_MAP_JSON = "artifacts/oracles/gpt_primary_partition_map.json"
BACKUP_MAP_JSON = "artifacts/oracles/gpt_backup_partition_map.json"

def format_guid(b: bytes) -> str:
    """Format 16-byte GUID using standard mixed-endian representation."""
    d1 = struct.unpack('<I', b[0:4])[0]
    d2 = struct.unpack('<H', b[4:6])[0]
    d3 = struct.unpack('<H', b[6:8])[0]
    d4 = b[8:16]
    return f"{d1:08x}-{d2:04x}-{d3:04x}-{d4[0]:02x}{d4[1]:02x}-{d4[2:].hex()}"

def decode_canonical_name(name_bytes: bytes) -> str:
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

def parse_gpt_header(data: bytes, label: str):
    sig = data[0:8]
    if sig != b'EFI PART':
        raise ValueError(f"{label}: Invalid GPT signature {sig}")
    
    rev, hdr_size, crc_stored, reserved = struct.unpack('<IIII', data[8:24])
    my_lba, alt_lba, first_usable, last_usable = struct.unpack('<QQQQ', data[24:56])
    disk_guid = data[56:72]
    part_entry_lba, num_entries, entry_size, array_crc = struct.unpack('<QIII', data[72:92])

    hdr_copy = bytearray(data[:hdr_size])
    hdr_copy[16:20] = b'\x00\x00\x00\x00'
    crc_calc = zlib.crc32(hdr_copy) & 0xFFFFFFFF

    return {
        "label": label,
        "signature": sig.decode('latin1'),
        "revision": rev,
        "header_size": hdr_size,
        "crc_stored": crc_stored,
        "crc_calc": crc_calc,
        "crc_match": (crc_stored == crc_calc),
        "reserved": reserved,
        "my_lba": my_lba,
        "alt_lba": alt_lba,
        "first_usable_lba": first_usable,
        "last_usable_lba": last_usable,
        "disk_guid_raw": disk_guid.hex(),
        "disk_guid_formatted": format_guid(disk_guid),
        "partition_entry_lba": part_entry_lba,
        "num_entries": num_entries,
        "entry_size": entry_size,
        "array_crc_stored": array_crc,
    }

def main():
    print("================================================================================")
    print("PHASE D3-M3: INDEPENDENT HOST BACKUP GPT ORACLE")
    print("================================================================================")

    if not os.path.exists(LBA1_PATH):
        print(f"[FAIL] Missing Primary Header oracle: {LBA1_PATH}")
        sys.exit(1)
    if not os.path.exists(BACKUP_HEADER_PATH):
        print(f"[FAIL] Missing Backup Header oracle: {BACKUP_HEADER_PATH}")
        sys.exit(1)
    if not os.path.exists(BACKUP_ENTRIES_PATH):
        print(f"[FAIL] Missing Backup Entries oracle: {BACKUP_ENTRIES_PATH}")
        sys.exit(1)
    if not os.path.exists(PRIMARY_ENTRIES_PATH):
        print(f"[FAIL] Missing Primary Entries oracle: {PRIMARY_ENTRIES_PATH}")
        sys.exit(1)

    with open(LBA1_PATH, "rb") as f:
        lba1_bytes = f.read()
    with open(BACKUP_HEADER_PATH, "rb") as f:
        backup_hdr_bytes = f.read()
    with open(BACKUP_ENTRIES_PATH, "rb") as f:
        backup_array_bytes = f.read()
    with open(PRIMARY_ENTRIES_PATH, "rb") as f:
        primary_array_bytes = f.read()

    hdr_sha256 = hashlib.sha256(backup_hdr_bytes).hexdigest()
    print(f"HOST_BACKUP_HEADER_BYTES:  {len(backup_hdr_bytes)}")
    print(f"HOST_BACKUP_HEADER_SHA256: {hdr_sha256}")

    pri = parse_gpt_header(lba1_bytes, "PRIMARY")
    bak = parse_gpt_header(backup_hdr_bytes, "BACKUP")

    print("\n1. BACKUP HEADER PARSER & CRC32 AUDIT:")
    print(f"  MyLBA:                     {bak['my_lba']}")
    print(f"  AlternateLBA:              {bak['alt_lba']}")
    print(f"  HeaderSize:                {bak['header_size']}")
    print(f"  Stored Header CRC32:       0x{bak['crc_stored']:08X}")
    print(f"  Calculated Header CRC32:   0x{bak['crc_calc']:08X}")
    print(f"  HOST_BACKUP_HEADER_CRC32_MATCH: {'yes' if bak['crc_match'] else 'no'}")

    if not bak['crc_match']:
        print("[FAIL] Backup Header CRC32 mismatch!")
        sys.exit(1)

    print("\n2. PRIMARY / BACKUP RECIPROCAL RELATIONSHIP AUDIT:")
    reciprocal_links_valid = (pri['my_lba'] == 1 and
                              pri['alt_lba'] == bak['my_lba'] and
                              bak['alt_lba'] == pri['my_lba'])
    print(f"  PRIMARY.MyLBA == 1:                       {'yes' if pri['my_lba'] == 1 else 'no'} ({pri['my_lba']})")
    print(f"  PRIMARY.AlternateLBA == BACKUP.MyLBA:     {'yes' if pri['alt_lba'] == bak['my_lba'] else 'no'} ({pri['alt_lba']} == {bak['my_lba']})")
    print(f"  BACKUP.AlternateLBA == PRIMARY.MyLBA:     {'yes' if bak['alt_lba'] == pri['my_lba'] else 'no'} ({bak['alt_lba']} == {pri['my_lba']})")
    print(f"  GPT_HEADER_RECIPROCAL_LINKS_VALID:        {'yes' if reciprocal_links_valid else 'no'}")

    rev_match = (pri['revision'] == bak['revision'])
    usable_match = (pri['first_usable_lba'] == bak['first_usable_lba'] and
                    pri['last_usable_lba'] == bak['last_usable_lba'])
    guid_match = (pri['disk_guid_raw'] == bak['disk_guid_raw'])
    count_match = (pri['num_entries'] == bak['num_entries'])
    size_match = (pri['entry_size'] == bak['entry_size'])
    array_crc_match = (pri['array_crc_stored'] == bak['array_crc_stored'])

    print(f"  PRIMARY_BACKUP_REVISION_MATCH:            {'yes' if rev_match else 'no'}")
    print(f"  PRIMARY_BACKUP_USABLE_RANGE_MATCH:        {'yes' if usable_match else 'no'} ([{bak['first_usable_lba']}..{bak['last_usable_lba']}])")
    print(f"  PRIMARY_BACKUP_DISK_GUID_MATCH:           {'yes' if guid_match else 'no'} ({bak['disk_guid_formatted']})")
    print(f"  PRIMARY_BACKUP_ENTRY_COUNT_MATCH:         {'yes' if count_match else 'no'} ({bak['num_entries']})")
    print(f"  PRIMARY_BACKUP_ENTRY_SIZE_MATCH:          {'yes' if size_match else 'no'} ({bak['entry_size']})")
    print(f"  PRIMARY_BACKUP_ARRAY_CRC_FIELD_MATCH:     {'yes' if array_crc_match else 'no'} (0x{bak['array_crc_stored']:08X})")

    if not (reciprocal_links_valid and rev_match and usable_match and guid_match and count_match and size_match and array_crc_match):
        print("[FAIL] Primary and Backup headers do not satisfy required consistency!")
        sys.exit(1)

    print("\n3. BACKUP ENTRY ARRAY GEOMETRY DERIVATION:")
    count = bak['num_entries']
    size = bak['entry_size']
    array_bytes = count * size
    array_sectors = (array_bytes + 511) // 512
    first_lba = bak['partition_entry_lba']
    last_lba = first_lba + array_sectors - 1
    immediately_precedes = (last_lba + 1 == bak['my_lba'])

    print(f"  runtime_backup_entry_count:               {count}")
    print(f"  runtime_backup_entry_size:                {size}")
    print(f"  backup_array_bytes:                       {array_bytes}")
    print(f"  backup_array_sectors:                     {array_sectors}")
    print(f"  backup_array_first_lba:                   {first_lba}")
    print(f"  backup_array_last_lba:                    {last_lba}")
    print(f"  backup_header_lba:                        {bak['my_lba']}")
    print(f"  BACKUP_ARRAY_IMMEDIATELY_PRECEDES_HEADER: {'yes' if immediately_precedes else 'no'}")

    bounds_ok = (first_lba > bak['last_usable_lba'] and
                 last_lba < bak['my_lba'])
    print(f"  BACKUP_ARRAY_BOUNDS_VALID:                {'yes' if bounds_ok else 'no'}")

    if not bounds_ok:
        print("[FAIL] Backup array bounds are invalid!")
        sys.exit(1)

    print("\n4. BACKUP ENTRY ARRAY INTEGRITY & CRC32 AUDIT:")
    array_sha256 = hashlib.sha256(backup_array_bytes).hexdigest()
    array_crc_calc = zlib.crc32(backup_array_bytes[:array_bytes]) & 0xFFFFFFFF
    array_crc_match_bak = (array_crc_calc == bak['array_crc_stored'])

    print(f"  HOST_BACKUP_ARRAY_BYTES:                  {len(backup_array_bytes)}")
    print(f"  HOST_BACKUP_ARRAY_SHA256:                 {array_sha256}")
    print(f"  Stored Backup Array CRC32:                0x{bak['array_crc_stored']:08X}")
    print(f"  Calculated Backup Array CRC32:            0x{array_crc_calc:08X}")
    print(f"  HOST_BACKUP_ARRAY_CRC32_MATCH:            {'yes' if array_crc_match_bak else 'no'}")

    if not array_crc_match_bak:
        print("[FAIL] Backup Entry Array CRC32 mismatch!")
        sys.exit(1)

    print("\n5. PRIMARY VS BACKUP RAW ARRAY COMPARISON:")
    arrays_byte_match = (primary_array_bytes == backup_array_bytes)
    print(f"  HOST_PRIMARY_BACKUP_ARRAY_BYTE_MATCH:     {'yes' if arrays_byte_match else 'no'}")
    if not arrays_byte_match:
        print("[FAIL] Host primary and backup entry arrays differ in bytes!")
        sys.exit(1)

    print("\n6. PARSING BACKUP PARTITION MAP:")
    used_partitions = []
    unused_count = 0
    all_slots = []

    for slot_idx in range(count):
        offset = slot_idx * size
        entry_raw = backup_array_bytes[offset:offset+size]
        type_guid_raw = entry_raw[0:16]

        if all(b == 0 for b in type_guid_raw):
            unused_count += 1
            all_slots.append({"slot_index": slot_idx, "used": False})
            continue

        unique_guid_raw = entry_raw[16:32]
        start_lba, end_lba, attr = struct.unpack('<QQQ', entry_raw[32:56])
        name_raw = entry_raw[56:128]
        canonical_name = decode_canonical_name(name_raw)

        sector_count = (end_lba - start_lba + 1) if end_lba >= start_lba else 0
        size_bytes = sector_count * 512

        record = {
            "slot_index": slot_idx,
            "used": True,
            "type_guid_raw_hex": type_guid_raw.hex(),
            "type_guid_formatted": format_guid(type_guid_raw),
            "unique_guid_raw_hex": unique_guid_raw.hex(),
            "unique_guid_formatted": format_guid(unique_guid_raw),
            "start_lba": start_lba,
            "end_lba": end_lba,
            "sector_count": sector_count,
            "size_bytes": size_bytes,
            "attributes_raw_hex": f"0x{attr:016x}",
            "name_raw_hex": name_raw.hex(),
            "name_canonical": canonical_name
        }
        used_partitions.append(record)
        all_slots.append(record)

    backup_map_data = {
        "metadata": {
            "source": "artifacts/oracles/mmcblk0_gpt_backup_entries.bin",
            "host_entry_slots": count,
            "host_used_entry_count": len(used_partitions),
            "host_unused_entry_count": unused_count,
            "backup_array_sha256": array_sha256,
            "backup_array_crc32": f"0x{array_crc_calc:08X}"
        },
        "used_partitions": used_partitions
    }

    with open(BACKUP_MAP_JSON, "w") as f:
        json.dump(backup_map_data, f, indent=2)

    backup_map_json_bytes = open(BACKUP_MAP_JSON, "rb").read()
    backup_map_sha256 = hashlib.sha256(backup_map_json_bytes).hexdigest()
    print(f"  HOST_BACKUP_PARTITION_MAP_JSON:           {BACKUP_MAP_JSON}")
    print(f"  HOST_BACKUP_PARTITION_MAP_JSON_SHA256:    {backup_map_sha256}")
    print(f"  BACKUP_USED_ENTRY_COUNT:                  {len(used_partitions)}")
    print(f"  BACKUP_UNUSED_ENTRY_COUNT:                {unused_count}")

    print("\n7. PRIMARY VS BACKUP PARTITION MAP EQUALITY AUDIT:")
    with open(PRIMARY_MAP_JSON, "r") as f:
        pri_map_data = json.load(f)

    pri_used = pri_map_data["used_partitions"]
    diffs = 0
    if len(pri_used) != len(used_partitions):
        print(f"  [FAIL] Used partition count mismatch: Pri={len(pri_used)}, Bak={len(used_partitions)}")
        sys.exit(1)

    for i in range(len(used_partitions)):
        p = pri_used[i]
        b = used_partitions[i]
        for k in ["slot_index", "type_guid_raw_hex", "type_guid_formatted",
                  "unique_guid_raw_hex", "unique_guid_formatted",
                  "start_lba", "end_lba", "sector_count", "size_bytes",
                  "attributes_raw_hex", "name_raw_hex", "name_canonical"]:
            if p[k] != b[k]:
                print(f"  [DIFF Slot {p['slot_index']}] {k}: Pri={p[k]} Bak={b[k]}")
                diffs += 1

    if diffs != 0:
        print(f"[FAIL] Found {diffs} differences between host primary and backup maps!")
        sys.exit(1)

    print(f"  HOST_PRIMARY_BACKUP_PARTITION_MAP_MATCH:  yes")
    print("================================================================================")
    print("HOST BACKUP GPT ORACLE AUDIT PASSED [100% PARITY WITH PRIMARY ORACLE]")
    print("================================================================================")

if __name__ == "__main__":
    main()
