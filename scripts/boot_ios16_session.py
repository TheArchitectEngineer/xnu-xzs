#!/usr/bin/env python3
"""
Boot artifacts/builds/xzs-xnu-boot.img on Sony Xperia XZs (BH905SX976),
perform the USB 1209:000a bulk transport handshake, and capture live
iOS 16 kernel + launchd + SpringBoard bring-up telemetry.
"""

import importlib.util
import subprocess
import sys
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
_TOOL = ROOT / "tools" / "xzs-console" / "xzs-console.py"
_spec = importlib.util.spec_from_file_location("xzs_console", _TOOL)
xzs_console = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(xzs_console)


def main():
    boot_img = ROOT / "artifacts" / "builds" / "xzs-xnu-boot.img"
    log_path = ROOT / "artifacts" / "builds" / "ios16-boot-live.log"

    # If 1209:000a is currently connected, try sending __XZS_FASTBOOT__ first
    dev_existing = xzs_console.open_stable_device(timeout_sec=1)
    if dev_existing is not None:
        print("=== Sending __XZS_FASTBOOT__ to active XNU USB console (1209:000a) ===", flush=True)
        try:
            xzs_console.bulk_write(dev_existing, b"__XZS_FASTBOOT__", timeout_ms=500)
        except Exception:
            pass
        time.sleep(1.0)

    print(f"=== Booting {boot_img} via fastboot (waiting for fastboot mode if needed) ===", flush=True)
    subprocess.run(
        ["/opt/homebrew/bin/fastboot", "boot", str(boot_img)],
        check=True,
    )
    time.sleep(1.5)

    print("=== Waiting for XNU USB device 1209:000a ===", flush=True)
    dev = xzs_console.open_stable_device(timeout_sec=45)
    if dev is None:
        print("ERROR: USB device 1209:000a did not enumerate within 45s", flush=True)
        sys.exit(1)

    print("=== Performing USB Bulk Transport Handshake ===", flush=True)
    if not xzs_console.transport_handshake(dev):
        print("WARNING: transport_handshake returned False; attempting raw read...", flush=True)

    window_sec = float(sys.argv[1]) if len(sys.argv) > 1 else 90.0
    print(f"=== Capturing Live iOS 16 Console Output ({window_sec:.0f}s window) ===", flush=True)
    t_start = time.time()
    collected = bytearray()

    with open(log_path, "wb") as lf:
        while time.time() - t_start < window_sec:
            try:
                chunk, kind = xzs_console.bulk_read(dev, timeout_ms=250)
            except Exception as exc:
                print(f"\n[USB disconnected: {exc}]", flush=True)
                break
            if chunk:
                collected.extend(chunk)
                lf.write(chunk)
                lf.flush()
                sys.stdout.write(chunk.decode("utf-8", errors="replace"))
                sys.stdout.flush()
            else:
                if kind == "error":
                    print("\n[USB bulk_read error / device rebooted]", flush=True)
                    break
                time.sleep(0.02)

    print(f"\n=== Capture finished ({len(collected)} bytes saved to {log_path}) ===", flush=True)


if __name__ == "__main__":
    main()
