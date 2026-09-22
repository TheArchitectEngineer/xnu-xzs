#!/usr/bin/env python3
"""Host-side Z1–Z4 evidence. Does not certify EL0 consumption or seal D7-T1."""

import importlib.util
import sys
import time
from pathlib import Path

_TOOL = Path(__file__).resolve().parents[1] / "tools" / "xzs-console" / "xzs-console.py"
_spec = importlib.util.spec_from_file_location("xzs_console", _TOOL)
xzs_console = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(xzs_console)

_LIVE_FIELDS = (
    "HOST_PRE_SEND_PROMPT_OBSERVED",
    "HOST_SENT_ABC_LF",
    "HOST_POST_READ_PROMPT_OBSERVED",
    "HOST_TO_TARGET_TRANSFER_OBSERVED",
    "TARGET_TO_HOST_TRANSFER_OBSERVED",
    "TARGET_SIDE_READ_PROOF_AVAILABLE",
    "LIVE_BIDIRECTIONAL_TRANSPORT_HOST_ONLY",
    "FULL_EL0_BIDIRECTIONAL_ACCEPTANCE",
)


def evaluate_live_exchange(
    pre_send_prompt_observed,
    payload_sent,
    post_send_prompt_observed,
    device_present,
):
    """Classify the ABC / post-read prompt exchange.

    A missed pre-send prompt does not fail host bidirectional transport.
    Host bytes do not prove the target EL0 read.
    """
    if not device_present:
        result = {key: "no" for key in _LIVE_FIELDS}
        result["FULL_EL0_BIDIRECTIONAL_ACCEPTANCE"] = "requires_target_evidence"
        result["transport_ok"] = False
        return result

    host_to_target = bool(payload_sent)
    target_to_host = bool(post_send_prompt_observed)
    result = {
        "HOST_PRE_SEND_PROMPT_OBSERVED": "yes" if pre_send_prompt_observed else "no",
        "HOST_SENT_ABC_LF": "yes" if payload_sent else "no",
        "HOST_POST_READ_PROMPT_OBSERVED": "yes" if post_send_prompt_observed else "no",
        "HOST_TO_TARGET_TRANSFER_OBSERVED": "yes" if host_to_target else "no",
        "TARGET_TO_HOST_TRANSFER_OBSERVED": "yes" if target_to_host else "no",
        "TARGET_SIDE_READ_PROOF_AVAILABLE": "no",
        "LIVE_BIDIRECTIONAL_TRANSPORT_HOST_ONLY": "yes" if host_to_target and target_to_host else "no",
        "FULL_EL0_BIDIRECTIONAL_ACCEPTANCE": "requires_target_evidence",
        "transport_ok": host_to_target and target_to_host,
    }
    return result


def format_live_exchange(result):
    return "\n".join(f"{key}={result[key]}" for key in _LIVE_FIELDS)


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
        print(format_live_exchange(evaluate_live_exchange(False, False, False, False)))
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
    after = b""
    deadline = time.time() + 20
    while time.time() < deadline and b"xzs#" not in after:
        chunk = xzs_console.test_bulk_in(dev, timeout_ms=1000)
        if chunk:
            after += chunk
        else:
            time.sleep(0.2)
    live = evaluate_live_exchange(prompt, bool(sent), b"xzs#" in after, True)
    print(format_live_exchange(live))
    print("EL0_TO_HOST_OUTPUT_WORKING=" + live["HOST_POST_READ_PROMPT_OBSERVED"])
    print("HOST_TO_EL0_INPUT_WORKING=see_target_el0_read")
    print("HOST_INTERACTIVE_TEST_COMMAND_1=none")
    print("HOST_INTERACTIVE_TEST_COMMAND_2=none")
    print("AVAILABLE_SHELL_COMMANDS=none")
    print("D7_T1_SEALED=not_declared_by_host")
    z_ok = out_ok and in_data == expected_in and loop_ok
    return 0 if z_ok and live["transport_ok"] else 1


if __name__ == "__main__":
    sys.exit(main())
