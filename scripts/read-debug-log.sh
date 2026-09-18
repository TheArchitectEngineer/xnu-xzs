#!/bin/bash
set -euo pipefail

echo "============================================================"
echo "XZS TELEMETRY USB READER (macOS CDC ACM)"
echo "============================================================"

OUT_DIR="artifacts/logs"
mkdir -p "${OUT_DIR}"
TIMESTAMP=$(date +%Y%m%d-%H%M%S)
LOG_FILE="${OUT_DIR}/telemetry-${TIMESTAMP}.log"

echo "Waiting for Sony Xperia XZs USB modem (/dev/cu.usbmodem*)..."

TIMEOUT=30
MODEM=""
for ((i=1; i<=TIMEOUT; i++)); do
    MODEM=$(ls /dev/cu.usbmodem* /dev/cu.usbserial* 2>/dev/null | head -n 1 || true)
    if [ -n "${MODEM}" ]; then
        echo "Found USB Telemetry Channel: ${MODEM}"
        break
    fi
    sleep 1
    echo -n "."
done

echo ""

if [ -z "${MODEM}" ]; then
    echo "Error: No /dev/cu.usbmodem* device detected within ${TIMEOUT}s."
    echo "Ensure:"
    echo "  1. Device was booted with: fastboot boot artifacts/builds/xzs-debug-dumper.img"
    echo "  2. USB Type-C cable is connected securely to Mac."
    exit 1
fi

echo "Reading diagnostic stream from ${MODEM}..."
echo "--- BEGIN TELEMETRY ---"

# Read telemetry packet from serial port
# Stty configure raw mode
stty -f "${MODEM}" 115200 cs8 -cstopb -parenb raw 2>/dev/null || true

# Capture up to 5 seconds of stream
python3 -c '
import sys, time

modem = sys.argv[1]
outfile = sys.argv[2]

try:
    with open(modem, "rb", buffering=0) as f, open(outfile, "wb") as out:
        t_end = time.time() + 5.0
        seen_banner = False
        while time.time() < t_end:
            chunk = f.read(512)
            if chunk:
                sys.stdout.buffer.write(chunk)
                sys.stdout.buffer.flush()
                out.write(chunk)
                out.flush()
                if b"--- END OF LOG BUFFER ---" in chunk or b"RAM_PERSISTS_ACROSS_FASTBOOT = NO" in chunk:
                    break
            else:
                time.sleep(0.05)
except Exception as e:
    print(f"\nRead error: {e}")
' "${MODEM}" "${LOG_FILE}"

echo "--- END TELEMETRY ---"
echo "Telemetry saved to: ${LOG_FILE}"
ls -lh "${LOG_FILE}"
