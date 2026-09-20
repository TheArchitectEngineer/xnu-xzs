# Phase D5: MSM8996 Reset / Runtime State Isolation Report

## 1. Executive Summary & Required Classifications

```text
D5_M3_LKG_COMMIT=7a4232c
TEST_ARTIFACT_IDENTICAL=yes
BOOT_IMAGE_SHA256=c6f5bc2da9767c34d0ae993ad0097f98dc930974fa3e812238316ec8acb0e8d0

RESET_STATE_TEST_A=current_fastboot_session (warm)
TEST_A_LAST_CHECKPOINT=[XZS-BOOT] [D44] BSD AUTOCONF ENTER
TEST_A_D510_60_REACHED=no
TEST_A_D520_90_REACHED=no
TEST_A_RETURN_TIME=+40s

RESET_STATE_TEST_B=cold
COLD_RESET_METHOD=unplug_usb_power_volup_3vib_pmic_cut
POWER_REMOVAL_CONFIRMED=yes
BATTERY_REMOVAL_USED=no
USB_POWER_PRESENT_DURING_RESET=no

TEST_B_LAST_CHECKPOINT=[D44] (IOService::pingConfig / creating configThread)
TEST_B_D510_60_REACHED=no
TEST_B_D520_90_REACHED=no
TEST_B_RETURN_TIME=+40s
FINAL_DEVICE_STATE=fastboot
FASTBOOT_RETURN_METHOD=twrp_scripted
MANUAL_INTERVENTION_USED=no

DECISION_MATRIX_CASE=Case C
RESET_STATE_EFFECT_OBSERVED=possible
D5_M3_REPRODUCED=no
D5_M4_CAUSALITY=UNPROVEN
RESET_STATE_DEPENDENCE_NOT_DEMONSTRATED=yes

PLUS_40S_WATCHDOG_CORRELATION=strong
APCS_WATCHDOG_CAUSAL=UNPROVEN

KERNEL_RESET_DESTINATION=
```

---

## 2. Controlled Experiment Side-by-Side Comparison

| Metric / Parameter | Test A: Current/Warm | Test B: Cold Power Cycle |
| :--- | :--- | :--- |
| **Artifact Identity** | Exact archived D5-M3 LKG (`artifacts/archive/d5m3-7a4232c/xzs-xnu-boot.img`) | Exact archived D5-M3 LKG (`artifacts/archive/d5m3-7a4232c/xzs-xnu-boot.img`) |
| **Boot Image SHA256** | `c6f5bc2da9767c34d0ae993ad0097f98dc930974fa3e812238316ec8acb0e8d0` | `c6f5bc2da9767c34d0ae993ad0097f98dc930974fa3e812238316ec8acb0e8d0` |
| **Commit** | `7a4232c` | `7a4232c` |
| **Rebuilt** | **NO** | **NO** |
| **Reset State Entry** | Warm reboot via TWRP `adb reboot bootloader` | Full cold reset: USB unplugged, Power + Vol Up held past 3 vibrations (PMIC cut), 10s wait, Vol Up held + USB reconnected |
| **Power Removal Confirmed** | No | Yes (PMIC power cut, USB power absent during reset) |
| **Battery Removal Used** | No | No (Sealed OEM battery) |
| **USB Power During Reset** | Yes | No |
| **Pre-Boot State Capture** | `fastboot getvar all` (saved) | `fastboot getvar all` (saved) |
| **Last Checkpoint** | `[XZS-BOOT] [D44] BSD AUTOCONF ENTER` | `[D44] (IOService::pingConfig / creating configThread)` |
| **D510/60 Reached** | **no** | **no** |
| **D520/90 Reached** | **no** | **no** |
| **Return Time** | `+40s` | `+40s` |
| **Reset Telemetry** | tzdbg reset=0x0, boot warmboot=0x12 | tzdbg reset=0x0, boot warmboot=0x1d |
| **Manual Intervention (Boot/Extract)** | No | No |
| **Fastboot Return Method** | `twrp_scripted` | `twrp_scripted` |
| **Final Device State** | `fastboot` | `fastboot` |

---

## 3. Detailed Telemetry Analysis

### Test A (Warm Session) Progression
- Booted `c6f5bc2d...` from warm Fastboot session.
- Extracted dmesg reached `[XZS-BOOT] [D44] BSD AUTOCONF ENTER`.
- Stalled before `bpf_init` or `fsevents_init`.
- Returned to Fastboot at `+40s`.

### Test B (Cold Reset) Progression
- Achieved genuine cold power cut via hardware keys (`Power + Volume Up` past 3 vibrations) with USB disconnected.
- Re-entered Fastboot via Volume Up + USB connection.
- Booted identical artifact `c6f5bc2d...`.
- Extracted telemetry demonstrates execution progressed farther than Test A:
  - `[XZS-BOOT] [D44] BSD AUTOCONF ENTER` passed
  - `bpf_init` completed (4 `/dev/bpf` nodes created)
  - `fsevents_init` completed (`fsevents` zone initialized)
  - `fbt_init` deferred (workaround)
  - `profile_init` deferred (workaround)
  - `publishResource` completed `setProperty` and `registerService`
  - `startMatching` executed
  - `[pingConfig] creating configThread` reached
- Stalled at `[pingConfig] creating configThread` without reaching `0xD510/0x60` or `0xD520/0x90`.
- Returned to Fastboot at `+40s`.

---

## 4. Decision Matrix Classification

Per the diagnostic protocol:
- **Case C**: *cold → progresses farther but does not reach D520/90*
  - `RESET_STATE_EFFECT_OBSERVED=possible`
  - `D5_M3_REPRODUCED=no`
  - `D5_M4_CAUSALITY=UNPROVEN`
  - `RESET_STATE_DEPENDENCE_NOT_DEMONSTRATED=yes` (a cold power cut alone is insufficient to reproduce the full historical D5-M3 LKG pass)

---

## 5. Watchdog & Reset Provenance

- **`PLUS_40S_WATCHDOG_CORRELATION=strong`**: All stalled runs across both D5-M4 and D5-M3 LKG consistently return to Fastboot at the +40s mark, strongly correlating with the MSM8996 APCS watchdog timer (~39s).
- **`APCS_WATCHDOG_CAUSAL=UNPROVEN`**: `/sys/kernel/debug/tzdbg/reset` counters remain `0x0`. No direct register bit proves the APCS watchdog triggered the reset.
- **`KERNEL_RESET_DESTINATION=`**: Unproven whether XNU kernel directly triggered Fastboot entry or whether an APCS reset forced aboot Fastboot fallback.
- **`FINAL_DEVICE_STATE=fastboot`**: The device reliably returned to Fastboot via `FASTBOOT_RETURN_METHOD=twrp_scripted`.

---

## 6. Hard Stop

Per protocol:
- Maximum cold test run count = 1.
- No autonomous retries.
- No source code modifications.
- D5-M4 development remains HALTED pending review.
