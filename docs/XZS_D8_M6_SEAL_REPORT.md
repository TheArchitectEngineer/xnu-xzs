# XZS D8-M6 Keyaki Panel DCS / Vendor Initialization Bring-Up Seal Report

## 1. Executive Summary & Canonical Baseline

This report documents the formal completion, physical hardware verification, and sealing of **Milestone D8-M6: Keyaki Panel DCS / Vendor Initialization** on physical Sony Xperia XZs (`G8231` / `keyaki` / MSM8996 v3.0 / `BH905SX976`).

- **Prerequisites Inherited:**
  - D8-M1: Hardware Audit & Baseline Register State (Sealed `0c3eec4`)
  - D8-M2: Display Power Domains & Clocks (Sealed `cb91097`)
  - D8-M3: DSI PLL Lock & 14nm PHY Calibration (Sealed `f9baed7`)
  - D8-M4: DSI0 Host Controller Initialization (Sealed `09ae224`)
  - D8-P1: Keyaki Display TLMM GPIO Configuration (Sealed `d508493`)
  - D8-P2: Keyaki Display PMIC Regulators LAB & IBB (Sealed `cd60919`)
  - D8-M5: Keyaki Panel Power & Reset Sequence (Sealed `77b0a30`)
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

The panel initialization sequence is derived directly from the Sony downstream DTS (`somc,sharp_synaptics_cmd_9_panel`):

### Authoritative DCS Command Packets

1. **Sleep Out (`SLPOUT`, 0x11):**
   - Payload: `0x11` (DCS Short Write, 0 parameters, Data Type `0x05`)
   - DSI Packet Structure: Header Word0 = `0x80050011` (Embedded Mode, Length 4, VC 0, DT 0x05, Payload 0x11)
   - Post-Wait: **120 ms** (120,000 µs) as per MIPI DCS specification.

2. **Set Tear On (`TEON`, 0x35 0x00):**
   - Payload: `0x35, 0x00` (DSI Long Write, 2 parameters, Data Type `0x39`)
   - DSI Packet Structure: Header Word0 = `0xc0390002` (Embedded Mode, Length 8, VC 0, DT 0x39, WC 2), Word1 = `0xffff0035` (Payload `0x35, 0x00`, followed by 0xFFFF filler)
   - Post-Wait: 0 µs.

3. **Display On (`DISPON`, 0x29):**
   - Payload: `0x29` (DCS Short Write, 0 parameters, Data Type `0x05`)
   - DSI Packet Structure: Header Word0 = `0x80050029` (Embedded Mode, Length 4, VC 0, DT 0x05, Payload 0x29)
   - Post-Wait: 0 µs.

4. **Display Off (`DISPOFF`, 0x28):**
   - Payload: `0x28` (DCS Short Write, 0 parameters, Data Type `0x05`)
   - DSI Packet Structure: Header Word0 = `0x80050028` (Embedded Mode, Length 4, VC 0, DT 0x05, Payload 0x28)
   - Post-Wait: 0 µs.

5. **Sleep In (`SLPIN`, 0x10):**
   - Payload: `0x10` (DCS Short Write, 0 parameters, Data Type `0x05`)
   - DSI Packet Structure: Header Word0 = `0x80050010` (Embedded Mode, Length 4, VC 0, DT 0x05, Payload 0x10)
   - Post-Wait: **120 ms** (120,000 µs) settling period.

---

## 4. Implementation Architecture

1. **Kernel Engine (`src/xnu/pexpert/arm/xzs_d8m6.h`):**
   - Implements full state machine:
     - `xzs_d8m6_status()`: Read-only inspection of DSI0 host, PHY, PLL, and safety counters.
     - `xzs_d8m6_dryrun()`: Validation of DMA alignment, packet headers, payload encodings, and prerequisite registers without triggering DMA.
     - `xzs_d8m6_stage1()`: Powers panel to idle, transmits single command (`SLPOUT 0x11`), verifies DMA completion, and safely shuts down panel.
     - `xzs_d8m6_stage2()`: Powers panel to idle, transmits prefix commands (`SLPOUT 0x11` + `TEON 0x35 0x00`), verifies DMA completions, and safely shuts down panel.
     - `xzs_d8m6_run()`: Full canonical bring-up: Power-up to idle -> ON sequence (`SLPOUT` -> `TEON` -> `DISPON`) -> Powered-active audit & TE sampling -> OFF sequence (`DISPOFF` -> `SLPIN`) -> Safe M5 power-down.
   - Low-Power Mode (`LPM=1`) command transmission over High-Speed clock lane.
   - Cycle-accurate timing and watchdog petting on all delay intervals.
2. **Diagnostic Hooks & Userland CLI (`src/xnu/pexpert/arm/xzs_diag.c`, `src/xzs-userland/shell.c`):**
   - Diag codes registered: 45 (`m6-status`), 46 (`m6-dryrun`), 47 (`m6-stage1`), 48 (`m6-stage2`), 49 (`m6-run`).
   - Userland shell commands: `display m6-status`, `display m6-dryrun`, `display m6-stage1`, `display m6-stage2`, `display m6-run`.
3. **Execution Scripting & Test Automation (`scripts/display/d8m6_run_session.py`):**
   - Automated host session orchestrating M2 clocks, M3 PHY/PLL, M4 DSI0 host, P1 GPIOs, pre-snapshot, dry-run validation, full DCS bring-up, and post-snapshot over bulk USB console.

---

## 5. Physical Hardware Verification (Two Independent Fresh Boots)

Physical hardware verification was performed on device serial `BH905SX976` across two independent fresh cold boots using boot image `artifacts/builds/xzs-xnu-boot.img` (SHA256: `7cf626b7be0ba5cd1d5589f435113eb63df23038622eff709fd9c1bb32b71efc`).

### Run Comparison Matrix

| Parameter / Checkpoint | Run 1 (Fresh Boot 1) | Run 2 (Fresh Boot 2) | Specification / Pass Criteria |
|---|---|---|---|
| **Boot Mode** | Fresh Fastboot Boot | Fresh Fastboot Boot | Fresh cold boot from Fastboot |
| **Prerequisites (PLL / DSI / Lanes)** | `PLL=0x2f, DSI=0x1f5, LANE=0x1f1f` | `PLL=0x2f, DSI=0x1f5, LANE=0x1f1f` | `0x2f / 0x1f5 / 0x1f1f` |
| **Dry-Run Verdict** | `RESULT=PASS_DRYRUN` | `RESULT=PASS_DRYRUN` | `PASS_DRYRUN` (5/5 exact matches) |
| **DMA Buffer Physical Alignment** | `0x831a2340` (64-byte aligned: PASS) | `0x831a2340` (64-byte aligned: PASS) | 64-byte boundary requirement |
| **Panel Power-Up Sequence** | `Reset=0 -> VDDIO=1 -> LAB=+5.6V -> IBB=-5.6V -> Reset Pulse` | `Reset=0 -> VDDIO=1 -> LAB=+5.6V -> IBB=-5.6V -> Reset Pulse` | M5 canonical power sequence |
| **SLPOUT (0x11) Elapsed** | **38 µs** (DMA Done, Timeout=0) | **38 µs** (DMA Done, Timeout=0) | Valid TX, Timeout=0 |
| **TEON (0x35 0x00) Elapsed** | **38 µs** (DMA Done, Timeout=0) | **38 µs** (DMA Done, Timeout=0) | Valid TX, Timeout=0 |
| **DISPON (0x29) Elapsed** | **38 µs** (DMA Done, Timeout=0) | **38 µs** (DMA Done, Timeout=0) | Valid TX, Timeout=0 |
| **DISPOFF (0x28) Elapsed** | **38 µs** (DMA Done, Timeout=0) | **38 µs** (DMA Done, Timeout=0) | Valid TX, Timeout=0 |
| **SLPIN (0x10) Elapsed** | **44 µs** (DMA Done, Timeout=0) | **38 µs** (DMA Done, Timeout=0) | Valid TX, Timeout=0 |
| **Total DMA Triggers** | **5** | **5** | Exactly 5 |
| **Total DMA Completions** | **5** | **5** | Exactly 5 |
| **Total DMA Timeouts** | **0** | **0** | Strictly 0 |
| **Total ACK / Bus Errors** | **0** | **0** | Strictly 0 |
| **Powered-Active Checkpoint** | `PLL=0x2f, CTRL=0x1f5, FIFO=0x11111000` | `PLL=0x2f, CTRL=0x1f5, FIFO=0x11111000` | Lane & FIFO integrity intact |
| **TE Pin (GPIO10) Sampling** | 19,921 samples / ~33 ms (0 transitions, stable LOW) | 19,921 samples / ~33 ms (0 transitions, stable LOW) | Inactive (MDP scanout not running) |
| **Graceful Panel Shutdown** | `DISPOFF -> SLPIN -> M5 Power-Down` | `DISPOFF -> SLPIN -> M5 Power-Down` | Graceful sequence followed by M5 |
| **Settling Window** | 300,000 µs (300 ms) | 300,000 µs (300 ms) | 300 ms settle |
| **Acceptance Verdict** | **`PASS_ACCEPTANCE`** | **`PASS_ACCEPTANCE`** | **PASS_ACCEPTANCE** |

---

## 6. Safety Counters & Strict Scope Locks

Throughout both full hardware bring-up runs, safety counters were strictly monitored:

| Safety Counter / Metric | Run 1 Value | Run 2 Value | Allowed Limit | Compliance Status |
|---|---|---|---|---|
| **WLED Backlight Writes** | **0** | **0** | **0** | **STRICT COMPLIANCE** |
| **MDP Kickoffs / Scanouts**| **0** | **0** | **0** | **STRICT COMPLIANCE** |
| **DMA Timeouts** | **0** | **0** | **0** | **STRICT COMPLIANCE** |
| **DSI ACK Errors** | **0** | **0** | **0** | **STRICT COMPLIANCE** |
| **Bus Aborts** | **0** | **0** | **0** | **STRICT COMPLIANCE** |
| **SError Exceptions** | **0** | **0** | **0** | **STRICT COMPLIANCE** |
| **Kernel Panics** | **0** | **0** | **0** | **STRICT COMPLIANCE** |
| **Unintended Resets** | **0** | **0** | **0** | **STRICT COMPLIANCE** |

---

## 7. Register Stability Verification

Pre- and post-execution snapshots of 35 display controller and PHY registers were captured in both runs:
- `artifacts/hw/d8m6/run1/pre_registers.json` == `artifacts/hw/d8m6/run1/post_registers.json`
- `artifacts/hw/d8m6/run2/pre_registers.json` == `artifacts/hw/d8m6/run2/post_registers.json`
- `artifacts/hw/d8m6/run1/post_registers.json` == `artifacts/hw/d8m6/run2/post_registers.json`

Cross-run register diff: **0 differences (100% deterministic)**.

---

## 8. Milestone Sealing Verdict

Milestone **D8-M6: Keyaki Panel DCS / Vendor Initialization** satisfies all gating criteria:
1. Canonical DCS command packets transmitted to the physical panel controller over MIPI DSI0 in command mode.
2. 5 DMA commands dispatched and completed with zero timeouts and zero ACK errors.
3. Strictly zero WLED backlight writes (`WLED_WRITES=0`) and zero MDP scanout kickoffs (`MDP_KICKOFFS=0`).
4. Graceful shutdown to safe M5 power state verified.
5. Two consecutive fresh cold boot runs on target hardware PASS with identical register states and zero regressions.
6. Zero ARMv8.3 PAC instructions in the kernel binary.

**Status: SEALED & HARDWARE-PROVEN.**
