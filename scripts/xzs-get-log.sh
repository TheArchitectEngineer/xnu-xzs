#!/bin/bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(cd "${SCRIPT_DIR}/.." && pwd)"

echo "============================================================"
echo "XZS BARE-METAL TELEMETRY LOG EXTRACTOR (xzs-get-log)"
echo "============================================================"

OUT_DIR="${ROOT_DIR}/artifacts/logs"
mkdir -p "${OUT_DIR}"
TIMESTAMP=$(date +%Y%m%d-%H%M%S)
RAW_LOG="${OUT_DIR}/telemetry-raw-${TIMESTAMP}.log"
LATEST_LOG="${OUT_DIR}/telemetry-latest.log"

# Step 1: Check if dumper is already running over USB
MODEM=$(ls /dev/cu.usbmodem* /dev/cu.usbserial* 2>/dev/null | head -n 1 || true)

if [ -z "${MODEM}" ]; then
    echo "[1/3] Checking device state..."
    # Check if in recovery
    ADB_DEV=$(adb devices 2>/dev/null | grep -F "recovery" | head -n 1 | awk '{print $1}' || true)
    if [ -n "${ADB_DEV}" ]; then
        echo "Device is currently in TWRP recovery (${ADB_DEV}). Rebooting to bootloader..."
        adb reboot bootloader
        sleep 3
    fi

    DEV=""
    for i in {1..10}; do
        DEV=$(fastboot devices 2>/dev/null | head -n 1 | awk '{print $1}' || true)
        if [ -n "${DEV}" ]; then break; fi
        sleep 1
    done

    if [ -z "${DEV}" ]; then
        echo "ERROR: Target device not found in Fastboot or USB Modem mode!"
        echo "Please ensure device is in Fastboot (Blue LED) and run again."
        exit 1
    fi
    echo "Target device in Fastboot: ${DEV}"

    echo "[2/3] Booting bare-metal log dumper: artifacts/builds/xzs-log-dumper.img..."
    fastboot boot "${ROOT_DIR}/artifacts/builds/xzs-log-dumper.img"

    echo "Waiting for USB CDC ACM Telemetry Channel (/dev/cu.usbmodem*)..."
    for i in {1..20}; do
        MODEM=$(ls /dev/cu.usbmodem* /dev/cu.usbserial* 2>/dev/null | head -n 1 || true)
        if [ -n "${MODEM}" ]; then
            echo "USB Telemetry Channel connected: ${MODEM} (+${i}s)"
            break
        fi
        sleep 1
        echo -n "."
    done
    echo ""
fi

if [ -z "${MODEM}" ]; then
    echo "ERROR: /dev/cu.usbmodem* did not appear within timeout."
    exit 1
fi

echo "[3/3] Reading telemetry stream from ${MODEM}..."

# Configure serial port
stty -f "${MODEM}" 115200 cs8 -cstopb -parenb raw 2>/dev/null || true

# Capture stream using python
python3 -c '
import sys, time, os

modem = sys.argv[1]
raw_out = sys.argv[2]
latest_out = sys.argv[3]

collected = bytearray()
t_end = time.time() + 6.0

try:
    fd = os.open(modem, os.O_RDONLY | os.O_NONBLOCK)
    while time.time() < t_end:
        try:
            chunk = os.read(fd, 2048)
            if chunk:
                collected.extend(chunk)
                if b"============================================================" in chunk and len(collected) > 200:
                    # Let it complete the packet
                    time.sleep(0.3)
                    try:
                        extra = os.read(fd, 4096)
                        if extra: collected.extend(extra)
                    except: pass
                    break
            else:
                time.sleep(0.05)
        except BlockingIOError:
            time.sleep(0.05)
        except Exception as e:
            time.sleep(0.05)
    os.close(fd)
except Exception as e:
    print(f"Serial open error: {e}")

if collected:
    with open(raw_out, "wb") as f:
        f.write(collected)
    with open(latest_out, "wb") as f:
        f.write(collected)
' "${MODEM}" "${RAW_LOG}" "${LATEST_LOG}"

if [ ! -s "${LATEST_LOG}" ]; then
    echo "Warning: No telemetry data captured."
    exit 1
fi

echo ""
echo "============================================================"
echo "EXTRACTED TELEMETRY STREAM"
echo "============================================================"
cat "${LATEST_LOG}"
echo "============================================================"

# Parse fields and decode
python3 -c '
import sys, re, subprocess

with open(sys.argv[1], "r", errors="ignore") as f:
    text = f.read()

# Extract stages
stage_m = re.search(r"Last Stage:\s*(0x[0-9a-fA-F]+)\s*\[([^\]]+)\]", text)
last_stage_hex = stage_m.group(1) if stage_m else None
last_stage_name = stage_m.group(2) if stage_m else None

esr_m = re.search(r"Last ESR_EL1:\s*(0x[0-9a-fA-F]+)", text)
last_esr = esr_m.group(1) if esr_m else None

elr_m = re.search(r"Last ELR_EL1:\s*(0x[0-9a-fA-F]+)", text)
last_elr = elr_m.group(1) if elr_m else None

far_m = re.search(r"Last FAR_EL1:\s*(0x[0-9a-fA-F]+)", text)
last_far = far_m.group(1) if far_m else None

spsr_m = re.search(r"Last SPSR_EL1:\s*(0x[0-9a-fA-F]+)", text)
last_spsr = spsr_m.group(1) if spsr_m else None

print("\n--- DIAGNOSTIC DECODING ---")
if last_stage_hex:
    print(f"Verified Last Stage: {last_stage_hex} ({last_stage_name})")

if last_esr and int(last_esr, 16) != 0:
    print(f"\nDecoding ESR: {last_esr}")
    subprocess.run(["python3", "scripts/decode_esr.py", last_esr])

if last_elr and int(last_elr, 16) != 0:
    print(f"\nSymbolicating ELR: {last_elr}")
    subprocess.run(["python3", "scripts/symbolicate_elr.py", last_elr])

if last_esr and int(last_esr, 16) != 0:
    subprocess.run([
        "python3", "scripts/generate_failure_report.py",
        "--marker", last_stage_name or "Unknown",
        "--esr", last_esr,
        "--elr", last_elr or "",
        "--far", last_far or "",
        "--spsr", last_spsr or ""
    ])
' "${LATEST_LOG}"

echo ""
echo "Telemetry successfully extracted and saved to: ${RAW_LOG}"
echo "============================================================"
