#!/usr/bin/env python3
"""
scripts/decode_features.py - ARM64 CPU Identification and Feature Register Decoder
Decodes ID_AA64MMFR0_EL1, ID_AA64PFR0_EL1, ID_AA64ISAR0_EL1, ID_AA64ISAR1_EL1.
"""

import sys
import argparse

def decode_mmfr0(val):
    tgran4 = (val >> 28) & 0xf
    tgran64 = (val >> 24) & 0xf
    tgran16 = (val >> 20) & 0xf
    bigend = (val >> 16) & 0xf
    snsbl = (val >> 12) & 0xf
    bigendel0 = (val >> 8) & 0xf
    asidbits = (val >> 4) & 0xf
    parange = val & 0xf

    lines = [f"ID_AA64MMFR0_EL1 = 0x{val:016x}"]
    lines.append(f"  TGran4:   0x{tgran4:x} -> {'SUPPORTED' if tgran4 == 0 else 'NOT supported' if tgran4 == 0xf else 'LPA2 supported' if tgran4 == 1 else 'Unknown'}")
    lines.append(f"  TGran16:  0x{tgran16:x} -> {'NOT supported' if tgran16 == 0 else 'SUPPORTED' if tgran16 == 1 else 'SUPPORTED (52-bit PA)' if tgran16 == 2 else 'Unknown'}")
    lines.append(f"  TGran64:  0x{tgran64:x} -> {'SUPPORTED' if tgran64 == 0 else 'NOT supported' if tgran64 == 0xf else 'LPA2 supported' if tgran64 == 1 else 'Unknown'}")

    pa_map = {0: "32 bits (4 GB)", 1: "36 bits (64 GB)", 2: "40 bits (1 TB)", 3: "42 bits (4 TB)",
              4: "44 bits (16 TB)", 5: "48 bits (256 TB)", 6: "52 bits (4 PB)"}
    lines.append(f"  PARange:  0x{parange:x} -> {pa_map.get(parange, 'Unknown')}")
    lines.append(f"  ASIDBits: 0x{asidbits:x} -> {'8-bit ASID' if asidbits == 0 else '16-bit ASID' if asidbits == 2 else 'Unknown'}")
    return "\n".join(lines)

def decode_pfr0(val):
    lines = [f"ID_AA64PFR0_EL1  = 0x{val:016x}"]
    el3 = (val >> 12) & 0xf
    el2 = (val >> 8) & 0xf
    el1 = (val >> 4) & 0xf
    el0 = val & 0xf
    fp  = (val >> 16) & 0xf
    advsimd = (val >> 20) & 0xf
    gic = (val >> 24) & 0xf
    ras = (val >> 28) & 0xf
    sve = (val >> 32) & 0xf

    lines.append(f"  EL0:      {'AArch64 & AArch32' if el0 == 2 else 'AArch64 only' if el0 == 1 else 'Not implemented'}")
    lines.append(f"  EL1:      {'AArch64 & AArch32' if el1 == 2 else 'AArch64 only' if el1 == 1 else 'Not implemented'}")
    lines.append(f"  EL2:      {'Implemented' if el2 else 'Not implemented'}")
    lines.append(f"  EL3:      {'Implemented' if el3 else 'Not implemented'}")
    lines.append(f"  FP:       {'Implemented' if fp == 0 else 'Half-precision' if fp == 1 else 'None'}")
    lines.append(f"  AdvSIMD:  {'Implemented' if advsimd == 0 else 'Half-precision' if advsimd == 1 else 'None'}")
    lines.append(f"  GIC:      {'GIC CPU interface sysregs supported' if gic == 1 else 'None'}")
    lines.append(f"  RAS:      {'Implemented' if ras else 'None'}")
    lines.append(f"  SVE:      {'Implemented' if sve else 'None'}")
    return "\n".join(lines)

def decode_isar0(val):
    lines = [f"ID_AA64ISAR0_EL1 = 0x{val:016x}"]
    aes = (val >> 4) & 0xf
    sha1 = (val >> 8) & 0xf
    sha2 = (val >> 12) & 0xf
    crc32 = (val >> 16) & 0xf
    atomic = (val >> 20) & 0xf
    rdm = (val >> 28) & 0xf
    sha3 = (val >> 32) & 0xf
    sm3 = (val >> 36) & 0xf
    sm4 = (val >> 40) & 0xf
    dp = (val >> 44) & 0xf

    lines.append(f"  AES:      {'AES + PMULL' if aes == 2 else 'AES' if aes == 1 else 'None'}")
    lines.append(f"  SHA1:     {'Implemented' if sha1 == 1 else 'None'}")
    lines.append(f"  SHA2:     {'SHA-256' if sha2 == 1 else 'SHA-512' if sha2 == 2 else 'None'}")
    lines.append(f"  CRC32:    {'Implemented (ARMv8-A CRC)' if crc32 == 1 else 'None'}")
    lines.append(f"  LSE Atom: {'Implemented (ARMv8.1 LSE atomics)' if atomic == 2 else 'None (ARMv8.0 exclusive LL/SC only)'}")
    lines.append(f"  RDM:      {'Implemented' if rdm == 1 else 'None'}")
    lines.append(f"  SHA3:     {'Implemented' if sha3 == 1 else 'None'}")
    lines.append(f"  DotProd:  {'Implemented' if dp == 1 else 'None'}")
    return "\n".join(lines)

def decode_isar1(val):
    lines = [f"ID_AA64ISAR1_EL1 = 0x{val:016x}"]
    dpb = val & 0xf
    apa = (val >> 4) & 0xf
    api = (val >> 8) & 0xf
    jpa = (val >> 12) & 0xf
    jpi = (val >> 16) & 0xf
    fcma = (val >> 20) & 0xf
    lrcpc = (val >> 24) & 0xf
    gpa = (val >> 28) & 0xf
    gpi = (val >> 32) & 0xf
    sb  = (val >> 36) & 0xf
    specres = (val >> 40) & 0xf
    bf16 = (val >> 44) & 0xf
    i8mm = (val >> 52) & 0xf

    lines.append(f"  PAC (APA):{'QARMA PAC supported' if apa else 'None (No PAC)'}")
    lines.append(f"  PAC (API):{'Implementation-defined PAC supported' if api else 'None (No PAC)'}")
    lines.append(f"  SpecBarrier: {'SB implemented' if sb else 'None'}")
    lines.append(f"  LRCPC:    {'Implemented' if lrcpc else 'None'}")
    lines.append(f"  BF16:     {'Implemented' if bf16 else 'None'}")
    lines.append(f"  I8MM:     {'Implemented' if i8mm else 'None'}")
    return "\n".join(lines)

def main():
    parser = argparse.ArgumentParser(description="Decode ARM64 CPU Feature Registers")
    parser.add_argument("--mmfr0", default=None, help="Hex ID_AA64MMFR0_EL1")
    parser.add_argument("--pfr0", default=None, help="Hex ID_AA64PFR0_EL1")
    parser.add_argument("--isar0", default=None, help="Hex ID_AA64ISAR0_EL1")
    parser.add_argument("--isar1", default=None, help="Hex ID_AA64ISAR1_EL1")
    args = parser.parse_args()

    print("============================================================")
    print("ARM64 Hardware Feature Registers Decode")
    print("============================================================")
    if args.mmfr0:
        val = int(args.mmfr0, 16) if args.mmfr0.startswith(("0x", "0X")) else int(args.mmfr0, 0)
        print(decode_mmfr0(val))
        print("------------------------------------------------------------")
    if args.pfr0:
        val = int(args.pfr0, 16) if args.pfr0.startswith(("0x", "0X")) else int(args.pfr0, 0)
        print(decode_pfr0(val))
        print("------------------------------------------------------------")
    if args.isar0:
        val = int(args.isar0, 16) if args.isar0.startswith(("0x", "0X")) else int(args.isar0, 0)
        print(decode_isar0(val))
        print("------------------------------------------------------------")
    if args.isar1:
        val = int(args.isar1, 16) if args.isar1.startswith(("0x", "0X")) else int(args.isar1, 0)
        print(decode_isar1(val))
        print("------------------------------------------------------------")
    if not (args.mmfr0 or args.pfr0 or args.isar0 or args.isar1):
        print("Provide at least one register with --mmfr0, --pfr0, --isar0, or --isar1")

if __name__ == "__main__":
    main()
