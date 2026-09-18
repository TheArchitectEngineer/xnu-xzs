#!/usr/bin/env python3
"""
scripts/symbolicate_elr.py - Symbolicate ARM64 ELR_EL1 addresses for XNU kernel
Maps raw ELR_EL1 runtime addresses (KVA or physical entry addresses) to exact symbols and source lines.
"""

import sys
import os
import subprocess
import argparse
import re

DEFAULT_KERNEL_PATHS = [
    "src/xnu/BUILD/obj/DEVELOPMENT_ARM64_VMAPPLE/kernel.development.vmapple",
    "artifacts/builds/kernel.development.vmapple",
    "src/xnu/BUILD/obj/kernel.development.vmapple",
    "src/xnu/BUILD/obj/kernel.development.unslid",
]

LINK_BASE = 0xfffffe0007004000
PHYS_BASE = 0x82000000

def find_kernel_binary(custom_path=None):
    if custom_path:
        if os.path.exists(custom_path):
            return custom_path
        print(f"Error: Specified kernel '{custom_path}' not found.")
        sys.exit(1)

    for path in DEFAULT_KERNEL_PATHS:
        if os.path.exists(path):
            return path
    print("Error: Could not locate kernel Mach-O binary. Please build XNU or specify with --kernel.")
    sys.exit(1)

def parse_nm_symbols(kernel_path):
    cmd = ["/usr/bin/nm", "-n", kernel_path]
    try:
        out = subprocess.check_output(cmd, stderr=subprocess.DEVNULL).decode('utf-8', errors='ignore')
    except Exception as e:
        return []

    symbols = []
    for line in out.splitlines():
        parts = line.strip().split()
        if len(parts) >= 3:
            addr_str, stype, sname = parts[0], parts[1], parts[2]
            try:
                addr = int(addr_str, 16)
                if addr >= LINK_BASE:
                    symbols.append((addr, sname.lstrip('_')))
            except ValueError:
                continue
    symbols.sort(key=lambda x: x[0])
    return symbols

def find_nearest_symbol(symbols, addr):
    if not symbols:
        return ("<unknown>", 0)
    
    lo, hi = 0, len(symbols) - 1
    best = None
    while lo <= hi:
        mid = (lo + hi) // 2
        if symbols[mid][0] <= addr:
            best = symbols[mid]
            lo = mid + 1
        else:
            hi = mid - 1
    
    if best:
        return (best[1], addr - best[0])
    return ("<before kernel start>", 0)

def run_atos(kernel_path, addr):
    cmd = ["/usr/bin/atos", "-o", kernel_path, f"0x{addr:x}"]
    try:
        out = subprocess.check_output(cmd, stderr=subprocess.DEVNULL).decode('utf-8', errors='ignore').strip()
        return out
    except Exception:
        return ""

def symbolicate(elr_raw, slide=0, kernel_path=None):
    kernel = find_kernel_binary(kernel_path)
    
    # Check if address is physical (before MMU) or virtual
    runtime_addr = elr_raw
    if PHYS_BASE <= runtime_addr < (PHYS_BASE + 0x08000000):
        # Physical address prior to MMU transition
        kva = LINK_BASE + (runtime_addr - PHYS_BASE)
        is_phys = True
    else:
        kva = runtime_addr
        is_phys = False

    unslid = kva - slide
    symbols = parse_nm_symbols(kernel)
    sym_name, offset = find_nearest_symbol(symbols, unslid)
    atos_out = run_atos(kernel, unslid)

    source_info = "<unknown>"
    # Extract source file & line from atos output if present: e.g. "pmap_bootstrap (in kernel) (pmap.c:2288)"
    match = re.search(r'\(([^)]+:[0-9]+)\)', atos_out)
    if match:
        source_info = match.group(1)
    elif "in " in atos_out:
        source_info = atos_out

    return {
        "raw_elr": f"0x{elr_raw:016x}",
        "is_phys": is_phys,
        "runtime_kva": f"0x{kva:016x}",
        "unslid_addr": f"0x{unslid:016x}",
        "sym_name": sym_name,
        "sym_offset": offset,
        "source": source_info,
        "atos_full": atos_out,
        "kernel": kernel
    }

def main():
    parser = argparse.ArgumentParser(description="Symbolicate ARM64 ELR_EL1 addresses for XNU")
    parser.add_argument("elr", help="Hex ELR_EL1 value (e.g. 0xfffffe0007512bbc or 0x82512bbc)")
    parser.add_argument("--slide", default="0x0", help="KASLR slide in hex (default: 0x0)")
    parser.add_argument("--kernel", default=None, help="Path to kernel Mach-O binary")
    args = parser.parse_args()

    elr_val = int(args.elr, 16) if args.elr.startswith(("0x", "0X")) else int(args.elr, 0)
    slide_val = int(args.slide, 16) if args.slide.startswith(("0x", "0X")) else int(args.slide, 0)

    res = symbolicate(elr_val, slide_val, args.kernel)

    print(f"Runtime ELR : {res['raw_elr']}")
    print(f"Kernel slide: 0x{slide_val:x}")
    print(f"Unslid ELR  : {res['unslid_addr']}")
    print(f"Symbol      : {res['sym_name']}")
    print(f"Offset      : +0x{res['sym_offset']:x}")
    print(f"Source      : {res['source']}")


if __name__ == "__main__":
    main()
