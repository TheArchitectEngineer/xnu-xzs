#!/bin/bash
set -euo pipefail

TARGET_SERIAL="${FASTBOOT_SERIAL:-}"
BOOT_IMG="artifacts/builds/xzs-xnu-boot.img"

if [ ! -f "$BOOT_IMG" ]; then
    echo "ERROR: $BOOT_IMG not found. Run ./scripts/package-boot.sh first."
    exit 1
fi

if [ -z "$TARGET_SERIAL" ]; then
    TARGET_SERIAL="$(fastboot devices 2>/dev/null | head -n 1 | awk '{print $1}')"
    if [ -z "$TARGET_SERIAL" ]; then
        echo "ERROR: No fastboot device detected!"
        exit 1
    fi
    echo "=== Auto-detected target device $TARGET_SERIAL ==="
else
    echo "=== Verifying target device $TARGET_SERIAL ==="
    CURRENT_SERIAL="$(fastboot getvar serialno 2>&1 | awk '/serialno:/ {print $2}')"
    if [ "$CURRENT_SERIAL" != "$TARGET_SERIAL" ]; then
        echo "ERROR: Connected device ($CURRENT_SERIAL) does not match target ($TARGET_SERIAL)!"
        exit 1
    fi
fi

echo "=== Executing temporary RAM boot (fastboot boot $BOOT_IMG) ==="
fastboot -s "$TARGET_SERIAL" boot "$BOOT_IMG" 2>&1 | tee artifacts/logs/boot-test-"$(date +%Y%m%d-%H%M%S)".log
echo "=== Fastboot boot command completed ==="
