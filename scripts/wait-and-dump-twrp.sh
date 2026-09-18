#!/bin/bash
set -e

FASTBOOT_SERIAL="${FASTBOOT_SERIAL:-}"
ADB_SERIAL="${ADB_SERIAL:-}"

echo "=== ĐANG CHỜ XPERIA XZS VÀO FASTBOOT ==="
while true; do
    if [ -n "$FASTBOOT_SERIAL" ]; then
        DEV=$(fastboot devices 2>/dev/null | grep -F "$FASTBOOT_SERIAL" | awk '{print $1}' || true)
    else
        DEV=$(fastboot devices 2>/dev/null | head -n 1 | awk '{print $1}' || true)
        if [ -n "$DEV" ]; then FASTBOOT_SERIAL="$DEV"; fi
    fi
    if [ -n "$DEV" ]; then
        echo ">>> ĐÃ PHÁT HIỆN FASTBOOT: $DEV <<<"
        break
    fi
    sleep 0.5
done

echo ">>> Khởi động TWRP Kagura để trích xuất RAM..."
fastboot -s "$FASTBOOT_SERIAL" boot artifacts/builds/twrp-kagura.img

echo "=== CHỜ TWRP RECOVERY ADB ==="
for i in $(seq 1 40); do
    sleep 1
    if [ -n "$ADB_SERIAL" ]; then
        ADB_DEV=$(adb devices 2>/dev/null | grep -F "$ADB_SERIAL" | grep -F "recovery" | awk '{print $1}' || true)
    else
        ADB_DEV=$(adb devices 2>/dev/null | grep -F "recovery" | head -n 1 | awk '{print $1}' || true)
        if [ -n "$ADB_DEV" ]; then ADB_SERIAL="$ADB_DEV"; fi
    fi
    if [ -n "$ADB_DEV" ]; then
        echo ">>> TWRP đã sẵn sàng: $ADB_DEV tại +${i}s <<<"
        break
    fi
done

sleep 3
mkdir -p artifacts/logs/pstore_new
echo "=== DANH SÁCH PSTORE ==="
adb -s "$ADB_SERIAL" shell "ls -la /sys/fs/pstore" || true

echo "=== NỘI DUNG CONSOLE RAMOOPS ==="
adb -s "$ADB_SERIAL" shell "cat /sys/fs/pstore/console-ramoops" | tee artifacts/logs/pstore_new/console-ramoops.log || true

echo "=== NỘI DUNG DMESG RAMOOPS ==="
adb -s "$ADB_SERIAL" shell "cat /sys/fs/pstore/dmesg-ramoops-0" | tee artifacts/logs/pstore_new/dmesg-ramoops.log || true

echo "=== HOÀN TẤT TRÍCH XUẤT ==="
