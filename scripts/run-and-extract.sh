#!/bin/bash
set -u

echo "============================================================"
echo "XZS END-TO-END XNU BOOT & TELEMETRY EXTRACTION"
echo "============================================================"

# Step 0: Mandatory Binary Patch Verification Gate
echo "[0/4] Verifying binary patches before boot..."
if ! python3 scripts/verify_binary_patch.py --check artifacts/builds/twrp-kagura.img; then
    echo "CRITICAL ERROR: Binary patch verification FAILED! Halting execution. NO IMAGES WILL BE BOOTED."
    exit 1
fi
echo "[+] Binary patch verification: 100% PASS"

# Step 1: Check fastboot or recovery
echo "[1/4] Checking device state..."
ADB_DEV=$(adb devices 2>/dev/null | grep -F "recovery" | head -n 1 | awk '{print $1}' || true)
if [ -n "$ADB_DEV" ]; then
    echo "Device is currently in TWRP recovery ($ADB_DEV). Rebooting to bootloader..."
    adb reboot bootloader
    sleep 3
fi

DEV=""
for i in {1..10}; do
    DEV=$(fastboot devices 2>/dev/null | head -n 1 | awk '{print $1}')
    if [ -n "$DEV" ]; then break; fi
    sleep 1
done

if [ -z "$DEV" ]; then
    echo "ERROR: Device not in fastboot mode!"
    exit 1
fi
echo "Target device found in Fastboot: $DEV"

# Step 2: Boot XNU
echo "[2/4] Booting artifacts/builds/xzs-xnu-boot.img..."

# Background injector for D7-M4 RX probe
(
    if [ -e "/dev/cu.debug-console" ]; then
        stty -f /dev/cu.debug-console 115200 cs8 -cstopb -parenb 2>/dev/null || true
        # Send test byte at multiple intervals to cover the RX probe window
        for t in 4 5 6 7 8 9 10; do
            sleep 1
            python3 -c "import os; fd = os.open('/dev/cu.debug-console', os.O_WRONLY | os.O_NONBLOCK | os.O_NOCTTY); os.write(fd, b'A'); os.close(fd)" 2>/dev/null || true
        done
        echo ">>> [HOST-INJECTOR] Sent 0x41 ('A') x7 over /dev/cu.debug-console <<<"
    fi
) &

fastboot boot artifacts/builds/xzs-xnu-boot.img
sleep 3

echo "[3/4] Waiting for automated warm reset back to Fastboot (timeout 90s)..."
RETURNED=""
for i in {1..90}; do
    sleep 1
    DEV=$(fastboot devices 2>/dev/null | head -n 1 | awk '{print $1}')
    if [ -n "$DEV" ]; then
        echo ">>> Target returned to Fastboot at +${i}s: $DEV <<<"
        RETURNED="$DEV"
        BOOT_TIME="$i"
        break
    fi
    echo -n "."
done
echo ""

if [ -z "$RETURNED" ]; then
    echo "Warning: Target did not return to Fastboot within 90s."
    echo "Checking current USB state..."
    fastboot devices
    adb devices
    exit 1
fi

# Step 3: Boot TWRP dumper
echo "[4/4] Booting TWRP dumper to extract pstore..."
fastboot boot artifacts/builds/twrp-kagura.img

echo "Waiting for TWRP Recovery ADB (timeout 75s)..."
TWRP_DEV=""
for i in {1..75}; do
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

sleep 1
# User visual & haptic confirmation: vibrate and set LED green so user knows TWRP is alive
adb shell "echo 300 > /sys/class/timed_output/vibrator/enable; echo 255 > /sys/class/leds/led:rgb_green/brightness; echo 0 > /sys/class/leds/led:rgb_red/brightness" 2>/dev/null || true
echo ">>> Device signaled: Vibrated + LED turned GREEN (TWRP alive) <<<"

echo "============================================================"
echo "EXTRACTING PSTORE & HARDWARE TELEMETRY"
echo "============================================================"
rm -rf artifacts/logs/pstore
mkdir -p artifacts/logs/pstore
adb shell "ls -la /sys/fs/pstore"

echo "--- TWRP Ramoops Kernel Attach ---"
adb shell "dmesg | grep -iE 'ramoops|pstore|persistent_ram'" || true

echo "--- Pulling all pstore files ---"
adb pull /sys/fs/pstore/. artifacts/logs/pstore/ || true

echo "--- dmesg-ramoops-0 ---"
adb shell "cat /sys/fs/pstore/dmesg-ramoops-0 2>/dev/null" | tee artifacts/logs/xnu-dmesg-extracted.log || true

echo "--- console-ramoops ---"
adb shell "cat /sys/fs/pstore/console-ramoops 2>/dev/null" | tee artifacts/logs/xnu-console-extracted.log || true

echo "--- Reading IMEM SRAM Hardware Breadcrumb (0x066bf660) ---"
adb shell "mknod /dev/mem c 1 1 2>/dev/null || true"
IMEM_DUMP=$(adb shell "for off in 0 4 8 12 16 20 24 28; do /sbin/devmem \$(printf '0x%08x' \$((0x066bf660 + off))) 32 2>/dev/null || echo '0x00000000'; done" 2>/dev/null || true)
echo "Raw IMEM SRAM (32 bytes):"
echo "$IMEM_DUMP"

echo "--- Reading Persistent DRAM Hardware Breadcrumb (0x80060020) ---"
DRAM_BC=$(adb shell "for off in 0 4 8 12 16 20 24 28; do /sbin/devmem \$(printf '0x%08x' \$((0x80060020 + off))) 32 2>/dev/null || echo '0x00000000'; done" 2>/dev/null || true)
echo "Raw Persistent DRAM Breadcrumb (32 bytes at 0x80060020):"
echo "$DRAM_BC"

echo "--- TrustZone Reset & Boot Status ---"
adb shell "cat /sys/kernel/debug/tzdbg/reset 2>/dev/null; echo '--- BOOT ---'; cat /sys/kernel/debug/tzdbg/boot 2>/dev/null" | tee artifacts/logs/tzdbg-status.log || true

adb shell "dmesg" > artifacts/logs/twrp-dmesg-full.log 2>&1 || true

echo "============================================================"
echo "TELEMETRY & BREADCRUMB ANALYSIS"
echo "============================================================"
LAST_BC=$(grep -E "\[BREADCRUMB\]|\[XZS-BOOT\]|\[PANIC" artifacts/logs/xnu-console-extracted.log artifacts/logs/xnu-dmesg-extracted.log 2>/dev/null | tail -n 10 || true)
if [ -n "$LAST_BC" ]; then
    echo "[PASS] Successfully extracted XNU checkpoints:"
    echo "$LAST_BC"
else
    echo "[INFO] Scanning full raw dmesg for XNU markers..."
    grep -E "XZS|XNU|BREADCRUMB|D51|vfs|PANIC" artifacts/logs/xnu-console-extracted.log artifacts/logs/xnu-dmesg-extracted.log artifacts/logs/twrp-dmesg-full.log 2>/dev/null | tail -n 10 || true
fi

# Check for panic / ELR symbolication
PANIC_ELR=$(grep -oE "pc=0x[0-9a-fA-F]+" artifacts/logs/xnu-console-extracted.log 2>/dev/null | head -n 1 | cut -d'=' -f2 || true)
if [ -n "$PANIC_ELR" ]; then
    echo "--- Symbolicating Panic Address: $PANIC_ELR ---"
    python3 scripts/symbolicate_elr.py "$PANIC_ELR" || true
fi

echo "============================================================"
echo "EXTRACTION COMPLETE (Automated Return: +${BOOT_TIME:-unknown}s)"
echo "============================================================"

# Auto-reboot to Fastboot if requested or by default for test loops
if [ "${AUTO_REBOOT_FASTBOOT:-1}" = "1" ]; then
    echo "Rebooting TWRP to bootloader (Fastboot) for next test cycle..."
    adb reboot bootloader
    sleep 3
    fastboot devices
fi


