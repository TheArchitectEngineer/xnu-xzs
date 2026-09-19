#!/usr/bin/env python3
"""
verify_d4_acceptance.py — Comprehensive acceptance audit for Phase D4 End-to-End
"""

import sys
import re
import hashlib
import zlib
from pathlib import Path

REQUIRED_KEYS = {
    "D4_STATUS": "COMPLETE",
    "D3_ALREADY_INTEGRATED": "yes",
    "D4-M1_COMPLETE": "yes",
    "D4-M2_COMPLETE": "yes",
    "D4-M3_COMPLETE": "yes",
    "D4-M4_COMPLETE": "yes",
    "D4-M5_COMPLETE": "yes",
    "D4-M6_COMPLETE": "yes",
    "PERSISTENT_EMMC_RUNTIME_VERIFIED": "yes",
    "BSD_BLOCK_STRATEGY_VERIFIED": "yes",
    "BDEVSW_IMPLEMENTED": "yes",
    "BDEV_MAJOR_DYNAMIC": "yes",
    "CONTROLLER_SERIALIZATION_ENABLED": "yes",
    "CONTROLLER_LOCK_INITIALIZED": "yes",
    "MAX_COMMANDS_IN_FLIGHT": "1",
    "DEVFS_DISK0_PUBLISHED": "yes",
    "DEVFS_RDISK0_PUBLISHED": "no",
    "WHOLE_DISK_DEVFS_IDENTITY_VERIFIED": "yes",
    "DISK0_BLOCK_SIZE": "512",
    "DISK0_BLOCK_COUNT": "61071360",
    "DISK0_WRITE_OPEN_REJECTED": "yes",
    "D4_RUNTIME_GPT_MAP_INITIALIZED": "yes",
    "D4_GPT_LOADED_VIA_BLOCK_LAYER": "yes",
    "D4_RUNTIME_GPT_HEADER_CRC_VERIFIED": "yes",
    "D4_RUNTIME_GPT_ARRAY_CRC_VERIFIED": "yes",
    "D4_RUNTIME_GPT_MAP_MATCHES_D3_AUTHORITATIVE_MAP": "yes",
    "PARTITION_SLICES_PUBLISHED": "yes",
    "PUBLISHED_SLICE_COUNT": "55",
    "PARTITION_MINOR_MAPPING_DERIVED": "yes",
    "PARTITION_MINOR_MAPPING_HARDCODED": "no",
    "GPT_USED_ENTRY_COUNT": "55",
    "MINOR_TO_GPT_SLOT_UNIQUE": "yes",
    "GPT_SLOT_TO_MINOR_UNIQUE": "yes",
    "SLICE_TEST_NAME": "boot",
    "SLICE_TEST_GPT_SLOT": "29",
    "SLICE_TEST_MINOR": "30",
    "SLICE_TEST_FIRST_LBA": "208896",
    "SLICE_TEST_LAST_LBA": "339967",
    "SLICE_TEST_SECTOR_COUNT": "131072",
    "SLICE_TEST_GEOMETRY_DERIVED_FROM_D3_ORACLE": "yes",
    "SLICE_TEST_GEOMETRY_HARDCODED": "no",
    "HOST_BYNAME_FIRST_SECTOR_MATCHES_PHYSICAL_LBA": "yes",
    "SLICE_FIRST_SECTOR_BYTE_MATCH": "yes",
    "SLICE_FIRST_SECTOR_CRC32": "0x7756D106",
    "SLICE_LAST_SECTOR_BYTE_MATCH": "yes",
    "SLICE_LAST_SECTOR_CRC32": "0xB2AA7578",
    "SLICE_OUT_OF_RANGE_REJECTED": "yes",
    "SLICE_OUT_OF_RANGE_ERRNO": "EINVAL",
    "SLICE_OUT_OF_RANGE_CMD17_DELTA": "0",
    "PARTITION_CONTENT_READS_AUTHORIZED_FOR_SLICE_TEST": "yes",
    "FILESYSTEM_PROBES": "0",
    "FILESYSTEM_TYPE_INFERENCE_PERFORMED": "no",
    "ROOTFS_SELECTION_PERFORMED": "no",
    "IOKIT_STORAGE_NUB_PUBLISHED": "yes",
    "IOKIT_NUB_IMPLEMENTATION_LANGUAGE": "C++",
    "IOKIT_NUB_CLASS_MODEL_AUDITED": "yes",
    "BSD_PROPERTY_KEYS_AUDITED": "yes",
    "IOKIT_NUB_PUBLICATION_ORDER_AUDITED": "yes",
    "IOKIT_BSD_IDENTITY_DISCOVERABLE": "yes",
    "IOKIT_BSD_ROOT_DISCOVERY_COMPATIBLE": "yes",
    "DISCOVERED_BSD_NAME": "disk0",
    "DISCOVERED_BSD_MINOR": "0",
    "GLOBAL_ROOTDEV_MUTATED": "no",
    "BDEVVP_ACQUISITION_VERIFIED": "yes",
    "VNOP_OPEN_READ_SUCCESS": "yes",
    "BDEVVP_LBA1_BYTE_MATCH": "yes",
    "BDEVVP_LBA1_RESID": "0",
    "BDEVVP_LBA1_ERROR": "0",
    "BDEVVP_LBA1_CRC32": "0xD3A34BC1",
    "BDEVVP_REFERENCE_OWNERSHIP_AUDITED": "yes",
    "BDEVVP_RELEASE_API": "vnode_close(vp, FREAD, vfs_context_kernel())",
    "VNODE_IO_CONTEXT_SOURCE": "vfs_context_kernel()",
    "BUF_BREAD_BLOCK_NUMBER_SEMANTICS_AUDITED": "yes",
    "DKIOCSETBLOCKSIZE_REQUIRED_BY_LOCAL_SOURCE": "yes",
    "DKIOCSETBLOCKSIZE_ISSUES_MMC_COMMANDS": "0",
    "DEVFS_NODE_LIFETIME_AUDITED": "yes",
    "SLICE_MAP_FROZEN_AFTER_PUBLICATION": "yes",
    "CMD18_COUNT": "0",
    "CMD24_COUNT": "0",
    "CMD25_COUNT": "0",
    "ERASE_COUNT": "0",
    "DISCARD_COUNT": "0",
    "ZERO_STORAGE_WRITES": "yes",
    "READ_ONLY": "yes",
    "D4_COMPLETE": "yes",
    "D5_IMPLEMENTATION_STARTED": "no"
}

def main():
    log_path = Path("artifacts/logs/console-ramoops.log")
    if len(sys.argv) > 1:
        log_path = Path(sys.argv[1])

    if not log_path.exists():
        print(f"Error: {log_path} not found", file=sys.stderr)
        sys.exit(1)

    text = log_path.read_text(errors="replace")
    print(f"Auditing Phase D4 acceptance from {log_path} ({len(text)} bytes)...")

    # 1. Audit key-value pairs
    failures = 0
    parsed_values = {}

    for line in text.splitlines():
        if ":" in line:
            parts = line.split(":", 1)
            key = parts[0].strip()
            val = parts[1].strip()
            parsed_values[key] = val

    print("\n--- Key Value Checks ---")
    for k, exp in REQUIRED_KEYS.items():
        actual = parsed_values.get(k)
        if actual == exp:
            print(f"  [PASS] {k}: {actual}")
        else:
            print(f"  [FAIL] {k}: expected '{exp}', got '{actual}'")
            failures += 1

    # Check dynamic major match
    bdev_major = parsed_values.get("BDEV_MAJOR_ALLOCATED")
    disc_major = parsed_values.get("DISCOVERED_BSD_MAJOR")
    if bdev_major and disc_major and bdev_major == disc_major:
        print(f"  [PASS] DISCOVERED_BSD_MAJOR == BDEV_MAJOR_ALLOCATED ({bdev_major})")
    else:
        print(f"  [FAIL] Major mismatch: bdev={bdev_major}, discovered={disc_major}")
        failures += 1

    # 2. Reconstruct LBA1 buffer
    print("\n--- Reconstructing D4 LBA1 Buffer ---")
    lba1_chunks = {}
    chunk_re = re.compile(r"D4_LBA1_CHUNK\[(\d+)\]:\s*([0-9a-fA-F]{32})")
    for line in text.splitlines():
        m = chunk_re.search(line)
        if m:
            lba1_chunks[int(m.group(1))] = bytes.fromhex(m.group(2))

    if len(lba1_chunks) == 32:
        lba1_bytes = b"".join(lba1_chunks[i] for i in range(32))
        oracle_lba1 = Path("artifacts/oracles/d4m1_lba1.bin").read_bytes()
        if lba1_bytes == oracle_lba1:
            print("  [PASS] D4 LBA1 exactly matches d4m1_lba1.bin (512/512 bytes)")
        else:
            print("  [FAIL] D4 LBA1 differs from d4m1_lba1.bin")
            failures += 1
    else:
        print(f"  [FAIL] Incomplete D4 LBA1 chunks: got {len(lba1_chunks)}/32")
        failures += 1

    # 3. Reconstruct SLICE_FIRST buffer
    print("\n--- Reconstructing SLICE_FIRST Buffer ---")
    first_chunks = {}
    f_chunk_re = re.compile(r"SLICE_FIRST_CHUNK\[(\d+)\]:\s*([0-9a-fA-F]{32})")
    for line in text.splitlines():
        m = f_chunk_re.search(line)
        if m:
            first_chunks[int(m.group(1))] = bytes.fromhex(m.group(2))

    if len(first_chunks) == 32:
        first_bytes = b"".join(first_chunks[i] for i in range(32))
        oracle_first = Path("artifacts/oracles/slice_test_first.bin").read_bytes()
        if first_bytes == oracle_first:
            print("  [PASS] SLICE_FIRST exactly matches slice_test_first.bin (512/512 bytes)")
        else:
            print("  [FAIL] SLICE_FIRST differs from slice_test_first.bin")
            failures += 1
    else:
        print(f"  [FAIL] Incomplete SLICE_FIRST chunks: got {len(first_chunks)}/32")
        failures += 1

    # 4. Reconstruct SLICE_LAST buffer
    print("\n--- Reconstructing SLICE_LAST Buffer ---")
    last_chunks = {}
    l_chunk_re = re.compile(r"SLICE_LAST_CHUNK\[(\d+)\]:\s*([0-9a-fA-F]{32})")
    for line in text.splitlines():
        m = l_chunk_re.search(line)
        if m:
            last_chunks[int(m.group(1))] = bytes.fromhex(m.group(2))

    if len(last_chunks) == 32:
        last_bytes = b"".join(last_chunks[i] for i in range(32))
        oracle_last = Path("artifacts/oracles/slice_test_last.bin").read_bytes()
        if last_bytes == oracle_last:
            print("  [PASS] SLICE_LAST exactly matches slice_test_last.bin (512/512 bytes)")
        else:
            print("  [FAIL] SLICE_LAST differs from slice_test_last.bin")
            failures += 1
    else:
        print(f"  [FAIL] Incomplete SLICE_LAST chunks: got {len(last_chunks)}/32")
        failures += 1

    print("\n------------------------------------------------------------")
    if failures == 0:
        print(">>> PHASE D4 ACCEPTANCE AUDIT: 100% PASSED <<<")
        sys.exit(0)
    else:
        print(f">>> PHASE D4 ACCEPTANCE AUDIT: FAILED with {failures} error(s) <<<")
        sys.exit(1)

if __name__ == "__main__":
    main()
