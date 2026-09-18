#!/bin/bash
set -euo pipefail

echo "============================================================"
echo "WAITING FOR XZS FASTBOOT CONNECTION..."
echo "============================================================"

while true; do
    DEV=$(fastboot devices 2>/dev/null | head -n 1 | awk '{print $1}')
    if [ -n "$DEV" ]; then
        echo ">>> FASTBOOT DETECTED: ${DEV} <<<"
        break
    fi
    sleep 1
    echo -n "."
done
echo ""

echo "[1/2] Booting TWRP Kagura to extract pstore..."
fastboot boot artifacts/builds/twrp-kagura.img

echo "[2/2] Waiting for TWRP Recovery ADB (timeout 45s)..."
TWRP_DEV=""
for i in {1..45}; do
    sleep 1
    ADB_DEV=$(adb devices 2>/dev/null | grep -F "recovery" | head -n 1 | awk '{print $1}' || true)
    if [ -n "$ADB_DEV" ]; then
        echo "TWRP Recovery active: $ADB_DEV at +${i}s"
        TWRP_DEV="$ADB_DEV"
        break
    fi
    echo -n "."
done
echo ""

if [ -z "$TWRP_DEV" ]; then
    echo "ERROR: TWRP recovery did not appear!"
    exit 1
fi

sleep 2
echo "============================================================"
echo "EXTRACTING PSTORE TELEMETRY"
echo "============================================================"
mkdir -p artifacts/logs/pstore
adb shell "ls -la /sys/fs/pstore"

echo "--- Pulling all pstore files ---"
adb pull /sys/fs/pstore/ artifacts/logs/pstore/ || true

echo "--- console-ramoops ---"
adb shell "cat /sys/fs/pstore/console-ramoops 2>/dev/null" | tee artifacts/logs/xnu-console-extracted.log || true

echo "--- dmesg-ramoops-0 ---"
adb shell "cat /sys/fs/pstore/dmesg-ramoops-0 2>/dev/null" | tee artifacts/logs/xnu-dmesg-extracted.log || true

echo "--- TrustZone Reset & Boot Status ---"
adb shell "cat /sys/kernel/debug/tzdbg/reset 2>/dev/null; echo '--- BOOT ---'; cat /sys/kernel/debug/tzdbg/boot 2>/dev/null" | tee artifacts/logs/tzdbg-status.log || true

adb shell "dmesg" > artifacts/logs/twrp-dmesg-full.log 2>&1 || true

echo "============================================================"
echo "EXTRACTION COMPLETE"
echo "============================================================"
