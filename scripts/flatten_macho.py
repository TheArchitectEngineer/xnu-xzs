#!/usr/bin/env python3
"""
Flatten a Mach-O kernel binary so that each LC_SEGMENT_64 segment sits at
its exact virtual offset (vmaddr - virtBase) in physical RAM.

Standard Mach-O files omit zero-fill BSS gaps (such as between __DATA and __BOOTDATA),
so loading the raw file directly into RAM causes all subsequent segments (including
page tables and stacks in __BOOTDATA) to be misaligned by hundreds of kilobytes.

This script expands the Mach-O into a 1:1 linear memory image and updates all load
commands (segments, sections, symtab, linkedit data) so the file is both a valid Mach-O
and an in-memory mapped kernel image.
"""

import sys
import struct

def flatten_macho(in_path, out_path, base_va=None):
    with open(in_path, "rb") as f:
        raw = f.read()

    magic, cputype, cpusubtype, filetype, ncmds, sizeofcmds, flags, reserved = struct.unpack("<IIIIIIII", raw[:32])
    assert magic == 0xfeedfacf, "Not a 64-bit Mach-O"

    if base_va is None:
        off = 32
        for _ in range(ncmds):
            cmd, cmdsize = struct.unpack("<II", raw[off:off+8])
            if cmd == 0x19:
                segname = raw[off+8:off+24].rstrip(b"\x00").decode("latin1")
                if segname == "__TEXT":
                    base_va = struct.unpack("<Q", raw[off+24:off+32])[0]
                    break
            off += cmdsize
        print(f"Detected __TEXT base VA: 0x{base_va:x}")

    # Find max end of memory
    max_end = 0
    off = 32
    for _ in range(ncmds):
        cmd, cmdsize = struct.unpack("<II", raw[off:off+8])
        if cmd == 0x19: # LC_SEGMENT_64
            vmaddr, vmsize = struct.unpack("<QQ", raw[off+24:off+40])
            if vmaddr >= base_va:
                mem_off = vmaddr - base_va
                if mem_off + vmsize > max_end:
                    max_end = mem_off + vmsize
        off += cmdsize

    buf = bytearray(max_end)

    # 1. Copy segment file contents to their linear memory offsets
    off = 32
    for _ in range(ncmds):
        cmd, cmdsize = struct.unpack("<II", raw[off:off+8])
        if cmd == 0x19:
            segname = raw[off+8:off+24].rstrip(b"\x00").decode("latin1")
            vmaddr, vmsize, fileoff, filesize = struct.unpack("<QQQQ", raw[off+24:off+56])
            if vmaddr >= base_va and filesize > 0:
                mem_off = vmaddr - base_va
                buf[mem_off : mem_off + filesize] = raw[fileoff : fileoff + filesize]
        off += cmdsize

    # 2. Compute delta for __LINKEDIT
    old_linkedit_off = None
    new_linkedit_off = None
    off = 32
    for _ in range(ncmds):
        cmd, cmdsize = struct.unpack("<II", raw[off:off+8])
        if cmd == 0x19:
            segname = raw[off+8:off+24].rstrip(b"\x00").decode("latin1")
            if segname == "__LINKEDIT":
                vmaddr, vmsize, fileoff, filesize = struct.unpack("<QQQQ", raw[off+24:off+56])
                old_linkedit_off = fileoff
                new_linkedit_off = vmaddr - base_va
        off += cmdsize

    delta_linkedit = (new_linkedit_off - old_linkedit_off) if (old_linkedit_off and new_linkedit_off) else 0

    # 3. Update load commands in the flattened header
    off = 32
    for _ in range(ncmds):
        cmd, cmdsize = struct.unpack("<II", buf[off:off+8])
        if cmd == 0x19: # LC_SEGMENT_64
            vmaddr, vmsize, old_fileoff, filesize, maxprot, initprot, nsects, sflags = struct.unpack("<QQQQiiii", buf[off+24:off+72])
            if vmaddr >= base_va:
                new_fileoff = vmaddr - base_va
                struct.pack_into("<QQ", buf, off+40, new_fileoff, filesize)
                sect_off = off + 72
                for s in range(nsects):
                    s_addr, s_size, s_offset = struct.unpack("<QQI", buf[sect_off+32:sect_off+52])
                    if s_offset != 0 and s_addr >= base_va:
                        new_s_offset = s_addr - base_va
                        struct.pack_into("<I", buf, sect_off+48, new_s_offset)
                    sect_off += 80
        elif cmd == 0x2: # LC_SYMTAB
            symoff, nsyms, stroff, strsize = struct.unpack("<IIII", buf[off+8:off+24])
            struct.pack_into("<II", buf, off+8, symoff + delta_linkedit, nsyms)
            struct.pack_into("<II", buf, off+16, stroff + delta_linkedit, strsize)
        elif cmd == 0xb: # LC_DYSYMTAB
            fields = list(struct.unpack("<18I", buf[off+8:off+80]))
            for idx in [6, 8, 10, 12, 14, 16]:
                if fields[idx] != 0:
                    fields[idx] += delta_linkedit
            struct.pack_into("<18I", buf, off+8, *fields)
        elif cmd in (0x1d, 0x1e, 0x26, 0x29, 0x80000033, 0x80000034):
            dataoff, datasize = struct.unpack("<II", buf[off+8:off+16])
            if dataoff != 0:
                struct.pack_into("<I", buf, off+8, dataoff + delta_linkedit)
        off += cmdsize

    with open(out_path, "wb") as f:
        f.write(buf)

    print(f"Flattened {in_path} -> {out_path} ({len(buf)} bytes, delta_linkedit=0x{delta_linkedit:x})")

if __name__ == "__main__":
    if len(sys.argv) < 3:
        print("Usage: flatten_macho.py <in_kernel> <out_flat>")
        sys.exit(1)
    flatten_macho(sys.argv[1], sys.argv[2])
