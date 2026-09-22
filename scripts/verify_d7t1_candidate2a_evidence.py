#!/usr/bin/env python3
"""Independent evidence verifier for a D7-T1 Candidate-2A pstore console."""

import argparse
from pathlib import Path
import sys


def require(text: str, needle: str) -> None:
    if needle not in text:
        print(f"D7T1_CANDIDATE2A_EVIDENCE_VERIFIER=FAIL missing: {needle}")
        raise SystemExit(1)


def require_ordered(text: str, checkpoints: list[str]) -> None:
    cursor = -1
    for checkpoint in checkpoints:
        position = text.find(checkpoint, cursor + 1)
        if position < 0:
            print(f"D7T1_CANDIDATE2A_EVIDENCE_VERIFIER=FAIL missing checkpoint: {checkpoint}")
            raise SystemExit(1)
        cursor = position


parser = argparse.ArgumentParser()
parser.add_argument("console", type=Path)
args = parser.parse_args()

# xzs_d6m4_put_hex64() already includes 0x; normalize the historic caller prefix.
text = args.console.read_text(errors="replace").replace("0x0x", "0x")

require_ordered(text, (
    "D740/2A00 Candidate-2A read-only snapshot entered",
    "D740/2A01 DWC3 read-only snapshot complete",
    "D740/2A02 QSCRATCH read-only snapshot complete",
    "D740/2A03 QUSB2/GCC read-only snapshot complete",
    "D740/2A04 snapshot decoding ready",
    "D740/2A09 normal boot continuing",
))

for item in (
    "DWC3_GSNPSID=0x000000005533270a",
    "DWC3_GCTL=0x0000000000112000",
    "DWC3_DSTS=0x0000000000d38f74",
    "DWC3_DCFG=0x000000000008080c",
    "DWC3_DCTL=0x0000000000f00000",
    "QSCRATCH_GENERAL_CFG=0x000000000000000d",
    "QSCRATCH_HS_PHY_CTRL=0x0000000010100000",
    "QSCRATCH_HS_PHY_CTRL_UTMI_OTG_VBUS_VALID=yes",
    "QSCRATCH_HS_PHY_CTRL_SW_SESSVLD_SEL=yes",
    "QUSB2PHY_PLL_LOCKED=yes",
    "QUSB2PHY_POWER_DOWN=no",
    "GCC_QUSB2PHY_PRIM_BCR=0x0000000000000000",
    "CANDIDATE2A_READ_ONLY=yes",
    "DWC3_REGISTER_WRITES=0",
    "QSCRATCH_REGISTER_WRITES=0",
    "QUSB2_REGISTER_WRITES=0",
    "GCC_REGISTER_WRITES=0",
    "USB_STATE_MUTATED=no",
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

print("D7T1_CANDIDATE2A_CHECKPOINTS=PASS")
print("D7T1_CANDIDATE2A_READ_ONLY_CONTRACT=PASS")
print("D7T1_CANDIDATE2A_REGRESSION_PREFIX=PASS")
print("D7T1_CANDIDATE2A_EVIDENCE_VERIFIER=PASS")
