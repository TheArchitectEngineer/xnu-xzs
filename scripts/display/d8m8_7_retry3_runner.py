#!/usr/bin/env python3
"""D8-M8-7 Retry #3 Final Free-Run Command-Mode Scanout Hardware Runner.
Executes prerequisites M2..M6, M8-1..M8-6 (with free-run PP0 timing and DSI_CMD_MDP_CTRL=0x00000008),
verifies PREKICK_READY=YES, and then performs exactly ONE controlled MDP command-mode kickoff (CTL_START).
Halts strictly after one attempt.
Saves all evidence to artifacts/hw/d8m8/m8-7-retry3/.
"""

import argparse
import importlib.util
import os
import subprocess
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
    parser = argparse.ArgumentParser(description="D8-M8-7 Retry #3 Free-Run Kickoff Runner")
    parser.add_argument("--log-dir", default="artifacts/hw/d8m8/m8-7-retry3",
                        help="Directory to store hardware test logs and stage artifacts")
    parser.add_argument("--no-boot", action="store_true",
                        help="Skip fastboot boot step if already booted")
    args = parser.parse_args()

    log_dir = Path(args.log_dir)
    log_dir.mkdir(parents=True, exist_ok=True)
    host_file = log_dir / "host.txt"

    boot_img = Path("artifacts/builds/xzs-xnu-boot.img")
    if not boot_img.exists():
        print(f"ERROR: {boot_img} does not exist!", file=sys.stderr)
        sys.exit(1)

    print("=== D8-M8-7 RETRY #3 HARDWARE RUNNER START ===", flush=True)

    if not args.no_boot:
        print("=== STEP 0: FRESH BOOT VIA FASTBOOT ===", flush=True)
        boot_res = subprocess.run(["fastboot", "-s", "BH905SX976", "boot", str(boot_img)],
                                  capture_output=True, text=True)
        print(boot_res.stdout, flush=True)
        print(boot_res.stderr, flush=True)
        if boot_res.returncode != 0:
            print("ERROR: fastboot boot failed!", file=sys.stderr)
            sys.exit(1)

    print("=== WAITING FOR USB CONSOLE ENUMERATION ===", flush=True)
    dev = xzs_console.open_stable_device(timeout_sec=60)
    if dev is None:
        print("ERROR: could not open stable XNU USB device", flush=True)
        sys.exit(1)

    print("=== STEP 1: SYNC SHELL PROMPT ===", flush=True)
    start_t = time.time()
    while time.time() - start_t < 15.0:
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

    # 2. Prerequisite: M3 Lower Layer
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

    print("\n=== EXECUTING STAGE M8-1: FRAMEBUFFER ALLOCATION & CPU PATTERN ===", flush=True)
    m8_1_out = run_step("STAGE M8-1", "display m8-fb-init\n", 30.0, required_substr="M8_1             = PASS")
    if "M8_1             = PASS" not in m8_1_out:
        print("!!! STAGE M8-1 FAILED! Halting.", flush=True)
        host_file.write_text("".join(full_log))
        sys.exit(1)

    print("\n=== EXECUTING STAGE M8-2: RGB0 SSPP PROGRAMMING ===", flush=True)
    m8_2_out = run_step("STAGE M8-2", "display m8-rgb0-config\n", 15.0, required_substr="M8_2             = PASS")
    if "M8_2             = PASS" not in m8_2_out:
        print("!!! STAGE M8-2 FAILED! Halting.", flush=True)
        host_file.write_text("".join(full_log))
        sys.exit(1)

    print("\n=== EXECUTING STAGE M8-3: LM0 LAYER MIXER PROGRAMMING ===", flush=True)
    m8_3_out = run_step("STAGE M8-3", "display m8-lm0-config\n", 15.0, required_substr="M8_3             = PASS")
    if "M8_3             = PASS" not in m8_3_out:
        print("!!! STAGE M8-3 FAILED! Halting.", flush=True)
        host_file.write_text("".join(full_log))
        sys.exit(1)

    print("\n=== EXECUTING STAGE M8-4: PP0 & DSI MDP STREAM PROGRAMMING ===", flush=True)
    m8_4_out = run_step("STAGE M8-4", "display m8-stream-config\n", 15.0, required_substr="M8_4             = PASS")
    if "M8_4             = PASS" not in m8_4_out:
        print("!!! STAGE M8-4 FAILED! Halting.", flush=True)
        host_file.write_text("".join(full_log))
        sys.exit(1)

    print("\n=== EXECUTING STAGE M8-5: CTL0 ROUTING ===", flush=True)
    m8_5_out = run_step("STAGE M8-5", "display m8-ctl-config\n", 15.0, required_substr="CTL_LAYER_0")
    if "PASS" not in m8_5_out and "MATCH" not in m8_5_out:
        print("!!! STAGE M8-5 FAILED! Halting.", flush=True)
        host_file.write_text("".join(full_log))
        sys.exit(1)

    print("\n=== EXECUTING STAGE M8-6: CTL FLUSH PROGRAMMING ===", flush=True)
    m8_6_out = run_step("STAGE M8-6", "display m8-flush-config\n", 15.0, required_substr="PASS")
    if "M8_6" not in m8_6_out or "PASS" not in m8_6_out:
        print("!!! STAGE M8-6 FAILED! Halting.", flush=True)
        host_file.write_text("".join(full_log))
        sys.exit(1)

    # Pre-Kick Status Diagnostic
    print("\n=== EXECUTING PRE-KICK STATUS DIAGNOSTIC ===", flush=True)
    prekick_out = run_step("PREKICK STATUS", "display m8-prekick-status\n", 20.0, required_substr="=== M8 PRE-KICK STATUS ===")
    (log_dir / "pre-kick.txt").write_text(prekick_out)

    assert "CTL_START_COUNT=0" in prekick_out, "CTL_START_COUNT must be 0"
    assert "MDP_KICKOFF_COUNT=0" in prekick_out, "MDP_KICKOFF_COUNT must be 0"
    assert "FRAMEBUFFER_SCANOUT_COUNT=0" in prekick_out, "FRAMEBUFFER_SCANOUT_COUNT must be 0"
    assert "PREKICK_READY=YES" in prekick_out, "PREKICK_READY must be YES"

    # =========================================================================
    # 7. EXECUTING M8-7 RETRY #3: CONTROLLED SINGLE KICKOFF
    # =========================================================================
    print("\n=======================================================", flush=True)
    print("=== EXECUTING M8-7 RETRY #3: CONTROLLED SINGLE KICKOFF ===", flush=True)
    print("=======================================================", flush=True)

    kickoff_out = run_step("M8-7 RETRY #3 KICKOFF", "display m8-kickoff\n", 25.0, required_substr="M8_7_RETRY3=")
    (log_dir / "kickoff.txt").write_text(kickoff_out)

    # Extract completion and post-frame blocks
    completion_lines = []
    postframe_lines = []
    in_comp = False
    in_post = False
    for line in kickoff_out.splitlines():
        if "PP0_DONE_OBSERVED=" in line or "START_CYCLES=" in line:
            in_comp = True
        if "DSI_STATUS=" in line or "POST_DSI_ACK_ERR=" in line:
            in_post = True
        if in_comp:
            completion_lines.append(line)
        if in_post:
            postframe_lines.append(line)

    (log_dir / "completion.txt").write_text("\n".join(completion_lines) + "\n")
    (log_dir / "post-frame.txt").write_text("\n".join(postframe_lines) + "\n")

    # Save full session log
    host_file.write_text("".join(full_log))
    print(f"\nAll logs and stage artifacts written to {log_dir.resolve()}", flush=True)

    # Final Verification
    print("\n=== FINAL M8-7 RETRY #3 VERIFICATION ===", flush=True)
    if "M8_7_RETRY3=PASS" in kickoff_out:
        print(">>> RESULT: M8-7 RETRY #3 PASS! First MDP command-mode frame scanned out! <<<", flush=True)
    else:
        print(">>> RESULT: M8-7 RETRY #3 FAIL or TIMEOUT! Check artifacts for details. <<<", flush=True)


if __name__ == "__main__":
    main()
