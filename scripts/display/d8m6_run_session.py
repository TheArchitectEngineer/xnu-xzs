#!/usr/bin/env python3
"""Scripted D8-M6 execution session over USB bulk console.
Executes M2 clocks, M3 PLL & PHY bringup, M4 DSI host enable,
P1 GPIO configuration, M6 dry-run,
M6 Stage 1 (single command: SLPOUT 0x11),
M6 Stage 2 (prefix: SLPOUT + TEON 0x35 0x00),
M6 Full bring-up (power-on -> ON cmds -> powered-active check -> OFF cmds -> safe power-down).
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


def parse_key_value_status(output_text):
    data = {}
    for line in output_text.splitlines():
        line = line.strip()
        if "=" in line:
            parts = line.split("=", 1)
            k = parts[0].strip()
            v = parts[1].strip()
            if " " in v and not v.startswith("0x"):
                v = v.split()[0].strip()
            data[k] = v
    return data


def main():
    parser = argparse.ArgumentParser(description="D8-M6 Scripted Execution Session")
    parser.add_argument("--mode", choices=["dryrun", "stage1", "stage2", "run"], default="run",
                        help="Execution mode: dryrun, stage1 (single cmd), stage2 (prefix), or run (full M6 DCS init)")
    parser.add_argument("--log-dir", default="artifacts/hw/d8m6/run1",
                        help="Directory to store hardware test logs and snapshots")
    args = parser.parse_args()

    log_dir = Path(args.log_dir)
    log_dir.mkdir(parents=True, exist_ok=True)
    log_file = log_dir / "host.txt"

    print(f"=== D8-M6 SCRIPTED SESSION START (MODE={args.mode}) ===", flush=True)
    dev = xzs_console.open_stable_device(timeout_sec=120)
    if dev is None:
        print("ERROR: could not open stable XNU USB device", flush=True)
        sys.exit(1)

    print("=== STEP 0: TRANSPORT CHECK / HANDSHAKE ===", flush=True)
    # Drain initial boot strings and wait firmly for shell prompt xzs#
    drain_start = time.time()
    confirmed_prompt = False
    while time.time() - drain_start < 8.0:
        xzs_console.bulk_write(dev, b"\n", timeout_ms=500)
        d, _ = xzs_console.bulk_read(dev, timeout_ms=300)
        if b"xzs#" in d:
            confirmed_prompt = True
            break
        time.sleep(0.2)
    if confirmed_prompt:
        print(">>> Shell confirmed active at prompt xzs#. Continuing. <<<", flush=True)
    else:
        print(">>> Shell prompt probe timeout, proceeding with transport check. <<<", flush=True)

    time.sleep(0.3)
    full_log = []

    def run_step(name, cmd, wait_s=2.0, required_substr=None):
        print(f"\n--- {name} ---", flush=True)
        out = send_cmd(dev, cmd, wait_sec=wait_s, required_substr=required_substr)
        full_log.append(f"\n# {name}\n> {cmd}\n{out}")
        if required_substr and required_substr not in out and "ALREADY" not in out:
            print(f"Retrying {name}...", flush=True)
            time.sleep(0.5)
            out = send_cmd(dev, cmd, wait_sec=wait_s, required_substr=required_substr)
            full_log.append(f"\n# {name} (RETRY)\n> {cmd}\n{out}")
        return out

    # 1. Baseline State
    run_step("BASELINE PWD", "pwd\n", 1.5)
    run_step("BASELINE DISPLAY STATUS", "display status\n", 2.0)

    # 2. Power and Clock Bring-up (D8-M2 prerequisite)
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

    # 3. Establish M3 Lower Layer (PLL Locked + Clocks + 14nm PHY)
    print("\n=== RUNNING M3 LOWER-LAYER HARDWARE BRING-UP ===", flush=True)
    m3_out = run_step("RUN M3 FULL", "display m3-run full\n", 50.0, required_substr="PASS_FULL_M3")
    if "RESULT=PASS_FULL_M3" not in m3_out and "RESULT=M3_ALREADY_ATTEMPTED" not in m3_out:
        print("!!! M3 LOWER LAYER FAILED. Halting before M6.", flush=True)
        log_file.write_text("".join(full_log))
        sys.exit(1)
    print(">>> M3 LOWER LAYER VERIFIED (PLL LOCKED + CLOCKS + PHY READY)! <<<")

    # 4. Establish M4 DSI Host Layer
    print("\n=== RUNNING M4 DSI0 HOST BRING-UP ===", flush=True)
    m4_out = run_step("RUN M4 FULL", "display m4-run full\n", 10.0, required_substr="PASS_MODE2")
    if "RESULT=PASS_MODE2" not in m4_out and "already" not in m4_out.lower():
        print("!!! M4 DSI HOST FAILED. Halting before M6.", flush=True)
        log_file.write_text("".join(full_log))
        sys.exit(1)
    print(">>> M4 DSI HOST VERIFIED (COMMAND MODE, 4 LANES, STOPSTATE)! <<<")

    # 5. Establish P1 GPIO Layer
    print("\n=== RUNNING P1 TLMM GPIO BRING-UP ===", flush=True)
    p1_out = run_step("RUN P1", "display p1-run\n", 5.0, required_substr="RESULT=PASS_P1")
    if "RESULT=PASS_P1" not in p1_out:
        print("!!! D8-P1 HARDWARE EXECUTION FAILED. Halting before M6.", flush=True)
        log_file.write_text("".join(full_log))
        sys.exit(1)
    print(">>> D8-P1 TLMM GPIOS CONFIGURED & VERIFIED SAFE! <<<")

    # 6. Snapshot registers PRE
    print("\n=== SNAPSHOT DISPLAY REGISTERS PRE ===", flush=True)
    regs_pre_out = run_step("SNAPSHOT DISPLAY REGS PRE", "display regs\n", 10.0)
    pre_regs = parse_regs_dump(regs_pre_out)
    with open(log_dir / "pre_registers.json", "w") as f:
        json.dump({f"0x{addr:08x}": f"0x{val:08x}" for addr, val in sorted(pre_regs.items())}, f, indent=2)

    # 7. M6 Dry-Run Verification (M6-A & M6-B)
    print("\n=== STEP 1: D8-M6 DRY-RUN ===", flush=True)
    dry_out = run_step("DRY-RUN D8-M6", "display m6-dryrun\n", 15.0, required_substr="RESULT=PASS_DRYRUN")
    if "RESULT=PASS_DRYRUN" not in dry_out:
        print("!!! D8-M6 DRY-RUN FAILED. Halting.", flush=True)
        log_file.write_text("".join(full_log))
        sys.exit(1)
    print(">>> D8-M6 DRY-RUN PASSED 100%! <<<")

    if args.mode == "dryrun":
        print(">>> Dry-run complete. Exiting per --mode dryrun. <<<")
        log_file.write_text("".join(full_log))
        return

    if args.mode == "stage1":
        print("\n=== STEP 2: M6 STAGE 1 (SINGLE CMD: SLPOUT 0x11) ===", flush=True)
        stage1_out = run_step("RUN M6 STAGE 1", "display m6-stage1\n", 25.0, required_substr="RESULT=PASS_STAGE1")
        if "RESULT=PASS_STAGE1" not in stage1_out:
            print("!!! D8-M6 STAGE 1 FAILED. Halting.", flush=True)
            log_file.write_text("".join(full_log))
            sys.exit(1)
        print(">>> D8-M6 STAGE 1 (SINGLE CMD SLPOUT 0x11) PASSED! <<<")
        log_file.write_text("".join(full_log))
        return

    if args.mode == "stage2":
        print("\n=== STEP 2: M6 STAGE 2 (PREFIX: SLPOUT + TEON) ===", flush=True)
        stage2_out = run_step("RUN M6 STAGE 2", "display m6-stage2\n", 25.0, required_substr="RESULT=PASS_STAGE2")
        if "RESULT=PASS_STAGE2" not in stage2_out:
            print("!!! D8-M6 STAGE 2 FAILED. Halting.", flush=True)
            log_file.write_text("".join(full_log))
            sys.exit(1)
        print(">>> D8-M6 STAGE 2 (PREFIX SLPOUT + TEON) PASSED! <<<")
        log_file.write_text("".join(full_log))
        return

    # 8. M6 Full DCS Bringup & Graceful Shutdown
    print("\n=== STEP 2: M6 FULL DCS INITIALIZATION RUN ===", flush=True)
    m6_out = run_step("RUN M6 FULL", "display m6-run\n", 40.0, required_substr="RESULT=PASS_ACCEPTANCE")
    if "RESULT=PASS_ACCEPTANCE" not in m6_out:
        print("!!! D8-M6 FULL RUN FAILED. Halting.", flush=True)
        log_file.write_text("".join(full_log))
        sys.exit(1)
    print(">>> D8-M6 FULL DCS INITIALIZATION RUN PASSED 100%! <<<")

    # 11. Snapshot registers POST
    print("\n=== SNAPSHOT DISPLAY REGISTERS POST ===", flush=True)
    regs_post_out = run_step("SNAPSHOT DISPLAY REGS POST", "display regs\n", 10.0)
    post_regs = parse_regs_dump(regs_post_out)
    with open(log_dir / "post_registers.json", "w") as f:
        json.dump({f"0x{addr:08x}": f"0x{val:08x}" for addr, val in sorted(post_regs.items())}, f, indent=2)

    # 12. Save full host log and summary
    log_file.write_text("".join(full_log))
    print(f"\n=== D8-M6 SCRIPTED SESSION COMPLETED SUCCESSFULLY ===", flush=True)
    print(f"Log files saved to: {log_dir.resolve()}", flush=True)


if __name__ == "__main__":
    main()
