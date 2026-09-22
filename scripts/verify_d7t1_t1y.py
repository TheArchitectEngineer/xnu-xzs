#!/usr/bin/env python3
"""Static safety gate for D7-T1 T1-Y EP0-only physical enumeration."""

from pathlib import Path
import subprocess


ROOT = Path(__file__).resolve().parents[1]
SOURCE = ROOT / "src/xnu/pexpert/arm/xzs_usb.c"
HEADER = ROOT / "src/xnu/pexpert/arm/xzs_usb.h"
MIRROR = ROOT / "src/xnu/pexpert/pexpert/arm/xzs_usb.h"
STATUS = ROOT / "src/xnu/osfmk/arm64/status.c"
BASE = "ff9c9d1"


def fail(message: str) -> None:
    print(f"D7T1_T1Y_STATIC_GATE=FAIL: {message}")
    raise SystemExit(1)


def body(text: str, signature: str) -> str:
    start = text.find(signature)
    if start < 0:
        fail(f"missing function {signature}")
    brace = text.find("{", start)
    depth = 0
    for pos in range(brace, len(text)):
        if text[pos] == "{":
            depth += 1
        elif text[pos] == "}":
            depth -= 1
            if depth == 0:
                return text[start : pos + 1]
    fail(f"unterminated function {signature}")


source = SOURCE.read_text()
header = HEADER.read_text()
status = STATUS.read_text()
if header != MIRROR.read_text():
    fail("mirrored xzs_usb.h files differ")

q0 = source.find("#if 0 /* XZS_T1Y_QUARANTINED_LEGACY_BULK_TTY */")
q1 = source.find("#endif /* XZS_T1Y_QUARANTINED_LEGACY_BULK_TTY */")
if q0 < 0 or q1 < q0:
    fail("legacy Bulk/TTY implementation is not quarantined")
live = source[:q0] + source[q1 + len("#endif /* XZS_T1Y_QUARANTINED_LEGACY_BULK_TTY */") :]

for checkpoint in (
    "D740/Y00", "D740/Y10", "D740/Y11", "D740/Y12", "D740/Y20",
    "D740/Y21", "D740/Y30", "D740/Y31", "D740/Y40", "D740/Y41",
    "D740/Y42", "D740/Y50", "D740/Y51", "D740/Y52", "D740/Y60",
    "D740/Y61", "D740/Y70", "D740/Y71", "D740/Y80", "D740/Y81",
    "D740/Y90", "D740/Y98", "D740/Y99",
):
    if checkpoint not in live:
        fail(f"missing checkpoint {checkpoint}")

if "#define DWC3_DALEPENA                 0xC720" not in header:
    fail("DALEPENA is not the source-audited 0xC720 offset")
if "DWC3_DEVICE_EVENT_RESET       0x01u" not in header:
    fail("USB Reset event code is not 1")
if "DWC3_DEVICE_EVENT_CONNECT_DONE 0x02u" not in header:
    fail("ConnectDone event code is not 2")
if "if ((event & 1u) == 0)" not in live:
    fail("endpoint/device event discriminator is reversed")

reset = body(live, "xzs_t1y_ep0_only_reset(void)")
setup = body(live, "xzs_t1y_handle_setup(void)")
connect = body(live, "xzs_t1y_connect(void)")
irq = body(live, "xzs_t1y_drain_event_buffer(void)")

for forbidden in (
    "dwc3_configure_endpoints", "DWC3_PHYS_EP_BULK_OUT",
    "DWC3_PHYS_EP_BULK_IN", "dwc3_submit_bulk_out", "cons_cinput",
    "thread_call_enter(s_usb_rx_tty_call)",
):
    if forbidden in reset or forbidden in setup or forbidden in connect:
        fail(f"control path reaches forbidden operation {forbidden}")

if live.count("dwc3_write32(DWC3_DCTL") != 1:
    fail("live T1-Y source must have exactly one DCTL write call site")
if connect.count("dwc3_write32(DWC3_DCTL") != 1:
    fail("RUN_STOP transition is not isolated in xzs_t1y_connect")
if "DWC3_DCTL_RUN_STOP" not in connect:
    fail("RUN_STOP bit is not used by connect transition")
if "DWC3_GEVNTSIZ_INTMASK" not in connect or "DWC3_DEVTEN_T1Y_MASK" not in connect:
    fail("event delivery activation is incomplete")
if "consumed_events * sizeof(uint32_t)" not in irq:
    fail("IRQ does not acknowledge exactly consumed event bytes")
if "XZS_T1Y_IRQ_EVENT_BUDGET" not in irq or "thread_call_enter" not in irq:
    fail("IRQ drain is not bounded/deferred")
if "DEPCMD_STATUS(raw)" not in live or "DEPCMD_CMDACT" not in live:
    fail("new EP0 command completion/status gate is absent")

for token in (
    "USB_RESET_CALLS_BULK_SETUP=no", "SET_CONFIGURATION_CALLS_BULK_SETUP=no",
    "BULK_CONFIGURED=no", "TTY_BRIDGE_ACTIVE=no", "CONS_CINPUT_CALL_COUNT=0",
    "D7_M4_REGRESSION=", "T1_Y_TARGET_COMPLETE=", "D7_T1_COMPLETE=no",
    "D7_T1_SEALED=no",
):
    if token not in status:
        fail(f"missing T1-Y telemetry {token}")

changed = subprocess.check_output(
    ["git", "diff", "--name-only", BASE, "--"], cwd=ROOT, text=True
).splitlines()
sealed_paths = (
    "src/xnu/pexpert/arm/pe_serial.c",
    "src/xnu/osfmk/console/serial_console.c",
    "src/xnu/bsd/kern/tty.c",
    "src/xnu/bsd/kern/tty_tty.c",
)
for path in sealed_paths:
    if path in changed:
        fail(f"sealed D7-M4 source modified: {path}")

print("T1Y_EP0_ONLY_DISPATCH=yes")
print("USB_RESET_CALLS_BULK_SETUP=no")
print("SET_CONFIGURATION_CALLS_BULK_SETUP=no")
print("BULK_ENDPOINT_WRITE_COUNT=0")
print("TTY_BRIDGE_CALL_COUNT=0")
print("CONS_CINPUT_CALL_COUNT=0")
print("D7_M4_SOURCE_MODIFIED=no")
print("DCTL_RUN_STOP_WRITE_COUNT=1")
print("D7T1_T1Y_STATIC_GATE=PASS")
