#!/usr/bin/env python3
"""Verify D7-M4 internal development evidence or final external acceptance."""

import argparse
import os
import re
import subprocess
import sys


INTERNAL_SEQUENCE = [
    0x00, 0x10, 0x11, 0x20, 0x21, 0x30, 0x31, 0x40, 0x41,
    0x50, 0x51, 0x52, 0x53, 0x60, 0x70,
]
EXTERNAL_SEQUENCE = INTERNAL_SEQUENCE + [0x90, 0x91, 0x01]

COMMON_REQUIRED = {
    "UARTDM_RECEIVE_READY_WORKING": "yes",
    "UARTDM_RECEIVE_DATA_WORKING": "yes",
    "RX_BUFFER_WORKING": "yes",
    "TTY_INPUT_WORKING": "yes",
    "TTY_INPUT_WAKEUP_WORKING": "yes",
    "SHELL_READ_SYSCALL_ENTERED": "yes",
    "SHELL_READ_NATIVE": "yes",
    "SHELL_READ_BLOCKED": "yes",
    "SHELL_READ_AWAKENED": "yes",
    "SHELL_READ_RETURNED_TO_EL0": "yes",
    "EL0_READ_EXACT_BYTES": "yes",
    "POST_READ_EL0_EXECUTION": "yes",
    "EL0_READ_LENGTH": "4",
    "EL0_READ_HEX": "41 42 43 0a",
    "D7M4_INPUT_BYPASS": "no",
}

INTERNAL_REQUIRED = {
    **COMMON_REQUIRED,
    "D7M4_INPUT_SOURCE": "INTERNAL_LOOPBACK",
    "UARTDM_INTERNAL_LOOPBACK_VERIFIED": "yes",
    "UARTDM_RX_IRQ": "146",
    "GIC_INTERRUPT_TYPE": "SPI_114",
    "GIC_TRIGGER_TYPE": "LEVEL_HIGH",
    "UARTDM_RX_IRQ_CONFIGURED": "yes",
    "UARTDM_RX_IRQ_WORKING": "yes",
    "UARTDM_RX_MODE": "IRQ_WITH_BOUNDED_POLL_FALLBACK",
    "D7M4_INTERNAL_PIPELINE_COMPLETE": "yes",
    "EXTERNAL_UART_PIPELINE_PASS": "no",
    "SHELL_STDIN_WORKING": "no",
    "D7_M4_COMPLETE": "no",
    "D7_M4_SEALED": "no",
}

EXTERNAL_REQUIRED = {
    **COMMON_REQUIRED,
    "D7M4_INPUT_SOURCE": "EXTERNAL_UART",
    "UARTDM_RX_EXTERNAL_BYTE_OBSERVED": "yes",
    "UARTDM_RX_RAW_BYTE_MATCH": "yes",
    "EXTERNAL_UART_PIPELINE_PASS": "yes",
    "SHELL_STDIN_WORKING": "yes",
    "D7_M4_COMPLETE": "yes",
}


def parse_telemetry_block(text, block_name):
    telemetry = {}
    pattern = (
        rf"===\s*{re.escape(block_name)}\s+ACCEPTANCE\s+TELEMETRY\s+BEGIN\s*==="
        rf"(.*?)"
        rf"===\s*{re.escape(block_name)}\s+ACCEPTANCE\s+TELEMETRY\s+END\s*==="
    )
    match = re.search(pattern, text, re.DOTALL | re.IGNORECASE)
    if not match:
        return telemetry
    for line in match.group(1).splitlines():
        line = line.strip()
        if line and not line.startswith("#") and "=" in line:
            key, value = line.split("=", 1)
            telemetry[key.strip()] = value.strip()
    return telemetry


def parse_d730(text):
    return [
        int(match.group(1), 16)
        for match in re.finditer(
            r"CP[=:]\s*0x0*d730[,\s]+ERR[=:]\s*0x([0-9a-fA-F]+)",
            text,
            re.IGNORECASE,
        )
    ]


def validate_mode(text, mode):
    expected_sequence = INTERNAL_SEQUENCE if mode == "internal" else EXTERNAL_SEQUENCE
    observed = parse_d730(text)
    if observed != expected_sequence:
        return False, (
            "D730 sequence mismatch: "
            f"expected {[hex(v) for v in expected_sequence]}, "
            f"observed {[hex(v) for v in observed]}"
        )
    if "[XZS-D7M4] FATAL:" in text or "Kernel Panic" in text or "panic(cpu" in text:
        return False, "fatal marker or kernel panic present"

    block_name = "D7-M4 INTERNAL" if mode == "internal" else "D7-M4"
    telemetry = parse_telemetry_block(text, block_name)
    required = INTERNAL_REQUIRED if mode == "internal" else EXTERNAL_REQUIRED
    for key, expected in required.items():
        actual = telemetry.get(key)
        if actual != expected:
            return False, f"{key}={actual!r}, expected {expected!r}"
    if mode == "internal":
        for key in ("UARTDM_RX_IRQ_COUNT", "UARTDM_RX_IRQ_BYTE_COUNT"):
            try:
                value = int(telemetry.get(key, "0"), 0)
            except ValueError:
                return False, f"{key} is not an integer"
            if value <= 0:
                return False, f"{key} must be greater than zero"
    if mode == "external" and telemetry.get("D7M4_INPUT_SOURCE") != "EXTERNAL_UART":
        return False, "internal loopback evidence cannot seal external acceptance"
    return True, "PASS"


def run_regression(verifier, args):
    result = subprocess.run(
        [sys.executable, verifier, *args], capture_output=True, text=True, check=False
    )
    return result.returncode == 0, result.stdout + result.stderr


def main():
    parser = argparse.ArgumentParser()
    mode = parser.add_mutually_exclusive_group(required=True)
    mode.add_argument("--internal", action="store_true")
    mode.add_argument("--external", action="store_true")
    parser.add_argument("log")
    args = parser.parse_args()

    log_path = os.path.abspath(args.log)
    if not os.path.isfile(log_path):
        print(f"FAIL: log file not found: {log_path}")
        return 2

    selected_mode = "internal" if args.internal else "external"
    script_dir = os.path.dirname(os.path.abspath(__file__))
    gates = [
        ("D6", "verify_d6_acceptance.py", [log_path]),
        ("D7-M2", "verify_d7m2_acceptance.py", ["--regression", log_path]),
        ("D7-M3", "verify_d7m3_acceptance.py", [log_path]),
    ]

    print(f"XNU-XZS D7-M4 verifier mode={selected_mode}")
    for label, name, gate_args in gates:
        passed, output = run_regression(os.path.join(script_dir, name), gate_args)
        if not passed:
            print(f"FAIL: {label} regression gate\n{output}")
            return 1
        print(f"[PASS] {label} regression gate")

    with open(log_path, "r", errors="replace") as handle:
        text = handle.read()
    passed, reason = validate_mode(text, selected_mode)
    if not passed:
        print(f"FAIL: {reason}")
        return 1

    if selected_mode == "internal":
        print("D7M4_INTERNAL_PIPELINE_VERIFIER=PASS")
        print("D7_M4_COMPLETE=no (external UART remains mandatory)")
    else:
        print("D7_M4_ACCEPTANCE_VERIFIER=PASS")
    return 0


if __name__ == "__main__":
    sys.exit(main())
