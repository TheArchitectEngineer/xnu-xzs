#!/usr/bin/env bash
set -e

FASTBOOT_SERIAL="${FASTBOOT_SERIAL:-}"
ADB_SERIAL="${ADB_SERIAL:-}"

echo "[+] Waiting for fastboot device..."
while true; do
    if [ -n "$FASTBOOT_SERIAL" ]; then
        if fastboot devices | grep -q "$FASTBOOT_SERIAL"; then break; fi
    else
        DEV=$(fastboot devices 2>/dev/null | head -n 1 | awk '{print $1}')
        if [ -n "$DEV" ]; then FASTBOOT_SERIAL="$DEV"; break; fi
    fi
    sleep 1
done

echo "[+] Fastboot device detected: $FASTBOOT_SERIAL"
echo "[+] Booting TWRP to extract RAM pstore..."
fastboot -s "$FASTBOOT_SERIAL" boot artifacts/builds/twrp-kagura.img

echo "[+] Waiting for device in recovery mode..."
while ! adb devices | grep -q "recovery"; do
    sleep 2
done

if [ -z "$ADB_SERIAL" ]; then
    ADB_SERIAL=$(adb devices 2>/dev/null | grep "recovery" | head -n 1 | awk '{print $1}')
fi
echo "[+] TWRP recovery booted and ADB ready ($ADB_SERIAL)!"
mkdir -p artifacts/logs

echo "[+] Checking pstore files..."
adb -s "$ADB_SERIAL" shell "ls -la /sys/fs/pstore"

echo "[+] Dumping /sys/fs/pstore/console-ramoops..."
adb -s "$ADB_SERIAL" shell "cat /sys/fs/pstore/console-ramoops" > artifacts/logs/xnu-d51-extracted.log 2>/dev/null || true

if [[ -s artifacts/logs/xnu-d51-extracted.log ]]; then
    echo "[SUCCESS] Successfully extracted XNU console log ($(wc -c < artifacts/logs/xnu-d51-extracted.log) bytes)!"
    echo "=== TAIL OF EXTRACTED LOG ==="
    tail -n 40 artifacts/logs/xnu-d51-extracted.log
else
    echo "[!] console-ramoops empty, checking dmesg-ramoops..."
    adb -s "$ADB_SERIAL" shell "cat /sys/fs/pstore/dmesg-ramoops-0" > artifacts/logs/xnu-d51-dmesg.log 2>/dev/null || true
    if [[ -s artifacts/logs/xnu-d51-dmesg.log ]]; then
        echo "[SUCCESS] Found dmesg-ramoops-0!"
        tail -n 40 artifacts/logs/xnu-d51-dmesg.log
    fi
fi
