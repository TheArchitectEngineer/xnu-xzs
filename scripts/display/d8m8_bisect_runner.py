#!/usr/bin/env python3
"""D8-M8.5 Transport Bisection Runner.

Strict bisection between M8-1, mdss-ahb-debug (C1 rate), and m8-rgb0-config.
Aborts immediately on the first missing prompt or write failure.
"""

import hashlib
import os
import re
import subprocess
import sys
import time
from pathlib import Path
import importlib.util

ROOT = Path(__file__).resolve().parents[2]
LOG_DIR = ROOT / (sys.argv[1] if len(sys.argv) > 1 else "artifacts/hw/d8m8_bisect/r1")
BOOT = ROOT / "artifacts/builds/xzs-xnu-boot.img"
KERNEL = ROOT / "src/xnu/BUILD/obj/DEVELOPMENT_ARM64_VMAPPLE/kernel.development.vmapple"
SERIAL = "BH905SX976"

spec = importlib.util.spec_from_file_location(
    "r11a_helpers", ROOT / "scripts/display/d8m8_7_retry11a_runner.py"
)
helpers = importlib.util.module_from_spec(spec)
spec.loader.exec_module(helpers)


def sha256_of(path: Path) -> str:
    h = hashlib.sha256()
    with path.open("rb") as f:
        for chunk in iter(lambda: f.read(65536), b""):
            h.update(chunk)
    return h.hexdigest()


def main():
    LOG_DIR.mkdir(parents=True, exist_ok=True)
    transcript = []

    def note(msg):
        sys.stdout.write(msg)
        sys.stdout.flush()
        transcript.append(msg)

    dev = None
    boot_attempted = False
    run_failed = False
    failure_reason = None

    def fast_collect(dev, seconds=10.0, required_substr=None):
        end = time.time() + seconds
        buf = b""
        req_bytes = required_substr.encode("utf-8") if required_substr else None
        while time.time() < end:
            chunk, kind = helpers.xzs_console.bulk_read(dev, timeout_ms=100)
            if chunk:
                buf += chunk
                tail = buf[max(0, len(buf) - 200):]
                has_prompt = (b"xzs#" in tail or b"code=3" in tail)
                if req_bytes and (req_bytes in buf or b"ALREADY" in buf) and has_prompt:
                    time.sleep(0.1)
                    extra, _ = helpers.xzs_console.bulk_read(dev, timeout_ms=100)
                    if extra:
                        buf += extra
                    break
                elif not req_bytes and has_prompt:
                    time.sleep(0.1)
                    extra, _ = helpers.xzs_console.bulk_read(dev, timeout_ms=100)
                    if extra:
                        buf += extra
                    break
            else:
                time.sleep(0.02)
        return buf

    def strict_send_cmd(dev, cmd_str, wait_sec=15.0, required_substr=None):
        clean_cmd = cmd_str.strip() if isinstance(cmd_str, str) else cmd_str.decode("utf-8", errors="replace").strip()
        note(f">>> SEND: {clean_cmd}\n")
        # Drain pre-existing input
        for _ in range(5):
            pre_drain, _ = helpers.xzs_console.bulk_read(dev, timeout_ms=50)
            if not pre_drain:
                break
        time.sleep(0.25)
        payload = (clean_cmd + "\n").encode("utf-8")
        written, kind = helpers.xzs_console.bulk_write(dev, payload, timeout_ms=2000)
        if kind is not None or written != len(payload):
            err_msg = f"!!! HARD WRITE ERROR: written={written}, kind={kind} on cmd {clean_cmd!r}"
            note(err_msg + "\n")
            raise RuntimeError(err_msg)
        resp = fast_collect(dev, seconds=wait_sec, required_substr=required_substr)
        decoded = resp.decode("utf-8", errors="replace")
        note(decoded + "\n")
        if required_substr and (required_substr not in decoded and "ALREADY" not in decoded):
            err_msg = f"!!! MISSING EXPECTED SUBSTRING {required_substr!r} in response to {clean_cmd!r}"
            note(err_msg + "\n")
            raise RuntimeError(err_msg)
        if "xzs#" not in decoded and "code=3" not in decoded:
            err_msg = f"!!! MISSING PROMPT xzs# in response to {clean_cmd!r}"
            note(err_msg + "\n")
            raise RuntimeError(err_msg)
        return decoded

    def step(dev, label, command, timeout, expected):
        note(f"\n--- {label} ---\n> {command}\n")
        output = strict_send_cmd(dev, command, wait_sec=timeout, required_substr=expected)
        return output

    def verify_echo_alive(dev, tag):
        note(f"\n[BISECT CHECK] Verifying shell echo alive: {tag}...\n")
        echo_cmd = f"echo ALIVE_{tag}"
        resp = strict_send_cmd(dev, echo_cmd, wait_sec=10.0, required_substr=f"ALIVE_{tag}")
        note(f"[BISECT CHECK] PASSED: Shell alive after {tag}\n")
        return resp

    try:
        note("=======================================================\n")
        note("=== D8-M8.5: TRANSPORT BISECTION CAMPAIGN           ===\n")
        note("=======================================================\n")

        note("Verifying fastboot device presence...\n")
        detected = subprocess.run(["fastboot", "devices"], capture_output=True, text=True, check=True).stdout
        note(f"Fastboot devices:\n{detected}\n")
        if SERIAL not in detected:
            raise RuntimeError(f"Target device {SERIAL} not in fastboot devices output: {detected!r}")

        boot_hash = sha256_of(BOOT)
        kernel_hash = sha256_of(KERNEL)
        commit_sha = subprocess.run(["git", "rev-parse", "HEAD"], capture_output=True, text=True).stdout.strip()

        build_id = [
            f"HEAD={commit_sha}",
            f"BOOT_IMAGE={BOOT}",
            f"BOOT_SHA256={boot_hash}",
            f"KERNEL={KERNEL}",
            f"KERNEL_SHA256={kernel_hash}",
            f"PAC=0",
            f"PROTOCOL=D8-M8.5 Transport Bisection",
        ]
        (LOG_DIR / "build-identity.txt").write_text("\n".join(build_id) + "\n")
        note("\n=== BUILD IDENTITY ===\n" + "\n".join(build_id) + "\n\n")

        note(f"Booting {BOOT.name} via fastboot boot (ONE RAM BOOT ONLY)...\n")
        boot_attempted = True
        boot_proc = subprocess.run(["fastboot", "-s", SERIAL, "boot", str(BOOT)], capture_output=True, text=True)
        note(boot_proc.stdout + "\n" + boot_proc.stderr + "\n")
        if boot_proc.returncode != 0:
            raise RuntimeError(f"fastboot boot failed with code {boot_proc.returncode}")

        # Connect USB console
        note("Waiting for USB console...\n")
        dev = helpers.xzs_console.open_stable_device(timeout_sec=60)
        if dev is None:
            raise RuntimeError("USB console failed to enumerate after boot")

        note("XZS console connected.\n")
        prompt = False
        for _ in range(15):
            helpers.xzs_console.bulk_write(dev, b"\n", timeout_ms=1000)
            time.sleep(0.4)
            data = helpers.collect(dev, seconds=1.5)
            decoded = data.decode("utf-8", errors="replace")
            note(decoded)
            if "xzs#" in decoded:
                prompt = True
                break
        if not prompt:
            raise RuntimeError("Timed out waiting for initial xzs# shell prompt")

        # Initial baseline health check
        verify_echo_alive(dev, "INITIAL_PROMPT")

        # Prerequisites
        prerequisites = [
            ("MMAGIC_AHB", "clocks mmagic-ahb-on", 10, "PASS"),
            ("MMAGIC_CFG_AHB", "clocks mmagic-cfg-ahb-on", 10, "PASS"),
            ("MMAGIC_NOC", "clocks mmagic-mdss-noc-on", 10, "PASS"),
            ("MMAGIC_AXI", "clocks mmagic-mdss-axi-on", 10, "PASS"),
            ("MDSS_GDSC", "display power mdss-on", 10, "PASS"),
            ("MDSS_AHB", "clocks mdss-ahb-on", 10, "PASS"),
            ("MDSS_AXI", "clocks mdss-axi-on", 10, "PASS"),
            ("MDSS_MDP", "clocks mdp-on", 10, "PASS"),
            ("CORE_STATUS", "clocks mdss-critical-status", 20, "PASS"),
            ("M3", "display m3-run full", 50, "PASS_FULL_M3"),
            ("M4", "display m4-run full", 30, "PASS_MODE2"),
            ("P1", "display p1-run", 15, "RESULT=PASS_P1"),
        ]
        for label, command, timeout, expected in prerequisites:
            step(dev, label, command, timeout, expected)

        verify_echo_alive(dev, "POST_PREREQUISITES")

        # Panel Prepare
        prep_text = step(dev, "PANEL_PREPARE", "display m8-status", 30, "PANEL_READY=yes")
        (LOG_DIR / "panel-prepare.txt").write_text(prep_text)
        verify_echo_alive(dev, "POST_PANEL_PREPARE")

        # FB Init: draws 8 color bars + center badge
        step(dev, "M8-1", "display m8-fb-init", 60, "M8_1             = PASS")
        verify_echo_alive(dev, "POST_M8_1")

        # BISECT STAGE 1: mdss-ahb-debug (C1 rate)
        note("\n=======================================================\n")
        note("=== BISECT PROBE 1: clocks mdss-ahb-debug           ===\n")
        note("=======================================================\n")
        clock_text = step(dev, "CLOCK RATE", "clocks mdss-ahb-debug", 30, "[C1] C1_END")
        (LOG_DIR / "c1-rate-confirm.txt").write_text(clock_text)
        verify_echo_alive(dev, "POST_C1_RATE")

        # BISECT STAGE 2: m8-rgb0-config
        note("\n=======================================================\n")
        note("=== BISECT PROBE 2: display m8-rgb0-config          ===\n")
        note("=======================================================\n")
        step(dev, "M8-2", "display m8-rgb0-config", 20, "M8_2             = PASS")
        verify_echo_alive(dev, "POST_M8_2_RGB0")

        # If both survived, continue remaining pipeline to observe kickoff
        note("\n=======================================================\n")
        note("=== BISECT PASSED PROBES 1 & 2: CONTINUING PIPELINE ===\n")
        note("=======================================================\n")
        frame = [
            ("M8-3", "display m8-lm0-config", 20, "M8_3             = PASS"),
            ("M8-4", "display m8-stream-config", 20, "M8_4             = PASS"),
            ("M8-5", "display m8-ctl-config", 20, "M8_5             = PASS"),
            ("M8-6", "display m8-flush-config", 20, "M8_6              = PASS"),
            ("PREKICK", "display m8-prekick-status", 30, "PREKICK_READY=YES"),
        ]
        for label, command, timeout, expected in frame:
            step(dev, label, command, timeout, expected)
            verify_echo_alive(dev, f"POST_{label}")

        kickoff_text = step(dev, "KICKOFF", "display m8-kickoff", 120, "M8_KICKOFF_COMPLETE")
        (LOG_DIR / "kickoff.txt").write_text(kickoff_text)
        verify_echo_alive(dev, "POST_KICKOFF")

        note("\n[BISECT RESULT]: COMPLETE RUN PASSED UP TO KICKOFF WITHOUT TRANSPORT LOSS.\n")

    except Exception as exc:
        run_failed = True
        failure_reason = str(exc)
        note(f"\n[BISECT FAILURE / EARLY ABORT]: {exc}\n")
    finally:
        (LOG_DIR / "transcript.txt").write_text("".join(transcript))
        if dev is not None:
            note("Releasing USB device handle cleanly...\n")
            try:
                helpers.xzs_console.release_device(dev)
            except Exception:
                pass

    if run_failed:
        sys.exit(1)


if __name__ == "__main__":
    main()
