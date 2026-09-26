#!/usr/bin/env python3
"""Capture live golden display registers and properties from running TWRP on physical Xperia XZs.
Saves to artifacts/display-audit/m8/golden_live_hw_dump.json.
"""

import json
import subprocess
import sys
from pathlib import Path


def adb_cmd(cmd):
    full = ["adb", "shell", cmd]
    res = subprocess.run(full, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
    return res.stdout.strip()


def read_dsi0_regs(off_hex, count):
    adb_cmd(f"echo '{off_hex} {count}' > /sys/kernel/debug/mdp/dsi0_ctrl_off")
    raw = adb_cmd("cat /sys/kernel/debug/mdp/dsi0_ctrl_reg")
    regs = {}
    for line in raw.splitlines():
        if ":" in line:
            parts = line.split(":")
            base_addr = int(parts[0], 16)
            words = parts[1].strip().split()
            for idx, w in enumerate(words):
                regs[f"0x{base_addr + idx * 4:08x}"] = f"0x{w}"
    return regs


def read_mdp_regs(off_hex, count):
    adb_cmd(f"echo '{off_hex} {count}' > /sys/kernel/debug/mdp/off")
    raw = adb_cmd("cat /sys/kernel/debug/mdp/reg")
    regs = {}
    for line in raw.splitlines():
        if ":" in line:
            parts = line.split(":")
            base_addr = int(parts[0], 16)
            words = parts[1].strip().split()
            for idx, w in enumerate(words):
                regs[f"0x{base_addr + idx * 4:08x}"] = f"0x{w}"
    return regs


def main():
    out_file = Path("artifacts/display-audit/m8/golden_live_hw_dump.json")
    out_file.parent.mkdir(parents=True, exist_ok=True)

    print("=== CAPTURING LIVE TWRP GOLDEN STATE ON PHYSICAL HARDWARE ===", flush=True)

    # 1. Device identity
    serial = adb_cmd("getprop ro.serialno")
    model = adb_cmd("getprop ro.product.model")
    device = adb_cmd("getprop ro.product.device")
    print(f"Device: {model} ({device}), Serial: {serial}", flush=True)

    # 2. Framebuffer properties
    fb_props = {
        "virtual_size": adb_cmd("cat /sys/class/graphics/fb0/virtual_size"),
        "bits_per_pixel": adb_cmd("cat /sys/class/graphics/fb0/bits_per_pixel"),
        "stride": adb_cmd("cat /sys/class/graphics/fb0/stride"),
        "panel_info": adb_cmd("cat /sys/class/graphics/fb0/msm_fb_panel_info"),
        "type": adb_cmd("cat /sys/class/graphics/fb0/msm_fb_type"),
    }
    print(f"FB Props: {fb_props}", flush=True)

    # 3. MIPI parameters
    mipi_raw = adb_cmd("for f in /sys/kernel/debug/mdss_panel_fb0/intf0/mipi/*; do echo $(basename $f)=$(cat $f); done")
    mipi_props = dict(line.split("=", 1) for line in mipi_raw.splitlines() if "=" in line)

    # 4. TE parameters
    te_raw = adb_cmd("for f in /sys/kernel/debug/mdss_panel_fb0/intf0/te/*; do echo $(basename $f)=$(cat $f); done")
    te_props = dict(line.split("=", 1) for line in te_raw.splitlines() if "=" in line)

    # 5. Pipe usage stats
    pipe_stat = adb_cmd("cat /sys/kernel/debug/mdp/stat")

    # 6. DSI0 Control Registers
    dsi0_regs = read_dsi0_regs("0", "60")

    # 7. MDP Control & Core Registers
    mdp_top = read_mdp_regs("0", "20")
    ctl0_regs = read_mdp_regs("2000", "20")

    data = {
        "provenance": "GOLDEN_HW_PROVEN",
        "device": {
            "model": model,
            "device": device,
            "serial": serial,
            "soc": "MSM8996 v3.0"
        },
        "framebuffer": fb_props,
        "mipi_properties": mipi_props,
        "tear_check_properties": te_props,
        "pipe_stats": pipe_stat,
        "dsi0_ctrl_registers": dsi0_regs,
        "mdp_top_registers": mdp_top,
        "ctl0_registers": ctl0_regs
    }

    with open(out_file, "w") as f:
        json.dump(data, f, indent=2)

    print(f"Saved golden hardware dump to: {out_file}", flush=True)


if __name__ == "__main__":
    main()
