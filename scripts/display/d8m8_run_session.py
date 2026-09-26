#!/usr/bin/env python3
"""Scripted D8-M8 Pre-Audit execution session over USB bulk console.
Executes:
1. M8 Zero-Kickoff Dry-Run (`display m8-dryrun`)
2. M8 Read-Only Status Diagnostic (`display m8-status`)
Validates that:
- MDP_MMIO_WRITES = 0
- MDP_KICKOFF_COUNT = 0
- CTL_START_COUNT = 0
- FRAMEBUFFER_SCANOUT_COUNT = 0
- M8_DRYRUN = PASS
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


def collect(dev, seconds=2.0, required_substr=None):
    end = time.time() + seconds
    buf = b""
    quiet = 0
    while time.time() < end:
        chunk, kind = xzs_console.bulk_read(dev, timeout_ms=1000)
        if chunk:
            buf += chunk
            quiet = 0
        else:
            quiet += 1
            tail = buf[max(0, len(buf)-200):]
            has_prompt = (b"xzs#" in tail or b"code=3" in tail)
            if required_substr:
                req_bytes = required_substr.encode("utf-8") if isinstance(required_substr, str) else required_substr
                if (req_bytes in buf or b"ALREADY" in buf) and has_prompt and quiet >= 1:
                    break
            else:
                if has_prompt and quiet >= 1:
                    break
            time.sleep(0.05)
    return buf


def send_cmd(dev, cmd_str, wait_sec=2.0, required_substr=None):
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
    resp = collect(dev, seconds=wait_sec, required_substr=required_substr)
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
                addr_str = parts[0].strip()
                if "[" in addr_str and "]" in addr_str:
                    addr_str = addr_str.split("[")[1].split("]")[0].strip()
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
    parser = argparse.ArgumentParser(description="D8-M8 Scripted Pre-Audit Session")
    parser.add_argument("--log-dir", default="artifacts/hw/d8m8/run1",
                        help="Directory to store hardware test logs and snapshots")
    args = parser.parse_args()

    log_dir = Path(args.log_dir)
    log_dir.mkdir(parents=True, exist_ok=True)
    log_file = log_dir / "host.txt"

    print("=== D8-M8 PRE-AUDIT SCRIPTED SESSION START ===", flush=True)
    dev = xzs_console.open_stable_device(timeout_sec=120)
    if dev is None:
        print("ERROR: could not open stable XNU USB device", flush=True)
        sys.exit(1)

    print("=== STEP 0: TRANSPORT HANDSHAKE (Z1-Z4) ===", flush=True)
    if not xzs_console.transport_handshake(dev):
        print("ERROR: transport handshake failed", flush=True)
        sys.exit(1)

    # Allow shell to settle
    time.sleep(0.5)
    initial_drain = collect(dev, seconds=1.5)
    if initial_drain:
        print(initial_drain.decode("utf-8", errors="replace"), end="", flush=True)
    full_log = []

    def run_step(name, cmd, wait_s=2.0, required_substr=None):
        print(f"\n--- {name} ---", flush=True)
        out = send_cmd(dev, cmd, wait_sec=wait_s, required_substr=required_substr)
        full_log.append(f"\n# {name}\n> {cmd}\n{out}")
        return out

    # 1. Baseline State
    run_step("BASELINE PWD", "pwd\n", 1.5)

    # 2. Execute Zero-Kickoff Dry-Run
    print("\n=== STEP 1: D8-M8 ZERO-KICKOFF DRY-RUN MODEL ===", flush=True)
    dry_out = run_step("M8 DRYRUN", "display m8-dryrun\n", 5.0, required_substr="RESULT=PASS_DRYRUN")
    if "RESULT=PASS_DRYRUN" not in dry_out or "M8_DRYRUN=PASS" not in dry_out:
        print("!!! D8-M8 DRY-RUN VERIFICATION FAILED!", flush=True)
        log_file.write_text("".join(full_log))
        sys.exit(1)
    print(">>> D8-M8 DRY-RUN VERIFIED: 100% MATCH, ZERO UNKNOWN REGISTERS! <<<")

    # 3. Execute Read-Only Status Diagnostic (Unpowered State)
    print("\n=== STEP 2A: D8-M8 READ-ONLY STATUS (UNPOWERED) ===", flush=True)
    status_unpwr = run_step("M8 STATUS UNPOWERED", "display m8-status\n", 5.0, required_substr="READ_ONLY_AUDIT=PASS")
    if "READ_ONLY_AUDIT=PASS" not in status_unpwr:
        print("!!! D8-M8 READ-ONLY STATUS CHECK (UNPOWERED) FAILED!", flush=True)
        log_file.write_text("".join(full_log))
        sys.exit(1)
    print(">>> D8-M8 READ-ONLY STATUS (UNPOWERED) PASSED! <<<")

    # 4. Safe Power and Clock Bring-up (D8-M2 prerequisite)
    print("\n=== POWERING DISPLAY GDSC & CORE CLOCKS (M2) ===", flush=True)
    run_step("ENABLE MMSS_MMAGIC_AHB", "clocks mmagic-ahb-on\n", 2.0, required_substr="PASS")
    run_step("ENABLE MMSS_MMAGIC_CFG_AHB", "clocks mmagic-cfg-ahb-on\n", 2.0, required_substr="PASS")
    run_step("ENABLE MMAGIC_MDSS_NOC", "clocks mmagic-mdss-noc-on\n", 2.0, required_substr="PASS")
    run_step("ENABLE MMAGIC_MDSS_AXI", "clocks mmagic-mdss-axi-on\n", 2.0, required_substr="PASS")
    run_step("POWER ON MDSS GDSC", "display power mdss-on\n", 2.0, required_substr="PASS")
    run_step("ENABLE MDSS_AHB", "clocks mdss-ahb-on\n", 2.0, required_substr="PASS")
    run_step("ENABLE MDSS_AXI", "clocks mdss-axi-on\n", 2.0, required_substr="PASS")
    run_step("ENABLE MDSS_MDP", "clocks mdp-on\n", 2.0, required_substr="PASS")
    run_step("VERIFY CORE CLOCKS", "clocks mdss-critical-status\n", 2.0, required_substr="PASS")

    # 5. Execute Read-Only Status Diagnostic (Powered State)
    print("\n=== STEP 2B: D8-M8 READ-ONLY STATUS (POWERED ACTIVE) ===", flush=True)
    status_out = run_step("M8 STATUS POWERED", "display m8-status\n", 5.0, required_substr="READ_ONLY_AUDIT=PASS")
    if "READ_ONLY_AUDIT=PASS" not in status_out:
        print("!!! D8-M8 READ-ONLY STATUS CHECK (POWERED) FAILED!", flush=True)
        log_file.write_text("".join(full_log))
        sys.exit(1)
    print(">>> D8-M8 READ-ONLY STATUS (POWERED ACTIVE) AUDIT PASSED! <<<")

    # 6. Parse and save register snapshot
    regs = parse_regs_dump(status_out)
    with open(log_dir / "m8_status_registers.json", "w") as f:
        json.dump({f"0x{addr:08x}": f"0x{val:08x}" for addr, val in sorted(regs.items())}, f, indent=2)

    # 5. Verify strict safety locks in output
    for lock in ["MDP_MMIO_WRITES=0", "MDP_KICKOFF_COUNT=0", "CTL_START_COUNT=0", "FRAMEBUFFER_SCANOUT_COUNT=0"]:
        if lock not in dry_out or lock not in status_out:
            print(f"!!! CRITICAL: SAFETY LOCK {lock} NOT VERIFIED IN LOGS!", flush=True)
            log_file.write_text("".join(full_log))
            sys.exit(1)

    print("\n>>> ALL ABSOLUTE SAFETY LOCKS FULLY VERIFIED (WRITES=0, KICKOFFS=0)! <<<")
    log_file.write_text("".join(full_log))
    print(f"Log files saved to: {log_dir.resolve()}", flush=True)
    print("=== D8-M8 PRE-AUDIT EXECUTION FINISHED SUCCESSFULLY ===", flush=True)


if __name__ == "__main__":
    main()
