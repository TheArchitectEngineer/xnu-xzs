#!/usr/bin/env python3
"""Static safety gate for the D7-T1 Candidate-2A read-only USB snapshot."""

from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
SOURCE = ROOT / "src/xnu/pexpert/arm/xzs_usb.c"
STATUS = ROOT / "src/xnu/osfmk/arm64/status.c"
HEADER = ROOT / "src/xnu/pexpert/arm/xzs_usb.h"
MIRROR_HEADER = ROOT / "src/xnu/pexpert/pexpert/arm/xzs_usb.h"


def fail(message: str) -> None:
    print(f"D7T1_CANDIDATE2A_VERIFIER=FAIL: {message}")
    raise SystemExit(1)


def function_body(text: str, name: str) -> str:
    marker = f"int {name}(void)"
    start = text.find(marker)
    if start < 0:
        fail(f"cannot locate {name}")
    brace = text.find("{", start)
    depth = 0
    for index in range(brace, len(text)):
        if text[index] == "{":
            depth += 1
        elif text[index] == "}":
            depth -= 1
            if depth == 0:
                return text[start:index + 1]
    fail(f"unterminated {name}")
    return ""


source = SOURCE.read_text()
status = STATUS.read_text()
header = HEADER.read_text()
mirror_header = MIRROR_HEADER.read_text()
init = function_body(source, "xzs_usb_init")

for required in (
    "D740/2A00 Candidate-2A read-only snapshot entered",
    "D740/2A01 DWC3 read-only snapshot complete",
    "D740/2A02 QSCRATCH read-only snapshot complete",
    "D740/2A03 QUSB2/GCC read-only snapshot complete",
    "D740/2A04 snapshot decoding ready",
    "D740/2A09 normal boot continuing",
    "DWC3_GSTS",
    "DWC3_GUSB2PHYCFG0",
    "DWC3_GUSB3PIPECTL0",
    "DWC3_OSTS",
    "QSCRATCH_HS_PHY_CTRL",
    "QSCRATCH_SS_PHY_CTRL",
    "QSCRATCH_PWR_EVENT_IRQ_STAT",
    "QUSB2PHY_PORT_UTMI_STATUS",
    "GCC_QUSB2PHY_PRIM_BCR",
):
    if required not in init and required not in header:
        fail(f"missing source-audited snapshot item: {required}")

for forbidden in (
    "dwc3_write32(",
    "xzs_usb_poll_events(",
    "dwc3_configure_endpoints(",
    "dwc3_start_transfer(",
    "dwc3_ep_cmd(",
    "memset(s_event_buffer",
):
    if forbidden in init:
        fail(f"forbidden USB mutation path in Candidate-2A: {forbidden}")

for required in (
    "CANDIDATE2A_READ_ONLY=yes",
    "DWC3_REGISTER_WRITES=0",
    "QSCRATCH_REGISTER_WRITES=0",
    "QUSB2_REGISTER_WRITES=0",
    "GCC_REGISTER_WRITES=0",
    "USB_STATE_MUTATED=no",
):
    if required not in status:
        fail(f"missing telemetry contract: {required}")

if header != mirror_header:
    fail("mirrored xzs_usb headers differ")

print("D7T1_CANDIDATE2A_STATIC_READ_ONLY=PASS")
print("D7T1_CANDIDATE2A_VERIFIER=PASS")
