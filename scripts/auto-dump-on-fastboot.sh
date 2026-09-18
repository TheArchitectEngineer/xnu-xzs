#!/bin/bash

echo "============================================================"
echo "XZS AUTO-EXTRACT TELEMETRY (TWRP + PSTORE)"
echo "============================================================"

FASTBOOT_SERIAL="${FASTBOOT_SERIAL:-}"
ADB_SERIAL="${ADB_SERIAL:-}"

echo "Waiting for Sony Xperia XZs in Fastboot (BLUE LED)..."
while true; do
    if [ -n "$FASTBOOT_SERIAL" ]; then
        DEV=$(fastboot devices 2>/dev/null | grep -F "$FASTBOOT_SERIAL" | awk '{print $1}' || true)
    else
        DEV=$(fastboot devices 2>/dev/null | head -n 1 | awk '{print $1}')
        if [ -n "$DEV" ]; then FASTBOOT_SERIAL="$DEV"; fi
    fi
    if [ -n "$DEV" ]; then
        echo "Detected Fastboot device: $DEV"
        break
    fi
    sleep 1
done

echo "Booting TWRP dumper (artifacts/builds/twrp-kagura.img)..."
fastboot -s "$FASTBOOT_SERIAL" boot artifacts/builds/twrp-kagura.img

echo "Waiting for TWRP ADB recovery environment..."
while true; do
    if [ -n "$ADB_SERIAL" ]; then
        ADB_DEV=$(adb devices 2>/dev/null | grep -F "$ADB_SERIAL" | grep -F "recovery" | awk '{print $1}' || true)
    else
        ADB_DEV=$(adb devices 2>/dev/null | grep -F "recovery" | head -n 1 | awk '{print $1}' || true)
        if [ -n "$ADB_DEV" ]; then ADB_SERIAL="$ADB_DEV"; fi
    fi
    if [ -n "$ADB_DEV" ]; then
        echo "Detected TWRP Recovery: $ADB_DEV"
        break
    fi
    sleep 1
done

sleep 3
echo "=== EXTRACTING PSTORE RAMOOPS TELEMETRY ==="
mkdir -p artifacts/logs
adb -s "$ADB_SERIAL" shell "ls -la /sys/fs/pstore"
adb -s "$ADB_SERIAL" shell "cat /sys/fs/pstore/dmesg-ramoops-0 2>/dev/null" | tee artifacts/logs/dmesg-ramoops.log
adb -s "$ADB_SERIAL" shell "cat /sys/fs/pstore/console-ramoops 2>/dev/null" | tee artifacts/logs/console-ramoops.log
echo ""
echo "=== TELEMETRY EXTRACTION COMPLETE ==="
ls -lh artifacts/logs/
