#!/usr/bin/env python3
"""
Hardware execution session for D7-T2N-1: NATIVE XZSFS UBC ATTACHMENT.
Executes fastboot boot and performs the test sequence over USB console.
"""

import importlib.util
import os
import subprocess
import sys
import time
from pathlib import Path

_TOOL = Path(__file__).resolve().parents[1] / "tools" / "xzs-console" / "xzs-console.py"
_spec = importlib.util.spec_from_file_location("xzs_console", _TOOL)
xzs_console = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(xzs_console)


def collect(dev, seconds=1.0):
    end = time.time() + seconds
    buf = b""
    quiet = 0
    while time.time() < end:
        chunk, kind = xzs_console.bulk_read(dev, timeout_ms=200)
        if chunk:
            buf += chunk
            quiet = 0
        else:
            quiet += 1
            if buf and quiet >= 4:
                break
            time.sleep(0.05)
    return buf


def send_cmd(dev, cmd_str, wait_sec=1.5):
    print(f"\n>>> SEND: {cmd_str.strip()}", flush=True)
    payload = cmd_str.encode("utf-8") if isinstance(cmd_str, str) else cmd_str
    if not payload.endswith(b"\n"):
        payload += b"\n"
    written, kind = xzs_console.bulk_write(dev, payload, timeout_ms=2000)
    if kind is not None or written != len(payload):
        print(f"!!! WRITE ERROR: written={written}, kind={kind}", flush=True)
        return ""
    resp = collect(dev, seconds=wait_sec)
    resp_text = resp.decode("utf-8", errors="replace")
    print(resp_text, end="", flush=True)
    return resp_text


def main():
    log_dir = Path("artifacts/hw/t2n1-fd5549f")
    log_dir.mkdir(parents=True, exist_ok=True)
    log_file = log_dir / "host.txt"

    boot_img = Path("artifacts/builds/xzs-xnu-boot.img")
    if not boot_img.exists():
        print(f"ERROR: {boot_img} does not exist!", file=sys.stderr)
        sys.exit(1)

    print("=== STEP 0: BOOT CANDIDATE VIA FASTBOOT ===", flush=True)
    boot_res = subprocess.run(["fastboot", "boot", str(boot_img)], capture_output=True, text=True)
    print(boot_res.stdout, flush=True)
    print(boot_res.stderr, flush=True)
    if boot_res.returncode != 0:
        print("ERROR: fastboot boot failed!", file=sys.stderr)
        sys.exit(1)

    print("=== WAITING FOR USB CONSOLE ENUMERATION ===", flush=True)
    dev = xzs_console.open_stable_device(timeout_sec=120)
    if dev is None:
        print("ERROR: could not open stable XNU USB device", flush=True)
        sys.exit(1)

    print("=== TRANSPORT HANDSHAKE (Z1-Z4) ===", flush=True)
    if not xzs_console.transport_handshake(dev):
        print("ERROR: transport handshake failed", flush=True)
        sys.exit(1)

    time.sleep(0.5)
    initial_drain = collect(dev, seconds=1.5)
    if initial_drain:
        print(initial_drain.decode("utf-8", errors="replace"), end="", flush=True)

    full_log = []

    def run_step(name, cmd, wait_s=1.5):
        print(f"\n--- {name} ---", flush=True)
        out = send_cmd(dev, cmd, wait_sec=wait_s)
        full_log.append(f"\n# {name}\n> {cmd}\n{out}")
        return out

    # Step 1: Shell health
    run_step("STEP 1: SHELL HEALTH PWD", "pwd\n", 1.0)

    # Step 2: XZSFS health
    run_step("STEP 2: XZSFS HEALTH LS /bin", "ls /bin\n", 1.5)

    # Step 3: UBC diagnostic for /bin/hello
    run_step("STEP 3: UBC DIAGNOSTIC /bin/hello", "xzsfs ubc /bin/hello\n", 2.0)

    # Step 4: Shell health again
    run_step("STEP 4: SHELL HEALTH PWD AGAIN", "pwd\n", 1.0)

    # Step 5: Genericity check with /bin/args
    run_step("STEP 5: GENERICITY CHECK /bin/args", "xzsfs ubc /bin/args\n", 2.0)

    # Final health check
    run_step("FINAL HEALTH PWD", "pwd\n", 1.0)

    print("\n=== WRITING LOG FILE ===", flush=True)
    log_text = "".join(full_log)
    log_file.write_text(log_text)
    print(f"Logged to {log_file}")
    print("=== D7-T2N-1 HARDWARE SESSION COMPLETE ===", flush=True)


if __name__ == "__main__":
    main()
