#!/usr/bin/env python3
"""
Independent acceptance verifier for Phase D6-M1 (PID 1 Skeleton)
on Sony Xperia XZs (Qualcomm MSM8996).

Verifies:
1. Full D5 regression prefix (D530, D540, D550 through D550/91 and D550/01)
2. In-order D6-M1 checkpoint sequence (D600/00 through D600/91 and D600/01)
3. Absence of any fatal checkpoints
4. Canonical telemetry assertions for D6-M1 PID 1 process, task, thread objects
"""

import sys
import os
import re

CP_D5M4 = 0xD530
CP_D5M5 = 0xD540
CP_D5M6 = 0xD550
CP_D6M1 = 0xD600

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
    (0x01, "diagnostic terminal halt before D6-M2"),
]

REQUIRED_TELEMETRY = [
    ("D6-M1_COMPLETE", "yes"),
    ("PID1_PROCESS_CREATED", "yes"),
    ("PID1_PROC_NON_NULL", "yes"),
    ("PID1_PROC_PID", "1"),
    ("PID1_PROC_PPID", "0"),
    ("PID1_TASK_CREATED", "yes"),
    ("PID1_TASK_NON_NULL", "yes"),
    ("PID1_PROC_TASK_LINKED", "yes"),
    ("PID1_TASK_IS_KERNEL_TASK", "no"),
    ("PID1_THREAD_CREATED", "yes"),
    ("PID1_THREAD_NON_NULL", "yes"),
    ("PID1_THREAD_TASK_MATCH", "yes"),
    ("PID1_UTHREAD_CREATED", "yes"),
    ("PID1_THREAD_UTHREAD_LINKED", "yes"),
    ("PID1_STARTED", "no"),
    ("EXECVE_ATTEMPTED", "no"),
    ("MACHO_LOAD_ATTEMPTED", "no"),
    ("USER_VM_SETUP_ATTEMPTED", "no"),
    ("EL0_ENTRY_ATTEMPTED", "no"),
    ("FIRST_EL0_INSTRUCTION_EXECUTED", "no"),
    ("ROADMAP_ADVANCED_TO", "D6-M2"),
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

    print("=== D6-M1 ACCEPTANCE CHECKPOINT AUDIT ===")

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

    # 5. Check for fatal errors in D600
    fatals = [err for err in d6m1_cp if 0xE0 <= err <= 0xEF]
    if fatals:
        print(f"FAIL: fatal checkpoints observed in D600: {[hex(x) for x in fatals]}")
        return 1
    print("[PASS] no fatal checkpoints in D600 execution")

    # 6. Telemetry assertions
    print("\n=== D6-M1 ACCEPTANCE TELEMETRY AUDIT ===")
    for key, expected_value in REQUIRED_TELEMETRY:
        actual_value = telemetry.get(key)
        if actual_value is None:
            print(f"FAIL: missing telemetry key: {key}")
            return 1
        if actual_value != expected_value:
            print(f"FAIL: telemetry {key}={actual_value}, expected {expected_value}")
            return 1
        print(f"[PASS] {key}={actual_value}")

    print("\nD6-M1 ACCEPTANCE VERIFICATION: 100% PASS")
    print("D6_M1_ACCEPTANCE_VERIFIER=PASS")
    print("D6-M1_COMPLETE=yes")
    print("PID1_PROCESS_CREATED=yes")
    print("PID1_TASK_CREATED=yes")
    print("PID1_THREAD_CREATED=yes")
    print("PID1_STARTED=no")
    print("EXECVE_ATTEMPTED=no")
    print("MACHO_LOAD_ATTEMPTED=no")
    print("USER_VM_SETUP_ATTEMPTED=no")
    print("EL0_ENTRY_ATTEMPTED=no")
    print("ROADMAP_ADVANCED_TO=D6-M2")
    return 0


if __name__ == "__main__":
    sys.exit(main())
