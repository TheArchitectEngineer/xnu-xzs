#!/usr/bin/env python3
"""
Phase D3-M1 Verification & Acceptance Script
Extracts D3M1 raw LBA1 from console-ramoops.log, generates artifacts/builds/d3m1_lba1.bin,
computes SHA256, compares byte-for-byte with TWRP oracle, and validates host vs XNU fields.
"""

import os
import sys
import hashlib
import binascii
import struct
import re

LOG_PATH = "artifacts/logs/console-ramoops.log"
ORACLE_BIN = "artifacts/oracles/mmcblk0_lba1.bin"
BUILD_BIN = "artifacts/builds/d3m1_lba1.bin"

def main():
    print("================================================================================")
    print("PHASE D3-M1 OFFLINE VERIFICATION & HOST VS XNU ORACLE COMPARISON")
    print("================================================================================\n")

    # 1. Read console-ramoops.log
    if not os.path.exists(LOG_PATH):
        print(f"ERROR: {LOG_PATH} not found!", file=sys.stderr)
        sys.exit(1)

    with open(LOG_PATH, "r", encoding="latin1") as f:
        log_content = f.read()

    # 2. Extract D3M1_LBA1_RAW_HEX
    hex_match = re.search(r"D3M1_LBA1_RAW_HEX=([0-9a-fA-F]{1024})", log_content)
    if not hex_match:
        print("ERROR: Could not find D3M1_LBA1_RAW_HEX in console log!", file=sys.stderr)
        sys.exit(1)

    raw_hex = hex_match.group(1).lower()
    lba1_bytes = bytes.fromhex(raw_hex)

    if len(lba1_bytes) != 512:
        print(f"ERROR: Extracted bytes length is {len(lba1_bytes)}, expected 512!", file=sys.stderr)
        sys.exit(1)

    os.makedirs(os.path.dirname(BUILD_BIN), exist_ok=True)
    with open(BUILD_BIN, "wb") as f:
        f.write(lba1_bytes)
    print(f"Saved extracted raw LBA1 to: {BUILD_BIN} (512 bytes)\n")

    # 3. Read Oracle
    with open(ORACLE_BIN, "rb") as f:
        oracle_bytes = f.read()

    oracle_sha256 = hashlib.sha256(oracle_bytes).hexdigest()
    xnu_sha256 = hashlib.sha256(lba1_bytes).hexdigest()

    print("--- 1. BYTE-FOR-BYTE LBA 1 SECTOR COMPARISON ---")
    print(f"TWRP Oracle SHA256: {oracle_sha256}")
    print(f"XNU Live SHA256:    {xnu_sha256}")
    byte_match = (oracle_bytes == lba1_bytes)
    print(f"D3M1_LBA1_BYTE_FOR_BYTE_MATCH: {'yes' if byte_match else 'no'}\n")

    # 4. Host decode from oracle
    o_sig = oracle_bytes[0:8].decode("latin1", errors="replace")
    o_rev, o_hdr_size, o_crc_stored, o_reserved = struct.unpack("<IIII", oracle_bytes[8:24])
    o_my_lba, o_alt_lba, o_first_usable, o_last_usable = struct.unpack("<QQQQ", oracle_bytes[24:56])
    o_guid_raw = oracle_bytes[56:72]
    d1, d2, d3 = struct.unpack("<IH H", o_guid_raw[:8])
    d4 = o_guid_raw[8:]
    o_guid_fmt = f"{d1:08x}-{d2:04x}-{d3:04x}-{d4[:2].hex()}-{d4[2:].hex()}"
    o_part_lba, o_num_parts, o_part_size, o_part_crc = struct.unpack("<QIII", oracle_bytes[72:92])

    o_hdr_copy = bytearray(oracle_bytes[:o_hdr_size])
    o_hdr_copy[16:20] = b"\x00\x00\x00\x00"
    o_crc_calc = binascii.crc32(o_hdr_copy) & 0xFFFFFFFF

    # 5. Extract XNU telemetry values from log
    def get_telemetry(key):
        m = re.search(rf"^{key}=(.*)$", log_content, re.MULTILINE)
        return m.group(1).strip() if m else None

    xnu_sig_valid = get_telemetry("GPT_SIGNATURE_VALID")
    xnu_rev = get_telemetry("GPT_REVISION")
    xnu_hdr_size_raw = get_telemetry("GPT_HEADER_SIZE")
    xnu_crc_stored = get_telemetry("GPT_HEADER_CRC32_STORED")
    xnu_crc_calc = get_telemetry("GPT_HEADER_CRC32_CALCULATED")
    xnu_crc_match = get_telemetry("GPT_HEADER_CRC32_MATCH")
    xnu_guid_raw = get_telemetry("GPT_DISK_GUID_RAW")
    xnu_guid_fmt = get_telemetry("GPT_DISK_GUID_FORMATTED")
    xnu_my_lba = get_telemetry("GPT_MY_LBA")
    xnu_alt_lba = get_telemetry("GPT_ALTERNATE_LBA")
    xnu_first_usable = get_telemetry("GPT_FIRST_USABLE_LBA")
    xnu_last_usable = get_telemetry("GPT_LAST_USABLE_LBA")
    xnu_part_lba = get_telemetry("GPT_PARTITION_ENTRY_LBA")
    xnu_num_parts = get_telemetry("GPT_NUM_PARTITION_ENTRIES")
    xnu_part_size = get_telemetry("GPT_SIZE_OF_PARTITION_ENTRY")
    xnu_part_crc = get_telemetry("GPT_PARTITION_ARRAY_CRC32_STORED")
    xnu_geom_valid = get_telemetry("GPT_PARTITION_ARRAY_GEOMETRY_VALID")
    xnu_sec_count = get_telemetry("LIVE_SEC_COUNT")

    print("--- 2. HOST VS XNU FIELD-FOR-FIELD COMPARISON ---")
    sig_match = (xnu_sig_valid == "yes" and o_sig == "EFI PART")
    print(f"HOST_VS_XNU_SIGNATURE_MATCH:       {'yes' if sig_match else 'no'} (EFI PART)")

    rev_val = int(xnu_rev, 16) if xnu_rev else None
    rev_match = (rev_val == o_rev == 0x00010000)
    print(f"HOST_VS_XNU_REVISION_MATCH:        {'yes' if rev_match else 'no'} (0x00010000)")

    hdr_size_val = int(xnu_hdr_size_raw, 16) if xnu_hdr_size_raw else None
    hdr_size_match = (hdr_size_val == o_hdr_size == 92)
    print(f"HOST_VS_XNU_HEADER_SIZE_MATCH:     {'yes' if hdr_size_match else 'no'} (92)")

    crc_stored_val = int(xnu_crc_stored, 16) if xnu_crc_stored else None
    crc_calc_val = int(xnu_crc_calc, 16) if xnu_crc_calc else None
    crc_match = (crc_stored_val == o_crc_stored == 0xBFDF741D and
                 crc_calc_val == o_crc_calc == 0xBFDF741D and
                 xnu_crc_match == "yes")
    print(f"HOST_VS_XNU_HEADER_CRC32_MATCH:    {'yes' if crc_match else 'no'} (0xBFDF741D)")

    guid_raw_match = (xnu_guid_raw == o_guid_raw.hex())
    guid_fmt_match = (xnu_guid_fmt == o_guid_fmt)
    print(f"HOST_VS_XNU_DISK_GUID_MATCH:       {'yes' if (guid_raw_match and guid_fmt_match) else 'no'} ({o_guid_fmt})")

    my_lba_val = int(xnu_my_lba, 16) if xnu_my_lba else None
    my_lba_match = (my_lba_val == o_my_lba == 1)
    print(f"HOST_VS_XNU_MY_LBA_MATCH:          {'yes' if my_lba_match else 'no'} (1)")

    alt_lba_val = int(xnu_alt_lba, 16) if xnu_alt_lba else None
    alt_lba_match = (alt_lba_val == o_alt_lba == 61071359)
    print(f"HOST_VS_XNU_ALTERNATE_LBA_MATCH:   {'yes' if alt_lba_match else 'no'} (61071359)")

    first_usable_val = int(xnu_first_usable, 16) if xnu_first_usable else None
    first_usable_match = (first_usable_val == o_first_usable == 34)
    print(f"HOST_VS_XNU_FIRST_USABLE_LBA_MATCH:{'yes' if first_usable_match else 'no'} (34)")

    last_usable_val = int(xnu_last_usable, 16) if xnu_last_usable else None
    last_usable_match = (last_usable_val == o_last_usable == 61071326)
    print(f"HOST_VS_XNU_LAST_USABLE_LBA_MATCH: {'yes' if last_usable_match else 'no'} (61071326)")

    part_lba_val = int(xnu_part_lba, 16) if xnu_part_lba else None
    part_lba_match = (part_lba_val == o_part_lba == 2)
    print(f"HOST_VS_XNU_PART_ENTRY_LBA_MATCH:  {'yes' if part_lba_match else 'no'} (2)")

    num_parts_val = int(xnu_num_parts, 16) if xnu_num_parts else None
    num_parts_match = (num_parts_val == o_num_parts == 128)
    print(f"HOST_VS_XNU_NUM_PART_ENTRIES_MATCH:{'yes' if num_parts_match else 'no'} (128)")

    part_size_val = int(xnu_part_size, 16) if xnu_part_size else None
    part_size_match = (part_size_val == o_part_size == 128)
    print(f"HOST_VS_XNU_SIZE_OF_PART_ENTRY_MATCH:{'yes' if part_size_match else 'no'} (128)")

    part_crc_val = int(xnu_part_crc, 16) if xnu_part_crc else None
    part_crc_match = (part_crc_val == o_part_crc == 0x64EDE0F4)
    print(f"HOST_VS_XNU_PART_ARRAY_CRC32_MATCH:{'yes' if part_crc_match else 'no'} (0x64EDE0F4)")

    all_fields_match = (sig_match and rev_match and hdr_size_match and crc_match and
                        guid_raw_match and guid_fmt_match and my_lba_match and alt_lba_match and
                        first_usable_match and last_usable_match and part_lba_match and
                        num_parts_match and part_size_match and part_crc_match)

    print(f"\nHOST_VS_XNU_GPT_HEADER_MATCH:      {'yes' if all_fields_match else 'no'}\n")

    print("--- 3. MILESTONE GATES & INVARIANTS ---")
    print(f"PRIMARY_GPT_HEADER_VERIFIED:       {get_telemetry('PRIMARY_GPT_HEADER_VERIFIED')}")
    print(f"GPT_SIGNATURE_VALID:               {get_telemetry('GPT_SIGNATURE_VALID')}")
    print(f"GPT_HEADER_CRC32_MATCH:            {get_telemetry('GPT_HEADER_CRC32_MATCH')}")
    print(f"GPT_PARTITION_ARRAY_GEOMETRY_VALID:{get_telemetry('GPT_PARTITION_ARRAY_GEOMETRY_VALID')}")
    print(f"GPT_LBA1_RAW_BUFFER_MUTATED:       {get_telemetry('GPT_LBA1_RAW_BUFFER_MUTATED')}")
    print(f"PARTITION_ARRAY_CONTENT_READ:      {get_telemetry('PARTITION_ARRAY_CONTENT_READ')}")
    print(f"PARTITION_ENUMERATION_PERFORMED:   {get_telemetry('PARTITION_ENUMERATION_PERFORMED')}")
    print(f"GPT_PARTITION_ARRAY_CRC32_VERIFIED:{get_telemetry('GPT_PARTITION_ARRAY_CRC32_VERIFIED')}")
    print(f"ZERO_LBA2_PLUS_READS:              {get_telemetry('ZERO_LBA2_PLUS_READS')}")
    print(f"ZERO_STORAGE_WRITES:              {get_telemetry('ZERO_STORAGE_WRITES')}")
    print(f"D3_COMPLETE:                       {get_telemetry('D3_COMPLETE')}")

    if all_fields_match and byte_match and get_telemetry("PRIMARY_GPT_HEADER_VERIFIED") == "yes":
        print("\n>>> ALL PHASE D3-M1 PASS CRITERIA VERIFIED ON SILICON! <<<")
        sys.exit(0)
    else:
        print("\n>>> D3-M1 VERIFICATION FAILED! <<<", file=sys.stderr)
        sys.exit(1)

if __name__ == "__main__":
    main()
