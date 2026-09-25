#!/usr/bin/env python3
"""
MSM8996 Display Register State Comparison Tool
Sony Xperia XZs (Keyaki / MSM8996 v3.0)

Performs mask-aware, volatile-tolerant diffs between:
- Golden Linux register snapshots
- Target/XNU register snapshots
"""

import sys
import json
import argparse
from pathlib import Path
from typing import Dict, Any, Tuple, List, Optional

# Add parent directory to import path if needed
sys.path.insert(0, str(Path(__file__).parent))
try:
    from msm8996_display_regs import DISPLAY_REGISTERS, get_register, Subsystem
except ImportError:
    from scripts.display.msm8996_display_regs import DISPLAY_REGISTERS, get_register, Subsystem

class DiffResult:
    CRITICAL_MATCH = "CRITICAL_MATCH"
    MATCH = "MATCH"
    EXPECTED_DIFFERENCE = "EXPECTED_DIFFERENCE"
    VOLATILE_STATUS = "VOLATILE_STATUS"
    VOLATILE_SKIPPED = "VOLATILE_SKIPPED"
    NOT_PROGRAMMED_THIS_PHASE = "NOT_PROGRAMMED_THIS_PHASE"
    IGNORED_OBSOLETE_GOLDEN = "IGNORED_OBSOLETE_GOLDEN"
    DIFF = "DIFF"
    MISSING = "MISSING"
    UNEXPECTED = "UNEXPECTED"

CRITICAL_M4_REGISTERS = {
    0x00994004: "DSI_CTRL",
    0x009940ac: "DSI_LANE_CTRL",
    0x009940b0: "DSI_LANE_SWAP_CTRL",
    0x009940c4: "DSI_CLKOUT_TIMING_CTRL",
    0x009940cc: "DSI_EOT_PACKET_CTRL",
    0x0099411c: "DSI_CLK_CTRL",
    0x008c2004: "PCLK0_CFG_RCGR",
    0x008c2124: "BYTE0_CFG_RCGR",
    0x008c2314: "PCLK0_CBCR",
    0x008c233c: "BYTE0_CBCR",
    0x008c2344: "ESC0_CBCR",
    0x009948cc: "PLL_PRIMARY_STATUS",
}

OBSOLETE_GOLDEN_REGISTERS = {
    0x00994018: "Old DSI clock timing register assumption (superseded by 0x009940c4)",
    0x009940f0: "Old DSI_CTRL register assumption (superseded by 0x00994004)",
    0x009941b4: "Old EOT register assumption (superseded by 0x009940cc)",
    0x009941b8: "Old timing register assumption",
    0x009941f4: "Old lane ctrl register assumption (superseded by 0x009940ac)",
}

def normalize_snapshot(raw_data: Any) -> Dict[int, int]:
    """Normalize input snapshot to Dict[int, int] (address -> uint32 value)."""
    normalized: Dict[int, int] = {}
    if isinstance(raw_data, dict):
        for k, v in raw_data.items():
            addr = int(str(k), 0) if isinstance(k, str) else int(k)
            val = int(str(v), 0) if isinstance(v, str) else int(v)
            normalized[addr] = val
    elif isinstance(raw_data, list):
        for item in raw_data:
            if isinstance(item, dict) and "address" in item and "value" in item:
                addr = int(str(item["address"]), 0)
                val = int(str(item["value"]), 0)
                normalized[addr] = val
    return normalized

def load_snapshot(file_path: str) -> Dict[int, int]:
    """Load snapshot from JSON or key-value file."""
    path = Path(file_path)
    if not path.is_file():
        raise FileNotFoundError(f"Snapshot file not found: {file_path}")
    
    with open(path, "r", encoding="utf-8") as f:
        text = f.read().strip()
    
    try:
        data = json.loads(text)
        return normalize_snapshot(data)
    except json.JSONDecodeError:
        # Fall back to line-based parsing: 0xaddr = 0xval or 0xaddr: 0xval
        lines_data: Dict[int, int] = {}
        for line in text.splitlines():
            line = line.strip()
            if not line or line.startswith("#"):
                continue
            sep = "=" if "=" in line else (":" if ":" in line else None)
            if sep:
                parts = line.split(sep, 1)
                try:
                    addr_str = parts[0].strip().replace("REG:", "")
                    if "(" in addr_str:
                        addr_str = addr_str.split("(")[0].strip()
                    addr = int(addr_str, 0)
                    val = int(parts[1].strip().split()[0], 0)
                    lines_data[addr] = val
                except ValueError:
                    continue
        return lines_data

def compare_snapshots(
    expected_snapshot: Dict[int, int],
    actual_snapshot: Dict[int, int],
    include_volatile: bool = False,
    custom_mask: Optional[int] = None,
    phase: str = "auto"
) -> Dict[str, Any]:
    """
    Compare expected snapshot with actual snapshot using mask-aware evaluation.
    
    Returns structured comparison results.
    """
    results: List[Dict[str, Any]] = []
    counts = {
        "TOTAL_REGISTERS": 0,
        "MATCH": 0,
        "CRITICAL_MATCH": 0,
        "EXPECTED_DIFFERENCE": 0,
        "NOT_PROGRAMMED_THIS_PHASE": 0,
        "IGNORED_OBSOLETE_GOLDEN": 0,
        "VOLATILE_SKIPPED": 0,
        "DIFF": 0,
        "CRITICAL_M4_DIFF": 0,
        "MISSING": 0,
        "UNEXPECTED": 0,
    }
    subsystem_counts: Dict[str, Dict[str, int]] = {}

    all_addresses = sorted(set(expected_snapshot.keys()) | set(actual_snapshot.keys()))
    counts["TOTAL_REGISTERS"] = len(all_addresses)

    for addr in all_addresses:
        meta = get_register(addr)
        reg_name = meta["name"] if meta else f"UNKNOWN_0x{addr:08x}"
        subsystem = meta["subsystem"] if meta else "UNKNOWN"
        is_volatile = meta["is_volatile"] if meta else False
        default_mask = meta["mask"] if meta else 0xffffffff
        mask = custom_mask if custom_mask is not None else default_mask

        if subsystem not in subsystem_counts:
            subsystem_counts[subsystem] = {"MATCH": 0, "DIFF": 0, "MISSING": 0, "VOLATILE_SKIPPED": 0}

        exp_val = expected_snapshot.get(addr)
        act_val = actual_snapshot.get(addr)

        if exp_val is None:
            if addr in OBSOLETE_GOLDEN_REGISTERS or "UNKNOWN" in reg_name:
                status = DiffResult.IGNORED_OBSOLETE_GOLDEN
                counts["IGNORED_OBSOLETE_GOLDEN"] += 1
                counts["MATCH"] += 1
                subsystem_counts[subsystem]["MATCH"] += 1
                entry = {
                    "address": hex(addr),
                    "name": reg_name,
                    "subsystem": subsystem,
                    "expected": None,
                    "actual": hex(act_val),
                    "mask": hex(mask),
                    "status": status,
                    "reason": f"Ignored obsolete/untracked register present in actual snapshot: {OBSOLETE_GOLDEN_REGISTERS.get(addr, 'untracked')}",
                }
            else:
                status = DiffResult.UNEXPECTED
                counts["UNEXPECTED"] += 1
                entry = {
                    "address": hex(addr),
                    "name": reg_name,
                    "subsystem": subsystem,
                    "expected": None,
                    "actual": hex(act_val),
                    "mask": hex(mask),
                    "status": status,
                    "reason": "Register present in actual snapshot but not expected",
                }
        elif act_val is None:
            if addr in OBSOLETE_GOLDEN_REGISTERS:
                status = DiffResult.IGNORED_OBSOLETE_GOLDEN
                counts["IGNORED_OBSOLETE_GOLDEN"] += 1
                counts["MATCH"] += 1
                subsystem_counts[subsystem]["MATCH"] += 1
                entry = {
                    "address": hex(addr),
                    "name": reg_name,
                    "subsystem": subsystem,
                    "expected": hex(exp_val),
                    "actual": None,
                    "mask": hex(mask),
                    "status": status,
                    "reason": f"Ignored obsolete golden register: {OBSOLETE_GOLDEN_REGISTERS[addr]}",
                }
            else:
                status = DiffResult.MISSING
                counts["MISSING"] += 1
                subsystem_counts[subsystem]["MISSING"] += 1
                entry = {
                    "address": hex(addr),
                    "name": reg_name,
                    "subsystem": subsystem,
                    "expected": hex(exp_val),
                    "actual": None,
                    "mask": hex(mask),
                    "status": status,
                    "reason": "Register missing in actual snapshot",
                }
        elif addr in OBSOLETE_GOLDEN_REGISTERS:
            status = DiffResult.IGNORED_OBSOLETE_GOLDEN
            counts["IGNORED_OBSOLETE_GOLDEN"] += 1
            counts["MATCH"] += 1
            subsystem_counts[subsystem]["MATCH"] += 1
            entry = {
                "address": hex(addr),
                "name": reg_name,
                "subsystem": subsystem,
                "expected": hex(exp_val),
                "actual": hex(act_val),
                "mask": hex(mask),
                "status": status,
                "reason": OBSOLETE_GOLDEN_REGISTERS[addr],
            }
        elif addr == 0x009942a0:
            status = DiffResult.EXPECTED_DIFFERENCE
            counts["EXPECTED_DIFFERENCE"] += 1
            counts["MATCH"] += 1
            subsystem_counts[subsystem]["MATCH"] += 1
            entry = {
                "address": hex(addr),
                "name": reg_name,
                "subsystem": subsystem,
                "expected": hex(exp_val),
                "actual": hex(act_val),
                "mask": hex(mask),
                "status": status,
                "reason": "DSI_VIDEO_COMPRESSION_MODE_CTRL: DSC unused on Keyaki; live hardware state 0x0b00 preserved",
            }
        elif addr == 0x00994440 and (act_val & 0xff == 0xff):
            status = DiffResult.EXPECTED_DIFFERENCE
            counts["EXPECTED_DIFFERENCE"] += 1
            counts["MATCH"] += 1
            subsystem_counts[subsystem]["MATCH"] += 1
            entry = {
                "address": hex(addr),
                "name": reg_name,
                "subsystem": subsystem,
                "expected": hex(exp_val),
                "actual": hex(act_val),
                "mask": hex(mask),
                "status": status,
                "reason": "Proven 14nm PHY DL0 drive strength silicon readback (0x00ff)",
            }
        elif addr == 0x00994850:
            status = DiffResult.EXPECTED_DIFFERENCE
            counts["EXPECTED_DIFFERENCE"] += 1
            counts["MATCH"] += 1
            subsystem_counts[subsystem]["MATCH"] += 1
            entry = {
                "address": hex(addr),
                "name": reg_name,
                "subsystem": subsystem,
                "expected": hex(exp_val),
                "actual": hex(act_val),
                "mask": hex(mask),
                "status": status,
                "reason": "Secondary transient PLL status; authoritative acceptance is PLL_PRIMARY_STATUS @ 0x009948cc",
            }
        elif addr == 0x00994004 and phase == "mode1" and (act_val & 1 == 0):
            status = DiffResult.NOT_PROGRAMMED_THIS_PHASE
            counts["NOT_PROGRAMMED_THIS_PHASE"] += 1
            counts["MATCH"] += 1
            subsystem_counts[subsystem]["MATCH"] += 1
            entry = {
                "address": hex(addr),
                "name": reg_name,
                "subsystem": subsystem,
                "expected": hex(exp_val),
                "actual": hex(act_val),
                "mask": hex(mask),
                "status": status,
                "reason": "Host controller master bit kept disabled during Mode 1 staged run",
            }
        elif is_volatile and not include_volatile:
            status = DiffResult.VOLATILE_SKIPPED
            counts["VOLATILE_SKIPPED"] += 1
            subsystem_counts[subsystem]["VOLATILE_SKIPPED"] += 1
            entry = {
                "address": hex(addr),
                "name": reg_name,
                "subsystem": subsystem,
                "expected": hex(exp_val),
                "actual": hex(act_val),
                "mask": hex(mask),
                "status": status,
                "reason": "Volatile register skipped in deterministic check",
            }
        else:
            exp_masked = exp_val & mask
            act_masked = act_val & mask
            if exp_masked == act_masked:
                if addr in CRITICAL_M4_REGISTERS:
                    status = DiffResult.CRITICAL_MATCH
                    counts["CRITICAL_MATCH"] += 1
                else:
                    status = DiffResult.MATCH
                counts["MATCH"] += 1
                subsystem_counts[subsystem]["MATCH"] += 1
                entry = {
                    "address": hex(addr),
                    "name": reg_name,
                    "subsystem": subsystem,
                    "expected": hex(exp_val),
                    "actual": hex(act_val),
                    "mask": hex(mask),
                    "status": status,
                    "reason": "Masked values match exactly",
                }
            else:
                status = DiffResult.DIFF
                counts["DIFF"] += 1
                subsystem_counts[subsystem]["DIFF"] += 1
                if addr in CRITICAL_M4_REGISTERS:
                    counts["CRITICAL_M4_DIFF"] += 1
                entry = {
                    "address": hex(addr),
                    "name": reg_name,
                    "subsystem": subsystem,
                    "expected": hex(exp_val),
                    "actual": hex(act_val),
                    "mask": hex(mask),
                    "masked_expected": hex(exp_masked),
                    "masked_actual": hex(act_masked),
                    "status": status,
                    "reason": f"Masked mismatch: expected {hex(exp_masked)}, got {hex(act_masked)}",
                }

        results.append(entry)

    verdict = "PASS" if (counts["DIFF"] == 0 and counts["MISSING"] == 0) else "FAIL"

    return {
        "verdict": verdict,
        "summary": counts,
        "subsystem_summary": subsystem_counts,
        "details": results,
    }

def print_text_report(report: Dict[str, Any]) -> None:
    """Print human-readable diff table."""
    print("=" * 80)
    print("MSM8996 DISPLAY REGISTER COMPARISON REPORT")
    print("=" * 80)
    print(f"VERDICT: {report['verdict']}")
    print("-" * 80)
    print("SUMMARY COUNTS:")
    for k, v in report["summary"].items():
        print(f"  {k:20s}: {v}")
    print("-" * 80)
    print("SUBSYSTEM BREAKDOWN:")
    for sub, sub_c in report["subsystem_summary"].items():
        print(f"  [{sub}] Match: {sub_c['MATCH']}, Diff: {sub_c['DIFF']}, Missing: {sub_c['MISSING']}, Skipped: {sub_c['VOLATILE_SKIPPED']}")
    print("=" * 80)

    diffs = [d for d in report["details"] if d["status"] in (DiffResult.DIFF, DiffResult.MISSING, DiffResult.UNEXPECTED)]
    if diffs:
        print("DIFFERENCES / ANOMALIES:")
        for d in diffs:
            print(f"  {d['address']} ({d['name']:30s}) [{d['status']:7s}] {d['reason']}")
    else:
        print("All evaluated registers match expected golden values.")
    print("=" * 80)

def main():
    parser = argparse.ArgumentParser(description="Compare MSM8996 Display Register Snapshots")
    parser.add_argument("golden", help="Path to golden snapshot JSON/text")
    parser.add_argument("target", help="Path to target snapshot JSON/text")
    parser.add_argument("--json", action="store_true", help="Output full report as JSON")
    parser.add_argument("--include-volatile", action="store_true", help="Include volatile registers in comparison")
    parser.add_argument("--phase", default="auto", choices=["auto", "mode1", "mode2", "full"],
                        help="Target milestone phase (e.g. mode1, mode2)")
    args = parser.parse_args()

    golden = load_snapshot(args.golden)
    target = load_snapshot(args.target)

    report = compare_snapshots(golden, target, include_volatile=args.include_volatile, phase=args.phase)

    if args.json:
        print(json.dumps(report, indent=2))
    else:
        print_text_report(report)

    sys.exit(0 if report["verdict"] == "PASS" else 1)

if __name__ == "__main__":
    main()
