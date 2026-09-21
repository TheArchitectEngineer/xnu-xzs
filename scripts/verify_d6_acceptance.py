#!/usr/bin/env python3
"""
Final Phase D6 Acceptance & Full Regression Verifier for xnu-xzs.

Validates the complete D6 userspace foundation chain:
  D6-M1 (PID 1 process/task/thread skeleton)
  D6-M2 (Minimal Mach-O loader validation)
  D6-M3 (User VM map, PAGEZERO, RX __TEXT, RW/NX stack)
  D6-M4 (EL1 -> EL0 transition, first user instruction, canonical SVC64)
  D6-M5 (Real Darwin syscall dispatch & return-to-EL0 round-trip)
  D6-M6 (Native /dev/console stdio descriptors, real write(1), stable getpid loop)
"""

import os
import re
import subprocess
import sys


def parse_telemetry(text):
    telemetry = {}
    pattern = re.compile(r"^([A-Za-z0-9_-]+)\s*[=:]\s*(.+?)\s*$")
    for line in text.splitlines():
        cleaned = re.sub(r"^\[.*?\]\s*", "", line.strip())
        match = pattern.match(cleaned)
        if match:
            telemetry[match.group(1)] = match.group(2)
    return telemetry


def parse_breadcrumbs(text):
    breadcrumbs = []
    pattern = re.compile(r"CP[=:]\s*0x([0-9a-fA-F]+)[,\s]+ERR[=:]\s*0x([0-9a-fA-F]+)")
    for match in pattern.finditer(text):
        breadcrumbs.append((int(match.group(1), 16), int(match.group(2), 16)))
    return breadcrumbs


def main():
    if len(sys.argv) != 2 or not os.path.isfile(sys.argv[1]):
        print(f"Usage: {sys.argv[0]} <console-log>")
        return 2

    log_path = os.path.abspath(sys.argv[1])
    scripts_dir = os.path.dirname(os.path.abspath(__file__))

    with open(log_path, "r", encoding="utf-8", errors="replace") as stream:
        text = stream.read()

    print("============================================================")
    print("XNU-XZS PHASE D6 FINAL ACCEPTANCE & REGRESSION VERIFICATION")
    print(f"Target Log: {log_path}")
    print("============================================================\n")

    # 1. Fatal breadcrumb and crash check
    breadcrumbs = parse_breadcrumbs(text)
    fatal_crumbs = [f"CP:0x{cp:x} ERR:0x{err:x}" for cp, err in breadcrumbs if err >= 0xEE00]
    if fatal_crumbs:
        print(f"FAIL: Fatal breadcrumb(s) detected: {fatal_crumbs}")
        return 1

    if "UNEXPECTED EXCEPTION ON CPU" in text:
        print("FAIL: Unexpected CPU exception marker found")
        return 1

    if "[XZS-D6M" in text and "FATAL:" in text:
        print("FAIL: Explicit fatal marker detected in log")
        return 1

    # 2. Invoke Milestone Verifiers in Regression Mode
    # D6-M3 (Regression-only verifies D5, D6-M1, D6-M2, and D6-M3)
    d6m3_script = os.path.join(scripts_dir, "verify_d6m3_acceptance.py")
    res_m3 = subprocess.run(
        [sys.executable, d6m3_script, "--regression-only", log_path],
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        text=True,
    )
    if res_m3.returncode != 0:
        print("FAIL: D6-M3 regression verifier failed")
        print(res_m3.stdout[-1000:])
        return 1

    # Confirm D6-M1 and D6-M2 checkpoint coverage within D6-M3 output
    d6m1_cps = [err for cp, err in breadcrumbs if cp == 0xD600]
    if not (0x00 in d6m1_cps and 0x91 in d6m1_cps):
        print("FAIL: D6-M1 checkpoint span 0xD600 incomplete")
        return 1
    print("D6_M1_REGRESSION_VERIFIER=PASS")

    d6m2_cps = [err for cp, err in breadcrumbs if cp == 0xD610]
    if not (0x00 in d6m2_cps and 0x91 in d6m2_cps):
        print("FAIL: D6-M2 checkpoint span 0xD610 incomplete")
        return 1
    print("D6_M2_REGRESSION_VERIFIER=PASS")

    d6m3_cps = [err for cp, err in breadcrumbs if cp == 0xD620]
    if not (0x00 in d6m3_cps and 0x91 in d6m3_cps):
        print("FAIL: D6-M3 checkpoint span 0xD620 incomplete")
        return 1
    print("D6_M3_REGRESSION_VERIFIER=PASS")

    # D6-M4 Verifier
    d6m4_script = os.path.join(scripts_dir, "verify_d6m4_acceptance.py")
    res_m4 = subprocess.run(
        [sys.executable, d6m4_script, "--regression", log_path],
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        text=True,
    )
    if res_m4.returncode != 0:
        print("FAIL: D6-M4 regression verifier failed")
        print(res_m4.stdout[-1000:])
        return 1
    print("D6_M4_REGRESSION_VERIFIER=PASS")

    # D6-M5 Verifier
    d6m5_script = os.path.join(scripts_dir, "verify_d6m5_acceptance.py")
    res_m5 = subprocess.run(
        [sys.executable, d6m5_script, "--regression", log_path],
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        text=True,
    )
    if res_m5.returncode != 0:
        print("FAIL: D6-M5 regression verifier failed")
        print(res_m5.stdout[-1000:])
        return 1
    print("D6_M5_REGRESSION_VERIFIER=PASS")

    # D6-M6 Verifier
    d6m6_script = os.path.join(scripts_dir, "verify_d6m6_acceptance.py")
    res_m6 = subprocess.run(
        [sys.executable, d6m6_script, log_path],
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        text=True,
    )
    if res_m6.returncode != 0:
        print("FAIL: D6-M6 regression verifier failed")
        print(res_m6.stdout[-1000:])
        return 1
    print("D6_M6_REGRESSION_VERIFIER=PASS")

    # 3. Canonical Telemetry & Invariant Audit
    telemetry = parse_telemetry(text)

    # VM / W^X checks
    if telemetry.get("PID1_VM_UNEXPECTED_RWX_COUNT") != "0":
        print(f"FAIL: Unexpected RWX count is {telemetry.get('PID1_VM_UNEXPECTED_RWX_COUNT')}")
        return 1
    if telemetry.get("USER_TEXT_WRITABLE_AFTER_FINALIZE") != "no":
        print("FAIL: User text writable after finalize")
        return 1
    if telemetry.get("USER_STACK_EXECUTABLE") != "no":
        print("FAIL: User stack is executable")
        return 1

    # Syscall & EL0 checks
    if telemetry.get("PID1_STARTED") != "yes":
        print("FAIL: PID1_STARTED != yes")
        return 1
    if telemetry.get("FIRST_EL0_INSTRUCTION_EXECUTED") != "yes":
        print("FAIL: FIRST_EL0_INSTRUCTION_EXECUTED != yes")
        return 1
    if telemetry.get("SYSCALL_DISPATCH_REACHED") != "yes":
        print("FAIL: SYSCALL_DISPATCH_REACHED != yes")
        return 1
    if telemetry.get("WRITE_SYSCALL_HANDLER_COMPLETED") != "yes":
        print("FAIL: WRITE_SYSCALL_HANDLER_COMPLETED != yes")
        return 1
    if telemetry.get("WRITE_RETURN_VALUE") != "26":
        print(f"FAIL: WRITE_RETURN_VALUE is {telemetry.get('WRITE_RETURN_VALUE')}, expected 26")
        return 1

    # Console descriptor checks
    if telemetry.get("FD0_TARGET") != "/dev/console" or \
       telemetry.get("FD1_TARGET") != "/dev/console" or \
       telemetry.get("FD2_TARGET") != "/dev/console":
        print("FAIL: Console fd 0/1/2 target mismatch")
        return 1
    if telemetry.get("DEV_CONSOLE_VNODE_TYPE") != "VCHR" or telemetry.get("DEV_CONSOLE_DEVICE") != "0:0":
        print("FAIL: /dev/console device mismatch")
        return 1
    if telemetry.get("PID1_CONSOLE_OUTPUT_VERIFIED") != "yes":
        print("FAIL: PID1_CONSOLE_OUTPUT_VERIFIED != yes")
        return 1

    # Stable PID1 runtime loop
    if telemetry.get("PID1_STABLE_RUNTIME") != "yes":
        print("FAIL: PID1_STABLE_RUNTIME != yes")
        return 1
    if telemetry.get("STABLE_RUNTIME_SYSCALL") != "getpid":
        print("FAIL: STABLE_RUNTIME_SYSCALL != getpid")
        return 1

    count_str = telemetry.get("STABLE_RUNTIME_ROUNDTRIP_COUNT", "")
    try:
        round_trips = int(count_str, 16) if count_str.startswith("0x") else int(count_str)
    except ValueError:
        rt_match = re.search(r"sustained\s+getpid\s+round\s+trips\s*=\s*(\d+)", text, re.I)
        round_trips = int(rt_match.group(1)) if rt_match else 0

    if round_trips < 256:
        print(f"FAIL: Insufficient sustained round trips: {round_trips} < 256")
        return 1



    # UART transport boundary preservation
    if telemetry.get("CURRENT_INPUT_TRANSPORT") != "msm_uartdm_tx_only":
        print("FAIL: CURRENT_INPUT_TRANSPORT mismatch")
        return 1
    if telemetry.get("PHYSICAL_CONSOLE_RX_AVAILABLE") != "no":
        print("FAIL: PHYSICAL_CONSOLE_RX_AVAILABLE must remain 'no'")
        return 1

    print("\n============================================================")
    print("PHASE D6 USERSPACE FOUNDATION VERIFICATION RESULT: PASS")
    print(f"Sustained PID1 getpid round-trips: {round_trips}")
    print("============================================================\n")

    print("D6_COMPLETE=yes")
    print("D6_SEALED=yes")
    return 0


if __name__ == "__main__":
    sys.exit(main())
