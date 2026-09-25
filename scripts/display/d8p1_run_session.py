#!/usr/bin/env python3
"""Scripted D8-P1 execution session over USB bulk console.
Executes M2 clocks, M3 PLL & PHY bringup, M4 DSI host enable,
pre-P1 GPIO status, P1 dry-run, and P1 TLMM GPIO hardware programming.
"""

import argparse
import importlib.util
import json
import os
import subprocess
import sys
import time
from pathlib import Path

_TOOL = Path(__file__).resolve().parents[2] / "tools" / "xzs-console" / "xzs-console.py"
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
            if buf and (b"RESULT=" in buf or b"xzs#" in buf or b"[D8-P1] RESULT=" in buf) and quiet >= 4:
                break
            time.sleep(0.05)
    return buf


def send_cmd(dev, cmd_str, wait_sec=2.0):
    print(f"\n>>> SEND: {cmd_str.strip()}", flush=True)
    while True:
        pre_drain, _ = xzs_console.bulk_read(dev, timeout_ms=50)
        if not pre_drain:
            break
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


def parse_regs_dump(output_text):
    regs = {}
    for line in output_text.splitlines():
        line = line.strip()
        if "=" in line and ("0x" in line or "0X" in line):
            parts = line.split("=")
            if len(parts) == 2:
                addr_str = parts[0].replace("REG:", "").strip()
                if "(" in addr_str:
                    addr_str = addr_str.split("(")[0].strip()
                val_str = parts[1].strip()
                if " " in val_str:
                    val_str = val_str.split()[0].strip()
                try:
                    addr = int(addr_str, 16)
                    val = int(val_str, 16)
                    regs[addr] = val
                except ValueError:
                    pass
    return regs


def main():
    parser = argparse.ArgumentParser(description="D8-P1 Scripted Execution Session")
    parser.add_argument("--mode", choices=["dryrun", "run"], default="run",
                        help="Execution mode: dryrun or run (hardware write)")
    parser.add_argument("--log-dir", default="artifacts/hw/d8p1/run1",
                        help="Directory to store hardware test logs and snapshots")
    args = parser.parse_args()

    log_dir = Path(args.log_dir)
    log_dir.mkdir(parents=True, exist_ok=True)
    log_file = log_dir / "host.txt"

    print(f"=== D8-P1 SCRIPTED SESSION START (MODE={args.mode}) ===", flush=True)
    dev = xzs_console.open_stable_device(timeout_sec=120)
    if dev is None:
        print("ERROR: could not open stable XNU USB device", flush=True)
        sys.exit(1)

    print("=== STEP 0: TRANSPORT HANDSHAKE (Z1-Z4) ===", flush=True)
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

    # 1. Baseline State
    run_step("BASELINE PWD", "pwd\n", 1.0)
    run_step("BASELINE DISPLAY STATUS", "display status\n", 1.5)

    # 2. Power and Clock Bring-up (D8-M2 prerequisite)
    print("\n=== POWERING DISPLAY GDSC & CORE CLOCKS (M2) ===", flush=True)
    run_step("ENABLE MMSS_MMAGIC_AHB", "clocks mmagic-ahb-on\n", 1.5)
    run_step("ENABLE MMSS_MMAGIC_CFG_AHB", "clocks mmagic-cfg-ahb-on\n", 1.5)
    run_step("ENABLE MMAGIC_MDSS_NOC", "clocks mmagic-mdss-noc-on\n", 1.5)
    run_step("ENABLE MMAGIC_MDSS_AXI", "clocks mmagic-mdss-axi-on\n", 1.5)
    run_step("POWER ON MDSS GDSC", "display power mdss-on\n", 2.0)
    run_step("ENABLE MDSS_AHB", "clocks mdss-ahb-on\n", 2.0)
    run_step("ENABLE MDSS_AXI", "clocks mdss-axi-on\n", 2.0)
    run_step("ENABLE MDSS_MDP", "clocks mdp-on\n", 2.0)
    run_step("VERIFY CORE CLOCKS", "clocks mdss-critical-status\n", 1.5)

    # 3. Establish M3 Lower Layer (PLL Locked + Clocks + 14nm PHY)
    print("\n=== RUNNING M3 LOWER-LAYER HARDWARE BRING-UP ===", flush=True)
    m3_out = run_step("RUN M3 FULL", "display m3-run full\n", 50.0)
    if "RESULT=PASS_FULL_M3" not in m3_out:
        print("!!! M3 LOWER LAYER FAILED. Halting before P1.", flush=True)
        log_file.write_text("".join(full_log))
        sys.exit(1)
    print(">>> M3 LOWER LAYER VERIFIED (PLL LOCKED + CLOCKS + PHY READY)! <<<")

    # 4. Establish M4 DSI Host Layer
    print("\n=== RUNNING M4 DSI0 HOST BRING-UP ===", flush=True)
    m4_out = run_step("RUN M4 FULL", "display m4-run full\n", 10.0)
    if "RESULT=PASS_MODE2" not in m4_out:
        print("!!! M4 DSI HOST FAILED. Halting before P1.", flush=True)
        log_file.write_text("".join(full_log))
        sys.exit(1)
    print(">>> M4 DSI HOST VERIFIED (COMMAND MODE, 4 LANES, STOPSTATE)! <<<")

    # 5. Pre-P1 GPIO Status
    print("\n=== STEP 1: PRE-P1 GPIO STATUS ===", flush=True)
    run_step("PRE-P1 GPIO STATUS", "display gpio status\n", 3.0)

    # 6. P1 Dry-Run Verification
    print("\n=== STEP 2: D8-P1 DRY-RUN ===", flush=True)
    dry_out = run_step("DRY-RUN D8-P1", "display p1-dryrun\n", 5.0)
    if "RESULT=PASS_DRYRUN" not in dry_out:
        print("!!! D8-P1 DRY-RUN FAILED. Halting.", flush=True)
        log_file.write_text("".join(full_log))
        sys.exit(1)
    print(">>> D8-P1 DRY-RUN PASSED 100%! <<<")

    if args.mode == "dryrun":
        print(">>> Dry-run complete. Exiting per --mode dryrun. <<<")
        log_file.write_text("".join(full_log))
        return

    # 7. Real P1 Execution
    print("\n=== STEP 3: REAL D8-P1 HARDWARE CONFIGURATION ===", flush=True)
    p1_out = run_step("RUN P1", "display p1-run\n", 5.0)
    if "RESULT=PASS_P1" not in p1_out:
        print("!!! D8-P1 HARDWARE EXECUTION FAILED. Halting.", flush=True)
        log_file.write_text("".join(full_log))
        sys.exit(1)
    print(">>> D8-P1 HARDWARE CONFIGURATION PASSED! <<<")

    # 8. Post-P1 Status Check
    print("\n=== STEP 4: POST-P1 STATUS VERIFICATION ===", flush=True)
    run_step("POST-P1 GPIO STATUS", "display gpio status\n", 3.0)
    run_step("POST-P1 DSI HOST STATUS", "display m4-status\n", 3.0)

    # 9. Register Snapshot
    regs_out = run_step("SNAPSHOT DISPLAY REGS", "display regs\n", 3.0)
    post_regs = parse_regs_dump(regs_out)
    m4_stat = run_step("SNAPSHOT M4 STATUS", "display m4-status\n", 3.0)
    post_regs.update(parse_regs_dump(m4_stat))
    with open(log_dir / "xnu_post_p1_registers.json", "w") as f:
        json.dump({f"0x{addr:08x}": f"0x{val:08x}" for addr, val in sorted(post_regs.items())}, f, indent=2)

    # 10. System Health Check
    run_step("FINAL DISPLAY STATUS", "display status\n", 1.5)
    run_step("FINAL PWD CHECK", "pwd\n", 1.0)

    log_file.write_text("".join(full_log))
    print(f"\nFull session transcript written to {log_file}", flush=True)


if __name__ == "__main__":
    main()
