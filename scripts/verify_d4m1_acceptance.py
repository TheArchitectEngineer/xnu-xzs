#!/usr/bin/env python3
"""
verify_d4m1_acceptance.py - Phase D4-M1 Acceptance Verifier

Performs automated audit of XNU D4-M1 persistent eMMC runtime telemetry,
reconstructs the 4 target sectors (LBA1, LBA2, LBA33, Backup Header) from
ramoops logs, compares each byte-for-byte against independent frozen oracles,
and verifies CRC32 integrity and lifecycle invariants.
"""

import sys
import os
import re
import zlib
import hashlib

ORACLES = {
    "lba1": {
        "file": "artifacts/oracles/d4m1_lba1.bin",
        "expected_sha256": "e4b891b42fd57eb352ffbe0aa9098d04fe85f88e3425dcba529064cce72f862a",
        "expected_crc": 0xBFDF741D,
        "is_gpt_hdr": True,
        "out_reconstructed": "artifacts/builds/d4m1_reconstructed_lba1.bin",
        "chunk_prefix": "D4M1_LBA1_CHUNK",
    },
    "lba2": {
        "file": "artifacts/oracles/d4m1_lba2.bin",
        "expected_sha256": "614975343d7fd0d45cb2b61b17a8f23dcbc4f782b7972e18d2b70cabd8b31ee2",
        "expected_crc": None,
        "is_gpt_hdr": False,
        "out_reconstructed": "artifacts/builds/d4m1_reconstructed_lba2.bin",
        "chunk_prefix": "D4M1_LBA2_CHUNK",
    },
    "lba33": {
        "file": "artifacts/oracles/d4m1_lba33.bin",
        "expected_sha256": "076a27c79e5ace2a3d47f9dd2e83e4ff6ea8872b3c2218f66c92b89b55f36560",
        "expected_crc": None,
        "is_gpt_hdr": False,
        "out_reconstructed": "artifacts/builds/d4m1_reconstructed_lba33.bin",
        "chunk_prefix": "D4M1_LBA33_CHUNK",
    },
    "backup_header": {
        "file": "artifacts/oracles/d4m1_backup_header.bin",
        "expected_sha256": "94434f5b0c7b8d547e2ef9ee6e3322ed1c82631310c172ab42a4878e4ec88aee",
        "expected_crc": 0x03F02415,
        "is_gpt_hdr": True,
        "out_reconstructed": "artifacts/builds/d4m1_reconstructed_backup_header.bin",
        "chunk_prefix": "D4M1_BACKUP_CHUNK",
    },
}

REQUIRED_TELEMETRY = [
    ("D4M1_STATUS", "COMPLETE"),
    ("PERSISTENT_EMMC_RUNTIME_VERIFIED", "yes"),
    ("MULTI_SECTOR_PIPELINE_VERIFIED", "yes"),
    ("CONTEXT_VALIDITY_FAIL_CLOSED", "yes"),
    ("LBA_API_WIDTH", "64"),
    ("CMD17_ARGUMENT_NARROWING_CHECKED", "yes"),
    ("TRANSPORT_COMMAND", "CMD17"),
    ("CONCURRENT_CALLERS_ENABLED", "no"),
    ("CONTROLLER_SERIALIZATION_REQUIRED_BY_TEST", "no"),
    ("D4M1_PRIMARY_HEADER_CRC_MATCH", "yes"),
    ("D4M1_BACKUP_HEADER_CRC_MATCH", "yes"),
    ("PERSISTENT_CONTEXT_INITIALIZED", "yes"),
    ("CONTEXT_RCA", "2"),
    ("CONTEXT_SECTOR_SIZE", "512"),
    ("CONTEXT_SECTOR_COUNT", "61071360"),
    ("CONTEXT_LAST_PHYSICAL_LBA", "61071359"),
    ("CONTEXT_EXT_CSD_REV", "0x08"),
    ("INIT_CALL_COUNT", "2"),
    ("INITIALIZATION_COUNT", "1"),
    ("INITIALIZATION_REUSE_COUNT", "1"),
    ("CONTROLLER_RESET_COUNT", "1"),
    ("CMD0_COUNT", "1"),
    ("CMD2_COUNT", "1"),
    ("CMD3_COUNT", "1"),
    ("CMD9_COUNT", "1"),
    ("CMD7_COUNT", "1"),
    ("CMD8_COUNT", "1"),
    ("CMD17_COUNT", "4"),
    ("CMD18_COUNT", "0"),
    ("CMD24_COUNT", "0"),
    ("CMD25_COUNT", "0"),
    ("ERASE_COUNT", "0"),
    ("DISCARD_COUNT", "0"),
    ("READ_RETRY_COUNT", "0"),
    ("RESET_BETWEEN_READS", "0"),
    ("REINIT_BETWEEN_READS", "0"),
    ("REQUEST_RANGE_OVERFLOW_COUNT", "0"),
    ("REQUEST_BUFFER_SIZE_OVERFLOW_COUNT", "0"),
    ("SUCCESSFUL_SECTOR_READS", "4"),
    ("FAILED_SECTOR_READS", "0"),
    ("LBA1_INIT_GENERATION", "1"),
    ("LBA2_INIT_GENERATION", "1"),
    ("LBA33_INIT_GENERATION", "1"),
    ("BACKUP_HEADER_INIT_GENERATION", "1"),
    ("ALL_TARGET_READ_READY_FOR_DATA", "yes"),
    ("ALL_TARGET_READ_R1_REJECT_BITS", "0"),
    ("BDEVSW_IMPLEMENTED", "no"),
    ("DEVFS_DISK0_PUBLISHED", "no"),
    ("IOKIT_STORAGE_NUB_PUBLISHED", "no"),
    ("ZERO_STORAGE_WRITES", "yes"),
    ("READ_ONLY", "yes"),
    ("PARTITION_CONTENT_READS", "0"),
    ("FILESYSTEM_PROBES", "0"),
    ("ROOTFS_SELECTION_PERFORMED", "no"),
    ("D4_COMPLETE", "no"),
]

def main():
    log_file = sys.argv[1] if len(sys.argv) > 1 else "artifacts/logs/console-ramoops.log"

    print("================================================================================")
    print("PHASE D4-M1: PERSISTENT EMMC RUNTIME & MULTI-SECTOR PIPELINE ACCEPTANCE AUDIT")
    print("================================================================================")
    print(f"Target Log File: {log_file}")

    if not os.path.exists(log_file):
        print(f"[FAIL] Log file not found: {log_file}")
        sys.exit(1)

    for target_key, info in ORACLES.items():
        if not os.path.exists(info["file"]):
            print(f"[FAIL] Required oracle file missing: {info['file']}")
            sys.exit(1)

    with open(log_file, "r", errors="ignore") as f:
        log_content = f.read()

    start_marker = "=== D4M1 TELEMETRY START ==="
    end_marker = "=== D4M1 TELEMETRY END ==="

    if start_marker not in log_content or end_marker not in log_content:
        print("[FAIL] Missing D4M1 telemetry markers in log!")
        sys.exit(1)

    telemetry_block = log_content.split(start_marker)[1].split(end_marker)[0]

    telemetry = {}
    chunks = {k: {} for k in ORACLES}

    chunk_patterns = {
        k: re.compile(rf"{info['chunk_prefix']}\[(\d+)\]:\s*([0-9a-fA-F]+)")
        for k, info in ORACLES.items()
    }

    for line in telemetry_block.splitlines():
        line = line.strip().replace("\r", "")
        matched_chunk = False
        for k, pat in chunk_patterns.items():
            m = pat.match(line)
            if m:
                idx = int(m.group(1))
                chunks[k][idx] = bytes.fromhex(m.group(2))
                matched_chunk = True
                break
        if matched_chunk:
            continue

        if ":" in line:
            parts = line.split(":", 1)
            telemetry[parts[0].strip()] = parts[1].strip()

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

    os.makedirs("artifacts/builds", exist_ok=True)

    print("\n2. RECONSTRUCTING & AUDITING TARGET SECTORS:")
    all_sectors_match = True

    for target_key, info in ORACLES.items():
        target_chunks = chunks[target_key]
        if len(target_chunks) != 32:
            print(f"  [FAIL] {target_key}: expected 32 chunks, got {len(target_chunks)}")
            all_sectors_match = False
            continue

        reconstructed = bytearray()
        for i in range(32):
            if i not in target_chunks:
                print(f"  [FAIL] {target_key}: missing chunk {i}")
                all_sectors_match = False
                break
            reconstructed.extend(target_chunks[i])

        if len(reconstructed) != 512:
            print(f"  [FAIL] {target_key}: reconstructed size is {len(reconstructed)}, expected 512")
            all_sectors_match = False
            continue

        with open(info["out_reconstructed"], "wb") as f:
            f.write(reconstructed)

        with open(info["file"], "rb") as f:
            oracle_bytes = f.read()

        calc_sha = hashlib.sha256(reconstructed).hexdigest()
        oracle_sha = hashlib.sha256(oracle_bytes).hexdigest()

        if calc_sha != info["expected_sha256"]:
            print(f"  [FAIL] {target_key} SHA-256 mismatch against frozen oracle specification:")
            print(f"         Got:      {calc_sha}")
            print(f"         Expected: {info['expected_sha256']}")
            all_sectors_match = False
        elif calc_sha != oracle_sha:
            print(f"  [FAIL] {target_key} SHA-256 mismatch against local oracle file:")
            print(f"         Got:      {calc_sha}")
            print(f"         Oracle:   {oracle_sha}")
            all_sectors_match = False
        elif reconstructed != oracle_bytes:
            print(f"  [FAIL] {target_key} byte-for-byte comparison failed!")
            all_sectors_match = False
        else:
            print(f"  [PASS] {target_key.upper()}: 512 bytes reconstructed, SHA256={calc_sha} (EXACT BYTE MATCH)")

        # If GPT header, verify CRC32
        if info["is_gpt_hdr"]:
            stored_crc = int.from_bytes(reconstructed[16:20], byteorder="little")
            zeroed_hdr = bytearray(reconstructed[:92])
            zeroed_hdr[16:20] = b"\x00\x00\x00\x00"
            calc_crc = zlib.crc32(zeroed_hdr) & 0xFFFFFFFF
            if stored_crc != calc_crc or calc_crc != info["expected_crc"]:
                print(f"  [FAIL] {target_key} CRC32 mismatch: calc=0x{calc_crc:08X}, stored=0x{stored_crc:08X}, expected=0x{info['expected_crc']:08X}")
                all_sectors_match = False
            else:
                print(f"  [PASS] {target_key.upper()} CRC32: 0x{calc_crc:08X} (VALID)")

    print("\n--------------------------------------------------------------------------------")
    print("FINAL ACCEPTANCE VERIFICATION RESULTS:")
    print("--------------------------------------------------------------------------------")
    print("D4M1_LBA1_BYTE_MATCH=yes" if (len(chunks["lba1"]) == 32 and hashlib.sha256(open(ORACLES["lba1"]["out_reconstructed"], "rb").read()).hexdigest() == ORACLES["lba1"]["expected_sha256"]) else "D4M1_LBA1_BYTE_MATCH=no")
    print("D4M1_LBA2_BYTE_MATCH=yes" if (len(chunks["lba2"]) == 32 and hashlib.sha256(open(ORACLES["lba2"]["out_reconstructed"], "rb").read()).hexdigest() == ORACLES["lba2"]["expected_sha256"]) else "D4M1_LBA2_BYTE_MATCH=no")
    print("D4M1_LBA33_BYTE_MATCH=yes" if (len(chunks["lba33"]) == 32 and hashlib.sha256(open(ORACLES["lba33"]["out_reconstructed"], "rb").read()).hexdigest() == ORACLES["lba33"]["expected_sha256"]) else "D4M1_LBA33_BYTE_MATCH=no")
    print("D4M1_BACKUP_HEADER_BYTE_MATCH=yes" if (len(chunks["backup_header"]) == 32 and hashlib.sha256(open(ORACLES["backup_header"]["out_reconstructed"], "rb").read()).hexdigest() == ORACLES["backup_header"]["expected_sha256"]) else "D4M1_BACKUP_HEADER_BYTE_MATCH=no")
    print("D4M1_ALL_TARGET_SECTORS_BYTE_MATCH=" + ("yes" if all_sectors_match else "no"))
    print("--------------------------------------------------------------------------------")

    if all_sectors_match and telemetry_pass:
        print("[OVERALL PASS] Phase D4-M1 persistent eMMC runtime & multi-sector pipeline verified!")
        sys.exit(0)
    else:
        print("[OVERALL FAIL] Verification criteria not met!")
        sys.exit(1)

if __name__ == "__main__":
    main()
