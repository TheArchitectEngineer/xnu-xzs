#!/usr/bin/env bash
set -e

echo "[+] Waiting for fastboot device BH905SX976..."
while ! fastboot devices | grep -q "BH905SX976"; do
    sleep 1
done

echo "[+] Device detected in fastboot mode!"
echo "[+] Booting TWRP to extract RAM pstore..."
fastboot -s BH905SX976 boot artifacts/builds/twrp-kagura.img

echo "[+] Waiting for device in recovery mode..."
while ! adb devices | grep -q "recovery"; do
    sleep 2
done

echo "[+] TWRP recovery booted and ADB ready!"
mkdir -p artifacts/logs

echo "[+] Checking pstore files..."
adb -s BH905SX976 shell "ls -la /sys/fs/pstore"

echo "[+] Dumping /sys/fs/pstore/console-ramoops..."
adb -s BH905SX976 shell "cat /sys/fs/pstore/console-ramoops" > artifacts/logs/xnu-d51-extracted.log 2>/dev/null || true

if [[ -s artifacts/logs/xnu-d51-extracted.log ]]; then
    echo "[SUCCESS] Successfully extracted XNU console log ($(wc -c < artifacts/logs/xnu-d51-extracted.log) bytes)!"
    echo "=== TAIL OF EXTRACTED LOG ==="
    tail -n 40 artifacts/logs/xnu-d51-extracted.log
else
    echo "[!] console-ramoops empty, checking dmesg-ramoops..."
    adb -s BH905SX976 shell "cat /sys/fs/pstore/dmesg-ramoops-0" > artifacts/logs/xnu-d51-dmesg.log 2>/dev/null || true
    if [[ -s artifacts/logs/xnu-d51-dmesg.log ]]; then
        echo "[SUCCESS] Found dmesg-ramoops-0!"
        tail -n 40 artifacts/logs/xnu-d51-dmesg.log
    fi
fi
