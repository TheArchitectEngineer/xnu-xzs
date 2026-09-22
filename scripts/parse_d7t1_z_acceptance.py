#!/usr/bin/env python3
"""
Host-Side Acceptance Parser for D7-T1-Z
Combines xzs-console host tool output and kernel pstore (console-ramoops) telemetry.
Emits exact required fields:
- T1Z_HOST_DEVICE_FOUND=
- T1Z_BULK_OUT=
- T1Z_BULK_IN=
- T1Z_LOOPBACK=
- T1Z_USB_TO_TTY=
- T1Z_CONSOLE_TO_USB=
- T1Z_LIVE_SHELL=
"""

import sys
import re
from pathlib import Path


def main():
    if len(sys.argv) < 3:
        print(f"Usage: {sys.argv[0]} <xzs-console-host-log> <pstore-console-ramoops>", file=sys.stderr)
        sys.exit(2)

    host_path = Path(sys.argv[1])
    pstore_path = Path(sys.argv[2])

    host_text = host_path.read_text(errors="replace") if host_path.exists() else ""
    pstore_text = pstore_path.read_text(errors="replace") if pstore_path.exists() else ""

    # 1. T1Z_HOST_DEVICE_FOUND
    host_dev_found = "NOT_TESTED"
    if "USB_DEVICE_FOUND=yes" in host_text or "ENUM_TEST=PASS" in host_text:
        host_dev_found = "PASS"
    elif "USB_DEVICE_FOUND=no" in host_text or "ENUM_TEST=FAIL" in host_text:
        host_dev_found = "FAIL"

    # 2. T1Z_BULK_OUT
    bulk_out = "NOT_TESTED"
    if "BULK_OUT_STATUS=PASS" in host_text or "USB_BULK_OUT_PASS=yes" in host_text:
        bulk_out = "PASS"
    elif "BULK_OUT_STATUS=FAIL" in host_text or "BULK_OUT_STATUS=TIMEOUT" in host_text:
        bulk_out = "FAIL"

    # 3. T1Z_BULK_IN
    bulk_in = "NOT_TESTED"
    if "BULK_IN_STATUS=PASS" in host_text or "USB_BULK_IN_PASS=yes" in host_text:
        bulk_in = "PASS"
    elif "BULK_IN_STATUS=FAIL" in host_text or "BULK_IN_STATUS=TIMEOUT" in host_text:
        bulk_in = "FAIL"

    # 4. T1Z_LOOPBACK
    loopback = "NOT_TESTED"
    if "BULK_LOOPBACK_STATUS=PASS" in host_text or "BULK_LOOPBACK_HARDWARE=PASS" in host_text:
        loopback = "PASS"
    elif "BULK_LOOPBACK_STATUS=FAIL" in host_text or "BULK_LOOPBACK_STATUS=MISMATCH" in host_text:
        loopback = "FAIL"
    elif "BULK_LOOPBACK_HARDWARE=NOT_TESTED" in host_text:
        loopback = "NOT_TESTED"

    # 5. T1Z_USB_TO_TTY — target byte count only. Bulk OUT alone is not tty delivery.
    usb_to_tty = "NOT_TESTED"
    m_tty_in = re.search(r"USB_TO_TTY_BYTES\s*=\s*(0x[0-9a-fA-F]+|[0-9]+)", pstore_text)
    if m_tty_in:
        val = int(m_tty_in.group(1), 16) if m_tty_in.group(1).startswith("0x") else int(m_tty_in.group(1))
        usb_to_tty = "PASS" if val > 0 else "FAIL"

    # 6. T1Z_CONSOLE_TO_USB — target byte count only. A Bulk IN test payload is not console TX.
    console_to_usb = "NOT_TESTED"
    m_tty_out = re.search(r"TTY_TO_USB_BYTES\s*=\s*(0x[0-9a-fA-F]+|[0-9]+)", pstore_text)
    if m_tty_out:
        val = int(m_tty_out.group(1), 16) if m_tty_out.group(1).startswith("0x") else int(m_tty_out.group(1))
        console_to_usb = "PASS" if val > 0 else "FAIL"

    # 7. Live shell requires an explicit host observation. Target pstore cannot certify it.
    live_shell = "NOT_TESTED"
    if "HOST_LIVE_SHELL_PROMPT_OBSERVED=yes" in host_text:
        live_shell = "PASS"
    elif "HOST_LIVE_SHELL_PROMPT_OBSERVED=no" in host_text:
        live_shell = "FAIL"

    print("========================================")
    print("=== D7-T1-Z HOST ACCEPTANCE PARSER ===")
    print(f"HOST_LOG={host_path.name}")
    print(f"PSTORE_LOG={pstore_path.name}")
    print(f"T1Z_HOST_DEVICE_FOUND={host_dev_found}")
    print(f"T1Z_BULK_OUT={bulk_out}")
    print(f"T1Z_BULK_IN={bulk_in}")
    print(f"T1Z_LOOPBACK={loopback}")
    print(f"T1Z_USB_TO_TTY={usb_to_tty}")
    print(f"T1Z_CONSOLE_TO_USB={console_to_usb}")
    print(f"T1Z_LIVE_SHELL={live_shell}")
    print("========================================")


if __name__ == "__main__":
    main()
