#!/usr/bin/env python3
"""
XNU-XZS Phase D7-M4 Acceptance & Regression Verifier
Validates:
  1. Full Phase D6 regression suite (M1 through M6)
  2. Full Phase D7-M2 regression suite (PID1 -> /bin/sh EL0 handoff)
  3. Full Phase D7-M3 regression suite (Shell stdout banner and prompt)
  4. Strict monotonic ordering of D730 checkpoints
  5. Absence of fatal exceptions, panics, or fault markers
  6. D7-M4 acceptance telemetry block
  7. Genuine hardware RX byte visibility and layer correlation
"""

import sys
import re
import os
import subprocess

EXPECTED_D730_SEQUENCE = [
    0x00,  # D730/00: M4 entered
    0x10,  # D730/10: RX hardware audit complete
    0x11,  # D730/11: RX hardware configured
    0x20,  # D730/20: External RX byte detected in UARTDM
    0x21,  # D730/21: Raw UART byte validated
    0x30,  # D730/30: Driver receive_ready success
    0x31,  # D730/31: Driver receive_data success
    0x40,  # D730/40: RX buffer enqueue
    0x41,  # D730/41: RX buffer dequeue
    0x50,  # D730/50: tty input accepted via cons_cinput
    0x51,  # D730/51: tty wakeup issued to blocked reader
    0x60,  # D730/60: Shell read(0) entered
    0x61,  # D730/61: Shell read blocked normally on tty waitchannel
    0x62,  # D730/62: Shell read awakened by incoming character
    0x63,  # D730/63: Shell read returned to EL0 with data
    0x70,  # D730/70: Input bytes validated in EL0
    0x90,  # D730/90: M4 acceptance confirmed
    0x91,  # D730/91: Phase D7-M4 complete and verified
    0x01,  # D730/01: Terminal milestone
]

REQUIRED_TELEMETRY = {
    "D7_M4_ENTERED": "yes",
    "UARTDM_RX_HW_CONFIGURED": "yes",
    "UARTDM_RX_AVAILABLE": "yes",
    "UARTDM_RX_EXTERNAL_BYTE_OBSERVED": "yes",
    "UARTDM_RX_RAW_BYTE_MATCH": "yes",
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
    "SHELL_STDIN_WORKING": "yes",
    "D7_M4_COMPLETE": "yes",
}

def parse_telemetry_block(text, block_name):
    telemetry = {}
    pattern = rf"===\s*{block_name}\s+ACCEPTANCE\s+TELEMETRY\s+BEGIN\s*===(.*?)===\s*{block_name}\s+ACCEPTANCE\s+TELEMETRY\s+END\s*==="
    match = re.search(pattern, text, re.DOTALL | re.IGNORECASE)
    if not match:
        return telemetry
    for line in match.group(1).splitlines():
        line = line.strip()
        if not line or line.startswith("#"):
            continue
        if "=" in line:
            key, val = line.split("=", 1)
            telemetry[key.strip()] = val.strip()
    return telemetry

def main():
    if len(sys.argv) < 2:
        print("Usage: verify_d7m4_acceptance.py <console-log-path>")
        return 1

    log_path = os.path.abspath(sys.argv[1])
    if not os.path.exists(log_path):
        print(f"FAIL: log file not found: {log_path}")
        return 1

    script_dir = os.path.dirname(os.path.abspath(__file__))
    d6_verifier = os.path.join(script_dir, "verify_d6_acceptance.py")
    d7m2_verifier = os.path.join(script_dir, "verify_d7m2_acceptance.py")
    d7m3_verifier = os.path.join(script_dir, "verify_d7m3_acceptance.py")

    print("============================================================")
    print("XNU-XZS D7-M4 /bin/sh USERSPACE STDIN ACCEPTANCE VERIFIER")
    print(f"Target Log: {log_path}")
    print("============================================================\n")

    # Step 1: Phase D6 Regression Gate
    print("[1/8] Running Phase D6 Full Regression Verifier...")
    res_d6 = subprocess.run([sys.executable, d6_verifier, log_path], capture_output=True, text=True, check=False)
    if res_d6.returncode != 0:
        print(f"FAIL: Phase D6 regression verification failed!\n{res_d6.stdout}\n{res_d6.stderr}")
        return 1
    print("[PASS] D6 Full Regression Verifier: 100% PASS\n")

    # Step 2: Phase D7-M2 Regression Gate
    print("[2/8] Running Phase D7-M2 Regression Verifier...")
    res_m2 = subprocess.run([sys.executable, d7m2_verifier, "--regression", log_path], capture_output=True, text=True, check=False)
    if res_m2.returncode != 0:
        print(f"FAIL: Phase D7-M2 regression verification failed!\n{res_m2.stdout}\n{res_m2.stderr}")
        return 1
    print("[PASS] D7-M2 Regression Verifier: 100% PASS\n")

    # Step 3: Phase D7-M3 Regression Gate
    print("[3/8] Running Phase D7-M3 Regression Verifier...")
    res_m3 = subprocess.run([sys.executable, d7m3_verifier, log_path], capture_output=True, text=True, check=False)
    if res_m3.returncode != 0:
        print(f"FAIL: Phase D7-M3 regression verification failed!\n{res_m3.stdout}\n{res_m3.stderr}")
        return 1
    print("[PASS] D7-M3 Regression Verifier: 100% PASS\n")

    with open(log_path, "r", errors="replace") as f:
        text = f.read()

    # Step 4: D730 Breadcrumb Sequence
    print("[4/8] Auditing D730 checkpoint breadcrumb sequence...")
    d730_crumbs = [
        int(match.group(1), 16)
        for match in re.finditer(r"CP[=:]\s*0x0*d730[,\s]+ERR[=:]\s*0x([0-9a-fA-F]+)", text, re.IGNORECASE)
    ]
    if d730_crumbs != EXPECTED_D730_SEQUENCE:
        print(f"FAIL: D730 breadcrumb sequence mismatch:")
        print(f"  Expected: {[hex(x) for x in EXPECTED_D730_SEQUENCE]}")
        print(f"  Observed: {[hex(x) for x in d730_crumbs]}")
        return 1
    print(f"[PASS] D730 sequence complete ({len(d730_crumbs)} checkpoints verified in order)\n")

    # Step 5: Absence of Fatal / Panic Markers
    print("[5/8] Auditing for fatal markers or unexpected exceptions...")
    if "[XZS-D7M4] FATAL:" in text:
        print("FAIL: [XZS-D7M4] FATAL: marker detected in log")
        return 1
    if "Kernel Panic" in text or "panic(cpu" in text:
        print("FAIL: Kernel panic detected in log")
        return 1
    print("[PASS] Zero fatal markers or unexpected exceptions\n")

    # Step 6: Telemetry Verification
    print("[6/8] Auditing D7-M4 acceptance telemetry keys...")
    telemetry = parse_telemetry_block(text, "D7-M4")
    for key, expected in REQUIRED_TELEMETRY.items():
        actual = telemetry.get(key)
        if actual is None:
            print(f"FAIL: Missing required telemetry key: {key}")
            return 1
        if actual != expected:
            print(f"FAIL: Telemetry {key} = '{actual}', expected '{expected}'")
            return 1
        print(f"  [PASS] {key} = {actual}")

    # Step 7: RX Byte Correlation Proof
    print("\n[7/8] Auditing hardware RX byte correlation proof...")
    ext_byte = telemetry.get("EXTERNAL_TEST_BYTE")
    uart_byte = telemetry.get("UARTDM_RX_BYTE")
    driver_byte = telemetry.get("DRIVER_RX_BYTE")
    tty_byte = telemetry.get("TTY_RX_BYTE")
    el0_byte = telemetry.get("EL0_READ_BYTE")

    if not (ext_byte and uart_byte and driver_byte and tty_byte and el0_byte):
        print("FAIL: Missing byte correlation telemetry (EXTERNAL_TEST_BYTE, UARTDM_RX_BYTE, DRIVER_RX_BYTE, TTY_RX_BYTE, EL0_READ_BYTE)")
        return 1

    if not (ext_byte == uart_byte == driver_byte == tty_byte == el0_byte):
        print(f"FAIL: Byte mismatch across layers: ext={ext_byte}, uart={uart_byte}, driver={driver_byte}, tty={tty_byte}, el0={el0_byte}")
        return 1

    print(f"  [PASS] Layer byte correlation: {ext_byte} verified through all layers")

    # Step 8: Genuine Stdin Line Proof
    print("\n[8/8] Auditing shell stdin line execution...")
    line_exec = telemetry.get("SHELL_STDIN_LINE_PROCESSED")
    if line_exec != "yes":
        print("FAIL: SHELL_STDIN_LINE_PROCESSED != yes")
        return 1
    print("  [PASS] Shell stdin line processing verified in EL0")

    print("\n============================================================")
    print("D6_REGRESSION_VERIFIER: PASS")
    print("D7_M2_REGRESSION_VERIFIER: PASS")
    print("D7_M3_REGRESSION_VERIFIER: PASS")
    print("D7_M4_ACCEPTANCE_VERIFIER: PASS")
    print("/bin/sh userspace stdin verified in EL0.")
    print("============================================================")
    return 0

if __name__ == "__main__":
    sys.exit(main())
