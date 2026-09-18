#!/bin/bash
set -euo pipefail

echo "=== 1. Booting xzs-xnu-boot.img ==="
fastboot boot artifacts/builds/xzs-xnu-boot.img

echo "=== 2. Waiting for automated warm reset back to Fastboot ==="
for i in $(seq 1 15); do
    sleep 1
    DEV=$(fastboot devices 2>/dev/null | head -n 1 | awk '{print $1}')
    if [ -n "$DEV" ]; then
        echo "Detected Fastboot device: $DEV at +${i}s"
        break
    fi
done

if [ -z "${DEV:-}" ]; then
    echo "Device did not reappear in Fastboot within 15s."
    exit 1
fi

echo "=== 3. Booting TWRP dumper ==="
fastboot boot artifacts/builds/twrp-kagura.img

echo "=== 4. Waiting for ADB recovery ==="
for i in $(seq 1 20); do
    sleep 1
    ADB_DEV=$(adb devices 2>/dev/null | grep "recovery" | head -n 1 | awk '{print $1}')
    if [ -n "$ADB_DEV" ]; then
        echo "Detected TWRP Recovery: $ADB_DEV at +${i}s"
        break
    fi
done

sleep 2
echo "=== 5. Extracting Telemetry from persistent pstore ==="
mkdir -p artifacts/logs
adb shell "cat /sys/fs/pstore/dmesg-ramoops-0 2>/dev/null" | tee artifacts/logs/dmesg-ramoops.log
adb shell "cat /sys/fs/pstore/console-ramoops 2>/dev/null" | tee artifacts/logs/console-ramoops.log

echo "=== Done! Log size: ==="
ls -lh artifacts/logs/
