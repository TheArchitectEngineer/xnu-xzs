#!/usr/bin/env python3
"""
mkbootimg.py - Generate Android boot.img (Header Version 0)
Compatible with Qualcomm MSM8996 and Sony S1 Bootloader
"""

import argparse
import hashlib
import struct
import sys

BOOT_MAGIC = b"ANDROID!"
BOOT_MAGIC_SIZE = 8
BOOT_NAME_SIZE = 16
BOOT_ARGS_SIZE = 512
BOOT_EXTRA_ARGS_SIZE = 1024

def pad_bytes(data: bytes, page_size: int) -> bytes:
    remainder = len(data) % page_size
    if remainder != 0:
        return data + b"\x00" * (page_size - remainder)
    return data

def main():
    parser = argparse.ArgumentParser(description="Pack Android boot.img (v0)")
    parser.add_argument("--kernel", required=True, help="Path to kernel binary")
    parser.add_argument("--ramdisk", help="Path to ramdisk binary")
    parser.add_argument("--second", help="Path to second stage binary")
    parser.add_argument("--dtb", help="Path to device tree blob (DTB)")
    parser.add_argument("--cmdline", default="", help="Kernel command line")
    parser.add_argument("--base", type=lambda x: int(x, 0), default=0x80000000, help="Base physical memory address")
    parser.add_argument("--kernel_offset", type=lambda x: int(x, 0), default=0x00080000, help="Kernel offset from base")
    parser.add_argument("--ramdisk_offset", type=lambda x: int(x, 0), default=0x02200000, help="Ramdisk offset from base")
    parser.add_argument("--second_offset", type=lambda x: int(x, 0), default=0x00f00000, help="Second offset from base")
    parser.add_argument("--tags_offset", type=lambda x: int(x, 0), default=0x00000100, help="Tags offset from base")
    parser.add_argument("--pagesize", type=int, default=4096, choices=[2048, 4096, 8192, 16384], help="Page size")
    parser.add_argument("--os_version", default="8.0.0", help="OS version string")
    parser.add_argument("--os_patch_level", default="2018-03", help="OS security patch level string (YYYY-MM)")
    parser.add_argument("-o", "--output", required=True, help="Output boot.img path")
    args = parser.parse_args()

    # Read inputs
    with open(args.kernel, "rb") as f:
        kernel_data = f.read()

    ramdisk_data = b""
    if args.ramdisk:
        with open(args.ramdisk, "rb") as f:
            ramdisk_data = f.read()

    second_data = b""
    if args.second:
        with open(args.second, "rb") as f:
            second_data = f.read()

    dtb_data = b""
    if args.dtb:
        with open(args.dtb, "rb") as f:
            dtb_data = f.read()

    kernel_size = len(kernel_data)
    ramdisk_size = len(ramdisk_data)
    second_size = len(second_data)
    dt_size = len(dtb_data)

    kernel_addr = args.base + args.kernel_offset
    ramdisk_addr = args.base + args.ramdisk_offset
    second_addr = args.base + args.second_offset
    tags_addr = args.base + args.tags_offset

    # Parse OS version and patch level into bitfield
    os_ver = 0
    try:
        parts = [int(p) for p in args.os_version.split(".")]
        a = parts[0] if len(parts) > 0 else 0
        b = parts[1] if len(parts) > 1 else 0
        c = parts[2] if len(parts) > 2 else 0
        os_ver = (a << 14) | (b << 7) | c
    except Exception:
        pass

    os_patch = 0
    try:
        y, m = [int(p) for p in args.os_patch_level.split("-")]
        os_patch = ((y - 2000) << 4) | m
    except Exception:
        pass

    os_version_field = (os_ver << 11) | os_patch

    # Construct cmdline & extra_cmdline
    cmdline_bytes = args.cmdline.encode("utf-8")
    if len(cmdline_bytes) > BOOT_ARGS_SIZE:
        main_cmdline = cmdline_bytes[:BOOT_ARGS_SIZE]
        extra_cmdline = cmdline_bytes[BOOT_ARGS_SIZE:BOOT_ARGS_SIZE + BOOT_EXTRA_ARGS_SIZE]
    else:
        main_cmdline = cmdline_bytes
        extra_cmdline = b""

    main_cmdline = main_cmdline.ljust(BOOT_ARGS_SIZE, b"\x00")
    extra_cmdline = extra_cmdline.ljust(BOOT_EXTRA_ARGS_SIZE, b"\x00")

    # Compute SHA1 of items
    sha = hashlib.sha1()
    sha.update(kernel_data)
    sha.update(struct.pack("<I", kernel_size))
    sha.update(ramdisk_data)
    sha.update(struct.pack("<I", ramdisk_size))
    sha.update(second_data)
    sha.update(struct.pack("<I", second_size))
    if dt_size > 0:
        sha.update(dtb_data)
        sha.update(struct.pack("<I", dt_size))
    digest = sha.digest().ljust(32, b"\x00")

    # Pack header v0
    # struct boot_img_hdr {
    #   char magic[8];
    #   uint32_t kernel_size;
    #   uint32_t kernel_addr;
    #   uint32_t ramdisk_size;
    #   uint32_t ramdisk_addr;
    #   uint32_t second_size;
    #   uint32_t second_addr;
    #   uint32_t tags_addr;
    #   uint32_t page_size;
    #   uint32_t dt_size; // header version 0 uses this for dt_size
    #   uint32_t os_version;
    #   char name[16];
    #   char cmdline[512];
    #   char id[32];
    #   char extra_cmdline[1024];
    # }
    header = struct.pack(
        "<8s10I16s512s32s1024s",
        BOOT_MAGIC,
        kernel_size,
        kernel_addr,
        ramdisk_size,
        ramdisk_addr,
        second_size,
        second_addr,
        tags_addr,
        args.pagesize,
        dt_size,
        os_version_field,
        b"".ljust(BOOT_NAME_SIZE, b"\x00"),
        main_cmdline,
        digest,
        extra_cmdline
    )

    with open(args.output, "wb") as out:
        out.write(pad_bytes(header, args.pagesize))
        out.write(pad_bytes(kernel_data, args.pagesize))
        if ramdisk_size > 0:
            out.write(pad_bytes(ramdisk_data, args.pagesize))
        if second_size > 0:
            out.write(pad_bytes(second_data, args.pagesize))
        if dt_size > 0:
            out.write(pad_bytes(dtb_data, args.pagesize))

        total_bytes = out.tell()
    print(f"Created {args.output} successfully (total {total_bytes} bytes).")

if __name__ == "__main__":
    main()
