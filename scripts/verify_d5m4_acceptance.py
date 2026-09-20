#!/usr/bin/env python3
"""
verify_d5m4_acceptance.py - Phase D5-M4 Acceptance Verifier
Target: Sony Xperia XZs (MSM8996 / Kryo) - Real Bare-Metal Execution

Verifies the native XZSFS real root filesystem mount & root vnode telemetry:
1. Complete 0xD530 checkpoint sequence (0x00 .. 0x91, 0x01).
2. Canonical telemetry keys matching frozen D5-M4 requirements.
3. Strict enforcement of all hard boundaries (PID1=no, EXECVE=no, EL0=no, ZERO_WRITES=yes).
"""

import sys
import os
import re

REQUIRED_CHECKPOINTS = [
    (0xD530, 0x00, "0xD530/00: Enter D5-M4 probe"),
    (0xD530, 0x10, "0xD530/10: Root mount path enter"),
    (0xD530, 0x20, "0xD530/20: XZSFS mount dispatch enter"),
    (0xD530, 0x21, "0xD530/21: Mount-private allocation pass"),
    (0xD530, 0x22, "0xD530/22: devvp reference acquired"),
    (0xD530, 0x30, "0xD530/30: Superblock validation pass"),
    (0xD530, 0x31, "0xD530/31: Metadata CRC pass"),
    (0xD530, 0x32, "0xD530/32: Object graph validation pass"),
    (0xD530, 0x40, "0xD530/40: Root xzsfs_node resolved"),
    (0xD530, 0x50, "0xD530/50: Root vnode create enter"),
    (0xD530, 0x51, "0xD530/51: Root vnode create pass"),
    (0xD530, 0x60, "0xD530/60: VFS_ROOT dispatch pass"),
    (0xD530, 0x70, "0xD530/70: Root VNOP_GETATTR pass"),
    (0xD530, 0x71, "0xD530/71: VNOP_LOOKUP '.' pass"),
    (0xD530, 0x72, "0xD530/72: VNOP_LOOKUP '..' pass"),
    (0xD530, 0x80, "0xD530/80: XZSFS mounted read-only"),
    (0xD530, 0x81, "0xD530/81: Root filesystem identity pass"),
    (0xD530, 0x90, "0xD530/90: Global root vnode installed"),
    (0xD530, 0x91, "0xD530/91: D5-M4 complete"),
    (0xD530, 0x01, "0xD530/01: D5-M4 diagnostic terminal state"),
]

REQUIRED_TELEMETRY = [
    ("D5-M1_COMPLETE", "yes"),
    ("D5-M2_COMPLETE", "yes"),
    ("D5-M3_COMPLETE", "yes"),
    ("D5-M4_COMPLETE", "yes"),
    ("ROOTDEV_IS_MD0", "yes"),
    ("ROOTDEV_EQUALS_MDEVLOOKUP0", "yes"),
    ("XZSFS_REGISTERED", "yes"),
    ("XZSFS_REGISTRATION_COUNT", "1"),
    ("XZSFS_VFS_MOUNT_INVOKED", "yes"),
    ("XZSFS_MOUNT_PRIVATE_ATTACHED", "yes"),
    ("XZSFS_DEVVP_REFERENCE_HELD", "yes"),
    ("XZSFS_MOUNT_SUPERBLOCK_VALID", "yes"),
    ("XZSFS_MOUNT_METADATA_CRC_MATCH", "yes"),
    ("XZSFS_MOUNT_OBJECT_GRAPH_VALID", "yes"),
    ("XZSFS_REAL_VNODE_CREATED", "yes"),
    ("XZSFS_ROOT_VNODE_CREATED", "yes"),
    ("XZSFS_ROOT_VNODE_OBJECT_ID", "1"),
    ("XZSFS_VFS_ROOT_DISPATCH_VERIFIED", "yes"),
    ("XZSFS_VFS_ROOT_RETURNS_ROOT_VNODE", "yes"),
    ("XZSFS_VNOP_DISPATCH_VERIFIED", "yes"),
    ("XZSFS_ROOT_GETATTR_VNOP_PASS", "yes"),
    ("XZSFS_ROOT_DOT_LOOKUP_VNOP_PASS", "yes"),
    ("XZSFS_ROOT_DOTDOT_LOOKUP_VNOP_PASS", "yes"),
    ("XZSFS_MOUNT_READ_ONLY", "yes"),
    ("ROOT_FS_TYPE", "xzsfs"),
    ("ROOT_FS_DEVICE", "md0"),
    ("XZSFS_VFS_ROOT_READY", "yes"),
    ("GLOBAL_ROOTVNODE_INSTALLED", "yes"),
    ("XZSFS_ROOT_VNODE_CREATE_COUNT", "1"),
    ("XZSFS_ROOT_VNODE_RECLAIM_COUNT", "0"),
    ("XZSFS_MOUNT_FAILURE_UNWIND_AUDITED", "yes"),
    ("PID1_STARTED", "no"),
    ("EXECVE_ATTEMPTED", "no"),
    ("EL0_ENTRY_ATTEMPTED", "no"),
    ("CMD24_COUNT", "0"),
    ("CMD25_COUNT", "0"),
    ("ZERO_STORAGE_WRITES", "yes"),
    ("D5_COMPLETE", "no"),
]

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
    Matches lines like: [XZS_BREADCRUMB] CP=0xD530 ERR=0x00 or CP: 0xd530, err: 0x00
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
    print("=== AUDITING D5-M4 ACCEPTANCE LOG: " + log_path)
    print("=======================================================\n")

    # 1. Audit Checkpoint Sequence
    print("--- 1. AUDITING 0xD530 CHECKPOINT PROGRESSION ---")
    breadcrumbs = parse_breadcrumbs(log_text)
    d5m4_crumbs = [c for c in breadcrumbs if c[0] == 0xD530]

    print(f"Total 0xD530 breadcrumbs detected: {len(d5m4_crumbs)}")

    missing_cps = []
    crumb_errs = [c[1] for c in d5m4_crumbs]

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

    # Check numeric counters that should be >= 1
    for count_key in ["XZSFS_GET_VNODE_CALL_COUNT", "VNODE_CREATE_CALL_COUNT_FROM_XZSFS"]:
        val = telemetry.get(count_key)
        if val is not None:
            try:
                cnt = int(val, 0)
                if cnt >= 1:
                    print(f"  [PASS] {count_key} = {cnt} (>= 1)")
                else:
                    print(f"  [FAIL] {count_key} = {cnt} (< 1)")
                    telemetry_failed = True
            except ValueError:
                print(f"  [FAIL] {count_key} invalid int: {val}")
                telemetry_failed = True

    if telemetry_failed:
        print("\nCRITICAL: Telemetry validation failed!")
        print("TELEMETRY_AUDIT=FAIL")
        sys.exit(1)
    else:
        print("TELEMETRY_AUDIT=PASS")

    # 3. Final Acceptance Decision
    print("\n=======================================================")
    print("D5-M4 ACCEPTANCE VERIFICATION: 100% PASS")
    print("D5-M4_COMPLETE=yes")
    print("REAL_XZSFS_MOUNT_VERIFIED=yes")
    print("ROOT_VNODE_INSTALLED=yes")
    print("VNOP_DISPATCH_VERIFIED=yes")
    print("ZERO_STORAGE_WRITES=yes")
    print("=======================================================\n")
    sys.exit(0)


if __name__ == "__main__":
    main()
