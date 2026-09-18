#!/bin/bash
set -e

echo "=== ĐANG CHỜ XPERIA XZS VÀO FASTBOOT ==="
while true; do
    DEV=$(fastboot devices 2>/dev/null | grep -F "BH905SX976" | awk '{print $1}' || true)
    if [ -n "$DEV" ]; then
        echo ">>> ĐÃ PHÁT HIỆN FASTBOOT: $DEV <<<"
        break
    fi
    sleep 0.5
done

echo ">>> Khởi động TWRP Kagura để trích xuất RAM..."
fastboot -s BH905SX976 boot artifacts/builds/twrp-kagura.img

echo "=== CHỜ TWRP RECOVERY ADB ==="
for i in $(seq 1 40); do
    sleep 1
    ADB_DEV=$(adb devices 2>/dev/null | grep -F "BH905SX976" | grep -F "recovery" | awk '{print $1}' || true)
    if [ -n "$ADB_DEV" ]; then
        echo ">>> TWRP đã sẵn sàng: $ADB_DEV tại +${i}s <<<"
        break
    fi
done

sleep 3
mkdir -p artifacts/logs/pstore_new
echo "=== DANH SÁCH PSTORE ==="
adb -s BH905SX976 shell "ls -la /sys/fs/pstore" || true

echo "=== NỘI DUNG CONSOLE RAMOOPS ==="
adb -s BH905SX976 shell "cat /sys/fs/pstore/console-ramoops" | tee artifacts/logs/pstore_new/console-ramoops.log || true

echo "=== NỘI DUNG DMESG RAMOOPS ==="
adb -s BH905SX976 shell "cat /sys/fs/pstore/dmesg-ramoops-0" | tee artifacts/logs/pstore_new/dmesg-ramoops.log || true

echo "=== HOÀN TẤT TRÍCH XUẤT ==="
