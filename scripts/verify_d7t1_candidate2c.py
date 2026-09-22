#!/usr/bin/env python3
"""Static safety gate for D7-T1 Candidate-2C halted event-buffer ownership."""

from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
SOURCE = ROOT / "src/xnu/pexpert/arm/xzs_usb.c"
STATUS = ROOT / "src/xnu/osfmk/arm64/status.c"
HEADER = ROOT / "src/xnu/pexpert/arm/xzs_usb.h"
MIRROR = ROOT / "src/xnu/pexpert/pexpert/arm/xzs_usb.h"


def fail(message: str) -> None:
    print(f"D7T1_CANDIDATE2C_VERIFIER=FAIL: {message}")
    raise SystemExit(1)


def function(text: str) -> str:
    start = text.find("int xzs_usb_init(void)")
    brace = text.find("{", start)
    depth = 0
    for i in range(brace, len(text)):
        if text[i] == "{": depth += 1
        elif text[i] == "}":
            depth -= 1
            if not depth: return text[start:i + 1]
    fail("cannot parse xzs_usb_init")


source = SOURCE.read_text()
init = function(source)
for token in (
    "D740/2C00", "D740/2C01", "D740/2C02", "D740/2C10",
    "D740/2C11", "D740/2C12", "D740/2C20", "D740/2C21",
    "D740/2C22", "D740/2C23", "D740/2C30", "D740/2C31",
    "D740/2C40", "D740/2C90", "D740/2C91",
    "DWC3_GHWPARAMS1", "DWC3_NUM_EVENT_INTERRUPTS",
    "XZS_DWC3_EVENT_BUFFER_SIZE", "ml_vtophys", "flush_dcache",
    "DWC3_GEVNTSIZ_INTMASK", "DWC3_GEVNTCOUNT_PENDING_MASK",
):
    if token not in init and token not in source:
        fail(f"missing {token}")

for call in (
    "dwc3_write32(DWC3_DCFG", "dwc3_write32(DWC3_GEVNTADR0",
    "dwc3_write32(DWC3_GEVNTADR_HI0", "dwc3_write32(DWC3_GEVNTSIZ0",
    "dwc3_write32(DWC3_GEVNTCNT0",
):
    if init.count(call) != 1: fail(f"expected exactly one authorized call site: {call}")
for forbidden in (
    "dwc3_write32(DWC3_GCTL", "dwc3_write32(DWC3_DCTL",
    "dwc3_write32(DWC3_DEVTEN", "dwc3_write32(DWC3_DALEPENA",
    "dwc3_write32(DWC3_DEPCMD", "dwc3_ep_cmd(",
    "dwc3_start_transfer(", "dwc3_configure_endpoints(",
    "xzs_usb_poll_events(", "s_qcom_glue_base =", "s_qusb2_phy_base =", "s_gcc_base =",
):
    if forbidden in init: fail(f"forbidden Candidate-2C path: {forbidden}")
if HEADER.read_text() != MIRROR.read_text(): fail("mirrored headers differ")
for token in ("EVENT_BUFFER_DMA_WORKING=NOT_TESTED", "D7_T1_COMPLETE=no", "QUSB2_WRITE_COUNT=0"):
    if token not in STATUS.read_text(): fail(f"missing telemetry {token}")
print("CANDIDATE2C_DCFG_WRITE_ALLOWED=yes")
print("CANDIDATE2C_EVENT_REGISTER_WRITES=GEVNTADRLO0,GEVNTADRHI0,GEVNTSIZ0,GEVNTCOUNT0")
print("D7T1_CANDIDATE2C_STATIC_SAFETY=PASS")
