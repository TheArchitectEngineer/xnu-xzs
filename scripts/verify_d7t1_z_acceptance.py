#!/usr/bin/env python3
"""
XNU-XZS D7-T1-Z Hardware Acceptance Verifier
Validates future hardware telemetry from console-ramoops or host session logs.
Requires verified runtime telemetry markers:
- BULK_OUT_CONFIGURED
- BULK_IN_CONFIGURED
- BULK_OUT_TRANSFER_COMPLETE
- BULK_IN_TRANSFER_COMPLETE
- USB_TO_TTY_BYTES (>0)
- TTY_TO_USB_BYTES (>0)
- LIVE_SHELL_PROMPT
"""

import sys
import re
from pathlib import Path


def parse_evidence(log_text: str):
    results = {
        "USB_DEVICE_ENUMERATED": False,
        "BULK_OUT_CONFIGURED": False,
        "BULK_IN_CONFIGURED": False,
        "BULK_OUT_TRANSFER_COMPLETE": False,
        "BULK_IN_TRANSFER_COMPLETE": False,
        "USB_TO_TTY_BYTES": 0,
        "TTY_TO_USB_BYTES": 0,
        "LIVE_SHELL_PROMPT": False,
        "LIVE_COMMAND_ECHO": False,
    }

    # USB enumeration evidence
    if re.search(r"USB_DEVICE_ENUMERATED=yes", log_text) or re.search(r"g_xzs_usb_configured\s*=\s*1", log_text) or re.search(r"USB_CONFIGURATION_VALUE=1", log_text):
        results["USB_DEVICE_ENUMERATED"] = True

    # Bulk endpoints configuration
    if re.search(r"BULK_OUT_CONFIGURED=yes", log_text) or re.search(r"EP2 OUT.*SETEPCONFIG", log_text) or re.search(r"D740/30.*Bulk OUT configured", log_text) or re.search(r"USB_BULK_OUT_EP=0x01", log_text):
        results["BULK_OUT_CONFIGURED"] = True

    if re.search(r"BULK_IN_CONFIGURED=yes", log_text) or re.search(r"EP3 IN.*SETEPCONFIG", log_text) or re.search(r"D740/31.*Bulk IN configured", log_text) or re.search(r"USB_BULK_IN_EP=0x81", log_text):
        results["BULK_IN_CONFIGURED"] = True

    # Bulk transfer completion
    if re.search(r"BULK_OUT_TRANSFER_COMPLETE=yes", log_text) or re.search(r"dwc3_handle_bulk_out_complete", log_text) or re.search(r"USB_BULK_OUT_WORKING=yes", log_text):
        results["BULK_OUT_TRANSFER_COMPLETE"] = True

    if re.search(r"BULK_IN_TRANSFER_COMPLETE=yes", log_text) or re.search(r"dwc3_handle_bulk_in_complete", log_text) or re.search(r"USB_BULK_IN_WORKING=yes", log_text):
        results["BULK_IN_TRANSFER_COMPLETE"] = True

    # USB to TTY bytes transferred
    m_out = re.search(r"(?:USB_TO_TTY_BYTES|USB_BULK_OUT_RX_BYTES)\s*=\s*(0x[0-9a-fA-F]+|[0-9]+)", log_text)
    if m_out:
        val = m_out.group(1)
        results["USB_TO_TTY_BYTES"] = int(val, 16) if val.startswith("0x") else int(val)

    # TTY to USB bytes transferred
    m_in = re.search(r"(?:TTY_TO_USB_BYTES|USB_BULK_IN_TX_BYTES)\s*=\s*(0x[0-9a-fA-F]+|[0-9]+)", log_text)
    if m_in:
        val = m_in.group(1)
        results["TTY_TO_USB_BYTES"] = int(val, 16) if val.startswith("0x") else int(val)

    # Live shell prompt observation
    if re.search(r"LIVE_SHELL_PROMPT(?:_OBSERVED)?=yes", log_text) or re.search(r"(?:xzs#|sh-[0-9.]+[\$#]|\n# )", log_text):
        results["LIVE_SHELL_PROMPT"] = True

    if re.search(r"LIVE_INTERACTIVE_COMMAND_WORKING=yes", log_text) or re.search(r"COMMAND_OUTPUT_MATCH=yes", log_text):
        results["LIVE_COMMAND_ECHO"] = True

    return results


def main():
    if len(sys.argv) < 2:
        print(f"Usage: {sys.argv[0]} <path-to-console-ramoops-or-session-log>", file=sys.stderr)
        sys.exit(2)

    log_path = Path(sys.argv[1])
    if not log_path.exists():
        print(f"ERROR: File not found: {log_path}", file=sys.stderr)
        sys.exit(2)

    log_text = log_path.read_text(errors="replace")
    r = parse_evidence(log_text)

    bulk_out_working = r["BULK_OUT_CONFIGURED"] and r["BULK_OUT_TRANSFER_COMPLETE"]
    bulk_in_working = r["BULK_IN_CONFIGURED"] and r["BULK_IN_TRANSFER_COMPLETE"]
    usb_to_tty_working = bulk_out_working and (r["USB_TO_TTY_BYTES"] > 0)
    console_to_usb_working = bulk_in_working and (r["TTY_TO_USB_BYTES"] > 0)

    print("=======================================================")
    print("=== D7-T1-Z HARDWARE ACCEPTANCE VERIFICATION REPORT ===")
    print(f"TARGET_LOG={log_path.name}")
    print(f"USB_DEVICE_ENUMERATED={'yes' if r['USB_DEVICE_ENUMERATED'] else 'no'}")
    print(f"BULK_OUT_CONFIGURED={'yes' if r['BULK_OUT_CONFIGURED'] else 'no'}")
    print(f"BULK_IN_CONFIGURED={'yes' if r['BULK_IN_CONFIGURED'] else 'no'}")
    print(f"BULK_OUT_WORKING={'yes' if bulk_out_working else 'no'}")
    print(f"BULK_IN_WORKING={'yes' if bulk_in_working else 'no'}")
    print(f"USB_TO_TTY_BYTES={r['USB_TO_TTY_BYTES']}")
    print(f"TTY_TO_USB_BYTES={r['TTY_TO_USB_BYTES']}")
    print(f"USB_TO_TTY_WORKING={'yes' if usb_to_tty_working else 'no'}")
    print(f"CONSOLE_TO_USB_WORKING={'yes' if console_to_usb_working else 'no'}")
    print(f"LIVE_SHELL_PROMPT_OBSERVED={'yes' if r['LIVE_SHELL_PROMPT'] else 'no'}")
    print(f"LIVE_INTERACTIVE_COMMAND_WORKING={'yes' if r['LIVE_COMMAND_ECHO'] else 'no'}")

    all_pass = (
        r["USB_DEVICE_ENUMERATED"] and
        bulk_out_working and
        bulk_in_working and
        usb_to_tty_working and
        console_to_usb_working and
        r["LIVE_SHELL_PROMPT"] and
        r["LIVE_COMMAND_ECHO"]
    )

    print(f"D7_T1_Z_FINAL_ACCEPTANCE={'PASS' if all_pass else 'FAIL'}")
    print("=======================================================")

    sys.exit(0 if all_pass else 1)


if __name__ == "__main__":
    main()
