#!/bin/bash
set -euo pipefail

echo "============================================================"
echo "XZS EMPIRICAL RAM PERSISTENCE TEST"
echo "Target: Sony Xperia XZs (${FASTBOOT_SERIAL:-auto-detect})"
echo "============================================================"

WRITER_IMG="artifacts/builds/xzs-ram-test-writer.img"
DUMPER_IMG="artifacts/builds/xzs-debug-dumper.img"

if [ ! -f "${WRITER_IMG}" ]; then
    echo "Building writer image..."
    make -C src/xzs-ram-test all
fi

if [ ! -f "${DUMPER_IMG}" ]; then
    echo "Building dumper image..."
    make -C src/xzs-debug-dumper all
fi

echo ""
echo "--- STEP 1: Check Fastboot Connection ---"
DEVICE=$(fastboot devices | head -n 1 | awk '{print $1}')
if [ -z "${DEVICE}" ]; then
    echo "Error: No device detected in Fastboot mode."
    exit 1
fi
echo "Connected device: ${DEVICE}"

echo ""
echo "--- STEP 2: Booting RAM Test Writer Image ---"
echo "Booting ${WRITER_IMG}..."
fastboot boot "${WRITER_IMG}"

echo ""
echo "Canary values (MAGIC=0x585a5344, VAL=0x123456789abcdef0) are being written to DRAM (0x80060000)."
echo "Now, please bring the device back to FASTBOOT mode:"
echo "  1. Hold Volume Down + Power until device vibrates (or reconnect USB with Vol-Down held)."
echo "  2. The notification LED will turn BLUE."
echo ""
read -p "Press Enter once the device is back in FASTBOOT mode..."

echo ""
echo "Checking Fastboot re-connection..."
while true; do
    DEVICE=$(fastboot devices | head -n 1 | awk '{print $1}')
    if [ -n "${DEVICE}" ]; then
        echo "Device detected in Fastboot: ${DEVICE}"
        break
    fi
    sleep 1
    echo -n "."
done

echo ""
echo "--- STEP 3: Booting Debug Dumper to Read DRAM ---"
echo "Booting ${DUMPER_IMG}..."
fastboot boot "${DUMPER_IMG}"

echo ""
echo "--- STEP 4: Reading Telemetry via USB ---"
sleep 2
./scripts/read-debug-log.sh
