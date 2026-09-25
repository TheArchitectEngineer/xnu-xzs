# D8-P2 Milestone Seal Report: SPMI + PMIC LAB/IBB Power Rail Prerequisite

## 1. Executive Summary

Milestone **D8-P2** (SPMI + PMIC LAB/IBB Power Rail Prerequisite for Keyaki Panel Bring-Up: LAB `+5.60V` positive bias, IBB `-5.60V` negative bias, and mandatory safe disable) has completed full hardware execution and verification on the physical Sony Xperia XZs (`keyaki` / MSM8996 v3.0, serial `BH905SX976`).

Two consecutive, independent cold/fresh-boot hardware runs were executed over the physical USB console, achieving 100% pass rates across all validation checkpoints with zero regressions, zero panics, zero bus faults, and strict adherence to the display bring-up safety boundaries:
- **SPMI Transport**: SPMI Arbiter v2 verified across core (`0x0400F000`), write channels (`0x04400000`), and observer read channels (`0x04C00000`).
- **PMIC Identification**: Secondary PMIC correctly identified at SID 2 (`0x0100`) as PMI8996 (`type=0x51, subtype=0x13`).
- **Stage 1 (Safe Configuration Without Enable)**: LAB voltage programmed to `+5.60V` (`0x8a`), MODULE_RDY (`0x80`), ENABLE (`0x00`); IBB voltage programmed to `-5.60V` (`0xaa`), MODULE_RDY (`0x80`), ENABLE (`0x00`). Verified safely inactive with `VREG_OK=0`.
- **Stage 2A (LAB Enable)**: `LAB_ENABLE_CTL = 0x80` written -> `LAB_STATUS1 = 0xa0` (`VREG_OK=1`, positive rail actively regulating).
- **Stage 2B (IBB Enable)**: 8ms inter-rail delay -> `IBB_ENABLE_CTL = 0x80` written -> `IBB_STATUS1 = 0x80` (`VREG_OK=1`, negative rail actively regulating).
- **Stage 2C (Mandatory Safe Disable)**: IBB disabled first (`0xDC46 = 0x00`), `STATUS1 = 0x00` (OFF PASS) -> 8ms inter-rail delay -> LAB disabled (`0xDE46 = 0x00`), `STATUS1 = 0x00` (OFF PASS).
- **Timing & Watchdog**: Executed via microsecond hardware timer (`cntvct_el0` @ 19.2MHz) with continuous hardware watchdog petting.

---

## 2. Git & Binary Provenance

- **Branch**: `xzs-d8-display-p2`
- **Source Audit**: Verified against `artifacts/display-audit/keyaki.dts` (lines 8516–8555) and Qualcomm MSM8996 downstream `drivers/regulator/qpnp-labibb-regulator.c`
- **Kernel Binary**: `artifacts/builds/kernel.development.vmapple`
- **Boot Image**: `artifacts/builds/xzs-xnu-boot.img`
- **Boot Image SHA256**: `412b5674cd0c277deff0244427c7e2fcaeef7dce8e23c5f224aa8bdf778e675e`
- **PAC Violations**: 0 (`pac_check PASS`, 100% ARMv8.0-A compliant)

---

## 3. Hardware Execution Verification

### Checkpoint Results (Runs 1 & 2)

```text
[D8P2-10] PREREQUISITES:
  M2 Core Clocks & GDSC:          PASS
  M3 Lower Layer (PLL + Clocks):  PASS ([D8-M3] RESULT=PASS_FULL_M3)
  M4 DSI0 Host Controller:        PASS (DSI_CTRL=0x000001f5, LANE_STATUS=0x00001f1f)
  P1 TLMM GPIOs:                  PASS (GPIO8=0x200/0, GPIO10=0x5/0, GPIO51=0x200/0)

[D8P2-20] SPMI_TRANSPORT:
  SPMI Arbiter v2 Base:           core=0x0400F000, wr=0x04400000, rd=0x04C00000 -> PASS

[D8P2-30] PMIC_IDENTIFY:
  PMIC Type / Subtype:            type=0x51 subtype=0x13 (PMI8996) -> PASS

[D8P2-40] LAB_DISCOVERY:
  LAB Peripheral:                 SID 3 @ 0xDE00 (type=0x24) -> PASS

[D8P2-50] IBB_DISCOVERY:
  IBB Peripheral:                 SID 3 @ 0xDC00 (type=0x20) -> PASS

[D8P2-60] VOLTAGE_DERIVATION:
  LAB Voltage:                    target=+5.60V, base=4.60V, step=0.10V, code=10 (0x0A) | 0x80 = 0x8A -> PASS
  IBB Voltage:                    target=-5.60V, base=1.40V, step=0.10V, code=42 (0x2A) | 0x80 = 0xAA -> PASS

[D8P2-70] ENABLE_SEQUENCE_PLAN:   PASS
[D8P2-80] DISABLE_SEQUENCE_PLAN:  PASS
[D8-P2] RESULT=PASS_DRYRUN:       PASS

[D8P2-STAGE1] CONFIG_WITHOUT_ENABLE:
  LAB volt=0x8a rdy=0x80 en=0x00 status=0x00 (VREG_OK=0) -> PASS
  IBB volt=0xaa rdy=0x80 en=0x00 status=0x00 (VREG_OK=0) -> PASS
  [D8-P2] RESULT=PASS_CONFIG_ONLY

[D8P2-STAGE2A] LAB_ENABLE (+5.6V):
  LAB ENABLE written (0x80) -> final STATUS1=0xa0 (VREG_OK=1) -> PASS

[D8P2-STAGE2B] IBB_ENABLE (-5.6V):
  8ms inter-rail delay -> IBB ENABLE written (0x80) -> final STATUS1=0x80 (VREG_OK=1) -> PASS
  Both LAB (+5.6V) and IBB (-5.6V) ACTIVE and REGULATING -> PASS

[D8P2-STAGE2C] MANDATORY_SAFE_DISABLE:
  Disabling IBB (0xDC46 = 0x00) -> final STATUS1=0x00 (OFF PASS)
  8ms inter-rail delay
  Disabling LAB (0xDE46 = 0x00) -> final STATUS1=0x00 (OFF PASS)
  Both rails verified completely OFF -> PASS

[D8P2-INTEGRITY]:
  PLL:                            0x0000002f (LOCKED)
  DSI Controller:                 0x000001f5 (ACTIVE)
  DSI Lane Status:                0x00001f1f (STOPSTATE)
```

**Final Result**: `[D8-P2] RESULT=PASS_FULL_P2`

---

## 4. Authoritative Register Verification State

| Subsystem / Signal | Register Address | Observed Silicon Value | Decoded Hardware State | Functional Interpretation |
| :--- | :--- | :--- | :--- | :--- |
| **PMIC REVID** | SID 2 `0x0104..0x0105` | `0x51`, `0x13` | `type=0x51, subtype=0x13` | Confirmed PMI8996 companion PMIC |
| **LAB TYPE / SUBTYPE** | SID 3 `0xDE04..0xDE05` | `0x24`, `0x01` | `QPNP_TYPE_LAB (0x24)` | Confirmed hardware LAB peripheral |
| **LAB VOLTAGE** | SID 3 `0xDE41` | `0x8a` | `override=1, code=10` | Programmed `+5.60V` target |
| **LAB MODULE_RDY** | SID 3 `0xDE45` | `0x80` | `READY=1` | LAB module ready for software enable |
| **LAB ENABLE_CTL (ON)** | SID 3 `0xDE46` | `0x80` | `ENABLE=1` | LAB actively enabled |
| **LAB STATUS1 (ON)** | SID 3 `0xDE08` | `0xa0` | `VREG_OK=1, ON` | Positive bias rail actively regulating (+5.6V) |
| **IBB TYPE / SUBTYPE** | SID 3 `0xDC04..0xDC05` | `0x20`, `0x01` | `QPNP_TYPE_IBB (0x20)` | Confirmed hardware IBB peripheral |
| **IBB VOLTAGE** | SID 3 `0xDC41` | `0xaa` | `override=1, code=42` | Programmed `-5.60V` target |
| **IBB MODULE_RDY** | SID 3 `0xDC45` | `0x80` | `READY=1` | IBB module ready for software enable |
| **IBB ENABLE_CTL (ON)** | SID 3 `0xDC46` | `0x80` | `ENABLE=1` | IBB actively enabled |
| **IBB STATUS1 (ON)** | SID 3 `0xDC08` | `0x80` | `VREG_OK=1, ON` | Negative bias rail actively regulating (-5.6V) |
| **IBB ENABLE_CTL (POST)** | SID 3 `0xDC46` | `0x00` | `ENABLE=0` | IBB software disabled |
| **IBB STATUS1 (POST)** | SID 3 `0xDC08` | `0x00` | `VREG_OK=0, OFF` | Negative bias rail completely collapsed/OFF |
| **LAB ENABLE_CTL (POST)** | SID 3 `0xDE46` | `0x00` | `ENABLE=0` | LAB software disabled |
| **LAB STATUS1 (POST)** | SID 3 `0xDE08` | `0x00` | `VREG_OK=0, OFF` | Positive bias rail completely collapsed/OFF |
| **GPIO 8 (disp_reset_n)** | `0x01018000 / 04` | `0x200 / 0x0` | `OE=1, OUT=0` | Panel reset actively held asserted LOW |
| **DSI_CTRL** | `0x00994004` | `0x000001f5` | `master_en=1, cmd_mode=1` | DSI0 Host controller unperturbed |
| **DSI_LANE_STATUS** | `0x009940a8` | `0x00001f1f` | `DL0-DL3 + CLK stopstate` | MIPI DSI physical lanes remain stable |
| **PLL_PRIMARY_STATUS** | `0x009948cc` | `0x0000002f` | `locked=1` | DSI0 PLL remains locked and running |

---

## 5. Safety Invariant Audit

All safety bounds were strictly observed throughout P2 execution across both independent fresh-boot runs:

| Safety Counter / Invariant | Measured Value | Boundary Status |
| :--- | :--- | :--- |
| `DCS_PACKETS_SENT` | 0 | ZERO-TOUCH OBSERVED |
| `WLED_WRITES` | 0 | ZERO-TOUCH OBSERVED |
| `PANEL_RESET_HELD_LOW` | TRUE (GPIO 8 = 0) | STRICTLY OBSERVED |
| `PANEL_VDDIO_HELD_LOW` | TRUE (GPIO 51 = 0) | STRICTLY OBSERVED |
| `BUS_ABORT` | 0 | ZERO OBSERVED |
| `SError` | 0 | ZERO OBSERVED |
| `PANIC` | 0 | ZERO OBSERVED |
| `UNINTENDED_RESET` | 0 | ZERO OBSERVED |
| `USB_CONSOLE_ALIVE` | YES | 100% RESPONSIVE |
| `SHELL_ALIVE` | YES | 100% RESPONSIVE |

---

## 6. Conclusion & Next Milestone

Milestone **D8-P2** is complete, reproducible across two independent physical cold/fresh boots, and officially **SEALED**.

The display bring-up now has all electrical prerequisites proven on physical hardware:
- M2: MDSS GDSC and core clock infrastructure running.
- M3: DSI0 PLL locked at 540MHz, BYTE0/PCLK0/ESC0 unhalted, 14nm PHY Stage B ready.
- M4: DSI0 Host configured in Command Mode with 4 data lanes in Stopstate.
- P1: TLMM GPIO 8 (Reset asserted LOW), GPIO 10 (TE input), GPIO 51 (VDDIO LOW).
- P2: SPMI Arbiter v2 communication, LAB (+5.6V) and IBB (-5.6V) active regulation and clean safe shutdown.

Next Milestone: **D8-M5 (Panel Power/Reset Sequence)**.
