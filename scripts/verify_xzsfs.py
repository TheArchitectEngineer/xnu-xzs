#!/usr/bin/env python3
"""
verify_xzsfs.py - Independent Host Verifier for XZSFS v1 Filesystem Images
Target: Apple XNU on Sony Xperia XZs (MSM8996)
"""

import argparse
import hashlib
import os
import struct
import sys
import zlib

XZSFS_MAGIC = 0x5346535A  # 'XZSF'
XZSFS_FORMAT_VERSION = 1
XZSFS_SUPERBLOCK_SIZE = 512
XZSFS_SECTOR_SIZE = 512
XZSFS_OBJECT_SIZE = 64
XZSFS_DATA_ALIGNMENT = 512

XZSFS_TYPE_DIR = 1
XZSFS_TYPE_REG = 2


class XZSFSError(Exception):
    pass


class VerifiedObject:
    def __init__(self, obj_id, parent_id, obj_type, mode, uid, gid,
                 name_offset, name_length, data_offset, data_length, name):
        self.object_id = obj_id
        self.parent_id = parent_id
        self.type = obj_type
        self.mode = mode
        self.uid = uid
        self.gid = gid
        self.name_offset = name_offset
        self.name_length = name_length
        self.data_offset = data_offset
        self.data_length = data_length
        self.name = name
        self.full_path = ""


def parse_and_verify(image_bytes, source_root=None):
    """
    Independently parses and validates an XZSFS v1 image.
    Raises XZSFSError on any violation.
    Returns (superblock_dict, objects_dict, extracted_files).
    """
    img_len = len(image_bytes)
    if img_len < XZSFS_SUPERBLOCK_SIZE:
        raise XZSFSError(f"Image too small: {img_len} bytes (minimum {XZSFS_SUPERBLOCK_SIZE})")

    if img_len % XZSFS_SECTOR_SIZE != 0:
        raise XZSFSError(f"Image size {img_len} is not sector-aligned ({XZSFS_SECTOR_SIZE} bytes)")

    # 1. Parse Superblock
    sb_bytes = image_bytes[:512]
    (
        magic,
        format_version,
        header_size,
        sector_size,
        image_size_bytes,
        object_count,
        root_object_id,
        object_table_offset,
        object_table_size,
        string_table_offset,
        string_table_size,
        data_offset,
        data_size,
        metadata_crc32,
        reserved,
        padding
    ) = struct.unpack("<IIIIQIIQQQQQQII424s", sb_bytes)

    # 2. Validate Superblock fields
    if magic != XZSFS_MAGIC:
        raise XZSFSError(f"Invalid magic: 0x{magic:08x} (expected 0x{XZSFS_MAGIC:08x})")

    if format_version != XZSFS_FORMAT_VERSION:
        raise XZSFSError(f"Unsupported format version: {format_version} (expected {XZSFS_FORMAT_VERSION})")

    if header_size != XZSFS_SUPERBLOCK_SIZE:
        raise XZSFSError(f"Invalid header size: {header_size} (expected {XZSFS_SUPERBLOCK_SIZE})")

    if sector_size != XZSFS_SECTOR_SIZE:
        raise XZSFSError(f"Invalid sector size: {sector_size} (expected {XZSFS_SECTOR_SIZE})")

    if image_size_bytes != img_len:
        raise XZSFSError(f"Superblock image_size_bytes ({image_size_bytes}) != actual file size ({img_len})")

    if object_count == 0:
        raise XZSFSError("Superblock object_count is 0")

    if root_object_id != 1:
        raise XZSFSError(f"Invalid root_object_id: {root_object_id} (must be 1)")

    if object_table_offset != XZSFS_SUPERBLOCK_SIZE:
        raise XZSFSError(f"Object table offset must be {XZSFS_SUPERBLOCK_SIZE}, got {object_table_offset}")

    expected_obj_table_size = object_count * XZSFS_OBJECT_SIZE
    if object_table_size != expected_obj_table_size:
        raise XZSFSError(f"Object table size {object_table_size} != object_count * 64 ({expected_obj_table_size})")

    if object_table_offset + object_table_size > img_len:
        raise XZSFSError("Object table exceeds image bounds")

    if string_table_offset < object_table_offset + object_table_size:
        raise XZSFSError(f"String table offset ({string_table_offset}) overlaps object table")

    if string_table_offset % XZSFS_SECTOR_SIZE != 0:
        raise XZSFSError(f"String table offset ({string_table_offset}) is not sector-aligned")

    if string_table_size % XZSFS_SECTOR_SIZE != 0:
        raise XZSFSError(f"String table size ({string_table_size}) is not sector-aligned")

    if string_table_offset + string_table_size > img_len:
        raise XZSFSError("String table exceeds image bounds")

    if data_offset < string_table_offset + string_table_size:
        raise XZSFSError(f"Data offset ({data_offset}) overlaps string table")

    if data_offset % XZSFS_DATA_ALIGNMENT != 0:
        raise XZSFSError(f"Data offset ({data_offset}) is not aligned to {XZSFS_DATA_ALIGNMENT}")

    if data_offset + data_size > img_len:
        raise XZSFSError(f"Data area ({data_offset} + {data_size} = {data_offset + data_size}) exceeds image size ({img_len})")

    # 3. Verify Metadata CRC32
    sb_zero_crc = sb_bytes[:0x50] + b"\0\0\0\0" + sb_bytes[0x54:]
    crc = zlib.crc32(sb_zero_crc)
    crc = zlib.crc32(image_bytes[object_table_offset : object_table_offset + object_table_size], crc)
    crc = zlib.crc32(image_bytes[string_table_offset : string_table_offset + string_table_size], crc)
    computed_crc = crc & 0xFFFFFFFF

    if computed_crc != metadata_crc32:
        raise XZSFSError(f"Metadata CRC32 mismatch: computed 0x{computed_crc:08x}, header says 0x{metadata_crc32:08x}")

    # 4. Parse String Table
    str_table_bytes = image_bytes[string_table_offset : string_table_offset + string_table_size]

    # 5. Parse and Validate Object Table
    objects = {}
    obj_table_raw = image_bytes[object_table_offset : object_table_offset + object_table_size]
    for i in range(object_count):
        entry_bytes = obj_table_raw[i * 64 : (i + 1) * 64]
        (
            obj_id,
            parent_id,
            obj_type,
            mode,
            uid,
            gid,
            name_offset,
            name_length,
            f_data_offset,
            f_data_length,
            r1,
            r2
        ) = struct.unpack("<IIIIIIIIQQQQ", entry_bytes)

        # Validate unique object ID
        if obj_id in objects:
            raise XZSFSError(f"Duplicate object ID: {obj_id}")

        if obj_id < 1 or obj_id > object_count:
            raise XZSFSError(f"Object ID {obj_id} out of bounds (1..{object_count})")

        if obj_type not in (XZSFS_TYPE_DIR, XZSFS_TYPE_REG):
            raise XZSFSError(f"Object {obj_id}: invalid type {obj_type}")

        # Validate name range in string table
        if name_offset + name_length > len(str_table_bytes):
            raise XZSFSError(f"Object {obj_id}: name offset {name_offset} + len {name_length} exceeds string table size {len(str_table_bytes)}")

        if obj_id == 1:
            # Root directory
            if name_offset != 0 or name_length != 0:
                raise XZSFSError(f"Root object must have name_offset=0, name_length=0, got {name_offset}, {name_length}")
            if parent_id != 1:
                raise XZSFSError(f"Root object must have parent_id=1, got {parent_id}")
            if obj_type != XZSFS_TYPE_DIR:
                raise XZSFSError("Root object must be a directory")
            name = ""
        else:
            if name_length == 0:
                raise XZSFSError(f"Non-root object {obj_id} has empty name")
            # Must be NUL-terminated
            if name_offset + name_length >= len(str_table_bytes) or str_table_bytes[name_offset + name_length] != 0:
                raise XZSFSError(f"Object {obj_id}: name is not NUL-terminated at string table offset {name_offset + name_length}")
            raw_name = str_table_bytes[name_offset : name_offset + name_length]
            try:
                name = raw_name.decode('ascii')
            except UnicodeDecodeError:
                raise XZSFSError(f"Object {obj_id}: name is not valid ASCII")

            if '/' in name:
                raise XZSFSError(f"Object {obj_id}: name contains '/' ({name})")

        # Validate payload offsets
        if obj_type == XZSFS_TYPE_DIR:
            if f_data_offset != 0 or f_data_length != 0:
                raise XZSFSError(f"Directory object {obj_id} must have data_offset=0, data_length=0")
        elif obj_type == XZSFS_TYPE_REG:
            if f_data_offset < data_offset:
                raise XZSFSError(f"Regular file {obj_id}: data_offset {f_data_offset} < superblock data_offset {data_offset}")
            if f_data_offset % XZSFS_DATA_ALIGNMENT != 0:
                raise XZSFSError(f"Regular file {obj_id}: data_offset {f_data_offset} is not {XZSFS_DATA_ALIGNMENT}-byte aligned")
            if f_data_offset + f_data_length > img_len:
                raise XZSFSError(f"Regular file {obj_id}: data extent ({f_data_offset} + {f_data_length}) exceeds image size {img_len}")

        v_obj = VerifiedObject(
            obj_id, parent_id, obj_type, mode, uid, gid,
            name_offset, name_length, f_data_offset, f_data_length, name
        )
        objects[obj_id] = v_obj

    # 6. Validate Parent IDs and Hierarchy (Acyclic Check)
    for obj_id, obj in objects.items():
        if obj.parent_id not in objects:
            raise XZSFSError(f"Object {obj_id} references non-existent parent_id {obj.parent_id}")

        parent = objects[obj.parent_id]
        if parent.type != XZSFS_TYPE_DIR:
            raise XZSFSError(f"Object {obj_id} parent {obj.parent_id} is not a directory")

        # Trace path to root to detect cycles
        visited = set()
        curr = obj
        path_parts = []
        while curr.object_id != 1:
            if curr.object_id in visited:
                raise XZSFSError(f"Cycle detected in directory hierarchy involving object {curr.object_id}")
            visited.add(curr.object_id)
            path_parts.append(curr.name)
            curr = objects[curr.parent_id]

        path_parts.reverse()
        obj.full_path = "/" + "/".join(path_parts) if path_parts else "/"

    # 7. Check for duplicate names under same parent
    seen_under_parent = set()
    for obj_id, obj in objects.items():
        if obj_id > 1:
            key = (obj.parent_id, obj.name)
            if key in seen_under_parent:
                raise XZSFSError(f"Duplicate filename '{obj.name}' under parent {obj.parent_id}")
            seen_under_parent.add(key)

    # 8. Check for overlapping file extents
    reg_files = [o for o in objects.values() if o.type == XZSFS_TYPE_REG and o.data_length > 0]
    reg_files.sort(key=lambda o: o.data_offset)
    for idx in range(len(reg_files) - 1):
        f1 = reg_files[idx]
        f2 = reg_files[idx + 1]
        f1_end = f1.data_offset + f1.data_length
        if f1_end > f2.data_offset:
            raise XZSFSError(f"Overlapping data extents: {f1.full_path} (end {f1_end}) overlaps {f2.full_path} (start {f2.data_offset})")

    # 9. Extract files and optionally verify payloads against source tree
    extracted = {}
    for obj in reg_files:
        payload = image_bytes[obj.data_offset : obj.data_offset + obj.data_length]
        extracted[obj.full_path] = payload

        if source_root:
            src_rel = obj.full_path.lstrip("/")
            src_file = os.path.join(source_root, src_rel)
            if not os.path.isfile(src_file):
                raise XZSFSError(f"Source file missing for {obj.full_path}: {src_file}")
            src_payload = open(src_file, "rb").read()
            if payload != src_payload:
                raise XZSFSError(f"Payload mismatch for {obj.full_path}: extracted {len(payload)}B != source {len(src_payload)}B")

    return {
        "magic": magic,
        "format_version": format_version,
        "image_size_bytes": image_size_bytes,
        "object_count": object_count,
        "metadata_crc32": metadata_crc32,
        "data_offset": data_offset,
        "data_size": data_size,
    }, objects, extracted


def run_corruption_tests(valid_image_bytes):
    """
    Synthesize >=10 corrupted variants of a valid image and confirm verify_xzsfs rejects all.
    """
    tests = []

    def fix_crc(img_data):
        # Helper to recalculate metadata CRC32 so structural checks are tested directly
        sb_len = 512
        sb_bytes = img_data[:sb_len]
        sb_zero_crc = sb_bytes[:0x50] + b"\0\0\0\0" + sb_bytes[0x54:]
        obj_tbl_off = struct.unpack_from("<Q", sb_bytes, 0x20)[0]
        obj_tbl_sz = struct.unpack_from("<Q", sb_bytes, 0x28)[0]
        str_tbl_off = struct.unpack_from("<Q", sb_bytes, 0x30)[0]
        str_tbl_sz = struct.unpack_from("<Q", sb_bytes, 0x38)[0]
        c = zlib.crc32(sb_zero_crc)
        c = zlib.crc32(img_data[obj_tbl_off : obj_tbl_off + obj_tbl_sz], c)
        c = zlib.crc32(img_data[str_tbl_off : str_tbl_off + str_tbl_sz], c)
        struct.pack_into("<I", img_data, 0x50, c & 0xFFFFFFFF)

    # 1. Bad magic
    img = bytearray(valid_image_bytes)
    struct.pack_into("<I", img, 0, 0xDEADBEEF)
    fix_crc(img)
    tests.append(("Bad magic (0xDEADBEEF)", img))

    # 2. Unsupported version
    img = bytearray(valid_image_bytes)
    struct.pack_into("<I", img, 4, 2)
    fix_crc(img)
    tests.append(("Unsupported version (2)", img))

    # 3. Bad metadata CRC32
    img = bytearray(valid_image_bytes)
    orig_crc = struct.unpack_from("<I", img, 0x50)[0]
    struct.pack_into("<I", img, 0x50, orig_crc ^ 0xFFFFFFFF)
    tests.append(("Bad metadata CRC32", img))

    # 4. Object table out of bounds
    img = bytearray(valid_image_bytes)
    struct.pack_into("<Q", img, 0x20, len(img) + 512)  # object_table_offset past EOF
    # (CRC not fixable if table out of bounds)
    tests.append(("Object table offset past EOF", img))

    # 5. File payload out of bounds
    img = bytearray(valid_image_bytes)
    # Mutate data_length of first regular file in object table (entry 3 at offset 512 + 2*64 = 640)
    # data_length is at offset 0x28 within entry
    struct.pack_into("<Q", img, 512 + 2 * 64 + 0x28, len(img) * 2)
    fix_crc(img)
    tests.append(("File payload data_length past EOF", img))

    # 6. Duplicate object ID
    img = bytearray(valid_image_bytes)
    # Change object ID of entry 2 (offset 512 + 64) to 1
    struct.pack_into("<I", img, 512 + 64, 1)
    fix_crc(img)
    tests.append(("Duplicate object ID (two objects with ID 1)", img))

    # 7. Bad parent ID (non-existent parent)
    img = bytearray(valid_image_bytes)
    # Change parent ID of entry 2 to 999
    struct.pack_into("<I", img, 512 + 64 + 4, 999)
    fix_crc(img)
    tests.append(("Bad parent ID (parent_id = 999)", img))

    # 8. Cyclic parent relationship
    img = bytearray(valid_image_bytes)
    # Make entry 2 (parent_id was 1) and entry 4 (parent_id was 1) point to each other
    # Entry 2 obj_id=2 -> parent_id=4, Entry 4 obj_id=4 -> parent_id=2
    struct.pack_into("<I", img, 512 + 1 * 64 + 4, 4)  # obj 2 parent = 4
    struct.pack_into("<I", img, 512 + 3 * 64 + 4, 2)  # obj 4 parent = 2
    fix_crc(img)
    tests.append(("Cyclic parent relationship (2 <-> 4)", img))

    # 9. Invalid name range in string table
    img = bytearray(valid_image_bytes)
    # Entry 2 name_length = 50000 (past string table size)
    struct.pack_into("<I", img, 512 + 1 * 64 + 0x1C, 50000)
    fix_crc(img)
    tests.append(("Invalid name range (name_length exceeds string table)", img))

    # 10. Overlapping data ranges
    img = bytearray(valid_image_bytes)
    # Mutate data_offset of entry 7 (/sbin/launchd) to overlap entry 3 (/bin/sh)
    sh_offset = struct.unpack_from("<Q", img, 512 + 2 * 64 + 0x20)[0]
    struct.pack_into("<Q", img, 512 + 6 * 64 + 0x20, sh_offset)  # launchd at same offset as sh
    fix_crc(img)
    tests.append(("Overlapping data ranges (/sbin/launchd overlaps /bin/sh)", img))

    # 11. Directory with non-zero data_offset
    img = bytearray(valid_image_bytes)
    struct.pack_into("<Q", img, 512 + 1 * 64 + 0x20, 2048)  # /bin has data_offset=2048
    fix_crc(img)
    tests.append(("Directory with non-zero data_offset", img))

    # 12. Non-sector-aligned image size
    img = bytearray(valid_image_bytes) + b"\x42" * 17
    tests.append(("Image size not sector-aligned", img))

    print(f"\n[CORRUPTION TESTS] Running {len(tests)} negative tests...")
    rejected_count = 0
    for idx, (desc, corrupted_bytes) in enumerate(tests, start=1):
        try:
            parse_and_verify(corrupted_bytes)
            print(f"  FAILED (accepted!): Test {idx} - {desc}")
        except XZSFSError as e:
            print(f"  PASSED (rejected): Test {idx} - {desc} -> {e}")
            rejected_count += 1
        except Exception as e:
            print(f"  PASSED (rejected with exception): Test {idx} - {desc} -> {e}")
            rejected_count += 1

    all_passed = (rejected_count == len(tests))
    print(f"[CORRUPTION TESTS] Total: {len(tests)}, Rejected: {rejected_count}, Accepted: {len(tests) - rejected_count}")
    return len(tests), all_passed


def main():
    parser = argparse.ArgumentParser(description="Independent XZSFS v1 Image Verifier")
    parser.add_argument("image", help="Path to XZSFS image file")
    parser.add_argument("--root", default="rootfs/xzs-root", help="Root filesystem source tree for payload verification")
    parser.add_argument("--run-corruption-tests", action="store_true", help="Run negative corruption test suite")
    args = parser.parse_args()

    if not os.path.isfile(args.image):
        print(f"ERROR: Image file not found: {args.image}", file=sys.stderr)
        sys.exit(1)

    image_bytes = open(args.image, "rb").read()

    # Structural & Payload verification
    try:
        sb, objects, extracted = parse_and_verify(image_bytes, source_root=args.root)
    except XZSFSError as e:
        print(f"VERIFICATION FAILED: {e}", file=sys.stderr)
        sys.exit(1)

    print("=== XZSFS V1 IMAGE VERIFICATION SUCCESS ===")
    print(f"Image File:         {args.image}")
    print(f"Image Size:         {sb['image_size_bytes']} bytes")
    print(f"Format Version:     {sb['format_version']}")
    print(f"Object Count:       {sb['object_count']}")
    print(f"Metadata CRC32:     0x{sb['metadata_crc32']:08x}")
    print(f"Payload Data Size:  {sb['data_size']} bytes")
    print("\n--- Object Hierarchy ---")
    for obj_id in sorted(objects.keys()):
        obj = objects[obj_id]
        type_str = "DIR " if obj.type == XZSFS_TYPE_DIR else "FILE"
        print(f"  [{obj.object_id:2d}] parent={obj.parent_id:2d} {type_str} {oct(obj.mode)} {obj.full_path:20s} size={obj.data_length:6d} offset={obj.data_offset}")

    # Check Required Paths (§19)
    required_paths = {
        "/": XZSFS_TYPE_DIR,
        "/dev": XZSFS_TYPE_DIR,
        "/sbin": XZSFS_TYPE_DIR,
        "/sbin/launchd": XZSFS_TYPE_REG,
        "/bin": XZSFS_TYPE_DIR,
        "/bin/sh": XZSFS_TYPE_REG,
        "/etc": XZSFS_TYPE_DIR,
        "/tmp": XZSFS_TYPE_DIR,
        "/var": XZSFS_TYPE_DIR,
    }

    path_map = {obj.full_path: obj for obj in objects.values()}
    print("\n--- Required Path Verification (§19) ---")
    all_paths_ok = True
    for req_path, req_type in required_paths.items():
        if req_path not in path_map:
            print(f"  MISSING: {req_path}")
            all_paths_ok = False
        else:
            obj = path_map[req_path]
            if obj.type != req_type:
                print(f"  TYPE MISMATCH: {req_path} (expected type {req_type}, got {obj.type})")
                all_paths_ok = False
            else:
                print(f"  OK: {req_path} (ID {obj.object_id})")

    if not all_paths_ok:
        print("ERROR: Required paths missing or type mismatch", file=sys.stderr)
        sys.exit(1)

    # Read Extraction Test (§20)
    print("\n--- Payload Extraction Test (§20) ---")
    launchd_bytes = extracted.get("/sbin/launchd")
    sh_bytes = extracted.get("/bin/sh")

    if not launchd_bytes or not sh_bytes:
        print("ERROR: Failed to extract /sbin/launchd or /bin/sh", file=sys.stderr)
        sys.exit(1)

    src_launchd = open(os.path.join(args.root, "sbin/launchd"), "rb").read()
    src_sh = open(os.path.join(args.root, "bin/sh"), "rb").read()

    launchd_match = (launchd_bytes == src_launchd)
    sh_match = (sh_bytes == src_sh)

    print(f"  /sbin/launchd extracted: {len(launchd_bytes)}B, source: {len(src_launchd)}B, match={launchd_match}")
    print(f"  /bin/sh       extracted: {len(sh_bytes)}B, source: {len(src_sh)}B, match={sh_match}")

    if not (launchd_match and sh_match):
        print("ERROR: Payload extraction mismatch", file=sys.stderr)
        sys.exit(1)

    # Negative Corruption Tests (§21)
    if args.run_corruption_tests:
        count, all_rejected = run_corruption_tests(image_bytes)
        if not all_rejected or count < 10:
            print(f"ERROR: Corruption tests failed (rejected={all_rejected}, count={count})", file=sys.stderr)
            sys.exit(1)

    print("\n=== ALL HOST VERIFICATION GATES PASSED ===")


if __name__ == "__main__":
    main()
