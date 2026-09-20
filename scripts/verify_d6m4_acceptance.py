#!/usr/bin/env python3
"""Independent D6-M4 first-EL0 acceptance verifier."""

import os
import re
import subprocess
import sys


REQUIRED_SEQUENCE = [0x00, 0x10, 0x20, 0x21, 0x30, 0x31, 0x40, 0x41, 0x50, 0x90, 0x91, 0x01]
REQUIRED_TELEMETRY = {
    "D6-M3_REGRESSION_PASS": "yes",
    "D6-M4_COMPLETE": "yes",
    "PID1_STARTED": "yes",
    "EL0_ENTRY_ATTEMPTED": "yes",
    "FIRST_EL0_INSTRUCTION_EXECUTED": "yes",
    "FIRST_EL0_PROOF": "svc_register_signature",
    "FIRST_SVC_ENTERED": "yes",
    "SVC_IMMEDIATE": "0x0000000000000080",
    "SVC_SYSCALL_NUMBER_REGISTER": "x16",
    "SVC_SYSCALL_NUMBER": "4",
    "SYSCALL_DISPATCH_REACHED": "no",
    "FIRST_SYSCALL_ROUNDTRIP_COMPLETE": "no",
    "D6_M4_EXCEPTION_TELEMETRY_COMPLETE": "yes",
    "ROADMAP_ADVANCED_TO": "D6-M5",
}


def ordered(expected, actual):
    position = 0
    for value in actual:
        if position < len(expected) and value == expected[position]:
            position += 1
    return position == len(expected)


def main():
    if len(sys.argv) != 2 or not os.path.isfile(sys.argv[1]):
        print(f"Usage: {sys.argv[0]} <console-log>")
        return 2

    log_path = sys.argv[1]
    d6m3 = os.path.join(os.path.dirname(__file__), "verify_d6m3_acceptance.py")
    regression = subprocess.run([sys.executable, d6m3, log_path], check=False)
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

    telemetry = {}
    for line in text.splitlines():
        match = re.match(r"^([A-Za-z0-9_-]+)=(.+?)\s*$", line.strip())
        if match:
            telemetry[match.group(1)] = match.group(2)
    for key, expected in REQUIRED_TELEMETRY.items():
        if telemetry.get(key, "").lower() != expected.lower():
            print(f"FAIL: {key} expected {expected}, got {telemetry.get(key)}")
            return 1

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

    print("D6-M4 ACCEPTANCE VERIFICATION RESULT: PASS")
    print("Known launchd instructions executed in EL0; first SVC captured before dispatch.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
