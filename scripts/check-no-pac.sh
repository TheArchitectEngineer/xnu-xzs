#!/usr/bin/env bash
# ==============================================================================
# scripts/check-no-pac.sh — ARMv8.0-A PAC Instruction Guardrail
#
# Audits the compiled XNU kernel binary for executable Pointer Authentication
# (PAC) instructions. Because Qualcomm MSM8996 (Snapdragon 820) is ARMv8.0-A,
# any PAC instruction will cause an Undefined Instruction trap at runtime.
# ==============================================================================

set -euo pipefail

KERNEL_BIN="${1:-artifacts/builds/kernel.development.vmapple}"
DEVELOPER_DIR="${DEVELOPER_DIR:-/Applications/Xcode-beta.app/Contents/Developer}"

if [[ ! -f "$KERNEL_BIN" ]]; then
    # Check alternate location
    ALT="src/xnu/BUILD/obj/DEVELOPMENT_ARM64_VMAPPLE/kernel.development.vmapple"
    if [[ -f "$ALT" ]]; then
        KERNEL_BIN="$ALT"
    else
        echo "[ERROR] Kernel binary not found at $KERNEL_BIN"
        exit 1
    fi
fi

echo "=== Auditing $KERNEL_BIN for ARMv8.3 PAC instructions ==="

OBJDUMP="xcrun llvm-objdump"
if ! command -v xcrun &>/dev/null; then
    OBJDUMP="objdump"
fi

# Disassemble executable __TEXT_EXEC segment and filter for PAC instructions
PAC_COUNT=$(DEVELOPER_DIR="$DEVELOPER_DIR" $OBJDUMP -d "$KERNEL_BIN" 2>/dev/null \
    | { grep -E "\b(pacia|pacib|pacda|pacdb|autia|autib|autda|autdb|braa|brab|blraa|blrab|retaa|retab)\b" || true; } \
    | wc -l | tr -d ' ')

echo "Total executable PAC instructions detected: $PAC_COUNT"

if [[ "$PAC_COUNT" -ne 0 ]]; then
    echo "[FAILED] ARMv8.3 PAC instructions found in ARMv8.0 kernel binary!"
    echo "Sample offending instructions:"
    DEVELOPER_DIR="$DEVELOPER_DIR" $OBJDUMP -d "$KERNEL_BIN" 2>/dev/null \
        | grep -E -n "\b(pacia|pacib|pacda|pacdb|autia|autib|autda|autdb|braa|brab|blraa|blrab|retaa|retab)\b" \
        | head -n 20
    exit 1
fi

echo "[SUCCESS] Zero PAC instructions detected. Kernel is 100% ARMv8.0-A compliant!"
exit 0
