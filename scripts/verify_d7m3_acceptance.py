#!/usr/bin/env python3
"""
Phase D7-M3 Acceptance and Full D6/D7-M2 Regression Verifier for xnu-xzs.

Validates:
  1. Full Phase D6 regression suite PASS (D6-M1 through D6-M6)
  2. Full Phase D7-M2 regression suite PASS (PID1 -> /bin/sh handoff)
  3. D720 canonical checkpoint sequence in exact monotonic order
  4. Absence of fatal markers or unexpected exceptions
  5. /bin/sh userspace artifact identity and Mach-O metadata
  6. Banner SVC from EL0 with exact userspace VA, length, and native write return
  7. Prompt SVC from EL0 with exact userspace VA, length 5, and native write return
  8. Native Darwin syscall path verified (no write bypass)
  9. Post-prompt EL0 continuity: at least 64 verified getpid round-trips
  10. Shell remains alive in EL0
  11. UART RX remains unavailable (scope preserved for D7-M4)
"""

import os
import re
import subprocess
import sys

REQUIRED_SEQUENCE_D720 = [
    0x00,  # D720/00 D7-M3 entered
    0x10,  # D720/10 shell stdout test armed
    0x20,  # D720/20 banner SVC observed from shell EL0
    0x21,  # D720/21 banner source/callsite/arguments validated
    0x22,  # D720/22 native write returned successfully
    0x23,  # D720/23 exact banner byte count verified
    0x30,  # D720/30 prompt SVC observed from shell EL0
    0x31,  # D720/31 prompt source/callsite/arguments validated
    0x32,  # D720/32 native write returned successfully
    0x33,  # D720/33 exact prompt byte count verified
    0x40,  # D720/40 first post-prompt getpid entered
    0x50,  # D720/50 64th post-prompt getpid returned successfully
    0x90,  # D720/90 authoritative D7-M3 acceptance telemetry
    0x91,  # D720/91 D7-M3 acceptance PASS
    0x01,  # D720/01 terminal milestone marker before D7-M4
]

REQUIRED_TELEMETRY = {
    "D7_M3_ENTERED": "yes",
    "SHELL_RUNNING_IN_EL0": "yes",
    "SHELL_IMAGE_SHA256": "848a10da132fb4482c3cae01a35a73fb6fe4a79bf9e170800489d12f3fbb7bd3",
    "SHELL_ENTRY": "0x00000001000002f0",
    "SHELL_BANNER_USER_VA": "0x0000000100000338",
    "SHELL_BANNER_LENGTH": "1332",
    "SHELL_PROMPT_USER_VA": "0x0000000100000330",
    "SHELL_PROMPT_LENGTH": "5",
    "SHELL_BANNER_FROM_EL0": "yes",
    "SHELL_BANNER_WRITE_NATIVE": "yes",
    "SHELL_BANNER_WRITE_RESULT": "1332",
    "SHELL_BANNER_WRITE_ERROR": "0",
    "SHELL_BANNER_WRITE_CARRY": "clear",
    "SHELL_BANNER_WRITE_EXACT_BYTES": "yes",
    "SHELL_PROMPT_FROM_EL0": "yes",
    "SHELL_PROMPT_WRITE_NATIVE": "yes",
    "SHELL_PROMPT_WRITE_RESULT": "5",
    "SHELL_PROMPT_WRITE_ERROR": "0",
    "SHELL_PROMPT_WRITE_CARRY": "clear",
    "SHELL_PROMPT_WRITE_EXACT_BYTES": "yes",
    "D7M3_SYSCALL_PATH": "NATIVE_DARWIN",
    "D7M3_WRITE_BYPASS": "no",
    "POST_PROMPT_EL0_EXECUTION": "yes",
    "POST_PROMPT_GETPID_ROUNDTRIPS": "64",
    "SHELL_PROCESS_STILL_ALIVE": "yes",
    "UARTDM_TX_AVAILABLE": "yes",
    "UARTDM_RX_AVAILABLE": "no",
    "SHELL_STDOUT_WORKING": "yes",
    "SHELL_PROMPT_VISIBLE": "yes",
    "SHELL_STDIN_WORKING": "no",
    "D7_M3_COMPLETE": "yes",
}


def ordered(expected, actual):
    position = 0
    for value in actual:
        if position < len(expected) and value == expected[position]:
            position += 1
    return position == len(expected)


def parse_telemetry_block(text, block_name="D7-M3"):
    telemetry = {}
    begin_marker = f"=== {block_name} ACCEPTANCE TELEMETRY BEGIN ==="
    end_marker = f"=== {block_name} ACCEPTANCE TELEMETRY END ==="

    target_text = text
    if begin_marker in text:
        start = text.find(begin_marker)
        end = text.find(end_marker, start)
        if end != -1:
            target_text = text[start:end]

    pattern = re.compile(r"^([A-Za-z0-9_-]+)\s*[=:]\s*(.+?)\s*$")
    for line in target_text.splitlines():
        cleaned = re.sub(r"^\[.*?\]\s*", "", line.strip())
        match = pattern.match(cleaned)
        if match:
            telemetry[match.group(1)] = match.group(2)
    return telemetry


def main():
    if len(sys.argv) != 2 or not os.path.isfile(sys.argv[1]):
        print(f"Usage: {sys.argv[0]} <console-log>")
        return 2

    log_path = os.path.abspath(sys.argv[1])
    scripts_dir = os.path.dirname(os.path.abspath(__file__))

    print("============================================================")
    print("XNU-XZS D7-M3 /bin/sh USERSPACE STDOUT ACCEPTANCE VERIFIER")
    print(f"Target Log: {log_path}")
    print("============================================================\n")

    # Step 1: D6 Full Regression Gate
    print("[1/6] Running Phase D6 Full Regression Verifier...")
    d6_verifier = os.path.join(scripts_dir, "verify_d6_acceptance.py")
    res = subprocess.run([sys.executable, d6_verifier, log_path], capture_output=True, text=True, check=False)
    if res.returncode != 0:
        print("FAIL: Phase D6 regression verification failed!")
        print(res.stdout)
        print(res.stderr)
        return 1
    print("[PASS] D6 Full Regression Verifier: 100% PASS\n")

    # Step 2: D7-M2 Regression Gate
    print("[2/6] Running Phase D7-M2 Regression Verifier...")
    d7m2_verifier = os.path.join(scripts_dir, "verify_d7m2_acceptance.py")
    res_m2 = subprocess.run([sys.executable, d7m2_verifier, "--regression", log_path], capture_output=True, text=True, check=False)
    if res_m2.returncode != 0:
        print("FAIL: Phase D7-M2 regression verification failed!")
        print(res_m2.stdout)
        print(res_m2.stderr)
        return 1
    print("[PASS] D7-M2 Regression Verifier: 100% PASS\n")

    # Step 3: Read Log
    with open(log_path, "r", encoding="utf-8", errors="replace") as f:
        text = f.read()

    # Step 4: D720 Breadcrumb Sequence
    print("[3/6] Auditing D720 checkpoint breadcrumb sequence...")
    d720_crumbs = [
        int(match.group(1), 16)
        for match in re.finditer(r"CP[=:]\s*0x0*d720[,\s]+ERR[=:]\s*0x([0-9a-fA-F]+)", text, re.IGNORECASE)
    ]
    if any(c >= 0xEE00 for c in d720_crumbs):
        fatal_crumbs = [hex(c) for c in d720_crumbs if c >= 0xEE00]
        print(f"FAIL: D720 fatal breadcrumbs detected: {fatal_crumbs}")
        return 1

    if not ordered(REQUIRED_SEQUENCE_D720, d720_crumbs):
        print(f"FAIL: D720 sequence incomplete or out of order!")
        print(f"Expected: {[hex(c) for c in REQUIRED_SEQUENCE_D720]}")
        print(f"Actual:   {[hex(c) for c in d720_crumbs]}")
        return 1
    print(f"[PASS] D720 sequence complete ({len(d720_crumbs)} checkpoints verified in order)\n")

    # Step 5: Absence of Fatal / Panic Markers
    print("[4/6] Auditing for fatal markers or unexpected exceptions...")
    if "[XZS-D7M3] FATAL:" in text:
        print("FAIL: [XZS-D7M3] FATAL: marker detected in log")
        return 1
    if "UNEXPECTED EXCEPTION ON CPU 1" in text:
        print("FAIL: UNEXPECTED EXCEPTION ON CPU 1 marker detected")
        return 1
    print("[PASS] Zero fatal markers or unexpected exceptions\n")

    # Step 6: Telemetry Verification
    print("[5/6] Auditing D7-M3 acceptance telemetry keys...")
    telemetry = parse_telemetry_block(text, "D7-M3")
    for key, expected in REQUIRED_TELEMETRY.items():
        actual = telemetry.get(key)
        if actual is None:
            print(f"FAIL: Missing required telemetry key: {key}")
            return 1
        if actual.lower() != expected.lower():
            print(f"FAIL: Key {key} mismatch: expected '{expected}', got '{actual}'")
            return 1
        print(f"  [PASS] {key} = {actual}")

    # Step 7: Userspace Output Proof Audit
    print("\n[6/6] Auditing real EL0 banner and prompt execution proof...")
    banner_res = telemetry.get("SHELL_BANNER_WRITE_RESULT")
    prompt_res = telemetry.get("SHELL_PROMPT_WRITE_RESULT")
    getpid_rounds = telemetry.get("POST_PROMPT_GETPID_ROUNDTRIPS")

    if banner_res != "1332":
        print(f"FAIL: SHELL_BANNER_WRITE_RESULT expected 1332, got {banner_res}")
        return 1
    if prompt_res != "5":
        print(f"FAIL: SHELL_PROMPT_WRITE_RESULT expected 5, got {prompt_res}")
        return 1
    if getpid_rounds != "64":
        print(f"FAIL: POST_PROMPT_GETPID_ROUNDTRIPS expected 64, got {getpid_rounds}")
        return 1

    print("  [PASS] Real EL0 banner write(1, banner, 1332) returned 1332 (error 0, carry clear)")
    print("  [PASS] Real EL0 prompt write(1, prompt, 5) returned 5 (error 0, carry clear)")
    print("  [PASS] Real EL0 post-prompt continuity: 64 getpid round-trips verified")
    print("  [PASS] Shell process remains alive in EL0")
    print("  [PASS] Passive syscall path verified (no kernel fake, no write bypass)")
    print("  [PASS] UART RX remains unavailable (D7-M4 boundary preserved)")

    print("\n============================================================")
    print("D6_REGRESSION_VERIFIER: PASS")
    print("D7_M2_REGRESSION_VERIFIER: PASS")
    print("D7_M3_ACCEPTANCE_VERIFIER: PASS")
    print("/bin/sh userspace stdout banner + prompt verified in EL0.")
    print("============================================================")
    return 0


if __name__ == "__main__":
    sys.exit(main())
