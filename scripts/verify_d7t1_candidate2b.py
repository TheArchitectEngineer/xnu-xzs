#!/usr/bin/env python3
"""Static safety gate for D7-T1 Candidate-2B DCFG-only normalization."""

from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
SOURCE = ROOT / "src/xnu/pexpert/arm/xzs_usb.c"
STATUS = ROOT / "src/xnu/osfmk/arm64/status.c"
HEADER = ROOT / "src/xnu/pexpert/arm/xzs_usb.h"
MIRROR_HEADER = ROOT / "src/xnu/pexpert/pexpert/arm/xzs_usb.h"


def fail(message: str) -> None:
    print(f"D7T1_CANDIDATE2B_VERIFIER=FAIL: {message}")
    raise SystemExit(1)


def body(text: str, function: str) -> str:
    start = text.find(f"int {function}(void)")
    if start < 0:
        fail(f"cannot locate {function}")
    brace = text.find("{", start)
    depth = 0
    for index in range(brace, len(text)):
        if text[index] == "{":
            depth += 1
        elif text[index] == "}":
            depth -= 1
            if depth == 0:
                return text[start:index + 1]
    fail(f"unterminated {function}")
    return ""


source = SOURCE.read_text()
status = STATUS.read_text()
header = HEADER.read_text()
init = body(source, "xzs_usb_init")

for marker in (
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
):
    if marker not in init:
        fail(f"missing checkpoint marker: {marker}")

for required in (
    "DWC3_DCFG_SPEED_MASK",
    "DWC3_DCFG_DEVADDR_MASK",
    "DWC3_DCTL_RUN_STOP",
    "DWC3_DSTS_DEVCTRLHLT",
    "g_xzs_usb_dcfg_written = g_xzs_usb_dcfg_before &",
    "~(DWC3_DCFG_SPEED_MASK | DWC3_DCFG_DEVADDR_MASK)",
    "g_xzs_usb_candidate2b_precondition_run_stop_0",
    "g_xzs_usb_candidate2b_precondition_devctrlhlt_1",
    "xzs_mmio_read8(s_qusb2_phy_base, QUSB2PHY_PLL_STATUS)",
):
    if required not in init and required not in header:
        fail(f"missing DCFG normalization safety element: {required}")

if init.count("dwc3_write32(DWC3_DCFG, g_xzs_usb_dcfg_written)") != 1:
    fail("Candidate-2B must have exactly one DWC3_DCFG write call")

for forbidden in (
    "dwc3_write32(DWC3_GCTL",
    "dwc3_write32(DWC3_DCTL",
    "dwc3_write32(DWC3_DEVTEN",
    "dwc3_write32(DWC3_GUSB2PHYCFG",
    "dwc3_write32(DWC3_GUSB3PIPECTL",
    "dwc3_write32(DWC3_GEVNT",
    "dwc3_write32(DWC3_DALEPENA",
    "dwc3_write32(DWC3_DEPCMD",
    "dwc3_ep_cmd(",
    "dwc3_start_transfer(",
    "dwc3_configure_endpoints(",
    "xzs_usb_poll_events(",
    "memset(s_event_buffer",
):
    if forbidden in init:
        fail(f"forbidden Candidate-2B mutation path: {forbidden}")

for contract in (
    "CANDIDATE2B_DWC3_WRITE_TARGET=DCFG",
    "QSCRATCH_REGISTER_WRITES=0",
    "QUSB2_REGISTER_WRITES=0",
    "GCC_REGISTER_WRITES=0",
    "EVENT_BUFFER_UNCHANGED=",
    "EP0_UNTOUCHED=yes",
    "D7_T1_COMPLETE=no",
    "D7_T1_SEALED=no",
):
    if contract not in status:
        fail(f"missing telemetry contract: {contract}")

if header != MIRROR_HEADER.read_text():
    fail("mirrored xzs_usb headers differ")

print("D7T1_CANDIDATE2B_DCFG_WRITE_COUNT=1")
print("D7T1_CANDIDATE2B_STATIC_SAFETY=PASS")
print("D7T1_CANDIDATE2B_VERIFIER=PASS")
