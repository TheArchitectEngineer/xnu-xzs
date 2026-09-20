#!/usr/bin/env python3
"""
verify_d5m3_acceptance.py - Phase D5-M3 Acceptance Verifier
Target: Sony Xperia XZs (MSM8996 / Kryo) - Real Bare-Metal Execution

Verifies the native XZSFS read-only VFS driver diagnostic probe telemetry:
1. Complete 0xD520 checkpoint sequence (0x00 .. 0x90, 0x01).
2. Canonical telemetry keys matching frozen requirements.
3. Pre- and Post-mutation whole-md0 CRC32 matches (0x131e9191).
4. Strict enforcement of all hard boundaries (Strategy B, zero real vnodes, zero writes).
"""

import sys
import os
import re

REQUIRED_CHECKPOINTS = [
    (0xD520, 0x00, "0xD520/00: Enter D5-M3 probe"),
    (0xD520, 0x10, "0xD520/10: XZSFS registration pass"),
    (0xD520, 0x20, "0xD520/20: bdevvp(md0) pass"),
    (0xD520, 0x30, "0xD520/30: Superblock read pass"),
    (0xD520, 0x31, "0xD520/31: Superblock validation pass"),
    (0xD520, 0x40, "0xD520/40: Object table loaded"),
    (0xD520, 0x41, "0xD520/41: String table loaded"),
    (0xD520, 0x42, "0xD520/42: Metadata CRC pass"),
    (0xD520, 0x43, "0xD520/43: Graph validation pass"),
    (0xD520, 0x50, "0xD520/50: Root object resolved"),
    (0xD520, 0x60, "0xD520/60: Core lookup suite pass"),
    (0xD520, 0x61, "0xD520/61: Core readdir-format suite pass"),
    (0xD520, 0x62, "0xD520/62: Core getattr-format suite pass"),
    (0xD520, 0x70, "0xD520/70: launchd read pass"),
    (0xD520, 0x71, "0xD520/71: sh read pass"),
    (0xD520, 0x72, "0xD520/72: Partial / unaligned / cross-sector / EOF pass"),
    (0xD520, 0x73, "0xD520/73: PRE mutation whole-md0 CRC32 pass"),
    (0xD520, 0x80, "0xD520/80: Read-only helper rejection pass"),
    (0xD520, 0x81, "0xD520/81: POST mutation whole-md0 CRC32 pass"),
    (0xD520, 0x90, "0xD520/90: D5-M3 complete"),
    (0xD520, 0x01, "0xD520/01: D5-M3 diagnostic terminal state"),
]

REQUIRED_TELEMETRY = [
    ("D5-M1_COMPLETE", "yes"),
    ("D5-M2_COMPLETE", "yes"),
    ("D5-M3_COMPLETE", "yes"),
    ("XZSFS_REGISTERED", "yes"),
    ("XZSFS_REGISTRATION_COUNT", "1"),
    ("XZSFS_SUPERBLOCK_VALID", "yes"),
    ("XZSFS_METADATA_CRC_MATCH", "yes"),
    ("XZSFS_OBJECT_COUNT", "9"),
    ("XZSFS_OBJECT_GRAPH_VALID", "yes"),
    ("XZSFS_CORE_LOOKUP_VERIFIED", "yes"),
    ("XZSFS_CORE_READ_VERIFIED", "yes"),
    ("XZSFS_CORE_READDIR_FORMAT_VERIFIED", "yes"),
    ("XZSFS_CORE_GETATTR_FORMAT_VERIFIED", "yes"),
    ("XZSFS_ROFS_HELPER_VERIFIED", "yes"),
    ("XZSFS_LAUNCHD_SIZE_MATCH", "yes"),
    ("XZSFS_LAUNCHD_CRC32_MATCH", "yes"),
    ("XZSFS_SH_SIZE_MATCH", "yes"),
    ("XZSFS_SH_CRC32_MATCH", "yes"),
    ("XZSFS_UNALIGNED_READ_MATCH", "yes"),
    ("XZSFS_CROSS_SECTOR_READ_MATCH", "yes"),
    ("XZSFS_EOF_SEMANTICS_PASS", "yes"),
    ("PRE_MUTATION_MD0_CRC32", "0x131e9191"),
    ("POST_MUTATION_MD0_CRC32", "0x131e9191"),
    ("RAMDISK_CONTENT_UNCHANGED", "yes"),
    ("XZSFS_REAL_VNODE_CREATED", "no"),
    ("XZSFS_VNOP_DISPATCH_VERIFIED", "no"),
    ("XZSFS_VFS_MOUNT_INVOKED", "no"),
    ("XZSFS_GET_VNODE_CALL_COUNT", "0"),
    ("VNODE_CREATE_CALL_COUNT_FROM_XZSFS", "0"),
    ("VFS_MOUNTROOT_CALLED", "no"),
    ("XZSFS_ROOT_MOUNT_ATTEMPTED", "no"),
    ("ROOT_VNODE_INSTALLED", "no"),
    ("PID1_STARTED", "no"),
    ("EXECVE_ATTEMPTED", "no"),
    ("EL0_ENTRY_ATTEMPTED", "no"),
    ("CMD24_COUNT", "0"),
    ("CMD25_COUNT", "0"),
    ("ZERO_STORAGE_WRITES", "yes"),
    ("D5_COMPLETE", "no"),
]

# Aliases for telemetry keys to support historical formats
KEY_ALIASES = {
    "XZSFS_REGISTERED": ["XZSFS_VFS_REGISTERED"],
    "XZSFS_REGISTRATION_COUNT": ["XZSFS_VFS_REGISTRATION_COUNT"],
}


def parse_telemetry(log_text):
    """
    Extract key-value pairs from log text, supporting both KEY=value and KEY: value.
    """
    telemetry = {}
    for line in log_text.splitlines():
        line = line.strip()
        if not line or line.startswith("===") or line.startswith("---") or line.startswith("["):
            continue
        # Support KEY=value
        m_eq = re.match(r"^([A-Za-z0-9_\-]+)\s*=\s*(.+)$", line)
        if m_eq:
            k, v = m_eq.group(1).strip(), m_eq.group(2).strip()
            telemetry[k] = v
            continue
        # Support KEY: value
        m_col = re.match(r"^([A-Za-z0-9_\-]+)\s*:\s*(.+)$", line)
        if m_col:
            k, v = m_col.group(1).strip(), m_col.group(2).strip()
            telemetry[k] = v
            continue
    return telemetry


def parse_breadcrumbs(log_text):
    """
    Extract breadcrumbs from log text.
    Matches lines like: [XZS_BREADCRUMB] CP=0xD520 ERR=0x00 or CP: 0xd520, err: 0x00
    or raw hex markers.
    """
    crumbs = []
    pattern = re.compile(r"CP[=:]\s*0x([0-9a-fA-F]+)[,\s]+ERR[=:]\s*0x([0-9a-fA-F]+)")
    for line in log_text.splitlines():
        m = pattern.search(line)
        if m:
            cp = int(m.group(1), 16)
            err = int(m.group(2), 16)
            crumbs.append((cp, err))
    return crumbs


def main():
    if len(sys.argv) < 2:
        print(f"Usage: {sys.argv[0]} <log_file>")
        sys.exit(1)

    log_path = sys.argv[1]
    if not os.path.isfile(log_path):
        print(f"ERROR: Log file not found: {log_path}")
        sys.exit(1)

    with open(log_path, "r", encoding="utf-8", errors="replace") as f:
        log_text = f.read()

    print("=======================================================")
    print("=== AUDITING D5-M3 ACCEPTANCE LOG: " + log_path)
    print("=======================================================\n")

    # 1. Audit Checkpoint Sequence
    print("--- 1. AUDITING 0xD520 CHECKPOINT PROGRESSION ---")
    breadcrumbs = parse_breadcrumbs(log_text)
    d5m3_crumbs = [c for c in breadcrumbs if c[0] == 0xD520]

    print(f"Total 0xD520 breadcrumbs detected: {len(d5m3_crumbs)}")
    
    missing_cps = []
    crumb_errs = [c[1] for c in d5m3_crumbs]
    
    last_successful_cp = None
    for cp, err, desc in REQUIRED_CHECKPOINTS:
        if err in crumb_errs:
            print(f"  [PASS] {desc}")
            last_successful_cp = f"0x{cp:04X}/0x{err:02X}"
        else:
            print(f"  [FAIL] MISSING: {desc}")
            missing_cps.append(f"0x{cp:04X}/0x{err:02X}")

    if missing_cps:
        print(f"\nCRITICAL: Missing checkpoints: {missing_cps}")
        print(f"LAST_SUCCESSFUL_CHECKPOINT={last_successful_cp}")
        print("CHECKPOINT_AUDIT=FAIL")
        sys.exit(1)
    else:
        print("CHECKPOINT_AUDIT=PASS")

    # 2. Audit Telemetry Key-Value Invariants
    print("\n--- 2. AUDITING CANONICAL TELEMETRY INVARIANTS ---")
    telemetry = parse_telemetry(log_text)
    telemetry_failed = False

    for key, expected in REQUIRED_TELEMETRY:
        val = telemetry.get(key)
        if val is None:
            # Check aliases
            for alias in KEY_ALIASES.get(key, []):
                val = telemetry.get(alias)
                if val is not None:
                    break
        
        if val is None:
            print(f"  [FAIL] MISSING KEY: {key} (expected '{expected}')")
            telemetry_failed = True
        elif val != expected:
            print(f"  [FAIL] VALUE MISMATCH: {key} = '{val}' (expected '{expected}')")
            telemetry_failed = True
        else:
            print(f"  [PASS] {key} = {val}")

    if telemetry_failed:
        print("\nCRITICAL: Telemetry validation failed!")
        print("TELEMETRY_AUDIT=FAIL")
        sys.exit(1)
    else:
        print("TELEMETRY_AUDIT=PASS")

    # 3. Final Acceptance Decision
    print("\n=======================================================")
    print("D5-M3 ACCEPTANCE VERIFICATION: 100% PASS")
    print("D5-M3_COMPLETE=yes")
    print("XZSFS_ROFS_VFS_DRIVER_VERIFIED=yes")
    print("STRATEGY_B_INVARIANTS_MAINTAINED=yes")
    print("ZERO_STORAGE_WRITES=yes")
    print("=======================================================\n")
    sys.exit(0)


if __name__ == "__main__":
    main()
