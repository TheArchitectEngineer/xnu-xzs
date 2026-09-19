#!/usr/bin/env python3
import struct
import binascii
import sys

def main():
    oracle_path = "artifacts/oracles/mmcblk0_lba1.bin"
    try:
        with open(oracle_path, "rb") as f:
            data = f.read()
    except Exception as e:
        print(f"Error reading {oracle_path}: {e}", file=sys.stderr)
        sys.exit(1)

    if len(data) != 512:
        print(f"Error: expected 512 bytes, got {len(data)}", file=sys.stderr)
        sys.exit(1)

    sig = data[0:8]
    rev, hdr_size, crc_stored, reserved = struct.unpack("<IIII", data[8:24])
    my_lba, alt_lba, first_usable, last_usable = struct.unpack("<QQQQ", data[24:56])
    guid_bytes = data[56:72]
    part_lba, num_parts, part_size, part_crc = struct.unpack("<QIII", data[72:92])

    hdr_copy = bytearray(data[:hdr_size])
    hdr_copy[16:20] = b"\x00\x00\x00\x00"
    crc_calc = binascii.crc32(hdr_copy) & 0xFFFFFFFF

    d1, d2, d3 = struct.unpack("<IH H", guid_bytes[:8])
    d4 = guid_bytes[8:]
    guid_formatted = f"{d1:08x}-{d2:04x}-{d3:04x}-{d4[:2].hex()}-{d4[2:].hex()}"

    sig_str = sig.decode("latin1", errors="replace")
    all_zero = all(b == 0 for b in data[hdr_size:])

    array_bytes = num_parts * part_size
    array_sectors = (array_bytes // 512) + (1 if (array_bytes % 512) != 0 else 0)
    array_first_lba = part_lba
    array_last_lba = array_first_lba + array_sectors - 1
    geom_valid = (part_size >= 128 and (part_size & (part_size - 1)) == 0 and
                  array_first_lba >= 2 and array_last_lba < first_usable and
                  array_last_lba < 61071360)

    print(f"HOST_GPT_SIGNATURE:               {sig_str} (hex: {sig.hex()})")
    print(f"HOST_GPT_REVISION:                0x{rev:08x}")
    print(f"HOST_GPT_HEADER_SIZE:             {hdr_size}")
    print(f"HOST_GPT_HEADER_CRC32_STORED:     0x{crc_stored:08X}")
    print(f"HOST_GPT_HEADER_CRC32_CALCULATED: 0x{crc_calc:08X}")
    print(f"HOST_GPT_HEADER_CRC32_MATCH:      {'yes' if crc_stored == crc_calc else 'no'}")
    print(f"HOST_GPT_RESERVED:                0x{reserved:08x}")
    print(f"HOST_GPT_MY_LBA:                  {my_lba}")
    print(f"HOST_GPT_ALTERNATE_LBA:           {alt_lba}")
    print(f"HOST_GPT_FIRST_USABLE_LBA:        {first_usable}")
    print(f"HOST_GPT_LAST_USABLE_LBA:         {last_usable}")
    print(f"HOST_GPT_DISK_GUID_RAW:           {guid_bytes.hex()}")
    print(f"HOST_GPT_DISK_GUID_FORMATTED:     {guid_formatted}")
    print(f"HOST_GPT_PARTITION_ENTRY_LBA:     {part_lba}")
    print(f"HOST_GPT_NUM_PARTITION_ENTRIES:   {num_parts}")
    print(f"HOST_GPT_SIZE_OF_PARTITION_ENTRY: {part_size}")
    print(f"HOST_GPT_PARTITION_ARRAY_BYTES:   {array_bytes}")
    print(f"HOST_GPT_PARTITION_ARRAY_SECTORS: {array_sectors}")
    print(f"HOST_GPT_PARTITION_ARRAY_FIRST_LBA: {array_first_lba}")
    print(f"HOST_GPT_PARTITION_ARRAY_LAST_LBA:  {array_last_lba}")
    print(f"HOST_GPT_PARTITION_ARRAY_GEOMETRY_VALID: {'yes' if geom_valid else 'no'}")
    print(f"HOST_GPT_PARTITION_ARRAY_CRC32:   0x{part_crc:08X}")
    print(f"HOST_GPT_POST_HEADER_ALL_ZERO:    {'yes' if all_zero else 'no'}")

if __name__ == "__main__":
    main()
