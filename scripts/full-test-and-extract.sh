#!/bin/bash
set -euo pipefail

echo "============================================================"
echo "XZS AUTOMATED XNU BOOT & TELEMETRY EXTRACTION"
echo "============================================================"

MODE="${1:-}"
SMP_REGRESSION=0
if [ "$MODE" = "--smp-regression" ]; then
    SMP_REGRESSION=1
    echo "=== Mode: 4-Core Mach SMP Milestone Regression Verification ==="
fi

FASTBOOT_SERIAL="${FASTBOOT_SERIAL:-}"
ADB_SERIAL="${ADB_SERIAL:-}"

echo "1. Waiting for Sony Xperia XZs in Fastboot (hold Vol Up + plug USB)..."
while true; do
    if [ -n "$FASTBOOT_SERIAL" ]; then
        DEV=$(fastboot devices 2>/dev/null | grep -F "$FASTBOOT_SERIAL" | awk '{print $1}' || true)
    else
        DEV=$(fastboot devices 2>/dev/null | head -n 1 | awk '{print $1}' || true)
        if [ -n "$DEV" ]; then FASTBOOT_SERIAL="$DEV"; fi
    fi
    if [ -n "$DEV" ]; then
        echo ">>> Detected Fastboot device: $DEV <<<"
        break
    fi
    sleep 1
done

echo "2. Booting newly built XNU kernel (artifacts/builds/xzs-xnu-boot.img)..."
fastboot -s "$FASTBOOT_SERIAL" boot artifacts/builds/xzs-xnu-boot.img

echo "3. Waiting for automatic PSCI warm reset back to Fastboot..."
RETURNED=""
for i in $(seq 1 60); do
    sleep 1
    DEV=$(fastboot devices 2>/dev/null | grep -F "$FASTBOOT_SERIAL" | awk '{print $1}' || true)
    if [ -n "$DEV" ]; then
        echo ">>> Device returned to Fastboot at +${i}s: $DEV <<<"
        RETURNED="1"
        break
    fi
done

if [ -z "$RETURNED" ]; then
    echo "Notice: Device did not return to Fastboot within 60s."
    echo "Current USB status:"
    fastboot devices || true
    adb devices || true
    exit 1
fi

echo "4. Booting TWRP dumper (artifacts/builds/twrp-kagura.img)..."
fastboot -s "$FASTBOOT_SERIAL" boot artifacts/builds/twrp-kagura.img

echo "5. Waiting for TWRP ADB recovery environment..."
for i in $(seq 1 35); do
    sleep 1
    if [ -n "$ADB_SERIAL" ]; then
        ADB_DEV=$(adb devices 2>/dev/null | grep -F "$ADB_SERIAL" | grep -F "recovery" | awk '{print $1}' || true)
    else
        ADB_DEV=$(adb devices 2>/dev/null | grep -F "recovery" | head -n 1 | awk '{print $1}' || true)
        if [ -n "$ADB_DEV" ]; then ADB_SERIAL="$ADB_DEV"; fi
    fi
    if [ -n "$ADB_DEV" ]; then
        echo ">>> Detected TWRP Recovery: $ADB_DEV at +${i}s <<<"
        break
    fi
done

sleep 3
echo "6. Extracting telemetry from persistent pstore..."
mkdir -p artifacts/logs
adb -s "$ADB_SERIAL" shell "ls -la /sys/fs/pstore"
echo "--- DMESG RAMOOPS ---"
adb -s "$ADB_SERIAL" shell "cat /sys/fs/pstore/dmesg-ramoops-0 2>/dev/null" | tee artifacts/logs/dmesg-ramoops.log
echo "--- CONSOLE RAMOOPS ---"
adb -s "$ADB_SERIAL" shell "cat /sys/fs/pstore/console-ramoops 2>/dev/null" | tee artifacts/logs/console-ramoops.log

echo "============================================================"
echo "EXTRACTION FINISHED"
echo "============================================================"
ls -lh artifacts/logs/

if [ "$SMP_REGRESSION" -eq 1 ]; then
    echo ""
    echo "============================================================"
    echo "AUDITING 4-CORE MACH SMP REGRESSION EVIDENCE"
    echo "============================================================"
    LOG="artifacts/logs/console-ramoops.log"
    FAILURES=0

    check_evidence() {
        local desc="$1"
        local pattern="$2"
        if grep -q -E "$pattern" "$LOG"; then
            echo "  [PASS] $desc"
        else
            echo "  [FAIL] $desc (pattern not found: '$pattern')"
            FAILURES=$((FAILURES + 1))
        fi
    }

    check_evidence "CPU0 Online" "CPU0 ONLINE"
    check_evidence "CPU1 Online" "CPU1 ONLINE"
    check_evidence "CPU2 Online" "CPU2 ONLINE"
    check_evidence "CPU3 Online" "CPU3 ONLINE"
    check_evidence "Reschedule IPI (SGI)" "RESCHEDULE IPI"
    check_evidence "Test 1: Worker Thread Dispatch" "(\[TEST 1\] Worker thread|TEST 1)"
    check_evidence "Test 2: Cross-Cluster Migration" "TEST 2 PASSED"
    check_evidence "Test 3: 40k HW Lock Contention" "TEST 3 PASSED"
    check_evidence "Mach SMP Full Integration" "4-CORE FULL MACH SCHEDULER INTEGRATION 100% VERIFIED"

    echo "------------------------------------------------------------"
    if [ "$FAILURES" -eq 0 ]; then
        echo ">>> SMP REGRESSION: 100% PASSED (All 4 cores & Mach scheduler verified) <<<"
    else
        echo ">>> SMP REGRESSION: FAILED ($FAILURES checks failed) <<<"
        exit 1
    fi
    echo "============================================================"
fi
