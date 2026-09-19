#!/usr/bin/env python3
"""
verify_d4m2_acceptance.py - Phase D4-M2 Acceptance Verifier

Performs automated audit of XNU D4-M2 BSD bdevsw block device telemetry,
reconstructs LBA1 (512B) and LBA1..2 (1024B) buffers from ramoops logs,
compares each byte-for-byte against independent frozen oracles,
and verifies CRC32 integrity, error rejections, and lifecycle invariants.
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
        "expected_crc": 0xD3A34BC1,
        "expected_size": 512,
        "expected_chunks": 32,
        "out_reconstructed": "artifacts/builds/d4m2_reconstructed_lba1.bin",
        "chunk_prefix": "D4M2_LBA1_CHUNK",
    },
    "lba12": {
        "file": "artifacts/oracles/d4m2_lba12.bin",
        "expected_sha256": "4a161d7ec294bc215b8dd22989a4f87f1c501fac3250acf66e5e2a4085ece8d0",
        "expected_crc": 0xA21C1724,
        "expected_size": 1024,
        "expected_chunks": 64,
        "out_reconstructed": "artifacts/builds/d4m2_reconstructed_lba12.bin",
        "chunk_prefix": "D4M2_2SECTOR_CHUNK",
    },
}

REQUIRED_TELEMETRY = [
    ("D4M2_STATUS", "COMPLETE"),
    ("PERSISTENT_EMMC_RUNTIME_VERIFIED", "yes"),
    ("MULTI_SECTOR_PIPELINE_VERIFIED", "yes"),
    ("BSD_BLOCK_STRATEGY_VERIFIED", "yes"),
    ("BDEVSW_IMPLEMENTED", "yes"),
    ("BDEVSW_REGISTER_COUNT", "1"),
    ("BDEV_MAJOR_DYNAMIC", "yes"),
    ("BDEVSW_LAYOUT_SOURCE", "src/xnu/bsd/sys/conf.h"),
    ("BDEVSW_REFERENCE_DRIVER", "src/xnu/bsd/dev/memdev.c"),
    ("BDEVSW_REGISTRATION_CALLSITE", "src/xnu/bsd/kern/bsd_init.c:733"),
    ("BDEVSW_REGISTRATION_PHASE", "BSD_POST_VFSINIT"),
    ("CONTROLLER_SERIALIZATION_ENABLED", "yes"),
    ("CONTROLLER_LOCK_INITIALIZED", "yes"),
    ("MAX_COMMANDS_IN_FLIGHT", "1"),
    ("CONTROLLER_LOCK_ACQUIRE_COUNT", "2"),
    ("READ_ONLY_OPEN_SUCCESS", "yes"),
    ("WRITE_OPEN_REJECTED", "yes"),
    ("WRITE_OPEN_ERRNO", "EROFS"),
    ("CLOSE_RESETS_STORAGE", "no"),
    ("STRATEGY_REINITIALIZES_STORAGE", "no"),
    ("STRATEGY_WRITE_REQUEST_COUNT", "0"),
    ("STRATEGY_WRITE_REJECTED", "yes"),
    ("WRITE_TEST_CMD24_COUNT", "0"),
    ("WRITE_TEST_CMD25_COUNT", "0"),
    ("BSD_BLOCK_UNIT_BYTES", "512"),
    ("BSD_BLOCK_TO_EMMC_LBA_RULE", "start_lba = (uint64_t)buf_blkno(bp)"),
    ("BUF_MAPPING_API", "buf_map()"),
    ("D4M2_TEST_BUFFER_CREATION_API", "buf_alloc(NULLVP)"),
    ("D_PSIZE_UNIT", "512_BYTE_BLOCKS"),
    ("D_PSIZE_RUNTIME_GEOMETRY_MATCH", "yes"),
    ("IOCTL_BLOCK_SIZE_MATCH", "yes"),
    ("IOCTL_BLOCK_COUNT_MATCH", "yes"),
    ("IOCTL_ISWRITABLE_MATCH", "yes"),
    ("OUT_OF_RANGE_REQUEST_REJECTED", "yes"),
    ("OUT_OF_RANGE_CMD17_COUNT", "0"),
    ("MISALIGNED_REQUEST_REJECTED", "yes"),
    ("MISALIGNED_CMD17_COUNT", "0"),
    ("PARTITION_MINOR_SUPPORT_ENABLED", "no"),
    ("DEVFS_DISK0_PUBLISHED", "no"),
    ("DEVFS_NODE_COUNT_CREATED", "0"),
    ("IOKIT_STORAGE_NUB_PUBLISHED", "no"),
    ("PARTITION_SLICES_PUBLISHED", "no"),
    ("IOFIND_BSD_ROOT_INTEGRATION", "no"),
    ("FILESYSTEM_PROBES", "0"),
    ("ROOTFS_SELECTION_PERFORMED", "no"),
    ("ZERO_STORAGE_WRITES", "yes"),
    ("READ_ONLY", "yes"),
    ("CMD0_COUNT", "1"),
    ("CMD2_COUNT", "1"),
    ("CMD3_COUNT", "1"),
    ("CMD9_COUNT", "1"),
    ("CMD7_COUNT", "1"),
    ("CMD8_COUNT", "1"),
    ("CMD17_COUNT", "3"),
    ("CMD18_COUNT", "0"),
    ("CMD24_COUNT", "0"),
    ("CMD25_COUNT", "0"),
    ("ERASE_COUNT", "0"),
    ("DISCARD_COUNT", "0"),
    ("INITIALIZATION_COUNT", "1"),
    ("STRATEGY_REINITIALIZATION_COUNT", "0"),
    ("RESET_BETWEEN_STRATEGY_REQUESTS", "0"),
    ("BDEV_STRATEGY_CALL_COUNT", "5"),
    ("BDEV_STRATEGY_READ_COUNT", "2"),
    ("BDEV_STRATEGY_WRITE_REJECT_COUNT", "1"),
    ("BDEV_IOCTL_COUNT", "3"),
    ("BDEV_PSIZE_COUNT", "1"),
    ("STRATEGY_TOTAL_SECTORS_READ", "3"),
    ("D4M2_STRATEGY_LBA1_BYTES", "512"),
    ("D4M2_STRATEGY_LBA1_BYTE_MATCH", "yes"),
    ("D4M2_STRATEGY_LBA1_RESID", "0"),
    ("D4M2_STRATEGY_LBA1_ERROR", "0"),
    ("D4M2_STRATEGY_2SECTOR_BYTES", "1024"),
    ("D4M2_STRATEGY_2SECTOR_BYTE_MATCH", "yes"),
    ("D4M2_STRATEGY_2SECTOR_RESID", "0"),
    ("D4M2_STRATEGY_2SECTOR_ERROR", "0"),
    ("PARTIAL_READ_REPORTED_AS_SUCCESS", "no"),
    ("D4_COMPLETE", "no"),
]

def main():
    log_file = sys.argv[1] if len(sys.argv) > 1 else "artifacts/logs/console-ramoops.log"

    print("================================================================================")
    print("PHASE D4-M2: BSD BDEVSW READ-ONLY BLOCK DEVICE ACCEPTANCE AUDIT")
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

    start_marker = "=== D4M2 TELEMETRY START ==="
    end_marker = "=== D4M2 TELEMETRY END ==="

    if start_marker not in log_content or end_marker not in log_content:
        print("[FAIL] Missing D4M2 telemetry markers in log!")
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

    # Check dynamically allocated major is non-negative
    major_str = telemetry.get("BDEV_MAJOR_ALLOCATED")
    if major_str is not None and int(major_str) >= 0:
        print(f"  [PASS] Dynamic major allocated: {major_str} (>= 0)")
    else:
        print(f"  [FAIL] Dynamic major invalid: {major_str}")
        telemetry_pass = False

    if not telemetry_pass:
        print("\n[FAIL] Telemetry audit failed!")
        sys.exit(1)

    os.makedirs("artifacts/builds", exist_ok=True)

    print("\n2. RECONSTRUCTING & AUDITING BUFFER DATA:")
    all_buffers_match = True

    for target_key, info in ORACLES.items():
        target_chunks = chunks[target_key]
        expected_chunks = info["expected_chunks"]
        expected_size = info["expected_size"]

        if len(target_chunks) != expected_chunks:
            print(f"  [FAIL] {target_key}: expected {expected_chunks} chunks, got {len(target_chunks)}")
            all_buffers_match = False
            continue

        reconstructed = bytearray()
        for i in range(expected_chunks):
            if i not in target_chunks:
                print(f"  [FAIL] {target_key}: missing chunk {i}")
                all_buffers_match = False
                break
            reconstructed.extend(target_chunks[i])

        if len(reconstructed) != expected_size:
            print(f"  [FAIL] {target_key}: reconstructed size is {len(reconstructed)}, expected {expected_size}")
            all_buffers_match = False
            continue

        with open(info["out_reconstructed"], "wb") as f:
            f.write(reconstructed)

        with open(info["file"], "rb") as f:
            oracle_bytes = f.read()

        calc_sha = hashlib.sha256(reconstructed).hexdigest()
        oracle_sha = hashlib.sha256(oracle_bytes).hexdigest()
        calc_crc = zlib.crc32(reconstructed) & 0xFFFFFFFF

        if calc_sha != info["expected_sha256"]:
            print(f"  [FAIL] {target_key} SHA-256 mismatch against frozen oracle specification:")
            print(f"         Got:      {calc_sha}")
            print(f"         Expected: {info['expected_sha256']}")
            all_buffers_match = False
        elif calc_sha != oracle_sha:
            print(f"  [FAIL] {target_key} SHA-256 mismatch against local oracle file:")
            print(f"         Got:      {calc_sha}")
            print(f"         Oracle:   {oracle_sha}")
            all_buffers_match = False
        elif reconstructed != oracle_bytes:
            print(f"  [FAIL] {target_key} byte-for-byte comparison failed!")
            all_buffers_match = False
        else:
            print(f"  [PASS] {target_key.upper()}: {expected_size} bytes reconstructed, SHA256={calc_sha} (EXACT BYTE MATCH)")

        if calc_crc != info["expected_crc"]:
            print(f"  [FAIL] {target_key} CRC32 mismatch: calc=0x{calc_crc:08X}, expected=0x{info['expected_crc']:08X}")
            all_buffers_match = False
        else:
            print(f"  [PASS] {target_key.upper()} CRC32: 0x{calc_crc:08X} (MATCH)")

    print("\n--------------------------------------------------------------------------------")
    print("FINAL ACCEPTANCE VERIFICATION RESULTS:")
    print("--------------------------------------------------------------------------------")
    print("D4M2_STRATEGY_LBA1_BYTE_MATCH=" + ("yes" if (len(chunks["lba1"]) == 32 and hashlib.sha256(open(ORACLES["lba1"]["out_reconstructed"], "rb").read()).hexdigest() == ORACLES["lba1"]["expected_sha256"]) else "no"))
    print("D4M2_STRATEGY_2SECTOR_BYTE_MATCH=" + ("yes" if (len(chunks["lba12"]) == 64 and hashlib.sha256(open(ORACLES["lba12"]["out_reconstructed"], "rb").read()).hexdigest() == ORACLES["lba12"]["expected_sha256"]) else "no"))
    print("D4M2_ALL_STRATEGY_READS_BYTE_MATCH=" + ("yes" if all_buffers_match else "no"))
    print("--------------------------------------------------------------------------------")

    if all_buffers_match and telemetry_pass:
        print("[OVERALL PASS] Phase D4-M2 BSD bdevsw block device integration verified!")
        sys.exit(0)
    else:
        print("[OVERALL FAIL] Verification criteria not met!")
        sys.exit(1)

if __name__ == "__main__":
    main()
