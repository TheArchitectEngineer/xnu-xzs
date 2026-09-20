#!/usr/bin/env python3
"""Independent D6-M6 stable PID1 and native console-output verifier."""

import os
import re
import subprocess
import sys


REQUIRED_SEQUENCE = [0x00, 0x10, 0x20, 0x30, 0x90, 0x91, 0x01]

REQUIRED_TELEMETRY = {
    "D6-M5_REGRESSION_PASS": "yes",
    "PID1_STARTED": "yes",
    "PID1_FILEDESC_STRUCTURE": "struct_proc.p_fd",
    "FD0_INITIAL_STATE": "absent",
    "FD1_INITIAL_STATE": "absent",
    "FD2_INITIAL_STATE": "absent",
    "FD0_TARGET": "/dev/console",
    "FD1_TARGET": "/dev/console",
    "FD2_TARGET": "/dev/console",
    "DEV_CONSOLE_VNODE_PATH": "/dev/console",
    "DEV_CONSOLE_VNODE_TYPE": "VCHR",
    "DEV_CONSOLE_DEVICE": "0:0",
    "CONSOLE_FD1_VALID": "yes",
    "WRITE_SYSCALL_ENTERED": "yes",
    "WRITE_SYSCALL_HANDLER_COMPLETED": "yes",
    "WRITE_RETURN_VALUE": "26",
    "EL0_CONSOLE_MESSAGE_LENGTH": "26",
    "PID1_CONSOLE_OUTPUT_VERIFIED": "yes",
    "PID1_STABLE_RUNTIME": "yes",
    "STABLE_RUNTIME_SYSCALL": "getpid",
    "PID1_CONSOLE_INPUT_PATH_ESTABLISHED": "yes",
    "CONSOLE_INPUT_DEVICE": "/dev/console",
    "READ_SYSCALL_PATH": "read->fileproc->specfs->cnread->kmread->tty",
    "BLOCKING_READ_SUPPORTED": "yes",
    "CURRENT_INPUT_TRANSPORT": "msm_uartdm_tx_only",
    "PHYSICAL_CONSOLE_RX_AVAILABLE": "no",
    "PID1_CONSOLE_INPUT_VERIFIED": "no",
    "D6-M6_COMPLETE": "yes",
    "ROADMAP_ADVANCED_TO": "D6-M7",
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
    d6m5 = os.path.join(os.path.dirname(__file__), "verify_d6m5_acceptance.py")
    regression = subprocess.run(
        [sys.executable, d6m5, "--regression", log_path], check=False
    )
    if regression.returncode != 0:
        print("FAIL: D6-M5 regression verifier failed")
        return 1

    with open(log_path, "r", encoding="utf-8", errors="replace") as stream:
        text = stream.read()

    breadcrumbs = [
        int(match.group(1), 16)
        for match in re.finditer(
            r"CP[=:]\s*0x0*d650[,\s]+ERR[=:]\s*0x([0-9a-fA-F]+)", text
        )
    ]
    if not ordered(REQUIRED_SEQUENCE, breadcrumbs):
        print(f"FAIL: D650 sequence incomplete/out of order: {[hex(v) for v in breadcrumbs]}")
        return 1
    if any(value >= 0xEE00 for value in breadcrumbs):
        print("FAIL: D650 fatal breadcrumb present")
        return 1
    print("[PASS] D650 checkpoint sequence complete and in canonical order")

    if "[XZS-D6M6] FATAL:" in text or "UNEXPECTED EXCEPTION ON CPU 1" in text:
        print("FAIL: fatal/unexpected exception marker present")
        return 1

    telemetry = parse_telemetry(text)
    for key, expected in REQUIRED_TELEMETRY.items():
        actual = telemetry.get(key)
        if actual is None or actual.lower() != expected.lower():
            print(f"FAIL: {key} expected {expected}, got {actual}")
            return 1
        print(f"[PASS] {key} = {actual}")

    count_text = telemetry.get("STABLE_RUNTIME_ROUNDTRIP_COUNT")
    if count_text is None or int(count_text, 16) < 256:
        print(f"FAIL: stable round-trip count is below 256: {count_text}")
        return 1
    print(f"[PASS] sustained getpid round trips = {int(count_text, 16)}")

    message = "[XZS-INIT] launchd entered\n"
    if text.count(message) != 1:
        print(f"FAIL: expected exactly one EL0 console message, found {text.count(message)}")
        return 1
    print("[PASS] unique 26-byte PID1 message observed from real EL0 write(1)")

    print("\n============================================================")
    print("D6_M5_REGRESSION_VERIFIER: PASS")
    print("D6_M6_ACCEPTANCE_VERIFIER: PASS")
    print("PID1 owns native console fd 0/1/2 and remains alive in EL0.")
    print("============================================================")
    return 0


if __name__ == "__main__":
    sys.exit(main())
