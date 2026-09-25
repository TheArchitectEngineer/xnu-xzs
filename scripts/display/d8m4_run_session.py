#!/usr/bin/env python3
"""Scripted D8-M4 execution session over USB bulk console.
Executes display power bringup, M3 PLL & PHY bringup, register snapshot,
M4 dry-run, and DSI0 host controller programming on Sony Xperia XZs (MSM8996 v3.0).
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
            if buf and (b"RESULT=" in buf or b"xzs#" in buf or b"[D8-M4] RESULT=" in buf) and quiet >= 4:
                break
            time.sleep(0.05)
    return buf


def send_cmd(dev, cmd_str, wait_sec=2.0):
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
    parser = argparse.ArgumentParser(description="D8-M4 Scripted Execution Session")
    parser.add_argument("--mode", choices=["dryrun", "basic", "full"], default="dryrun",
                        help="Execution mode: dryrun, basic (Run A: basic/lane), or full (Run B: full host bringup)")
    parser.add_argument("--log-dir", default="artifacts/hw/d8m4/dryrun",
                        help="Directory to store hardware test logs and snapshots")
    args = parser.parse_args()

    log_dir = Path(args.log_dir)
    log_dir.mkdir(parents=True, exist_ok=True)
    log_file = log_dir / "host.txt"

    print(f"=== D8-M4 SCRIPTED SESSION START (MODE={args.mode}) ===", flush=True)
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
    m3_out = run_step("RUN M3 FULL", "display m3-run full\n", 20.0)
    if "RESULT=PASS_FULL_M3" not in m3_out:
        print("!!! M3 LOWER LAYER FAILED. Halting before M4.", flush=True)
        log_file.write_text("".join(full_log))
        sys.exit(1)
    print(">>> M3 LOWER LAYER VERIFIED (PLL LOCKED + CLOCKS + PHY READY)! <<<")

    # 4. Pre-M4 Host Status
    print("\n=== STEP 1: PRE-M4 HOST STATUS ===", flush=True)
    run_step("PRE-M4 DSI HOST STATUS", "display m4-status\n", 6.0)

    # 5. M4 Dry-Run Verification
    print("\n=== STEP 2: DSI HOST DRY-RUN (TRACE_ONLY) ===", flush=True)
    dry_out = run_step("DRY-RUN DSI HOST", "display m4-dryrun\n", 10.0)
    if "RESULT=PASS_DRYRUN" not in dry_out:
        print("!!! D8-M4 DRY-RUN FAILED. Halting.", flush=True)
        log_file.write_text("".join(full_log))
        sys.exit(1)
    print(">>> D8-M4 DRY-RUN PASSED 100%! <<<")

    if args.mode == "dryrun":
        print(">>> Dry-run complete. Exiting per --mode dryrun. <<<")
        log_file.write_text("".join(full_log))
        return

    # 6. Real M4 Execution
    if args.mode == "basic":
        print("\n=== STEP 3: DSI HOST BASIC & LANE CONFIG (MODE 1) ===", flush=True)
        basic_out = run_step("RUN M4 BASIC", "display m4-run basic\n", 8.0)
        if "RESULT=PASS_MODE1" not in basic_out and "RESULT=PASS_STAGE_B_BASIC" not in basic_out:
            print("!!! M4 BASIC CONFIG FAILED. Halting.", flush=True)
            log_file.write_text("".join(full_log))
            sys.exit(1)
        print(">>> D8-M4 BASIC CONFIG PASSED! <<<")
        run_step("DUMP HOST STATUS POST-BASIC", "display m4-status\n", 2.0)

    elif args.mode == "full":
        print("\n=== STEP 3: FULL DSI HOST BRING-UP (MODE 2) ===", flush=True)
        full_out = run_step("RUN M4 FULL", "display m4-run full\n", 10.0)
        if "RESULT=PASS_FULL_M4" not in full_out:
            print("!!! M4 FULL HOST BRING-UP FAILED. Halting.", flush=True)
            log_file.write_text("".join(full_log))
            sys.exit(1)
        print(">>> D8-M4 FULL HOST BRING-UP PASSED! <<<")
        run_step("DUMP HOST STATUS POST-M4", "display m4-status\n", 2.0)

    # 7. Post-Programming Register Snapshot
    print("\n=== STEP 4: POST-PROGRAMMING REGISTER SNAPSHOT ===", flush=True)
    post_regs_out = run_step("SNAPSHOT POST-M4 REGS", "display regs\n", 2.0)
    post_regs = parse_regs_dump(post_regs_out)
    m4_status_out = run_step("SNAPSHOT M4 STATUS", "display m4-status\n", 2.0)
    post_regs.update(parse_regs_dump(m4_status_out))
    with open(log_dir / "xnu_post_m4_registers.txt", "w") as f:
        for addr in sorted(post_regs.keys()):
            f.write(f"0x{addr:08x}: 0x{post_regs[addr]:08x}\n")
    post_json_path = log_dir / "xnu_post_m4_registers.json"
    with open(post_json_path, "w") as f:
        json.dump({f"0x{addr:08x}": f"0x{val:08x}" for addr, val in sorted(post_regs.items())}, f, indent=2)
    print(f"Captured {len(post_regs)} post-M4 registers.")

    # 8. Post-M4 Status & System Health
    run_step("FINAL DISPLAY STATUS", "display status\n", 1.5)
    run_step("FINAL PWD CHECK", "pwd\n", 1.0)

    log_file.write_text("".join(full_log))
    print(f"\nFull session transcript written to {log_file}", flush=True)

    # 9. Automated Register Diff against Linux Golden
    golden_path = Path("artifacts/display-audit-a1/golden_snapshot.json")
    if golden_path.exists():
        print("\n=== COMPARING XNU REGISTERS AGAINST LINUX GOLDEN ===", flush=True)
        diff_cmd = [
            sys.executable,
            "scripts/display/compare_display_state.py",
            str(golden_path),
            str(post_json_path),
            "--phase",
            "mode1" if args.mode == "basic" else ("mode2" if args.mode == "full" else "auto")
        ]
        diff_res = subprocess.run(diff_cmd, capture_output=True, text=True)
        print(diff_res.stdout)
        with open(log_dir / "diff_report.txt", "w") as f:
            f.write(diff_res.stdout)


if __name__ == "__main__":
    main()
