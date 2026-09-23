#!/usr/bin/env python3
"""Scripted D8-M2 execution session over the existing bulk console.
Follows the exact D8-M2 continuation handoff procedure.
"""

import importlib.util
import os
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
    log_dir = Path("artifacts/hw/d8m2-545398f")
    log_dir.mkdir(parents=True, exist_ok=True)
    log_file = log_dir / "host.txt"

    print("=== D8-M2 SCRIPTED SESSION START ===", flush=True)
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

    def run_step(name, cmd, wait_s=1.5):
        print(f"\n--- {name} ---", flush=True)
        out = send_cmd(dev, cmd, wait_sec=wait_s)
        full_log.append(f"\n# {name}\n> {cmd}\n{out}")
        return out

    # 1. Baseline State Capture (Read-Only)
    run_step("BASELINE PWD", "pwd\n", 1.0)
    pwr_base = run_step("BASELINE POWER STATUS", "display power status\n", 1.5)
    crit_base = run_step("BASELINE CRITICAL STATUS", "clocks mdss-critical-status\n", 1.5)
    ahb_base = run_step("BASELINE AHB STATUS", "clocks mdss-ahb-status\n", 1.5)
    ahb_dbg = run_step("BASELINE AHB DEBUG", "clocks mdss-ahb-debug\n", 1.5)

    # 2. Enable Critical MMAGIC Clocks Sequentially
    print("\n=======================================================", flush=True)
    print("=== ENABLING 4 CRITICAL MMAGIC BRANCHES ===", flush=True)

    # Branch 1: mmss_mmagic_ahb (0x5024)
    out1 = run_step("ENABLE MMSS_MMAGIC_AHB", "clocks mmagic-ahb-on\n", 2.0)
    if "PASS" not in out1 and "ALREADY_ON" not in out1:
        print("!!! STOP: MMSS_MMAGIC_AHB failed. Stopping critical clock enablement.", flush=True)
        log_file.write_text("".join(full_log))
        sys.exit(1)

    # Branch 2: mmss_mmagic_cfg_ahb (0x5054)
    out2 = run_step("ENABLE MMSS_MMAGIC_CFG_AHB", "clocks mmagic-cfg-ahb-on\n", 2.0)
    if "PASS" not in out2 and "ALREADY_ON" not in out2:
        print("!!! STOP: MMSS_MMAGIC_CFG_AHB failed. Stopping critical clock enablement.", flush=True)
        log_file.write_text("".join(full_log))
        sys.exit(1)

    # Branch 3: mmagic_mdss_noc_cfg_ahb (0x2478)
    out3 = run_step("ENABLE MMAGIC_MDSS_NOC", "clocks mmagic-mdss-noc-on\n", 2.0)
    if "PASS" not in out3 and "ALREADY_ON" not in out3:
        print("!!! STOP: MMAGIC_MDSS_NOC failed. Stopping critical clock enablement.", flush=True)
        log_file.write_text("".join(full_log))
        sys.exit(1)

    # Branch 4: mmagic_mdss_axi (0x2474)
    out4 = run_step("ENABLE MMAGIC_MDSS_AXI", "clocks mmagic-mdss-axi-on\n", 2.0)
    if "PASS" not in out4 and "ALREADY_ON" not in out4:
        print("!!! STOP: MMAGIC_MDSS_AXI failed. Stopping critical clock enablement.", flush=True)
        log_file.write_text("".join(full_log))
        sys.exit(1)

    # 3. Verify all 4 critical branches running
    crit_post = run_step("POST-CRITICAL STATUS VERIFY", "clocks mdss-critical-status\n", 1.5)

    # 4. Establish MDSS Power State
    pwr_chk = run_step("CHECK MDSS POWER", "display power status\n", 1.0)
    if "mdss=0x00222001" in pwr_chk or "0x00222001" in pwr_chk:
        mdss_pwr = run_step("POWER ON MDSS GDSC", "display power mdss-on\n", 2.0)
        if "PASS" not in mdss_pwr and "ALREADY_ON" not in mdss_pwr:
            print("!!! STOP: MDSS GDSC power on failed.", flush=True)
            log_file.write_text("".join(full_log))
            sys.exit(1)
        run_step("VERIFY MDSS POWER ON", "display power status\n", 1.0)
    else:
        print("MDSS GDSC already powered on.", flush=True)

    # 5. DECISIVE TEST: Retry mdss_ahb
    print("\n=======================================================", flush=True)
    print("=== DECISIVE TEST: RETRY MDSS_AHB ===", flush=True)
    ahb_retry = run_step("RETRY MDSS_AHB ENABLE", "clocks mdss-ahb-on\n", 2.5)

    if "PASS" in ahb_retry:
        print(">>> MDSS_AHB ENABLE: HARDWARE PASS! <<<", flush=True)

        # 6. Next: MDSS AXI
        print("\n=======================================================", flush=True)
        print("=== BRINGING UP MDSS AXI ===", flush=True)
        axi_out = run_step("ENABLE MDSS_AXI", "clocks mdss-axi-on\n", 2.0)

        # 7. Next: MDSS MDP
        if "PASS" in axi_out:
            print("\n=======================================================", flush=True)
            print("=== BRINGING UP MDSS MDP ===", flush=True)
            mdp_out = run_step("ENABLE MDSS_MDP", "clocks mdp-on\n", 2.0)
            if "PASS" in mdp_out:
                print("\n>>> ALL CLOCKS ENABLED: D8-M2 FULL PASS! <<<", flush=True)
            else:
                print("\n!!! MDP CLOCK FAILED !!!", flush=True)
        else:
            print("\n!!! MDSS AXI FAILED !!!", flush=True)
    else:
        print(">>> MDSS_AHB ENABLE: HALTED / TIMEOUT / FAILED <<<", flush=True)
        # Read debug status
        run_step("POST-FAIL AHB STATUS", "clocks mdss-ahb-status\n", 1.5)

    run_step("FINAL PWD CHECK", "pwd\n", 1.0)

    log_file.write_text("".join(full_log))
    print(f"\nFull session transcript written to {log_file}", flush=True)


if __name__ == "__main__":
    main()
