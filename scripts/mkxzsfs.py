#!/usr/bin/env python3
"""
mkxzsfs.py - Deterministic XZSFS v1 Root Filesystem Image Generator
Target: Apple XNU on Sony Xperia XZs (MSM8996)
"""

import argparse
import hashlib
import os
import re
import struct
import sys
import zlib

# XZSFS v1 Constants
XZSFS_MAGIC = 0x5346535A  # 'XZSF'
XZSFS_FORMAT_VERSION = 1
XZSFS_SUPERBLOCK_SIZE = 512
XZSFS_SECTOR_SIZE = 512
XZSFS_OBJECT_SIZE = 64
XZSFS_DATA_ALIGNMENT = 512

XZSFS_TYPE_DIR = 1
XZSFS_TYPE_REG = 2

NAME_REGEX = re.compile(r'^[a-zA-Z0-9._-]+$')


class FSObject:
    def __init__(self, rel_path, is_dir, host_path=None):
        self.rel_path = rel_path  # e.g. "", "sbin", "sbin/launchd"
        self.is_dir = is_dir
        self.host_path = host_path
        self.object_id = 0
        self.parent_id = 0
        self.type = XZSFS_TYPE_DIR if is_dir else XZSFS_TYPE_REG
        self.mode = 0o755 if (is_dir or (host_path and os.access(host_path, os.X_OK))) else 0o644
        self.uid = 0
        self.gid = 0
        self.name = os.path.basename(rel_path) if rel_path else ""
        self.name_offset = 0
        self.name_length = len(self.name.encode('ascii')) if self.name else 0
        self.data_offset = 0
        self.data_length = 0 if is_dir else os.path.getsize(host_path)
        self.payload = b"" if is_dir else open(host_path, "rb").read()
        self.payload_sha256 = None if is_dir else hashlib.sha256(self.payload).hexdigest()


def align_to(val, alignment):
    return (val + alignment - 1) & ~(alignment - 1)


def build_image(root_dir, output_img, manifest_path=None):
    if not os.path.isdir(root_dir):
        raise ValueError(f"Root directory does not exist: {root_dir}")

    # Discover objects deterministically
    # Root is always object 1
    root_obj = FSObject("", is_dir=True)
    objects = [root_obj]

    # Collect all items sorted by canonical relative path
    discovered = []
    for dirpath, dirnames, filenames in os.walk(root_dir):
        # Sort in-place for determinism
        dirnames.sort()
        filenames.sort()

        rel_dir = os.path.relpath(dirpath, root_dir)
        if rel_dir == ".":
            rel_dir = ""

        for d in dirnames:
            rel_path = os.path.join(rel_dir, d) if rel_dir else d
            host_path = os.path.join(dirpath, d)
            discovered.append((rel_path, True, host_path))

        for f in filenames:
            rel_path = os.path.join(rel_dir, f) if rel_dir else f
            host_path = os.path.join(dirpath, f)
            discovered.append((rel_path, False, host_path))

    # Sort discovered items deterministically
    discovered.sort(key=lambda x: x[0])

    for rel_path, is_dir, host_path in discovered:
        obj = FSObject(rel_path, is_dir, host_path)
        objects.append(obj)

    # Validate names and assign object IDs (1-based)
    path_to_id = {"": 1}
    for idx, obj in enumerate(objects, start=1):
        obj.object_id = idx
        path_to_id[obj.rel_path] = idx

        if obj.object_id > 1:
            if not NAME_REGEX.match(obj.name):
                raise ValueError(f"Invalid filename (must be ASCII [a-zA-Z0-9._-]): '{obj.name}'")
            if '/' in obj.name or '\0' in obj.name:
                raise ValueError(f"Filename contains illegal characters: '{obj.name}'")

    # Assign parent IDs and check for duplicate names under same parent
    for obj in objects:
        if obj.object_id == 1:
            obj.parent_id = 1
        else:
            parent_rel = os.path.dirname(obj.rel_path)
            if parent_rel not in path_to_id:
                raise ValueError(f"Parent directory not found for: '{obj.rel_path}'")
            obj.parent_id = path_to_id[parent_rel]

    # Validate no duplicate names under any parent
    seen_under_parent = set()
    for obj in objects:
        if obj.object_id > 1:
            key = (obj.parent_id, obj.name)
            if key in seen_under_parent:
                raise ValueError(f"Duplicate filename '{obj.name}' under parent ID {obj.parent_id}")
            seen_under_parent.add(key)

    # Build String Table
    # Starts with root string "" (NUL byte at offset 0)
    str_table = bytearray(b"\0")
    for obj in objects:
        if obj.object_id == 1:
            obj.name_offset = 0
            obj.name_length = 0
        else:
            name_bytes = obj.name.encode('ascii')
            obj.name_offset = len(str_table)
            obj.name_length = len(name_bytes)
            str_table.extend(name_bytes + b"\0")

    raw_str_table_size = len(str_table)
    padded_str_table_size = align_to(raw_str_table_size, XZSFS_SECTOR_SIZE)
    str_table.extend(b"\0" * (padded_str_table_size - raw_str_table_size))

    # Calculate Layout Offsets
    object_count = len(objects)
    object_table_offset = XZSFS_SUPERBLOCK_SIZE  # 512
    object_table_size = object_count * XZSFS_OBJECT_SIZE
    object_table_padded_size = align_to(object_table_size, XZSFS_SECTOR_SIZE)

    string_table_offset = object_table_offset + object_table_padded_size
    string_table_size = len(str_table)

    data_offset = string_table_offset + string_table_size

    # Assign data offsets to regular files
    current_data_offset = data_offset
    for obj in objects:
        if obj.is_dir:
            obj.data_offset = 0
            obj.data_length = 0
        else:
            obj.data_offset = current_data_offset
            padded_payload_len = align_to(obj.data_length, XZSFS_DATA_ALIGNMENT)
            current_data_offset += padded_payload_len

    data_size = current_data_offset - data_offset
    image_size_bytes = current_data_offset

    # Serialize Object Table
    obj_table_bytes = bytearray()
    for obj in objects:
        # struct: object_id(I), parent_id(I), type(I), mode(I), uid(I), gid(I),
        #         name_offset(I), name_length(I), data_offset(Q), data_length(Q),
        #         reserved1(Q), reserved2(Q) = 64 bytes
        entry = struct.pack(
            "<IIIIIIIIQQQQ",
            obj.object_id,
            obj.parent_id,
            obj.type,
            obj.mode,
            obj.uid,
            obj.gid,
            obj.name_offset,
            obj.name_length,
            obj.data_offset,
            obj.data_length,
            0,
            0
        )
        obj_table_bytes.extend(entry)

    # Pad object table to sector boundary
    obj_table_bytes.extend(b"\0" * (object_table_padded_size - object_table_size))

    # Construct Superblock with metadata_crc32 = 0
    # struct format:
    # 0x0000: magic (I)
    # 0x0004: format_version (I)
    # 0x0008: header_size (I)
    # 0x000C: sector_size (I)
    # 0x0010: image_size_bytes (Q)
    # 0x0018: object_count (I)
    # 0x001C: root_object_id (I)
    # 0x0020: object_table_offset (Q)
    # 0x0028: object_table_size (Q)
    # 0x0030: string_table_offset (Q)
    # 0x0038: string_table_size (Q)
    # 0x0040: data_offset (Q)
    # 0x0048: data_size (Q)
    # 0x0050: metadata_crc32 (I)
    # 0x0054: reserved (I)
    # 0x0058: padding (424s)
    # Total: 512 bytes
    sb_without_crc = struct.pack(
        "<IIIIQIIQQQQQQII424s",
        XZSFS_MAGIC,
        XZSFS_FORMAT_VERSION,
        XZSFS_SUPERBLOCK_SIZE,
        XZSFS_SECTOR_SIZE,
        image_size_bytes,
        object_count,
        1,  # root_object_id
        object_table_offset,
        object_table_size,
        string_table_offset,
        string_table_size,
        data_offset,
        data_size,
        0,  # metadata_crc32 = 0
        0,  # reserved
        b"\0" * 424
    )

    # Compute Metadata CRC32 (Superblock with crc=0 + Object Table + String Table)
    crc = zlib.crc32(sb_without_crc)
    crc = zlib.crc32(obj_table_bytes[:object_table_size], crc)
    crc = zlib.crc32(str_table[:string_table_size], crc)
    metadata_crc32 = crc & 0xFFFFFFFF

    # Final Superblock with computed CRC
    sb_final = struct.pack(
        "<IIIIQIIQQQQQQII424s",
        XZSFS_MAGIC,
        XZSFS_FORMAT_VERSION,
        XZSFS_SUPERBLOCK_SIZE,
        XZSFS_SECTOR_SIZE,
        image_size_bytes,
        object_count,
        1,
        object_table_offset,
        object_table_size,
        string_table_offset,
        string_table_size,
        data_offset,
        data_size,
        metadata_crc32,
        0,
        b"\0" * 424
    )

    # Assemble and write final image
    os.makedirs(os.path.dirname(os.path.abspath(output_img)), exist_ok=True)
    with open(output_img, "wb") as f:
        f.write(sb_final)
        f.write(obj_table_bytes)
        f.write(str_table)
        for obj in objects:
            if not obj.is_dir:
                f.write(obj.payload)
                pad_len = align_to(obj.data_length, XZSFS_DATA_ALIGNMENT) - obj.data_length
                if pad_len > 0:
                    f.write(b"\0" * pad_len)

    actual_file_size = os.path.getsize(output_img)
    if actual_file_size != image_size_bytes:
        raise RuntimeError(f"Image size mismatch: expected {image_size_bytes}, got {actual_file_size}")

    img_sha256 = hashlib.sha256(open(output_img, "rb").read()).hexdigest()

    # Generate manifest
    manifest_data = {
        "filesystem": "XZSFS",
        "format_version": XZSFS_FORMAT_VERSION,
        "image_size_bytes": image_size_bytes,
        "image_sha256": img_sha256,
        "metadata_crc32": f"0x{metadata_crc32:08x}",
        "object_count": object_count,
        "superblock": {
            "magic": f"0x{XZSFS_MAGIC:08x}",
            "format_version": XZSFS_FORMAT_VERSION,
            "header_size": XZSFS_SUPERBLOCK_SIZE,
            "sector_size": XZSFS_SECTOR_SIZE,
            "object_table_offset": object_table_offset,
            "object_table_size": object_table_size,
            "string_table_offset": string_table_offset,
            "string_table_size": string_table_size,
            "data_offset": data_offset,
            "data_size": data_size,
        },
        "objects": []
    }

    for obj in objects:
        display_path = "/" + obj.rel_path if obj.rel_path else "/"
        manifest_data["objects"].append({
            "object_id": obj.object_id,
            "parent_id": obj.parent_id,
            "type": "directory" if obj.is_dir else "regular",
            "path": display_path,
            "mode": oct(obj.mode),
            "uid": obj.uid,
            "gid": obj.gid,
            "size": obj.data_length,
            "data_offset": obj.data_offset,
            "payload_sha256": obj.payload_sha256
        })

    if manifest_path:
        import json
        os.makedirs(os.path.dirname(os.path.abspath(manifest_path)), exist_ok=True)
        with open(manifest_path, "w") as f:
            json.dump(manifest_data, f, indent=2)

    print(f"[XZSFS] Successfully built image: {output_img}")
    print(f"[XZSFS]   Total size: {image_size_bytes} bytes ({image_size_bytes / 1024:.1f} KiB)")
    print(f"[XZSFS]   Objects: {object_count}")
    print(f"[XZSFS]   Metadata CRC32: 0x{metadata_crc32:08x}")
    print(f"[XZSFS]   SHA-256: {img_sha256}")
    if manifest_path:
        print(f"[XZSFS]   Manifest: {manifest_path}")

    return manifest_data


def main():
    parser = argparse.ArgumentParser(description="Deterministic XZSFS v1 Image Generator")
    parser.add_argument("--root", default="rootfs/xzs-root", help="Root filesystem source directory")
    parser.add_argument("--output", default="artifacts/builds/xzs-rootfs.img", help="Output XZSFS image path")
    parser.add_argument("--manifest", default="artifacts/builds/xzs-rootfs-manifest.json", help="Output manifest JSON path")
    args = parser.parse_args()

    build_image(args.root, args.output, args.manifest)


if __name__ == "__main__":
    main()
