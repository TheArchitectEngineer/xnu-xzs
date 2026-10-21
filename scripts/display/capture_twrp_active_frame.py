#!/usr/bin/env python3
"""Capture frame-synchronous TWRP baseline on physical Xperia XZs (BH905SX976).
Boots TWRP via fastboot boot (RAM only, NO FLASH), samples active MDSS/PP0/DSI
registers during live frame redraws, collects interrupt counters,
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


def read_dsi0_regs_full():
    chunks = ["0 40", "40 40", "80 40", "c0 40", "100 40", "140 40", "180 40", "1c0 40"]
    regs = {}
    raw_parts = []
    for chunk in chunks:
        adb(f"echo '{chunk}' > /sys/kernel/debug/mdp/dsi0_ctrl_off")
        raw = adb("cat /sys/kernel/debug/mdp/dsi0_ctrl_reg")
        raw_parts.append(raw)
        for line in raw.splitlines():
            if ":" in line:
                parts = line.split(":")
                base_addr = int(parts[0], 16)
                words = parts[1].strip().split()
                for idx, w in enumerate(words):
                    addr_str = f"0x{base_addr + idx * 4:08x}"
                    regs[addr_str] = f"0x{w}"
    return regs, "\n".join(raw_parts)


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

        # 4. Active Frame-Synchronous Sampling Script
        print("\n--- 2. ACTIVE FRAME SAMPLING DURING SCREEN REDRAWS ---", flush=True)
        target_script = """#!/sbin/sh
OUT=/tmp/active_samples.txt
rm -f $OUT
for i in $(seq 1 100); do
    # Sample MDP PP0 (0x71000..0x71040)
    echo "71000 40" > /sys/kernel/debug/mdp/off
    PP_RAW=$(cat /sys/kernel/debug/mdp/reg)

    # Sample MDP Top & CTL0 (0x1010..0x1020, 0x2000..0x2030)
    echo "1010 10" > /sys/kernel/debug/mdp/off
    INTR_RAW=$(cat /sys/kernel/debug/mdp/reg)
    echo "2000 30" > /sys/kernel/debug/mdp/off
    CTL_RAW=$(cat /sys/kernel/debug/mdp/reg)

    # Sample RGB0 Current Src
    echo "5000 40" > /sys/kernel/debug/mdp/off
    RGB_RAW=$(cat /sys/kernel/debug/mdp/reg)

    # Sample DSI0 CTRL (0x0..0x40, 0x40..0x80, 0x80..0xc0, 0x180..0x1c0)
    echo "40 40" > /sys/kernel/debug/mdp/dsi0_ctrl_off
    DSI40_RAW=$(cat /sys/kernel/debug/mdp/dsi0_ctrl_reg)
    echo "80 40" > /sys/kernel/debug/mdp/dsi0_ctrl_off
    DSI80_RAW=$(cat /sys/kernel/debug/mdp/dsi0_ctrl_reg)
    echo "180 40" > /sys/kernel/debug/mdp/dsi0_ctrl_off
    DSI180_RAW=$(cat /sys/kernel/debug/mdp/dsi0_ctrl_reg)

    echo "=== SAMPLE $i ===" >> $OUT
    echo "PP:" >> $OUT
    echo "$PP_RAW" >> $OUT
    echo "INTR:" >> $OUT
    echo "$INTR_RAW" >> $OUT
    echo "CTL:" >> $OUT
    echo "$CTL_RAW" >> $OUT
    echo "RGB:" >> $OUT
    echo "$RGB_RAW" >> $OUT
    echo "DSI40:" >> $OUT
    echo "$DSI40_RAW" >> $OUT
    echo "DSI80:" >> $OUT
    echo "$DSI80_RAW" >> $OUT
    echo "DSI180:" >> $OUT
    echo "$DSI180_RAW" >> $OUT

    usleep 5000 2>/dev/null || sleep 0.01 2>/dev/null
done
"""
        # Push to /tmp/sample_active.sh and run
        adb(f"cat << 'EOF' > /tmp/sample_active.sh\n{target_script}\nEOF\nchmod +x /tmp/sample_active.sh")

        # Start sampling process in background
        print("Launching background active-sample loop...", flush=True)
        sample_proc = subprocess.Popen(["adb", "-s", SERIAL, "shell", "/sbin/sh /tmp/sample_active.sh"])
        time.sleep(0.05)

        # Trigger rapid screen redraws using twrp print, swipe, tap, and echo
        print("Triggering UI redraws on screen...", flush=True)
        for sw in range(15):
            adb(f"twrp print 'ACTIVE_FRAME_COMMIT_{sw}'")
            adb("input swipe 200 800 200 300 50")
            time.sleep(0.02)

        sample_proc.wait(timeout=25)
        print("Active sampling complete. Pulling data...", flush=True)

        samples_txt = adb("cat /tmp/active_samples.txt")
        (OUT_DIR / "active_frame_samples.txt").write_text(samples_txt + "\n")

        # 5. Final Interrupts
        intr_final = adb("cat /proc/interrupts")
        (OUT_DIR / "interrupts_final.txt").write_text(intr_final + "\n")

        # 6. Complete Detailed Register Dump of MDP & DSI
        print("\n--- 3. FULL SUBSYSTEM AUDIT SNAPSHOT ---", flush=True)
        mdp_top, mdp_top_raw = read_mdp_regs("0", "20")
        mdp_intr, mdp_intr_raw = read_mdp_regs("1010", "10")
        ctl0, ctl0_raw = read_mdp_regs("2000", "30")
        pp0, pp0_raw = read_mdp_regs("71000", "40")
        rgb0, rgb0_raw = read_mdp_regs("5000", "40")
        lm0, lm0_raw = read_mdp_regs("45000", "20")
        dsi0, dsi0_raw = read_dsi0_regs_full()

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
            f"=== DSI0 FULL ===\n{dsi0_raw}\n"
        )

        print(f"\nSuccessfully captured frame-synchronous TWRP baseline to: {OUT_DIR}", flush=True)

    finally:
        # Guardrail: Return device to Fastboot
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
