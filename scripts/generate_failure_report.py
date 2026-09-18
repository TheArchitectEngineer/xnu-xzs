#!/usr/bin/env python3
"""
scripts/generate_failure_report.py - Automated Boot Failure Report Generator
Generates artifacts/logs/failure-YYYYMMDD-HHMMSS.md with ESR decoding and ELR symbolication.
"""

import sys
import os
import subprocess
import datetime
import hashlib
import argparse

def get_git_commit(cwd=None):
    try:
        out = subprocess.check_output(["git", "rev-parse", "HEAD"], cwd=cwd, stderr=subprocess.DEVNULL)
        return out.decode('utf-8').strip()
    except Exception:
        return "unknown"

def get_sha256(filepath):
    if not os.path.exists(filepath):
        return "not_found"
    h = hashlib.sha256()
    with open(filepath, "rb") as f:
        while chunk := f.read(65536):
            h.update(chunk)
    return h.hexdigest()

def run_cmd(cmd):
    try:
        out = subprocess.check_output(cmd, stderr=subprocess.STDOUT)
        return out.decode('utf-8', errors='ignore').strip()
    except subprocess.CalledProcessError as e:
        return e.output.decode('utf-8', errors='ignore').strip()
    except Exception as e:
        return str(e)

def main():
    parser = argparse.ArgumentParser(description="Generate XZS XNU Boot Failure Report")
    parser.add_argument("--marker", default="Unknown", help="Last checkpoint marker (e.g. K4 - before MMU enable)")
    parser.add_argument("--esr", default="", help="Raw ESR_EL1 value in hex")
    parser.add_argument("--elr", default="", help="Raw ELR_EL1 value in hex")
    parser.add_argument("--far", default="", help="Raw FAR_EL1 value in hex")
    parser.add_argument("--spsr", default="", help="Raw SPSR_EL1 value in hex")
    parser.add_argument("--hypothesis", default="Under investigation", help="Engineering hypothesis")
    parser.add_argument("--patch", default="None", help="Patch applied")
    parser.add_argument("--retest", default="Pending", help="Retest result")
    parser.add_argument("--output", default=None, help="Custom output file path")
    args = parser.parse_args()

    now = datetime.datetime.now()
    timestamp_str = now.strftime("%Y%m%d-%H%M%S")
    out_dir = "artifacts/logs"
    os.makedirs(out_dir, exist_ok=True)
    out_file = args.output or os.path.join(out_dir, f"failure-{timestamp_str}.md")

    root_dir = os.path.abspath(os.path.join(os.path.dirname(__file__), ".."))
    xnu_commit = get_git_commit(os.path.join(root_dir, "src/xnu"))
    bootshim_commit = get_git_commit(root_dir)

    kernel_bin = os.path.join(root_dir, "artifacts/builds/kernel.development.vmapple")
    if not os.path.exists(kernel_bin):
        kernel_bin = os.path.join(root_dir, "src/xnu/BUILD/obj/kernel.development.vmapple")
    kernel_sha = get_sha256(kernel_bin)

    bootimg_bin = os.path.join(root_dir, "artifacts/builds/xzs-xnu-boot.img")
    bootimg_sha = get_sha256(bootimg_bin)

    esr_decode = ""
    if args.esr:
        decode_script = os.path.join(root_dir, "scripts/decode_esr.py")
        esr_decode = run_cmd([sys.executable, decode_script, args.esr])

    sym_func = "N/A"
    sym_source = "N/A"
    sym_atos = "N/A"
    if args.elr:
        sym_script = os.path.join(root_dir, "scripts/symbolicate_elr.py")
        sym_raw = run_cmd([sys.executable, sym_script, args.elr, "--kernel", kernel_bin])
        for line in sym_raw.splitlines():
            if line.startswith("symbol:"):
                sym_func = line.split(":", 1)[1].strip()
            elif line.startswith("source:"):
                sym_source = line.split(":", 1)[1].strip()
            elif line.startswith("atos detail:"):
                sym_atos = line.split(":", 1)[1].strip()

    report = f"""# XZS XNU Boot Failure

Generated: {now.strftime("%Y-%m-%d %H:%M:%S")}

## Build

- **XNU commit:** `{xnu_commit}`
- **Bootshim commit:** `{bootshim_commit}`
- **Kernel SHA256:** `{kernel_sha}`
- **Boot image SHA256:** `{bootimg_sha}`

## Last marker

`{args.marker}`

## Exception

- **ESR_EL1:** `{args.esr if args.esr else 'None'}`
- **ELR_EL1:** `{args.elr if args.elr else 'None'}`
- **FAR_EL1:** `{args.far if args.far else 'None'}`
- **SPSR_EL1:** `{args.spsr if args.spsr else 'None'}`

## ESR decode

```text
{esr_decode if esr_decode else 'No ESR provided'}
```

## Symbolication

- **Function:** `{sym_func}`
- **Source file & line:** `{sym_source}`
- **Atos detail:** `{sym_atos}`

## Hypothesis

{args.hypothesis}

## Patch applied

{args.patch}

## Retest

{args.retest}
"""

    with open(out_file, "w") as f:
        f.write(report)

    print(f"Report generated: {out_file}")

if __name__ == "__main__":
    main()
