#!/usr/bin/env python3
"""Independent D6-M5 real Darwin/XNU syscall round-trip verifier."""

import os
import re
import subprocess
import sys


REQUIRED_SEQUENCE = [0x00, 0x10, 0x20, 0x30, 0x40, 0x50, 0x60, 0x70, 0x90, 0x91, 0x01]

REQUIRED_TELEMETRY = {
    "D6-M4_REGRESSION_PASS": "yes",
    "FIRST_SVC_ENTERED": "yes",
    "SYSCALL_ABI_SOURCE_AUDITED": "yes",
    "SYSCALL_NUMBER_REGISTER": "x16",
    "FIRST_SYSCALL_NUMBER": "4",
    "FIRST_SYSCALL_NAME": "write",
    "PID1_FD1_EXISTS": "no",
    "PID1_FD1_TARGET": "none",
    "PID1_FD1_READY_FOR_WRITE": "no",
    "SYSCALL_DISPATCH_REACHED": "yes",
    "SYSCALL_HANDLER_ENTERED": "yes",
    "SYSCALL_HANDLER_COMPLETED": "yes",
    "SYSCALL_RETURN_VALUE": "0x0000000000000009",
    "SYSCALL_RETURN_ERROR": "EBADF",
    "SYSCALL_RETURN_CARRY": "set",
    "SYSCALL_RETURN_TO_EL0": "yes",
    "POST_SYSCALL_EL0_PC": "0x0000000100000310",
    "POST_SYSCALL_REGISTER_SIGNATURE": "x0:0,x16:1,carry:set",
    "POST_SYSCALL_EL0_INSTRUCTION_EXECUTED": "yes",
    "FIRST_SYSCALL_ROUNDTRIP_COMPLETE": "yes",
    "D6-M5_COMPLETE": "yes",
    "ROADMAP_ADVANCED_TO": "D6-M6",
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
    if len(sys.argv) != 2 or not os.path.isfile(sys.argv[1]):
        print(f"Usage: {sys.argv[0]} <console-log>")
        return 2

    log_path = sys.argv[1]
    d6m4 = os.path.join(os.path.dirname(__file__), "verify_d6m4_acceptance.py")
    regression = subprocess.run(
        [sys.executable, d6m4, "--regression", log_path], check=False
    )
    if regression.returncode != 0:
        print("FAIL: D6-M4 regression verifier failed")
        return 1

    with open(log_path, "r", encoding="utf-8", errors="replace") as stream:
        text = stream.read()

    breadcrumbs = [
        int(match.group(1), 16)
        for match in re.finditer(
            r"CP[=:]\s*0x0*d640[,\s]+ERR[=:]\s*0x([0-9a-fA-F]+)", text
        )
    ]
    if not ordered(REQUIRED_SEQUENCE, breadcrumbs):
        print(f"FAIL: D640 sequence incomplete/out of order: {[hex(v) for v in breadcrumbs]}")
        return 1
    if any(value >= 0xEE00 for value in breadcrumbs):
        print("FAIL: D640 fatal breadcrumb present")
        return 1
    print("[PASS] D640 checkpoint sequence complete and in canonical order")

    if "[XZS-D6M5] FATAL:" in text or "UNEXPECTED EXCEPTION ON CPU 1" in text:
        print("FAIL: fatal/unexpected exception marker present")
        return 1

    telemetry = parse_telemetry(text)
    for key, expected in REQUIRED_TELEMETRY.items():
        actual = telemetry.get(key)
        if actual is None or actual.lower() != expected.lower():
            print(f"FAIL: {key} expected {expected}, got {actual}")
            return 1
        print(f"[PASS] {key} = {actual}")

    print("\n============================================================")
    print("D6_M4_REGRESSION_VERIFIER: PASS")
    print("D6_M5_ACCEPTANCE_VERIFIER: PASS")
    print("Real sysent[4]/write handler completed and returned to EL0.")
    print("============================================================")
    return 0


if __name__ == "__main__":
    sys.exit(main())
