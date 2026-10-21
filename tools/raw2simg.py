#!/usr/bin/env python3
"""
Convert a raw HFS+ image (ios16-root.img) into an Android sparse image (.simg)
using the HFS+ Allocation File bitmap:
- Every allocated HFS+ block (bitmap bit == 1) is preserved 100%:
  - Non-zero allocated blocks -> CHUNK_TYPE_RAW (0xCAC1)
  - All-zero allocated blocks -> CHUNK_TYPE_FILL (0xCAC2, fill=0x00000000)
- Every unallocated HFS+ block (bitmap bit == 0) -> CHUNK_TYPE_DONT_CARE (0xCAC3)
"""

import os
import struct
import sys

SPARSE_HEADER_MAGIC = 0xED26FF3A
CHUNK_TYPE_RAW = 0xCAC1
CHUNK_TYPE_FILL = 0xCAC2
CHUNK_TYPE_DONT_CARE = 0xCAC3

BLK_SZ = 4096
ZERO_BLK = b"\x00" * BLK_SZ
MAX_RAW_BLKS = 16384  # 64 MB max per RAW chunk


def read_allocation_bitmap(fin, vh: bytes, total_blks: int) -> bytes:
    alloc_logical_size = struct.unpack(">Q", vh[112:120])[0]
    bitmap = bytearray()
    for i in range(8):
        off = 128 + i * 8
        start_blk, blk_cnt = struct.unpack(">II", vh[off : off + 8])
        if blk_cnt == 0:
            break
        fin.seek(start_blk * BLK_SZ)
        bitmap.extend(fin.read(blk_cnt * BLK_SZ))
    needed_bytes = (total_blks + 7) // 8
    return bytes(bitmap[: max(alloc_logical_size, needed_bytes)])


def main():
    if len(sys.argv) != 3:
        print(f"Usage: {sys.argv[0]} <input.img> <output.simg>")
        sys.exit(1)

    in_path, out_path = sys.argv[1], sys.argv[2]
    file_size = os.path.getsize(in_path)
    if file_size % BLK_SZ != 0:
        raise ValueError(f"Input size {file_size} is not a multiple of {BLK_SZ}")

    total_blks = file_size // BLK_SZ

    with open(in_path, "rb") as fin:
        fin.seek(1024)
        vh = fin.read(512)
        sig = vh[0:2]
        if sig not in (b"HX", b"H+"):
            raise ValueError(f"Unexpected HFS+ signature: {sig!r}")
        vh_blk_sz = struct.unpack(">I", vh[40:44])[0]
        vh_total_blks = struct.unpack(">I", vh[44:48])[0]
        vh_free_blks = struct.unpack(">I", vh[48:52])[0]
        used_blks = vh_total_blks - vh_free_blks
        print(
            f"[raw2simg] HFS+ sig={sig.decode()} blk_sz={vh_blk_sz} "
            f"total_blks={vh_total_blks} used_blks={used_blks} "
            f"({used_blks * BLK_SZ / (1024**3):.2f} GiB) free_blks={vh_free_blks}"
        )

        bitmap = read_allocation_bitmap(fin, vh, total_blks)

        with open(out_path, "wb") as fout:
            fout.write(b"\x00" * 28)
            total_chunks = 0
            raw_blks_written = 0
            fill_blks_written = 0
            dont_care_blks_written = 0

            cur_type = None  # 'RAW', 'FILL', 'DONT_CARE'
            cur_count = 0
            raw_buf = bytearray()

            def flush_current():
                nonlocal total_chunks, cur_type, cur_count
                nonlocal raw_blks_written, fill_blks_written, dont_care_blks_written
                if cur_type is None or cur_count == 0:
                    return
                if cur_type == "RAW":
                    hdr = struct.pack(
                        "<HHII", CHUNK_TYPE_RAW, 0, cur_count, 12 + len(raw_buf)
                    )
                    fout.write(hdr)
                    fout.write(raw_buf)
                    raw_buf.clear()
                    raw_blks_written += cur_count
                elif cur_type == "FILL":
                    hdr = struct.pack("<HHII", CHUNK_TYPE_FILL, 0, cur_count, 16)
                    fout.write(hdr)
                    fout.write(struct.pack("<I", 0))
                    fill_blks_written += cur_count
                elif cur_type == "DONT_CARE":
                    hdr = struct.pack("<HHII", CHUNK_TYPE_DONT_CARE, 0, cur_count, 12)
                    fout.write(hdr)
                    dont_care_blks_written += cur_count
                total_chunks += 1
                cur_type = None
                cur_count = 0

            fin.seek(0)
            BATCH_BLKS = 4096  # 16 MB
            blk_idx = 0
            while blk_idx < total_blks:
                to_read = min(BATCH_BLKS, total_blks - blk_idx)
                # Fast check: are all blocks in this batch unallocated?
                if blk_idx % 8 == 0 and to_read % 8 == 0:
                    b_start = blk_idx // 8
                    b_end = (blk_idx + to_read) // 8
                    if bitmap[b_start:b_end] == b"\x00" * (b_end - b_start):
                        fin.seek((blk_idx + to_read) * BLK_SZ)
                        if cur_type != "DONT_CARE":
                            flush_current()
                            cur_type = "DONT_CARE"
                        cur_count += to_read
                        blk_idx += to_read
                        continue

                buf = fin.read(to_read * BLK_SZ)
                mv = memoryview(buf)
                for i in range(to_read):
                    b = blk_idx + i
                    is_alloc = (bitmap[b >> 3] >> (7 - (b & 7))) & 1
                    if not is_alloc:
                        if cur_type != "DONT_CARE":
                            flush_current()
                            cur_type = "DONT_CARE"
                        cur_count += 1
                    else:
                        blk_bytes = mv[i * BLK_SZ : (i + 1) * BLK_SZ]
                        if blk_bytes == ZERO_BLK:
                            if cur_type != "FILL":
                                flush_current()
                                cur_type = "FILL"
                            cur_count += 1
                        else:
                            if cur_type != "RAW" or cur_count >= MAX_RAW_BLKS:
                                flush_current()
                                cur_type = "RAW"
                            raw_buf.extend(blk_bytes)
                            cur_count += 1
                blk_idx += to_read

            flush_current()

            fout.seek(0)
            sparse_hdr = struct.pack(
                "<IHHHHIIII",
                SPARSE_HEADER_MAGIC,
                1,
                0,
                28,
                12,
                BLK_SZ,
                total_blks,
                total_chunks,
                0,
            )
            fout.write(sparse_hdr)

    out_size = os.path.getsize(out_path)
    print(
        f"[raw2simg] Wrote {out_path}: {out_size / (1024**3):.2f} GiB "
        f"(chunks={total_chunks}, raw_blks={raw_blks_written}, "
        f"fill_blks={fill_blks_written}, dont_care_blks={dont_care_blks_written})"
    )


if __name__ == "__main__":
    main()
