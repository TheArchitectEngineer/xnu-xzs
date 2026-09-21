#!/usr/bin/env python3
"""Independent hardware-evidence verifier for D7-T1 Candidate-2B."""

import argparse
from pathlib import Path


def require(text: str, needle: str) -> None:
    if needle not in text:
        print(f"D7T1_CANDIDATE2B_EVIDENCE_VERIFIER=FAIL missing: {needle}")
        raise SystemExit(1)


def require_ordered(text: str, checkpoints: tuple[str, ...]) -> None:
    cursor = -1
    for checkpoint in checkpoints:
        position = text.find(checkpoint, cursor + 1)
        if position < 0:
            print(
                "D7T1_CANDIDATE2B_EVIDENCE_VERIFIER=FAIL "
                f"missing checkpoint: {checkpoint}"
            )
            raise SystemExit(1)
        cursor = position


parser = argparse.ArgumentParser()
parser.add_argument("console", type=Path)
args = parser.parse_args()

# The hex helper already contains a prefix in this historical telemetry stream.
text = args.console.read_text(errors="replace").replace("\x00", "").replace("0x0x", "0x")

require_ordered(text, (
    "D740/2B00 Candidate-2B DCFG-only normalization entered",
    "D740/2B01 precondition snapshot complete",
    "D740/2B02 RUN_STOP=0 confirmed",
    "D740/2B03 DEVCTRLHLT=1 confirmed",
    "D740/2B10 DCFG before captured",
    "D740/2B11 DCFG normalization write issued",
    "D740/2B12 DCFG readback complete",
    "D740/2B13 DCFG decode validated",
    "D740/2B20 post-mutation DWC3 snapshot complete",
    "D740/2B21 wrapper/PHY unchanged verified",
    "D740/2B30 normal boot continuing",
    "D740/2B90 acceptance reached",
    "D740/2B91 PASS",
))

for item in (
    "PRECONDITION_RUN_STOP_0=yes",
    "PRECONDITION_DEVCTRLHLT_1=yes",
    "DCFG_RAW_BEFORE=0x000000000008080c",
    "DCFG_RAW_WRITTEN=0x0000000000080800",
    "DCFG_RAW_READBACK=0x0000000000080800",
    "DCFG_DEVSPD_BEFORE=SUPER_SPEED_ENCODING",
    "DCFG_DEVSPD_AFTER=HIGH_SPEED",
    "DCFG_DEVADDR_BEFORE=0x0000000000000001",
    "DCFG_DEVADDR_AFTER=0x0000000000000000",
    "DCFG_NUMP_BEFORE=0x0000000000000004",
    "DCFG_NUMP_AFTER=0x0000000000000004",
    "DCFG_WRITE_MATCH=yes",
    "RUN_STOP_BEFORE=0",
    "RUN_STOP_AFTER=0",
    "DEVCTRLHLT_BEFORE=1",
    "DEVCTRLHLT_AFTER=1",
    "QUSB2_PLL_LOCKED_BEFORE=yes",
    "QUSB2_PLL_LOCKED_AFTER=yes",
    "QUSB2_PORT_POWERDOWN_BEFORE=0x0000000022222222",
    "QUSB2_PORT_POWERDOWN_AFTER=0x0000000022222222",
    "CANDIDATE2B_DWC3_WRITE_COUNT=0x0000000000000001",
    "CANDIDATE2B_DWC3_WRITE_TARGET=DCFG",
    "GCTL_UNCHANGED=yes",
    "GUSB2PHYCFG0_UNCHANGED=yes",
    "QSCRATCH_UNCHANGED=yes",
    "QUSB2_UNCHANGED=yes",
    "GCC_UNCHANGED=yes",
    "EVENT_BUFFER_UNCHANGED=yes",
    "EP0_UNTOUCHED=yes",
    "QSCRATCH_REGISTER_WRITES=0",
    "QUSB2_REGISTER_WRITES=0",
    "GCC_REGISTER_WRITES=0",
    "DCTL_RUN_STOP_AFTER=0",
    "DSTS_DEVCTRLHLT_AFTER=1",
    "CANDIDATE2B_COMPLETE=yes",
    "D7_T1_COMPLETE=no",
    "D7_T1_SEALED=no",
    "PSTORE_PIPELINE=PASS",
    "D6-M1_COMPLETE=yes",
    "D6-M2_COMPLETE=yes",
    "D6-M3_COMPLETE=yes",
    "D6-M4_COMPLETE=yes",
    "D6-M5_COMPLETE=yes",
    "D6-M6_COMPLETE=yes",
    "D7_M3_COMPLETE=yes",
    "D7M4_INTERNAL_PIPELINE_COMPLETE=yes",
):
    require(text, item)

print("D7T1_CANDIDATE2B_CHECKPOINTS=PASS")
print("D7T1_CANDIDATE2B_DCFG_ORACLE=PASS")
print("D7T1_CANDIDATE2B_HALTED_SAFETY=PASS")
print("D7T1_CANDIDATE2B_IMMUTABLE_DOMAINS=PASS")
print("D7T1_CANDIDATE2B_EVIDENCE_VERIFIER=PASS")
