# Phase D5-M4: Regression Isolation Diagnostic Report

## 1. Executive Summary & Required Classifications

```text
RUN_A_ARTIFACT_IS_EXACT_ARCHIVED_LKG=yes
RUN_A_BOOT_IMAGE_SHA256=c6f5bc2da9767c34d0ae993ad0097f98dc930974fa3e812238316ec8acb0e8d0
D5_M3_LKG_COMMIT=7a4232c

RUN_A_LAST_CHECKPOINT=[XZS-BOOT] [D44] BSD AUTOCONF ENTER
RUN_A_D520_90_REACHED=no
RUN_A_RETURN_TIME=+40s
RUN_A_RETURN_METHOD=twrp_scripted
RUN_A_MANUAL_INTERVENTION_USED=no

RUN_B_EXECUTED=no
RUN_B_REASON_SKIPPED=Gated on RUN_A_D520_90_REACHED=yes (Run A failed to reach D520/90)

DECISION_MATRIX_CASE=Case 3
EXACT_M3_LKG_REPRODUCIBLE=no
D5_M4_REGRESSION_CAUSAL=UNPROVEN
M4_CAUSALITY_UNPROVEN=yes
RESET_STATE_OR_DEVICE_STATE_INVESTIGATION_REQUIRED=yes

DEEPEST_OBSERVED_FAILURE_BOUNDARY=0xD510/0x53
APCS_WATCHDOG_CAUSAL=UNPROVEN
PLUS_40S_WATCHDOG_CORRELATION=strong

KERNEL_RESET_DESTINATION=
FINAL_DEVICE_STATE=fastboot
FASTBOOT_RETURN_METHOD=twrp_scripted
```

---

## 2. Controlled Diagnostic Pipeline Execution

### Run A — Exact Archived D5-M3 LKG Artifact
- **Artifact Source**: `artifacts/archive/d5m3-7a4232c/xzs-xnu-boot.img`
- **Rebuilt**: **NO** (Exact byte-for-byte copy deployed to `artifacts/builds/xzs-xnu-boot.img`)
- **Verified SHA256**: `c6f5bc2da9767c34d0ae993ad0097f98dc930974fa3e812238316ec8acb0e8d0`
- **Hardware Target**: Sony Xperia XZs (`BH905SX976`, MSM8996)
- **Pipeline Used**:
  1. Fastboot (`fastboot devices` -> `BH905SX976`)
  2. Boot XNU (`fastboot boot artifacts/builds/xzs-xnu-boot.img`)
  3. Execution stall at `[XZS-BOOT] [D44] BSD AUTOCONF ENTER`
  4. Device reset returned to Fastboot at `+40s` (`BH905SX976`)
  5. TWRP booted (`artifacts/builds/twrp-kagura.img`) at `+12s`
  6. Telemetry & pstore extracted
  7. TWRP rebooted to bootloader (`adb reboot bootloader`)
  8. Device confirmed in Fastboot mode (`BH905SX976`)
- **Execution Result**:
  - `RUN_A_LAST_CHECKPOINT`: `[XZS-BOOT] [D44] BSD AUTOCONF ENTER`
  - `RUN_A_D520_90_REACHED`: **no** (0 breadcrumbs observed from `0xD520` family)
  - `RUN_A_RETURN_METHOD`: `twrp_scripted`
  - `RUN_A_MANUAL_INTERVENTION_USED`: **no**

### Run B — M4 Code Linked, M4 Execution Disabled
- **Status**: **NOT EXECUTED**
- **Reason**: Per protocol section 1:
  > *If `D520/90` is NOT reached: STOP. Do not perform Run B. Report: EXACT_M3_LKG_REPRODUCES=no, D5_M4_REGRESSION_CAUSAL=UNPROVEN, RESET_STATE_OR_DEVICE_STATE_INVESTIGATION_REQUIRED=yes. No source edits.*

---

## 3. Decision Matrix Evaluation

| Case | Condition | Outcome | Status |
| :--- | :--- | :--- | :--- |
| **Case 1** | `RUN_A_D520_90_REACHED=yes` AND `RUN_B_D520_90_REACHED=yes` | M3 LKG reproducible, M4 linked binary compatible, M4 execution-path isolation required | N/A |
| **Case 2** | `RUN_A_D520_90_REACHED=yes` AND `RUN_B_D520_90_REACHED=no` | M3 LKG reproducible, M4 linked binary regression | N/A |
| **Case 3** | `RUN_A_D520_90_REACHED=no` | **EXACT_M3_LKG_REPRODUCIBLE=no**, **M4_CAUSALITY_UNPROVEN=yes**, Stop all M4 work | **ACTIVE** |

### Decision Matrix Classification
```text
DECISION_MATRIX_CASE=Case 3
EXACT_M3_LKG_REPRODUCIBLE=no
M4_CAUSALITY_UNPROVEN=yes
```

---

## 4. Reset, Recovery & Telemetry Analysis

1. **Hardware & Execution State**:
   - The exact binary artifact that historically passed all 21 checkpoints in D5-M3 (`c6f5bc2d...`) failed to execute past `[XZS-BOOT] [D44] BSD AUTOCONF ENTER` when booted under the current device/runtime session.
   - It exhibited the exact same `+40s` stall as seen in D5-M4 Attempt 1 (`[D44] configThread`) and Attempt 2 (`[D44] BSD AUTOCONF ENTER`).
   - `0xD510/0x53` remains the deepest failure boundary observed in any attempt (`DEEPEST_OBSERVED_FAILURE_BOUNDARY=0xD510/0x53`), but Run A stalled earlier at `[D44]`.

2. **Watchdog & Reset Provenance**:
   - `APCS_WATCHDOG_CAUSAL=UNPROVEN`: No direct register bit proves the APCS watchdog triggered the reset (tzdbg reset counter = 0x0).
   - `PLUS_40S_WATCHDOG_CORRELATION=strong`: The consistent 40-second return window across all stalled boots strongly correlates with the MSM8996 ~39s APCS watchdog bark/bite timeout.
   - `KERNEL_RESET_DESTINATION=`: Unproven whether XNU kernel directly branched to fastboot or whether board reset entered aboot fastboot loop.
   - `FINAL_DEVICE_STATE=fastboot`: Device was returned to Fastboot cleanly via `FASTBOOT_RETURN_METHOD=twrp_scripted`.

3. **Next Steps**:
   - Stop all D5-M4 development.
   - No source code edits.
   - Await user review and direction regarding device/reset/runtime state investigation.
