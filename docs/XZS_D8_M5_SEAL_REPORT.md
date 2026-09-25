# XZS D8-M5 Panel Power & Reset Bring-Up Seal Report

## 1. Executive Summary & Canonical Baseline

This report documents the formal completion, physical hardware verification, and sealing of **Milestone D8-M5: Keyaki Panel Power + Reset Sequence** on physical Sony Xperia XZs (`G8231` / `keyaki` / MSM8996 v3.0 / `BH905SX976`).

- **Prerequisites Inherited:**
  - D8-M1: Hardware Audit & Baseline Register State (Sealed `0c3eec4`)
  - D8-M2: Display Power Domains & Clocks (Sealed `cb91097`)
  - D8-M3: DSI PLL Lock & 14nm PHY Calibration (Sealed `f9baed7`)
  - D8-M4: DSI0 Host Controller Initialization (Sealed `09ae224`)
  - D8-P1: Keyaki Display TLMM GPIO Configuration (Sealed `d508493`)
  - D8-P2: Keyaki Display PMIC Regulators LAB & IBB (Sealed `cd60919`)
- **Milestone Outcome:** **COMPLETE**, **SEALED**, **HARDWARE_PROVEN** across two independent fresh boot cycles on physical target hardware.

---

## 2. Target Hardware Specifications

```text
Device:            Sony Xperia XZs (G8231)
Codename:          keyaki / tone platform
SoC:               Qualcomm Snapdragon 820 (MSM8996 v3.0, 4x Kryo)
PMIC:              PM8994 / PMI8994 (SPMI Arbiter v2)
Display Panel:     Sharp + Synaptics Command-Mode Panel
Internal Name:     "9" (somc,sharp_synaptics_cmd_9_panel)
Resolution:        1080 x 1920 (FHD), 60 Hz
Interface:         DSI0, Command Mode, 4 Data Lanes + 1 Clock Lane
VDDIO Rail:        TLMM GPIO 51 (1.8V LCD Logic Enable, Active HIGH)
Panel Reset:       TLMM GPIO 8 (disp_reset_n, Active LOW)
Panel TE:          TLMM GPIO 10 (mdp_vsync, Inbound Tear-Effect)
LAB Bias Rail:     PMI8994 SID 3, Base 0xDE00 (+5.60V, VSP)
IBB Bias Rail:     PMI8994 SID 3, Base 0xDC00 (-5.60V, VSN)
```

---

## 3. Sequence Audit & Downstream Kernel Equivalence

The power and reset state machine was derived strictly from downstream Sony kernel device trees (`artifacts/display-audit/keyaki.dts`):

### Authoritative Power-On Sequence
```text
1. VDDIO Enable:    Set GPIO 51 = 1 (1.8V Logic) -> Wait 10 ms (10,000 us)
2. LAB Enable:      Set PMI8994 0xDE46 = 0x80 (+5.60V) -> Poll VREG_OK == 1 -> Wait 10 ms (10,000 us)
3. IBB Enable:      Set PMI8994 0xDC46 = 0x80 (-5.60V) -> Poll VREG_OK == 1 -> Wait 0 ms
4. Reset Assert:    Set GPIO 8 = 0 (Active LOW) -> Wait 10 ms (10,000 us)
5. Reset Release:   Set GPIO 8 = 1 (Deasserted) -> Wait 10 ms (10,000 us) stabilization
```

### Authoritative Power-Off Sequence
```text
1. Reset Assert:    Set GPIO 8 = 0 (Active LOW) -> Wait 5 ms (5,000 us)
2. IBB Disable:     Set PMI8994 0xDC46 = 0x00 -> Poll VREG_OK == 0 -> Wait 10 ms (10,000 us)
3. LAB Disable:     Set PMI8994 0xDE46 = 0x00 -> Poll VREG_OK == 0 -> Wait 10 ms (10,000 us)
4. VDDIO Disable:   Set GPIO 51 = 0 -> Wait 0 ms
5. Settle Window:   Wait 300 ms (300,000 us) settling period before any subsequent re-power
```

---

## 4. Implementation Architecture

1. **Kernel State Machine (`src/xnu/pexpert/arm/xzs_d8m5.h`):**
   - Implements full state machine:
     - `xzs_d8m5_status()`: Read-only inspection of GPIOs, PMIC rails, DSI0 host, and safety counters.
     - `xzs_d8m5_dryrun()`: Emulated power and reset sequence with register check validation.
     - `xzs_d8m5_stage1()`: Power domains established with reset held LOW, followed by safe power-down.
     - `xzs_d8m5_run()`: Full power-on, reset assert/release with cycle-accurate timestamping (`cntvct_el0`), powered-idle checkpoint, and mandatory safe shutdown.
     - `xzs_d8m5_power_down()`: Source-audited safe shutdown sequence.
   - Delay mechanisms: Hardware cycle-accurate timing via `cntvct_el0` (19.2 MHz clock frequency, 192 ticks / 10 us) with watchdog petting, preventing any reliance on unrouted timer interrupts.
2. **SPMI Driver Hardening (`src/xnu/pexpert/arm/xzs_spmi.c`):**
   - Replaced all scheduler-dependent `delay()` calls with non-blocking hardware-loop `xzs_spmi_delay_us()` to prevent scheduler stalls.
3. **Diagnostic Hooks & Userland CLI (`src/xnu/pexpert/arm/xzs_diag.c`, `src/xzs-userland/shell.c`):**
   - Diag codes registered: 41 (`m5-status`), 42 (`m5-dryrun`), 43 (`m5-stage1`), 44 (`m5-run`).
   - Userland shell commands: `display m5-status`, `display m5-dryrun`, `display m5-stage1`, `display m5-run`.

---

## 5. Physical Hardware Verification (Two Independent Fresh Boots)

Physical hardware verification was performed on device serial `BH905SX976` across two independent fresh cold boots using boot image `artifacts/builds/xzs-xnu-boot.img` (SHA256: `4d44f7c291fd27ac91803f8d544071836fe518dbe0dfcf37cf90c481cd4adac4`).

### Run Comparison Matrix

| Parameter / Checkpoint | Run 1 (Fresh Boot 1) | Run 2 (Fresh Boot 2) | Specification / Pass Criteria |
|---|---|---|---|
| **Boot Timestamp** | 2026-09-25T06:55:29Z | 2026-09-25T07:07:54Z | Fresh physical boot |
| **Prerequisites (PLL / DSI / Lanes)** | `PLL=0x2f, DSI=0x1f5, LANE=0x1f1f` | `PLL=0x2f, DSI=0x1f5, LANE=0x1f1f` | `0x2f / 0x1f5 / 0x1f1f` |
| **Pre-Flight Safe State** | GPIO8=0, GPIO51=0, LAB=OFF, IBB=OFF | GPIO8=0, GPIO51=0, LAB=OFF, IBB=OFF | All rails OFF, Reset LOW |
| **Dry-Run Check** | `RESULT=PASS_DRYRUN` | `RESULT=PASS_DRYRUN` | `PASS_DRYRUN` |
| **Stage 1 (Reset Held LOW)** | `STAGE1_RESULT=PASS` | `STAGE1_RESULT=PASS` | VDDIO/LAB/IBB ON, Reset LOW |
| **Stage 1 Safe Power-Down** | `PASS` (Settled 300ms) | `PASS` (Settled 300ms) | All rails disabled safely |
| **Reset Assert Duration** | **10,000 µs** (10.0 ms) | **10,002 µs** (10.002 ms) | Min 10,000 µs |
| **Reset Release Stabilization** | 10,000 µs | 10,000 µs | Min 10,000 µs |
| **Powered-Idle GPIO8 (Reset)** | HIGH (`0x00000002`) | HIGH (`0x00000002`) | HIGH (Deasserted) |
| **Powered-Idle GPIO10 (TE)** | LOW (`0x00000000`) | LOW (`0x00000000`) | LOW (Inactive, scanout off) |
| **Powered-Idle GPIO51 (VDDIO)**| HIGH (`0x00000002`) | HIGH (`0x00000002`) | HIGH (1.8V Active) |
| **Powered-Idle LAB (+5.6V)** | `0xa0` (VREG_OK=1) | `0xa0` (VREG_OK=1) | VREG_OK bit set |
| **Powered-Idle IBB (-5.6V)** | `0x80` (VREG_OK=1) | `0x80` (VREG_OK=1) | VREG_OK bit set |
| **Powered-Idle DSI Host Status**| `CTRL=0x1f5, LANE=0x1f1f, FIFO=0x11111000` | `CTRL=0x1f5, LANE=0x1f1f, FIFO=0x11111000` | Stopstate on all lanes |
| **Mandatory Safe Power-Down** | `POWER_DOWN: PASS` | `POWER_DOWN: PASS` | All rails disabled safely |
| **Post-Shutdown Settle Window** | 300,000 µs (300 ms) | 300,000 µs (300 ms) | Settled before completion |
| **Lower-Layer Regression Check**| `NONE (PASS)` | `NONE (PASS)` | M2/M3/M4 intact |
| **Milestone Verdict** | **`PASS_FULL_M5`** | **`PASS_FULL_M5`** | **PASS_FULL_M5** |

---

## 6. Safety Counters & Strict Scope Locks

Throughout both full hardware bring-up runs, safety counters were strictly monitored:

| Safety Counter / Metric | Run 1 Value | Run 2 Value | Allowed Limit | Compliance Status |
|---|---|---|---|---|
| **DCS Packets Transmitted** | **0** | **0** | **0** | **STRICT COMPLIANCE** |
| **WLED Backlight Writes** | **0** | **0** | **0** | **STRICT COMPLIANCE** |
| **MDP Kickoffs / DMA Triggers**| **0** | **0** | **0** | **STRICT COMPLIANCE** |
| **BTA SW Triggers** | **0** | **0** | **0** | **STRICT COMPLIANCE** |
| **Bus Aborts** | **0** | **0** | **0** | **STRICT COMPLIANCE** |
| **SError Exceptions** | **0** | **0** | **0** | **STRICT COMPLIANCE** |
| **Kernel Panics** | **0** | **0** | **0** | **STRICT COMPLIANCE** |
| **Unintended Resets** | **0** | **0** | **0** | **STRICT COMPLIANCE** |

---

## 7. Artifact Directory Listing

All raw hardware artifacts have been captured and verified:

```text
artifacts/hw/d8m5/
├── run1/
│   ├── host.txt                      (Complete session transcript with shell output)
│   ├── metadata.json                 (Run metadata and safety results)
│   ├── pre_registers.json            (Snapshot of 35 hardware registers before power-on)
│   ├── powered_registers.json        (Snapshot of panel power rails & DSI0 host state)
│   └── post_shutdown_registers.json   (Snapshot of registers after mandatory safe shutdown)
└── run2/
    ├── host.txt                      (Complete session transcript with shell output)
    ├── metadata.json                 (Run metadata and safety results)
    ├── pre_registers.json            (Snapshot of 35 hardware registers before power-on)
    ├── powered_registers.json        (Snapshot of panel power rails & DSI0 host state)
    └── post_shutdown_registers.json   (Snapshot of registers after mandatory safe shutdown)
```

---

## 8. Milestone Seal Declaration

Milestone **D8-M5 (Keyaki Panel Power + Reset Sequence)** is hereby declared:

```text
STATUS: COMPLETE
SEALED: YES
HARDWARE_PROVEN: YES
PROVEN ON: Sony Xperia XZs (G8231 / MSM8996 v3.0 / BH905SX976)
LOCKS MAINTAINED: NO DCS, NO WLED, NO MDP SCANOUT
READY FOR D8-M6: YES
```
