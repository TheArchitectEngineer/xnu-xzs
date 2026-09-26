#!/usr/bin/env python3
"""D8-M8 Staged Hardware Runner: M8-1 through M8-6.
Executes prerequisites M2..M6 and staged MDP scanout implementation.
Halts strictly before M8-7 (CTL_START).
"""

import argparse
import importlib.util
import json
import os
import sys
import time
from pathlib import Path

_TOOL = Path(__file__).resolve().parents[2] / "tools" / "xzs-console" / "xzs-console.py"
_spec = importlib.util.spec_from_file_location("xzs_console", _TOOL)
xzs_console = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(xzs_console)


def collect(dev, seconds=5.0, required_substr=None):
    end = time.time() + seconds
    buf = b""
    while time.time() < end:
        chunk, kind = xzs_console.bulk_read(dev, timeout_ms=300)
        if chunk:
            buf += chunk
            tail = buf[max(0, len(buf)-200):]
            has_prompt = (b"xzs#" in tail or b"code=3" in tail)
            if required_substr:
                req_bytes = required_substr.encode("utf-8") if isinstance(required_substr, str) else required_substr
                if (req_bytes in buf or b"ALREADY" in buf) and has_prompt:
                    time.sleep(0.1)
                    extra, _ = xzs_console.bulk_read(dev, timeout_ms=100)
                    if extra:
                        buf += extra
                    break
            else:
                if has_prompt:
                    time.sleep(0.1)
                    extra, _ = xzs_console.bulk_read(dev, timeout_ms=100)
                    if extra:
                        buf += extra
                    break
        time.sleep(0.05)
    return buf


def send_cmd(dev, cmd_str, wait_sec=5.0, required_substr=None):
    print(f"\n>>> SEND: {cmd_str.strip()}", flush=True)
    for _ in range(5):
        pre_drain, _ = xzs_console.bulk_read(dev, timeout_ms=100)
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


def main():
    parser = argparse.ArgumentParser(description="D8-M8 Staged Hardware Runner")
    parser.add_argument("--log-dir", default="artifacts/hw/d8m8/prekick",
                        help="Directory to store hardware test logs and stage artifacts")
    args = parser.parse_args()

    log_dir = Path(args.log_dir)
    log_dir.mkdir(parents=True, exist_ok=True)
    host_file = log_dir / "host.txt"

    print("=== D8-M8 STAGED RUNNER START ===", flush=True)
    dev = xzs_console.open_stable_device(timeout_sec=30)
    if dev is None:
        print("ERROR: could not open stable XNU USB device", flush=True)
        sys.exit(1)

    print("=== STEP 0: SYNC SHELL PROMPT ===", flush=True)
    start_t = time.time()
    while time.time() - start_t < 10.0:
        xzs_console.bulk_write(dev, b"\n", timeout_ms=1000)
        time.sleep(0.5)
        initial_drain = collect(dev, seconds=1.0)
        if initial_drain:
            print(initial_drain.decode("utf-8", errors="replace"), end="", flush=True)
            if b"xzs#" in initial_drain:
                break

    full_log = []

    def run_step(name, cmd, wait_s=5.0, required_substr=None):
        print(f"\n--- {name} ---", flush=True)
        out = send_cmd(dev, cmd, wait_sec=wait_s, required_substr=required_substr)
        full_log.append(f"\n# {name}\n> {cmd}\n{out}")
        return out

    # Baseline PWD
    run_step("BASELINE PWD", "pwd\n", 1.5)

    # 1. Bring up Prerequisites: M2 Power and Clocks
    print("\n=== PREREQUISITE: M2 DISPLAY GDSC & CORE CLOCKS ===", flush=True)
    run_step("ENABLE MMSS_MMAGIC_AHB", "clocks mmagic-ahb-on\n", 2.0, required_substr="PASS")
    run_step("ENABLE MMSS_MMAGIC_CFG_AHB", "clocks mmagic-cfg-ahb-on\n", 2.0, required_substr="PASS")
    run_step("ENABLE MMAGIC_MDSS_NOC", "clocks mmagic-mdss-noc-on\n", 2.0, required_substr="PASS")
    run_step("ENABLE MMAGIC_MDSS_AXI", "clocks mmagic-mdss-axi-on\n", 2.0, required_substr="PASS")
    run_step("POWER ON MDSS GDSC", "display power mdss-on\n", 2.0, required_substr="PASS")
    run_step("ENABLE MDSS_AHB", "clocks mdss-ahb-on\n", 2.0, required_substr="PASS")
    run_step("ENABLE MDSS_AXI", "clocks mdss-axi-on\n", 2.0, required_substr="PASS")
    run_step("ENABLE MDSS_MDP", "clocks mdp-on\n", 2.0, required_substr="PASS")
    run_step("VERIFY CORE CLOCKS", "clocks mdss-critical-status\n", 2.0, required_substr="PASS")

    # 2. Prerequisite: M3 Lower Layer (PLL Locked + Clocks + 14nm PHY)
    print("\n=== PREREQUISITE: M3 LOWER-LAYER HARDWARE BRING-UP ===", flush=True)
    m3_out = run_step("RUN M3 FULL", "display m3-run full\n", 50.0, required_substr="PASS_FULL_M3")
    if "RESULT=PASS_FULL_M3" not in m3_out and "RESULT=M3_ALREADY_ATTEMPTED" not in m3_out:
        print("!!! M3 LOWER LAYER FAILED. Halting.", flush=True)
        host_file.write_text("".join(full_log))
        sys.exit(1)

    # 3. Prerequisite: M4 DSI Host Layer
    print("\n=== PREREQUISITE: M4 DSI0 HOST BRING-UP ===", flush=True)
    m4_out = run_step("RUN M4 FULL", "display m4-run full\n", 10.0, required_substr="PASS_MODE2")
    if "RESULT=PASS_MODE2" not in m4_out and "already" not in m4_out.lower():
        print("!!! M4 DSI HOST FAILED. Halting.", flush=True)
        host_file.write_text("".join(full_log))
        sys.exit(1)

    # 4. Prerequisite: P1 GPIO Layer
    print("\n=== PREREQUISITE: P1 TLMM GPIO BRING-UP ===", flush=True)
    p1_out = run_step("RUN P1", "display p1-run\n", 5.0, required_substr="RESULT=PASS_P1")
    if "RESULT=PASS_P1" not in p1_out:
        print("!!! D8-P1 HARDWARE EXECUTION FAILED. Halting.", flush=True)
        host_file.write_text("".join(full_log))
        sys.exit(1)

    # 5. Prerequisite: M6 Dry-Run
    print("\n=== PREREQUISITE: M6 DRYRUN ===", flush=True)
    run_step("DRY-RUN D8-M6", "display m6-dryrun\n", 10.0, required_substr="RESULT=PASS_DRYRUN")

    # 6. STAGED M8 EXECUTION: M8-1 through M8-6
    # =========================================================================

    # M8-1: Framebuffer Allocation + CPU Pattern + Cache Clean
    print("\n=== EXECUTING STAGE M8-1: FRAMEBUFFER ALLOCATION & CPU PATTERN ===", flush=True)
    m8_1_out = run_step("STAGE M8-1", "display m8-fb-init\n", 10.0, required_substr="M8_1             = PASS")
    (log_dir / "m8-1.txt").write_text(m8_1_out)
    if "M8_1             = PASS" not in m8_1_out:
        print("!!! STAGE M8-1 FAILED! Halting.", flush=True)
        host_file.write_text("".join(full_log))
        sys.exit(1)

    # M8-2: RGB0 SSPP Programming
    print("\n=== EXECUTING STAGE M8-2: RGB0 SSPP PROGRAMMING ===", flush=True)
    m8_2_out = run_step("STAGE M8-2", "display m8-rgb0-config\n", 10.0, required_substr="M8_2             = PASS")
    (log_dir / "m8-2.txt").write_text(m8_2_out)
    if "M8_2             = PASS" not in m8_2_out:
        print("!!! STAGE M8-2 FAILED! Halting.", flush=True)
        host_file.write_text("".join(full_log))
        sys.exit(1)

    # M8-3: LM0 Layer Mixer Programming
    print("\n=== EXECUTING STAGE M8-3: LM0 LAYER MIXER PROGRAMMING ===", flush=True)
    m8_3_out = run_step("STAGE M8-3", "display m8-lm0-config\n", 10.0, required_substr="M8_3             = PASS")
    (log_dir / "m8-3.txt").write_text(m8_3_out)
    if "M8_3             = PASS" not in m8_3_out:
        print("!!! STAGE M8-3 FAILED! Halting.", flush=True)
        host_file.write_text("".join(full_log))
        sys.exit(1)

    # M8-4: PP0 + DSI Stream Programming
    print("\n=== EXECUTING STAGE M8-4: PP0 & DSI MDP STREAM PROGRAMMING ===", flush=True)
    m8_4_out = run_step("STAGE M8-4", "display m8-stream-config\n", 10.0, required_substr="M8_4             = PASS")
    (log_dir / "m8-4.txt").write_text(m8_4_out)
    if "M8_4             = PASS" not in m8_4_out:
        print("!!! STAGE M8-4 FAILED! Halting.", flush=True)
        host_file.write_text("".join(full_log))
        sys.exit(1)

    # M8-5: CTL0 Routing
    print("\n=== EXECUTING STAGE M8-5: CTL0 ROUTING ===", flush=True)
    m8_5_out = run_step("STAGE M8-5", "display m8-ctl-config\n", 10.0, required_substr="M8_5             = PASS")
    (log_dir / "m8-5.txt").write_text(m8_5_out)
    if "M8_5             = PASS" not in m8_5_out:
        print("!!! STAGE M8-5 FAILED! Halting.", flush=True)
        host_file.write_text("".join(full_log))
        sys.exit(1)

    # M8-6: CTL Flush Programming
    print("\n=== EXECUTING STAGE M8-6: CTL FLUSH PROGRAMMING ===", flush=True)
    m8_6_out = run_step("STAGE M8-6", "display m8-flush-config\n", 10.0, required_substr="PASS")
    (log_dir / "m8-6.txt").write_text(m8_6_out)
    if "M8_6" not in m8_6_out or "PASS" not in m8_6_out:
        print("!!! STAGE M8-6 FAILED! Halting.", flush=True)
        host_file.write_text("".join(full_log))
        sys.exit(1)

    # Pre-Kick Status Diagnostic
    print("\n=== EXECUTING PRE-KICK STATUS DIAGNOSTIC ===", flush=True)
    prekick_out = run_step("PREKICK STATUS", "display m8-prekick-status\n", 10.0, required_substr="=== M8 PRE-KICK STATUS ===")
    (log_dir / "prekick-status.txt").write_text(prekick_out)

    # Save full session log
    host_file.write_text("".join(full_log))
    print(f"\nAll logs and stage artifacts written to {log_dir.resolve()}", flush=True)

    # Final Verification of Safety Locks
    print("\n=== AUDITING STRICT SAFETY LOCKS ===", flush=True)
    assert "CTL_START_COUNT=0" in prekick_out, "CTL_START_COUNT must be 0"
    assert "MDP_KICKOFF_COUNT=0" in prekick_out, "MDP_KICKOFF_COUNT must be 0"
    assert "FRAMEBUFFER_SCANOUT_COUNT=0" in prekick_out, "FRAMEBUFFER_SCANOUT_COUNT must be 0"
    assert "PREKICK_READY=YES" in prekick_out, "PREKICK_READY must be YES"

    print("\n>>> ALL STAGES M8-1 THROUGH M8-6 COMPLETED SUCCESSFULLY! <<<")
    print(">>> PREKICK_READY=YES. CTL_START STRICTLY GUARDED (0). <<<")


if __name__ == "__main__":
    main()
