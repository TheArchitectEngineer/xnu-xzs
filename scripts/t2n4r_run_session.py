#!/usr/bin/env python3
"""
Hardware execution session for D7-T2N-4R: POST-EXIT RESET / TEARDOWN FORENSIC.
Supports --mode run_a (control) and --mode run_b (pagecheck).
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
    while time.time() < end:
        chunk, kind = xzs_console.bulk_read(dev, timeout_ms=200)
        if chunk:
            buf += chunk
        else:
            time.sleep(0.05)
    return buf


def send_cmd(dev, cmd_str, wait_sec=5.0, wait_for_prompt=True):
    print(f"\n>>> SEND: {cmd_str.strip()}", flush=True)
    payload = cmd_str.encode("utf-8") if isinstance(cmd_str, str) else cmd_str
    if not payload.endswith(b"\n"):
        payload += b"\n"
    written, kind = xzs_console.bulk_write(dev, payload, timeout_ms=2000)
    if kind is not None or written != len(payload):
        print(f"!!! WRITE ERROR: written={written}, kind={kind}", flush=True)
        return ""

    end = time.time() + wait_sec
    buf = b""
    quiet = 0
    while time.time() < end:
        chunk, kind = xzs_console.bulk_read(dev, timeout_ms=200)
        if chunk:
            buf += chunk
            sys.stdout.write(chunk.decode("utf-8", errors="replace"))
            sys.stdout.flush()
            quiet = 0
            if wait_for_prompt and b"xzs# " in buf[len(payload):]:
                # Found shell prompt after command echo, command is complete
                break
        else:
            quiet += 1
            if not wait_for_prompt and buf and quiet >= 10:
                break
            time.sleep(0.05)
    resp_text = buf.decode("utf-8", errors="replace")
    return resp_text


def main():
    mode = "run_1"
    for arg in sys.argv:
        if arg.startswith("--mode="):
            mode = arg.split("=")[1]
        elif arg.startswith("--run_"):
            mode = arg[2:]

    rev_res = subprocess.run(["git", "rev-parse", "--short=7", "HEAD"], capture_output=True, text=True)
    short_commit = rev_res.stdout.strip() if rev_res.returncode == 0 else "unknown"

    log_dir = Path(f"artifacts/hw/t2n4f-{short_commit}-{mode}")
    log_dir.mkdir(parents=True, exist_ok=True)
    log_file = log_dir / "host.txt"

    boot_img = Path("artifacts/builds/xzs-xnu-boot.img")
    if not boot_img.exists():
        print(f"ERROR: {boot_img} does not exist!", file=sys.stderr)
        sys.exit(1)

    skip_boot = "--no-boot" in sys.argv
    if not skip_boot:
        print(f"=== STEP 0: BOOT CANDIDATE VIA FASTBOOT (MODE={mode.upper()}) ===", flush=True)
        print("Waiting for fastboot device BH905SX976 (up to 120s)...", flush=True)
        fb_found = False
        for _ in range(120):
            fb_check = subprocess.run(["fastboot", "devices"], capture_output=True, text=True)
            if "BH905SX976" in fb_check.stdout:
                fb_found = True
                break
            time.sleep(1)
        if not fb_found:
            print("ERROR: fastboot device BH905SX976 not found!", file=sys.stderr)
            sys.exit(1)
        boot_res = subprocess.run(["fastboot", "-s", "BH905SX976", "boot", str(boot_img)], capture_output=True, text=True)
        print(boot_res.stdout, flush=True)
        print(boot_res.stderr, flush=True)
        if boot_res.returncode != 0:
            print("ERROR: fastboot boot failed!", file=sys.stderr)
            sys.exit(1)
    else:
        print(f"=== STEP 0: SKIPPING FASTBOOT BOOT (MODE={mode.upper()}) ===", flush=True)

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
    if initial_drain:
        full_log.append(initial_drain.decode("utf-8", errors="replace"))

    def run_step(name, cmd, wait_s=2.0):
        print(f"\n--- {name} ---", flush=True)
        out = send_cmd(dev, cmd, wait_sec=wait_s)
        full_log.append(f"\n# {name}\n> {cmd}\n{out}")
        return out

    # Step 1: Shell health baseline
    run_step("STEP 1: SHELL HEALTH PWD", "pwd\n", 1.0)

    # Optional intermediate steps
    if mode in ("run_a",):
        run_step("STEP 2: BASELINE UBC /bin/hello (CONTROL - NO PAGECHECK)", "xzsfs ubc /bin/hello\n", 2.0)
    elif mode in ("run_3", "run_b"):
        run_step("STEP 2: BASELINE UBC + PAGECHECK /bin/hello", "xzsfs ubc /bin/hello:pagecheck\n", 4.0)

    # Execute /bin/hello
    print("\n--- STEP: EXECUTE /bin/hello ---", flush=True)
    hello_out = send_cmd(dev, "/bin/hello\n", wait_sec=8.0, wait_for_prompt=True)
    full_log.append(f"\n# STEP: EXECUTE /bin/hello\n> /bin/hello\n{hello_out}")

    # Post-exec health check
    print("\n--- STEP: POST-EXEC HEALTH CHECK ---", flush=True)
    try:
        post_out = send_cmd(dev, "pwd\n", wait_sec=3.0, wait_for_prompt=True)
        full_log.append(f"\n# STEP: POST-EXEC PWD\n> pwd\n{post_out}")
    except Exception as e:
        print(f"Post-exec communication check ended: {e}", flush=True)
        full_log.append(f"\n# STEP: POST-EXEC PWD\nException: {e}\n")

    print("\n=== WRITING LOG FILE ===", flush=True)
    log_text = "".join(full_log)
    log_file.write_text(log_text)
    print(f"Logged to {log_file}")
    print(f"=== D7-T2N-4R ({mode.upper()}) HARDWARE SESSION COMPLETE ===", flush=True)


if __name__ == "__main__":
    main()
