#!/usr/bin/env python3
"""Independent D6-M4 first-EL0 acceptance verifier."""

import os
import re
import subprocess
import sys


REQUIRED_SEQUENCE = [
    0x00, 0x10, 0x20, 0x21, 0x30, 0x31, 0x32,
    0x33, 0x34, 0x35, 0x36, 0x37,
    0x40, 0x41, 0x50, 0x90, 0x91, 0x01,
]

REQUIRED_INVARIANTS = {
    "D6-M3_REGRESSION_PASS": "yes",
    "D6-M4_COMPLETE": "yes",
    "NATIVE_AF_DELTA_ONLY_AF": "yes",
    "XZS_EXEC_DELTA_ONLY_UXN": "yes",
    "TOTAL_BOOTSTRAP_DELTA_AF_AND_UXN": "yes",
    "UNRELATED_PTE_BITS_UNCHANGED": "yes",
    "PARENT_EL0_EXEC_BLOCKED": "no",
    "PP_ATTR_REFERENCED": "1",
    "PP_ATTR_REFFAULT": "0",
    "PID1_STARTED": "yes",
    "EL0_ENTRY_ATTEMPTED": "yes",
    "FIRST_EL0_INSTRUCTION_EXECUTED": "yes",
    "FIRST_EL0_PROOF": "svc_register_signature",
    "FIRST_SVC_ENTERED": "yes",
    "SVC_IMMEDIATE": "0x0000000000000080",
    "SVC_SYSCALL_NUMBER_REGISTER": "x16",
    "SVC_SYSCALL_NUMBER": "4",
    "D6_M4_EXCEPTION_TELEMETRY_COMPLETE": "yes",
    "ROADMAP_ADVANCED_TO": "D6-M5",
}

HISTORICAL_BOUNDARY = {
    "SYSCALL_DISPATCH_REACHED": "no",
    "FIRST_SYSCALL_ROUNDTRIP_COMPLETE": "no",
}


def ordered(expected, actual):
    position = 0
    for value in actual:
        if position < len(expected) and value == expected[position]:
            position += 1
    return position == len(expected)


def parse_telemetry(text):
    telemetry = {}
    pattern = re.compile(r"^([A-Za-z0-9_-]+)\s*[=:]\s*(.+?)\s*$")
    for line in text.splitlines():
        cleaned = re.sub(r"^\[.*?\]\s*", "", line.strip())
        match = pattern.match(cleaned)
        if match:
            telemetry[match.group(1)] = match.group(2)
    return telemetry


def main():
    regression_mode = len(sys.argv) == 3 and sys.argv[1] == "--regression"
    log_arg = sys.argv[2] if regression_mode else (sys.argv[1] if len(sys.argv) == 2 else None)
    if log_arg is None or not os.path.isfile(log_arg):
        print(f"Usage: {sys.argv[0]} [--regression] <console-log>")
        return 2

    log_path = log_arg
    d6m3 = os.path.join(os.path.dirname(__file__), "verify_d6m3_acceptance.py")
    regression = subprocess.run([sys.executable, d6m3, "--regression-only", log_path], check=False)
    if regression.returncode != 0:
        print("FAIL: D6-M3 regression verifier failed")
        return 1

    with open(log_path, "r", encoding="utf-8", errors="replace") as stream:
        text = stream.read()

    breadcrumbs = [
        int(match.group(1), 16)
        for match in re.finditer(
            r"CP[=:]\s*0x0*d630[,\s]+ERR[=:]\s*0x([0-9a-fA-F]+)", text
        )
    ]
    if not ordered(REQUIRED_SEQUENCE, breadcrumbs):
        print(f"FAIL: D630 sequence incomplete/out of order: {[hex(v) for v in breadcrumbs]}")
        return 1
    if any(value >= 0xEE00 for value in breadcrumbs):
        print("FAIL: D630 fatal breadcrumb present")
        return 1
    print("[PASS] D630 checkpoint sequence complete and in canonical order")

    telemetry = parse_telemetry(text)
    required = dict(REQUIRED_INVARIANTS)
    if not regression_mode:
        required.update(HISTORICAL_BOUNDARY)
    for key, expected in required.items():
        actual = telemetry.get(key)
        if actual is None or actual.lower() != expected.lower():
            print(f"FAIL: {key} expected {expected}, got {actual}")
            return 1
        print(f"[PASS] {key} = {actual}")

    expected_registers = {
        "ESR_EL1": 0x56000080,
        "ELR_EL1": 0x100000304,
        "SPSR_EL1": 0,
        "SP_EL0": 0x16FDFFFB0,
        "SVC_X16": 4,
    }
    for key, expected in expected_registers.items():
        value = telemetry.get(key)
        if value is None or int(value, 16) != expected:
            print(f"FAIL: {key} expected 0x{expected:x}, got {value}")
            return 1
        print(f"[PASS] {key} verified: {value}")

    # Checkpoint 0x41 is emitted only when sleh.c verifies:
    # x0 == 1, x1 == 0x100000320, x2 == 0x1a, x16 == 4
    if 0x41 not in breadcrumbs:
        print("FAIL: D630/41 launchd EL0 register signature breadcrumb missing")
        return 1
    print("[PASS] EL0 register signature verified (x0=1, x1=0x100000320, x2=0x1a, x16=4)")

    print("\n============================================================")
    print("D6_M3_REGRESSION_VERIFIER: PASS")
    if regression_mode:
        print("D6_M4_REGRESSION_VERIFIER: PASS")
        print("D6-M4 EL0-entry, PTE, register-signature, and exception invariants preserved.")
    else:
        print("D6_M4_ACCEPTANCE_VERIFIER: PASS")
        print("Known launchd instructions executed in EL0; first SVC captured before dispatch.")
    print("============================================================")
    return 0


if __name__ == "__main__":
    sys.exit(main())
