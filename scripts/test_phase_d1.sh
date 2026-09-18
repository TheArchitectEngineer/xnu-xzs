#!/usr/bin/env bash
set -e

FASTBOOT_SERIAL="${FASTBOOT_SERIAL:-}"
ADB_SERIAL="${ADB_SERIAL:-}"

echo "=== PHASE D1 BOOT & EXTRACTION ==="
echo "[1/4] Checking device connection..."
if adb devices | grep -q "recovery"; then
    echo "[+] Device is in recovery. Rebooting to bootloader (fastboot)..."
    if [ -n "$ADB_SERIAL" ]; then
        adb -s "$ADB_SERIAL" reboot bootloader
    else
        adb reboot bootloader
    fi
    sleep 3
fi

echo "[2/4] Waiting for fastboot mode..."
while true; do
    if [ -n "$FASTBOOT_SERIAL" ]; then
        if fastboot devices | grep -q "$FASTBOOT_SERIAL"; then break; fi
    else
        DEV=$(fastboot devices 2>/dev/null | head -n 1 | awk '{print $1}')
        if [ -n "$DEV" ]; then FASTBOOT_SERIAL="$DEV"; break; fi
    fi
    sleep 1
done
echo "[+] Detected device in fastboot mode ($FASTBOOT_SERIAL)!"

echo "[3/4] Booting new XNU kernel (SHA256: $(shasum -a 256 artifacts/builds/xzs-xnu-boot.img | awk '{print $1}'))..."
fastboot -s "$FASTBOOT_SERIAL" boot artifacts/builds/xzs-xnu-boot.img

echo "[+] XNU kernel is now executing on hardware!"
echo "[+] Expected execution duration: ~10-15 seconds."
echo "[+] When finished, XNU will flush RAM and warm-reset to Fastboot (Solid Blue LED)."
echo "=========================================================================="
echo "👉 KHI ĐÈN LED ĐỔI SANG MÀU XANH DƯƠNG: HÃY RÚT CÁP VÀ CẮM LẠI NGAY!"
echo "   (Tuyệt đối không bấm nút nguồn hay nút âm lượng)"
echo "=========================================================================="

echo "[4/4] Waiting for fastboot re-enumeration (waiting for cable replug)..."
while true; do
    if fastboot devices | grep -q "$FASTBOOT_SERIAL"; then break; fi
    sleep 1
done
echo "[+] Fastboot detected after warm reset!"

echo "[+] Booting TWRP to extract RAM logs..."
fastboot -s "$FASTBOOT_SERIAL" boot artifacts/builds/twrp-kagura.img

echo "[+] Waiting for TWRP recovery..."
while ! adb devices | grep -q "recovery"; do
    sleep 1
done
if [ -z "$ADB_SERIAL" ]; then
    ADB_SERIAL=$(adb devices 2>/dev/null | grep "recovery" | head -n 1 | awk '{print $1}')
fi
echo "[+] TWRP recovery is up ($ADB_SERIAL)! Checking pstore in DRAM..."
sleep 2

adb -s "$ADB_SERIAL" shell "ls -la /sys/fs/pstore"
adb -s "$ADB_SERIAL" shell "cat /sys/fs/pstore/console-ramoops" > artifacts/logs/xnu-d51-terminal.log 2>/dev/null || true

if [[ -s artifacts/logs/xnu-d51-terminal.log ]]; then
    echo "=========================================================================="
    echo "✅ [SUCCESS] EXTRACTED XNU BOOT LOG FROM RAM ($(wc -c < artifacts/logs/xnu-d51-terminal.log) bytes):"
    echo "=========================================================================="
    cat artifacts/logs/xnu-d51-terminal.log
else
    echo "[!] Checking dmesg-ramoops..."
    adb -s "$ADB_SERIAL" shell "cat /sys/fs/pstore/dmesg-ramoops-0" > artifacts/logs/xnu-d51-terminal-dmesg.log 2>/dev/null || true
    if [[ -s artifacts/logs/xnu-d51-terminal-dmesg.log ]]; then
        cat artifacts/logs/xnu-d51-terminal-dmesg.log
    fi
fi
