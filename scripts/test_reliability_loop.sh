#!/bin/bash
set -euo pipefail

TOTAL_RUNS=${1:-3}

echo "================================================================================"
echo "XZS AUTOMATED RELIABILITY LOOP ($TOTAL_RUNS CONSECUTIVE RUNS)"
echo "Zero physical button intervention required."
echo "================================================================================"

mkdir -p artifacts/logs/loop_runs

# Step 0: Enforce Binary Patch Verification Gate
echo "[GATE] Running binary patch verification..."
python3 scripts/verify_binary_patch.py --check artifacts/builds/twrp-kagura.img

SUCCESSFUL_RUNS=0
declare -a RUN_RESULTS

for RUN in $(seq 1 "$TOTAL_RUNS"); do
    echo ""
    echo "################################################################################"
    echo "### STARTING TEST RUN $RUN of $TOTAL_RUNS"
    echo "################################################################################"

    RUN_LOG="artifacts/logs/loop_runs/run_${RUN}.log"
    ./scripts/run-and-extract.sh 2>&1 | tee "$RUN_LOG"

    # Analyze result
    if grep -q "EXTRACTION COMPLETE" "$RUN_LOG"; then
        RETURN_TIME=$(grep "Automated Return:" "$RUN_LOG" | tail -n 1 || echo "unknown")
        echo ">>> RUN $RUN: PASS ($RETURN_TIME) <<<"
        SUCCESSFUL_RUNS=$((SUCCESSFUL_RUNS + 1))
        RUN_RESULTS+=("Run $RUN: PASS ($RETURN_TIME)")
    else
        echo ">>> RUN $RUN: FAIL <<<"
        RUN_RESULTS+=("Run $RUN: FAIL")
        break
    fi

    # Prepare device for next iteration if not final run
    if [ "$RUN" -lt "$TOTAL_RUNS" ]; then
        if adb devices 2>/dev/null | grep -q "recovery"; then
            echo "Rebooting TWRP to bootloader..."
            adb reboot bootloader || true
            sleep 3
        fi
        fastboot devices
    fi
done

echo ""
echo "================================================================================"
echo "RELIABILITY LOOP RESULTS SUMMARY"
echo "================================================================================"
echo "Total Runs Attempted:  $TOTAL_RUNS"
echo "Successful Runs:       $SUCCESSFUL_RUNS / $TOTAL_RUNS"
for RES in "${RUN_RESULTS[@]}"; do
    echo " - $RES"
done
echo "================================================================================"

if [ "$SUCCESSFUL_RUNS" -eq "$TOTAL_RUNS" ]; then
    echo "ACCEPTANCE CRITERIA MET: $SUCCESSFUL_RUNS/$TOTAL_RUNS consecutive autonomous boot/recovery cycles passed."
    exit 0
else
    echo "ACCEPTANCE CRITERIA FAILED: Only $SUCCESSFUL_RUNS/$TOTAL_RUNS passed."
    exit 1
fi
