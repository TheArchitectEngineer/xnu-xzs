#!/bin/bash

echo "============================================================"
echo "XZS AUTO-EXTRACT TELEMETRY (TWRP + PSTORE)"
echo "============================================================"

echo "Waiting for Sony Xperia XZs in Fastboot (BLUE LED)..."
while true; do
    DEV=$(fastboot devices 2>/dev/null | head -n 1 | awk '{print $1}')
    if [ -n "$DEV" ]; then
        echo "Detected Fastboot device: $DEV"
        break
    fi
    sleep 1
done

echo "Booting TWRP dumper (artifacts/builds/twrp-kagura.img)..."
fastboot -s BH905SX976 boot artifacts/builds/twrp-kagura.img

echo "Waiting for TWRP ADB recovery environment..."
while true; do
    ADB_DEV=$(adb devices 2>/dev/null | grep -F "recovery" | head -n 1 | awk '{print $1}' || true)
    if [ -n "$ADB_DEV" ]; then
        echo "Detected TWRP Recovery: $ADB_DEV"
        break
    fi
    sleep 1
done

sleep 3
echo "=== EXTRACTING PSTORE RAMOOPS TELEMETRY ==="
mkdir -p artifacts/logs
adb -s BH905SX976 shell "ls -la /sys/fs/pstore"
adb -s BH905SX976 shell "cat /sys/fs/pstore/dmesg-ramoops-0 2>/dev/null" | tee artifacts/logs/dmesg-ramoops.log
adb -s BH905SX976 shell "cat /sys/fs/pstore/console-ramoops 2>/dev/null" | tee artifacts/logs/console-ramoops.log
echo ""
echo "=== TELEMETRY EXTRACTION COMPLETE ==="
ls -lh artifacts/logs/
