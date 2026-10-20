#!/usr/bin/env python3
"""Capture frame-synchronous TWRP baseline on physical Xperia XZs (BH905SX976).
Boots TWRP via fastboot boot (RAM only, NO FLASH), samples active MDSS/PP0/DSI
registers during live frame redraws, collects ftrace and interrupt counters,
saves all raw artifacts to artifacts/hw/d8m8/active-frame-twrp-baseline/,
and returns device to fastboot.
"""

import json
import os
import subprocess
import sys
import time
from pathlib import Path

SERIAL = "BH905SX976"
ROOT = Path("/Users/lechaukha12/Desktop/xnu-xzs")
TWRP_IMG = ROOT / "artifacts/builds/twrp-kagura.img"
OUT_DIR = ROOT / "artifacts/hw/d8m8/active-frame-twrp-baseline"


def run_cmd(cmd_list, timeout=60):
    res = subprocess.run(cmd_list, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True, timeout=timeout)
    return res.stdout.strip(), res.stderr.strip(), res.returncode


def adb(cmd_str, timeout=15):
    full = ["adb", "-s", SERIAL, "shell", cmd_str]
    stdout, stderr, code = run_cmd(full, timeout=timeout)
    return stdout


def read_mdp_regs(off_hex, count_hex):
    adb(f"echo '{off_hex} {count_hex}' > /sys/kernel/debug/mdp/off")
    raw = adb("cat /sys/kernel/debug/mdp/reg")
    regs = {}
    for line in raw.splitlines():
        if ":" in line:
            parts = line.split(":")
            base_addr = int(parts[0], 16)
            words = parts[1].strip().split()
            for idx, w in enumerate(words):
                addr_str = f"0x{base_addr + idx * 4:08x}"
                regs[addr_str] = f"0x{w}"
    return regs, raw


def read_dsi0_regs(off_hex, count_hex):
    adb(f"echo '{off_hex} {count_hex}' > /sys/kernel/debug/mdp/dsi0_ctrl_off")
    raw = adb("cat /sys/kernel/debug/mdp/dsi0_ctrl_reg")
    regs = {}
    for line in raw.splitlines():
        if ":" in line:
            parts = line.split(":")
            base_addr = int(parts[0], 16)
            words = parts[1].strip().split()
            for idx, w in enumerate(words):
                addr_str = f"0x{base_addr + idx * 4:08x}"
                regs[addr_str] = f"0x{w}"
    return regs, raw


def main():
    OUT_DIR.mkdir(parents=True, exist_ok=True)
    print("=== D8-M8: CAPTURE FRAME-SYNCHRONOUS TWRP BASELINE ===", flush=True)

    if not TWRP_IMG.exists():
        print(f"ERROR: {TWRP_IMG} does not exist!", file=sys.stderr)
        sys.exit(1)

    try:
        # 1. Check device status
        stdout, _, _ = run_cmd(["fastboot", "devices"])
        print(f"Fastboot devices:\n{stdout}", flush=True)
        if SERIAL not in stdout:
            adb_devs, _, _ = run_cmd(["adb", "devices"])
            print(f"ADB devices:\n{adb_devs}", flush=True)
            if SERIAL not in adb_devs:
                print("ERROR: Device not found in fastboot or adb!", file=sys.stderr)
                sys.exit(1)
            print("Device already in ADB mode.", flush=True)
        else:
            print(f"Booting TWRP from {TWRP_IMG} (RAM-only, NO FLASH)...", flush=True)
            boot_out, boot_err, boot_rc = run_cmd(["fastboot", "-s", SERIAL, "boot", str(TWRP_IMG)], timeout=60)
            print(f"fastboot boot output:\n{boot_out}\n{boot_err}", flush=True)
            if boot_rc != 0:
                print("ERROR: fastboot boot failed!", file=sys.stderr)
                sys.exit(1)

            print("Waiting for ADB recovery...", flush=True)
            run_cmd(["adb", "-s", SERIAL, "wait-for-recovery"], timeout=60)

        # Allow TWRP GUI and display driver to initialize
        print("Waiting 10 seconds for TWRP GUI to initialize...", flush=True)
        time.sleep(10)

        # Check root and kernel
        who = adb("id")
        kernel = adb("uname -a")
        print(f"ADB User: {who}", flush=True)
        print(f"Kernel: {kernel}", flush=True)
        (OUT_DIR / "device_info.txt").write_text(f"USER: {who}\nKERNEL: {kernel}\n")

        # 2. Panel Info, MIPI, TE Properties
        print("\n--- 1. PANEL METADATA & TE PROPERTIES ---", flush=True)
        panel_info = adb("cat /sys/class/graphics/fb0/msm_fb_panel_info 2>/dev/null")
        fb_type = adb("cat /sys/class/graphics/fb0/msm_fb_type 2>/dev/null")
        te_props = adb("for f in /sys/kernel/debug/mdss_panel_fb0/intf0/te/*; do echo $(basename $f)=$(cat $f 2>/dev/null); done")
        mipi_props = adb("for f in /sys/kernel/debug/mdss_panel_fb0/intf0/mipi/*; do echo $(basename $f)=$(cat $f 2>/dev/null); done")
        (OUT_DIR / "panel_info.txt").write_text(panel_info + "\n" + fb_type + "\n")
        (OUT_DIR / "te_properties.txt").write_text(te_props + "\n")
        (OUT_DIR / "mipi_properties.txt").write_text(mipi_props + "\n")

        # 3. Initial snapshot of interrupts
        intr_initial = adb("cat /proc/interrupts")
        (OUT_DIR / "interrupts_initial.txt").write_text(intr_initial + "\n")

        # 4. Ftrace setup
        print("\n--- 2. FTRACE SETUP ---", flush=True)
        adb("echo nop > /sys/kernel/debug/tracing/current_tracer")
        adb("echo 0 > /sys/kernel/debug/tracing/tracing_on")
        adb("echo > /sys/kernel/debug/tracing/trace")
        adb("echo 1 > /sys/kernel/debug/tracing/events/mdss/mdp_cmd_kickoff/enable")
        adb("echo 1 > /sys/kernel/debug/tracing/events/mdss/mdp_cmd_pingpong_done/enable")
        adb("echo 1 > /sys/kernel/debug/tracing/events/mdss/mdp_cmd_wait_pingpong/enable")
        adb("echo 1 > /sys/kernel/debug/tracing/events/mdss/mdp_commit/enable")
        adb("echo 1 > /sys/kernel/debug/tracing/events/mdss/mdp_mixer_update/enable")
        adb("echo 1 > /sys/kernel/debug/tracing/events/mdss/mdp_perf_update_bus/enable")
        adb("echo 1 > /sys/kernel/debug/tracing/tracing_on")
        print("Ftrace armed.", flush=True)

        # 5. Active Frame-Synchronous Sampling Script
        # We write a standalone shell script that runs on target, samples PP0 and DSI registers
        # tightly in a loop while triggering redraws via touch input swipe.
        print("\n--- 3. ACTIVE FRAME SAMPLING DURING SCREEN REDRAWS ---", flush=True)
        target_script = """#!/sbin/sh
OUT=/tmp/active_samples.txt
rm -f $OUT
for i in $(seq 1 40); do
    # Sample MDP PP0 (0x71000..0x71030)
    echo "71000 40" > /sys/kernel/debug/mdp/off
    PP_RAW=$(cat /sys/kernel/debug/mdp/reg)

    # Sample MDP Top & CTL0 (0x1000..0x1030, 0x2000..0x2030)
    echo "1010 10" > /sys/kernel/debug/mdp/off
    INTR_RAW=$(cat /sys/kernel/debug/mdp/reg)
    echo "2000 20" > /sys/kernel/debug/mdp/off
    CTL_RAW=$(cat /sys/kernel/debug/mdp/reg)

    # Sample RGB0 Current Src
    echo "5000 30" > /sys/kernel/debug/mdp/off
    RGB_RAW=$(cat /sys/kernel/debug/mdp/reg)

    # Sample DSI0 CTRL (0x0..0x60)
    echo "0 60" > /sys/kernel/debug/mdp/dsi0_ctrl_off
    DSI_RAW=$(cat /sys/kernel/debug/mdp/dsi0_ctrl_reg)

    echo "=== SAMPLE $i ===" >> $OUT
    echo "PP:" >> $OUT
    echo "$PP_RAW" >> $OUT
    echo "INTR:" >> $OUT
    echo "$INTR_RAW" >> $OUT
    echo "CTL:" >> $OUT
    echo "$CTL_RAW" >> $OUT
    echo "RGB:" >> $OUT
    echo "$RGB_RAW" >> $OUT
    echo "DSI:" >> $OUT
    echo "$DSI_RAW" >> $OUT

    usleep 5000 2>/dev/null || sleep 0.01 2>/dev/null
done
"""
        # Push to /tmp/sample_active.sh and run
        adb(f"cat << 'EOF' > /tmp/sample_active.sh\n{target_script}\nEOF\nchmod +x /tmp/sample_active.sh")

        # Start sampling process in background
        print("Launching background active-sample loop...", flush=True)
        sample_proc = subprocess.Popen(["adb", "-s", SERIAL, "shell", "/sbin/sh /tmp/sample_active.sh"])
        time.sleep(0.05)

        # Trigger rapid screen redraws using input swipes
        print("Triggering UI redraws on touch screen...", flush=True)
        for sw in range(10):
            adb("input swipe 400 1400 400 600 80")
            time.sleep(0.03)

        sample_proc.wait(timeout=20)
        print("Active sampling complete. Pulling data...", flush=True)

        samples_txt = adb("cat /tmp/active_samples.txt")
        (OUT_DIR / "active_frame_samples.txt").write_text(samples_txt + "\n")

        # 6. Collect Ftrace
        print("\n--- 4. FTRACE RETRIEVAL ---", flush=True)
        adb("echo 0 > /sys/kernel/debug/tracing/tracing_on")
        trace_txt = adb("cat /sys/kernel/debug/tracing/trace")
        (OUT_DIR / "ftrace_mdss.txt").write_text(trace_txt + "\n")

        # 7. Final Interrupts
        intr_final = adb("cat /proc/interrupts")
        (OUT_DIR / "interrupts_final.txt").write_text(intr_final + "\n")

        # 8. Complete Single Detailed Snapshot of MDP & DSI
        print("\n--- 5. FULL SUBSYSTEM AUDIT SNAPSHOT ---", flush=True)
        mdp_top, mdp_top_raw = read_mdp_regs("0", "20")
        mdp_intr, mdp_intr_raw = read_mdp_regs("1010", "10")
        ctl0, ctl0_raw = read_mdp_regs("2000", "20")
        pp0, pp0_raw = read_mdp_regs("71000", "40")
        rgb0, rgb0_raw = read_mdp_regs("5000", "40")
        lm0, lm0_raw = read_mdp_regs("45000", "20")
        dsi0, dsi0_raw = read_dsi0_regs("0", "70")

        full_dump = {
            "MDP_TOP": mdp_top,
            "MDP_INTR": mdp_intr,
            "CTL0": ctl0,
            "PP0": pp0,
            "RGB0": rgb0,
            "LM0": lm0,
            "DSI0": dsi0,
        }
        (OUT_DIR / "twrp_active_registers.json").write_text(json.dumps(full_dump, indent=2))
        (OUT_DIR / "twrp_active_registers_raw.txt").write_text(
            f"=== MDP TOP ===\n{mdp_top_raw}\n\n"
            f"=== MDP INTR ===\n{mdp_intr_raw}\n\n"
            f"=== CTL0 ===\n{ctl0_raw}\n\n"
            f"=== PP0 ===\n{pp0_raw}\n\n"
            f"=== RGB0 ===\n{rgb0_raw}\n\n"
            f"=== LM0 ===\n{lm0_raw}\n\n"
            f"=== DSI0 ===\n{dsi0_raw}\n"
        )

        print(f"\nSuccessfully captured frame-synchronous TWRP baseline to: {OUT_DIR}", flush=True)

    finally:
        # 9. Guardrail: Return device to Fastboot
        print("\n--- GUARDRAIL: RETURNING DEVICE TO FASTBOOT ---", flush=True)
        adb("reboot bootloader")
        time.sleep(4)
        fb_check, _, _ = run_cmd(["fastboot", "devices"])
        print(f"Fastboot check:\n{fb_check}", flush=True)
        if SERIAL in fb_check:
            print("[PASS] Device confirmed back in Fastboot mode.", flush=True)
        else:
            print("[WARN] Device not immediately seen in Fastboot. Check connection.", flush=True)


if __name__ == "__main__":
    main()
