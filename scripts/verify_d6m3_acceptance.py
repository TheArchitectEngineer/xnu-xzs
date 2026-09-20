#!/usr/bin/env python3
"""
Independent acceptance verifier for Phase D6-M3 (PID1 User VM + Initial Stack)
on Sony Xperia XZs (Qualcomm MSM8996).

Verifies:
1. Full D5 regression prefix (D530, D540, D550 through D550/91 and D550/01)
2. In-order D6-M1 regression sequence (D600/00 through D600/91 and D600/01)
3. In-order D6-M2 regression sequence (D610/00 through D610/91)
4. In-order D6-M3 checkpoint sequence (D620/00 through D620/91 and D620/01)
5. Absence of any fatal checkpoints
6. Canonical telemetry assertions for D6-M3 user VM and stack validation
"""

import sys
import os
import re

CP_D5M4 = 0xD530
CP_D5M5 = 0xD540
CP_D5M6 = 0xD550
CP_D6M1 = 0xD600
CP_D6M2 = 0xD610
CP_D6M3 = 0xD620

REQUIRED_D5M5_SEQUENCE = [
    (0x00, "enter D5-M5 probe"),
    (0x10, "namei('/') returned global rootvnode"),
    (0x20, "namei('/sbin/launchd') returned VREG"),
    (0x21, "launchd identity/getattr verified"),
    (0x30, "devfs_kernel_mount('/dev') entered"),
    (0x31, "devfs_kernel_mount('/dev') succeeded"),
    (0x40, "namei('/dev') crossed into devfs"),
    (0x50, "namei('/dev/console') returned VCHR"),
    (0x51, "console device identity 0:0 verified"),
    (0x60, "namespace and devfs overlay complete"),
    (0x90, "D5-M5 acceptance telemetry emitted"),
    (0x91, "D5-M5 complete"),
    (0x01, "D5-M5 handoff to D5-M6"),
]

REQUIRED_D5M6_SEQUENCE = [
    (0x00, "enter D5-M6 final seal probe"),
    (0x10, "D5-M1/M2 RAMDisk md0 rootdev regression verified"),
    (0x20, "D5-M3 XZSFS VFS driver regression verified"),
    (0x30, "D5-M4 real mounted rootvnode regression verified"),
    (0x40, "D5-M5 namespace and devfs overlay regression verified"),
    (0x50, "namei('/bin/sh') returned VREG"),
    (0x51, "/bin/sh identity & VNOP_GETATTR verified"),
    (0x60, "root filesystem read-only invariant verified"),
    (0x61, "zero storage write invariant verified"),
    (0x70, "D6 boundary closed"),
    (0x90, "final D5 acceptance telemetry emitted"),
    (0x91, "PHASE D5 COMPLETE & SEALED"),
    (0x01, "D5-M6 complete — handoff to D6-M1"),
]

REQUIRED_D6M1_SEQUENCE = [
    (0x00, "enter D6-M1 PID 1 skeleton validation"),
    (0x10, "PID 1 proc create ENTER"),
    (0x11, "PID 1 proc created"),
    (0x20, "PID 1 task acquire/create ENTER"),
    (0x21, "PID 1 task ready"),
    (0x22, "proc<->task linkage verified"),
    (0x30, "PID 1 thread create ENTER"),
    (0x31, "PID 1 thread created"),
    (0x32, "thread<->task linkage verified"),
    (0x33, "uthread linkage verified"),
    (0x40, "PID identity verified (pid=1)"),
    (0x41, "process relationship/state verified (ppid=0, stat=SRUN)"),
    (0x50, "thread safely parked / non-EL0 verified"),
    (0x70, "D6-M2 boundary closed"),
    (0x90, "canonical D6-M1 acceptance telemetry emitted"),
    (0x91, "PHASE D6-M1 COMPLETE & VERIFIED"),
    (0x01, "D6-M1 complete — handoff to D6-M2"),
]

REQUIRED_D6M2_SEQUENCE = [
    (0x00, "enter D6-M2 minimal Mach-O loader"),
    (0x10, "/sbin/launchd lookup ENTER"),
    (0x11, "launchd vnode resolved"),
    (0x12, "launchd identity verified"),
    (0x20, "Mach-O header read"),
    (0x21, "Mach-O header valid"),
    (0x30, "load command parse ENTER"),
    (0x31, "load commands valid"),
    (0x40, "segment enumeration complete"),
    (0x41, "executable segment verified"),
    (0x50, "entrypoint command found"),
    (0x51, "initial PC resolved"),
    (0x52, "initial PC inside executable segment"),
    (0x60, "static/no-dyld contract verified"),
    (0x70, "D6-M3 boundary closed"),
    (0x90, "canonical D6-M2 acceptance telemetry emitted"),
    (0x91, "PHASE D6-M2 COMPLETE & VERIFIED"),
    (0x01, "D6-M2 handoff to D6-M3"),
]

REQUIRED_D6M3_SEQUENCE = [
    (0x00, "enter D6-M3 user VM and initial stack probe"),
    (0x10, "PID1 task and map verified"),
    (0x20, "PAGEZERO guard region verified"),
    (0x30, "map __TEXT segment into PID1 user VM"),
    (0x31, "load and verify __TEXT contents"),
    (0x32, "finalize and verify __TEXT protection RX"),
    (0x40, "resolve __LINKEDIT policy"),
    (0x41, "__LINKEDIT unmapped state verified"),
    (0x50, "create PID1 user stack mapping (RW, NX)"),
    (0x51, "construct Darwin initial argument frame"),
    (0x52, "verify 16-byte aligned initial SP"),
    (0x60, "install ARM64 initial user register state"),
    (0x61, "verify saved PC and SP"),
    (0x70, "perform full VM map audit"),
    (0x71, "verify zero unexpected RWX mappings"),
    (0x72, "verify D6-M4 boundary remains closed"),
    (0x90, "emit canonical D6-M3 acceptance telemetry"),
    (0x91, "PHASE D6-M3 COMPLETE & VERIFIED"),
    (0x01, "diagnostic terminal halt before D6-M4"),
]

REQUIRED_TELEMETRY = [
    ("D6-M1_REGRESSION_PASS", "yes"),
    ("D6-M2_REGRESSION_PASS", "yes"),
    ("D6-M3_COMPLETE", "yes"),
    ("D620_91_REACHED", "yes"),
    ("PID1_USER_VM_READY", "yes"),
    ("PID1_VM_MAP_VERIFIED", "yes"),
    ("PID1_EXISTING_MAP_PRESERVED", "yes"),
    ("PID1_MAP_REINITIALIZATION_REQUIRED", "no"),
    ("USER_PAGEZERO_GUARD_VALID", "yes"),
    ("USER_PAGEZERO_OVERLAPPING_MAPPING_COUNT", "0"),
    ("USER_MAP_WRITE_PRIMITIVE", "vm_map_write_user"),
    ("USER_MAP_READ_PRIMITIVE", "vm_map_read_user"),
    ("CROSS_MAP_ACCESS_SOURCE_AUDITED", "yes"),
    ("USER_TEXT_MAPPED", "yes"),
    ("USER_TEXT_CONTENT_VERIFIED", "yes"),
    ("USER_TEXT_PROTECTION", "RX"),
    ("TEXT_VMADDR", "0x0000000100000000"),
    ("TEXT_VMSIZE", "0x0000000000004000"),
    ("TEXT_FILEOFF", "0x0000000000000000"),
    ("TEXT_FILESIZE", "0x0000000000004000"),
    ("TEXT_CURRENT_PROT", "RX"),
    ("TEXT_MAX_PROT", "RX"),
    ("TEXT_REQUESTED_FINAL_PROT", "RX"),
    ("USER_TEXT_WRITABLE_AFTER_FINALIZE", "no"),
    ("TEXT_WX_TRANSITION_USED", "yes"),
    ("USER_TEXT_FILE_BYTES_VERIFIED", "yes"),
    ("USER_TEXT_ZEROFILL_VALID", "yes"),
    ("USER_LINKEDIT_RUNTIME_REQUIRED", "no"),
    ("USER_LINKEDIT_MAPPED", "no"),
    ("USER_LINKEDIT_PROTECTION", "NONE"),
    ("PID1_USER_STACK_READY", "yes"),
    ("USER_STACK_PROTECTION", "RW"),
    ("USER_STACK_EXECUTABLE", "no"),
    ("DARWIN_INITIAL_STACK_ABI_SOURCE_AUDITED", "yes"),
    ("PID1_ARGC", "1"),
    ("PID1_ARGV0", "/sbin/launchd"),
    ("PID1_INITIAL_SP_ALIGNED", "yes"),
    ("PID1_REGISTER_STATE_READY", "yes"),
    ("PID1_REGISTER_STATE_INSTALLED", "yes"),
    ("VM_MAP_AUDIT_LOCKING_VALID", "yes"),
    ("PID1_VM_UNEXPECTED_RWX_COUNT", "0"),
    ("PID1_THREAD_REMAINS_SUSPENDED", "yes"),
    ("PID1_TASK_REMAINS_SUSPENDED", "yes"),
    ("PID1_SUSPEND_STATE_UNINTENTIONALLY_CHANGED", "no"),
    ("PID1_STARTED", "no"),
    ("EL0_ENTRY_ATTEMPTED", "no"),
    ("FIRST_EL0_INSTRUCTION_EXECUTED", "no"),
    ("D6_M3_ACCEPTANCE_VERIFIER", "PASS"),
    ("FINAL_DEVICE_STATE", "fastboot"),
    ("FASTBOOT_RETURN_METHOD", "twrp_scripted"),
    ("ROADMAP_ADVANCED_TO", "D6-M4"),
]


def parse_breadcrumbs(text):
    pattern = re.compile(
        r"CP[=:]\s*0x([0-9a-fA-F]+)[,\s]+ERR[=:]\s*0x([0-9a-fA-F]+)"
    )
    return [
        (int(match.group(1), 16), int(match.group(2), 16))
        for match in pattern.finditer(text)
    ]


def parse_telemetry(text):
    telemetry = {}
    pattern = re.compile(r"^([A-Za-z0-9_-]+)\s*[=:]\s*(.+?)\s*$")
    for line in text.splitlines():
        match = pattern.match(line.strip())
        if match:
            telemetry[match.group(1)] = match.group(2)
    return telemetry


def is_ordered_subsequence(expected, actual):
    position = 0
    for value in actual:
        if position < len(expected) and value == expected[position]:
            position += 1
    return position == len(expected)


def main():
    if len(sys.argv) != 2:
        print(f"Usage: {sys.argv[0]} <console-log>")
        return 2

    log_path = sys.argv[1]
    if not os.path.isfile(log_path):
        print(f"ERROR: log file not found: {log_path}")
        return 2

    with open(log_path, "r", encoding="utf-8", errors="replace") as f:
        content = f.read()

    breadcrumbs = parse_breadcrumbs(content)
    telemetry = parse_telemetry(content)

    print("=== D6-M3 ACCEPTANCE CHECKPOINT AUDIT ===")

    # 1. D5-M4 Prefix check
    d5m4_cp = [err for cp, err in breadcrumbs if cp == CP_D5M4]
    if 0x91 not in d5m4_cp:
        print("FAIL: D5-M4 root mount prefix D530/91 not reached")
        return 1
    print("[PASS] D5-M4 root-mount regression prefix reached D530/91")

    # 2. D5-M5 Sequence check
    d5m5_cp = [err for cp, err in breadcrumbs if cp == CP_D5M5]
    print("\n--- D5-M5 Namespace & DevFS Sequence ---")
    for err, desc in REQUIRED_D5M5_SEQUENCE:
        if err in d5m5_cp:
            print(f"[PASS] D540/{err:02x}: {desc}")
        else:
            print(f"FAIL: D540/{err:02x} missing ({desc})")
            return 1
    if not is_ordered_subsequence([e for e, _ in REQUIRED_D5M5_SEQUENCE], d5m5_cp):
        print("FAIL: D540 sequence out of order")
        return 1
    print("[PASS] D540 checkpoints are in canonical order")

    # 3. D5-M6 Sequence check
    d5m6_cp = [err for cp, err in breadcrumbs if cp == CP_D5M6]
    print("\n--- D5-M6 Final Seal Sequence ---")
    for err, desc in REQUIRED_D5M6_SEQUENCE:
        if err in d5m6_cp:
            print(f"[PASS] D550/{err:02x}: {desc}")
        else:
            print(f"FAIL: D550/{err:02x} missing ({desc})")
            return 1
    if not is_ordered_subsequence([e for e, _ in REQUIRED_D5M6_SEQUENCE], d5m6_cp):
        print("FAIL: D550 sequence out of order")
        return 1
    print("[PASS] D550 checkpoints are in canonical order")

    # 4. D6-M1 Sequence check
    d6m1_cp = [err for cp, err in breadcrumbs if cp == CP_D6M1]
    print("\n--- D6-M1 PID 1 Skeleton Sequence ---")
    for err, desc in REQUIRED_D6M1_SEQUENCE:
        if err in d6m1_cp:
            print(f"[PASS] D600/{err:02x}: {desc}")
        else:
            print(f"FAIL: D600/{err:02x} missing ({desc})")
            return 1
    if not is_ordered_subsequence([e for e, _ in REQUIRED_D6M1_SEQUENCE], d6m1_cp):
        print("FAIL: D600 sequence out of order")
        return 1
    print("[PASS] D600 checkpoints are in canonical order")

    # 5. D6-M2 Sequence check
    d6m2_cp = [err for cp, err in breadcrumbs if cp == CP_D6M2]
    print("\n--- D6-M2 Mach-O Loader Sequence ---")
    for err, desc in REQUIRED_D6M2_SEQUENCE:
        if err in d6m2_cp:
            print(f"[PASS] D610/{err:02x}: {desc}")
        else:
            print(f"FAIL: D610/{err:02x} missing ({desc})")
            return 1
    if not is_ordered_subsequence([e for e, _ in REQUIRED_D6M2_SEQUENCE], d6m2_cp):
        print("FAIL: D610 sequence out of order")
        return 1
    print("[PASS] D610 checkpoints are in canonical order")

    # 6. D6-M3 Sequence check
    d6m3_cp = [err for cp, err in breadcrumbs if cp == CP_D6M3]
    print("\n--- D6-M3 User VM and Initial Stack Sequence ---")
    for err, desc in REQUIRED_D6M3_SEQUENCE:
        if err in d6m3_cp:
            print(f"[PASS] D620/{err:02x}: {desc}")
        else:
            print(f"FAIL: D620/{err:02x} missing ({desc})")
            return 1
    if not is_ordered_subsequence([e for e, _ in REQUIRED_D6M3_SEQUENCE], d6m3_cp):
        print("FAIL: D620 sequence out of order")
        return 1
    print("[PASS] D620 checkpoints are in canonical order")

    # 7. Check for fatal markers
    print("\n--- Fatal Error Check ---")
    fatal_errors = [err for cp, err in breadcrumbs if cp == CP_D6M3 and err >= 0xF0]
    if fatal_errors:
        print(f"FAIL: Fatal breadcrumb error detected: {[hex(e) for e in fatal_errors]}")
        return 1
    print("[PASS] Zero fatal error breadcrumbs detected")

    # 8. Canonical Telemetry Assertions
    print("\n=== D6-M3 CANONICAL TELEMETRY AUDIT ===")
    failed_keys = []
    for key, expected_val in REQUIRED_TELEMETRY:
        actual_val = telemetry.get(key)
        if actual_val is None:
            print(f"FAIL: Missing telemetry key '{key}'")
            failed_keys.append(key)
        elif actual_val.lower() != expected_val.lower():
            print(f"FAIL: Telemetry '{key}' expected '{expected_val}', got '{actual_val}'")
            failed_keys.append(key)
        else:
            print(f"[PASS] {key} = {actual_val}")

    # Specific register value checks
    initial_pc = telemetry.get("PID1_INITIAL_PC")
    if initial_pc and int(initial_pc, 16) == 0x1000002F0:
        print(f"[PASS] PID1_INITIAL_PC verified: {initial_pc}")
    else:
        print(f"FAIL: PID1_INITIAL_PC invalid: {initial_pc}")
        failed_keys.append("PID1_INITIAL_PC")

    initial_sp = telemetry.get("PID1_INITIAL_SP")
    if initial_sp and int(initial_sp, 16) == 0x16FDFFFB0:
        print(f"[PASS] PID1_INITIAL_SP verified: {initial_sp}")
    else:
        print(f"FAIL: PID1_INITIAL_SP invalid: {initial_sp}")
        failed_keys.append("PID1_INITIAL_SP")

    if failed_keys:
        print(f"\nFAIL: {len(failed_keys)} telemetry assertions failed")
        return 1

    print("\n============================================================")
    print("D6-M3 ACCEPTANCE VERIFICATION RESULT: PASS")
    print("PID1 User VM and Initial Stack fully verified on hardware.")
    print("Zero user execution observed (PID1_STARTED=no).")
    print("============================================================")
    return 0


if __name__ == "__main__":
    sys.exit(main())
