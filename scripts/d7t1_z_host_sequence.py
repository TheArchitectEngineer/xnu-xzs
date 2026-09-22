#!/usr/bin/env python3
"""Host-side Z1–Z4 evidence. Does not certify a shell or seal D7-T1."""

import importlib.util
import sys
import time
from pathlib import Path

_TOOL = Path(__file__).resolve().parents[1] / "tools" / "xzs-console" / "xzs-console.py"
_spec = importlib.util.spec_from_file_location("xzs_console", _TOOL)
xzs_console = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(xzs_console)


def main() -> int:
    deadline = time.time() + 70
    dev = None
    while time.time() < deadline and dev is None:
        dev = xzs_console.find_xzs_device()
        if dev is None:
            time.sleep(0.2)
    print("HOST_DEVICE_WAIT_DONE=yes")
    if dev is None:
        print("USB_DEVICE_FOUND=no")
        print("ENUM_TEST=FAIL")
        print("HOST_LOOPBACK_EXACT_MATCH=no")
        print("HOST_LIVE_SHELL_PROMPT_OBSERVED=no")
        print("HOST_LIVE_INTERACTIVE_COMMAND_WORKING=no")
        return 1

    try:
        if dev.is_kernel_driver_active(0):
            dev.detach_kernel_driver(0)
    except Exception:
        pass
    try:
        dev.set_configuration()
        xzs_console.usb.util.claim_interface(dev, 0)
    except Exception:
        pass

    out_ok = False
    for _ in range(20):
        out_ok = xzs_console.test_bulk_out(dev, timeout_ms=1000)
        if out_ok:
            break
        time.sleep(0.4)
    in_data = None
    for _ in range(10):
        in_data = xzs_console.test_bulk_in(dev, timeout_ms=1000)
        if in_data is not None:
            break
        time.sleep(0.3)
    expected_in = b"XZS-BULK-IN-TEST\n"
    print("HOST_BULK_IN_EXACT_MATCH=" + ("yes" if in_data == expected_in else "no"))
    time.sleep(0.3)
    loop_ok = False
    for _ in range(8):
        loop_ok = xzs_console.test_loopback(dev, timeout_ms=2000)
        if loop_ok:
            break
        time.sleep(0.3)
    print("HOST_LOOPBACK_EXACT_MATCH=" + ("yes" if loop_ok else "no"))
    print("USB_DEVICE_FOUND=yes")
    print("HOST_VID_PID_MATCH=yes")
    seen = b""
    prompt = False
    deadline = time.time() + 50
    while time.time() < deadline and not prompt:
        chunk = xzs_console.test_bulk_in(dev, timeout_ms=1000)
        if chunk:
            seen += chunk
            if b"xzs#" in seen:
                prompt = True
        else:
            time.sleep(0.2)
    print("HOST_LIVE_SHELL_PROMPT_OBSERVED=" + ("yes" if prompt else "no"))
    sent = xzs_console.test_bulk_out(dev, payload=b"ABC\n", timeout_ms=2000)
    print("HOST_SENT_ABC_LF=" + ("yes" if sent else "no"))
    after = b""
    deadline = time.time() + 20
    while time.time() < deadline and b"xzs#" not in after:
        chunk = xzs_console.test_bulk_in(dev, timeout_ms=1000)
        if chunk:
            after += chunk
        else:
            time.sleep(0.2)
    print("EL0_TO_HOST_OUTPUT_WORKING=" + ("yes" if b"xzs#" in after else "no"))
    print("HOST_TO_EL0_INPUT_WORKING=see_target_el0_read")
    print("LIVE_BIDIRECTIONAL_TRANSPORT=" + ("yes" if prompt and sent and b"xzs#" in after else "no"))
    print("HOST_INTERACTIVE_TEST_COMMAND_1=none")
    print("HOST_INTERACTIVE_TEST_COMMAND_2=none")
    print("AVAILABLE_SHELL_COMMANDS=none")
    print("D7_T1_SEALED=not_declared_by_host")
    return 0 if out_ok and in_data == expected_in and loop_ok and prompt and sent and b"xzs#" in after else 1


if __name__ == "__main__":
    sys.exit(main())
