# SONY XPERIA XZS (KEYAKI) DISPLAY AUDIT REPORT (D8-A0)

**Document Version:** 1.0  
**Phase:** D8-A0 (Display Source, DTB & Golden-State Audit)  
**Target Hardware:** Sony Xperia XZs G8231 (`keyaki` / `kagura-row`, MSM8996 v3.0, 4x Kryo, 4GB LPDDR4, Serial: `BH905SX976`)  
**Baseline Git Revision:** `7702715545fe027ed5d4fb2928e9dcd7618aa7a4` (tag: `xzs-d7t2-full-complete`)  
**Current Branch:** `xzs-d8-display-audit`  
**Companion Truth Table:** `docs/XZS_DISPLAY_TRUTH_TABLE.md` (`DISPLAY_TRUTH_TABLE_V1`)

---

## 1. Executive Summary

Phase **D8-A0** was established as a **strictly read-only audit phase** to eliminate all display unknowns before any register programming is undertaken for milestone D8-M3 (DSI PLL + PHY + Lane Clocks).

No DSI PLL/PHY registers were written, no DSI lanes were enabled, no panel reset was asserted, and no XNU display code was modified.

Through systematic multi-DTB container disassembly of `artifacts/builds/twrp-extracted.dtb`, comparative analysis across all 4 kernel DTBs, and correlation against target hardware boot logs (`artifacts/logs/twrp-dmesg-full.log` on physical handset `BH905SX976`), **all 64 display parameters have been 100% resolved**.

### Key Breakthrough Findings:
1. **Physical Panel Identity Proven:** The target device `BH905SX976` unambiguously uses **Sharp Panel Name "9"** (`somc,sharp_synaptics_cmd_9_panel`). The bootloader ADC read `lcdid_adc=0x270C` falls directly into the Sharp range `0x00 .. 0xdea8`.
2. **DSI Topology Proven:** Single DSI controller (DSI0 at `0x00994000`), non-split (`split_mode:0 left:0 right:0`). DSI1 is disabled (`status = "disabled"`).
3. **Display Mode Proven:** Panel operates in **DSI Command Mode** (`dsi_cmd_mode`) across 4 data lanes at 1080x1920 60 Hz (RGB888). Tearing Effect (TE) synchronization is hardware-driven via GPIO 10.
4. **Clock Parameters Proven:** Pixel clock is **149,506,560 Hz** (~149.51 MHz), DSI bit clock is **897,039,360 Hz** (~897.04 MHz), byte clock is **112,129,920 Hz** (~112.13 MHz), and target VCO is **1,794,078,720 Hz** (~1.794 GHz).
5. **PHY Timing Blob Decoded:** The 40-byte blob `qcom,mdss-dsi-panel-timings-8996` maps 8 timing control bytes to each of the 5 PHY lanes (4 data + 1 clock).
6. **Power & Reset Sequence Proven:** VDDIO logic supply (GPIO 51), followed by LAB (+5.5V) and IBB (-5.5V) LCD bias rails, followed by GPIO 8 reset toggle (10 ms low, 10 ms high), followed by DCS Sleep Out (`0x11` + 120 ms delay), Tear ON (`0x35`), and Display ON (`0x29`).

---

## 2. D8-M1 & D8-M2 Retrospective

Prior to D8-A0, two display milestones were successfully sealed and verified on physical hardware:

### D8-M1: Safe MMIO Read Architecture
- **Commit:** `861032c` (Tag: `xzs-d8-m1-complete`)
- **Deliverable:** Established a read allowlist for MMCC registers via bootstrap identity mapping (`0x00000000 - 0x01ffffff`). Prevented bus hangs by enforcing strict power-domain checks before accessing unclocked slaves.
- **Hardware Reading:** Confirmed `MMAGIC_MDSS_GDSC` = `0xa0222000` (Powered ON) and `MDSS_GDSC` = `0x00222001` (Collapsed / OFF).

### D8-M2: Power Domains & Core Clocks
- **Commit:** `545398f` (Tag: `xzs-d8-m2-complete`)
- **Deliverable:** Safely enabled the complete display power and bus clock tree:
  1. Enabled 4 critical MMAGIC branch clocks (`mmss_mmagic_ahb`, `mmss_mmagic_cfg_ahb`, `mmagic_mdss_noc_cfg_ahb`, `mmagic_mdss_axi`).
  2. Powered on `MDSS_GDSC` (`0x2304`) by clearing `SW_COLLAPSE` and polling `PWR_ON` bit 31 (`0xa0222000`).
  3. Enabled `mdss_ahb` (`0x2308`) -> `0x20008001` (enable=1, halt=0).
  4. Enabled `mdss_axi` (`0x2310`) -> `0x00006221` (enable=1, halt=0).
  5. Enabled `mdss_mdp` (`0x231c`) -> `0x00006221` (enable=1, halt=0).
- **Physical Verification:** Verified on handset `BH905SX976` with zero panics, responsive shell, and clean execution.

---

## 3. Multi-DTB Container Audit

The genuine bootloader DTB container `artifacts/builds/twrp-extracted.dtb` (size 1,590,244 bytes, SHA256: `40bcd199ecf857f6d1a33ce3fa010f2113f31cd363e021339e7c93fbe63b08b1`) was extracted and decompiled into 4 distinct FDT blobs:

```text
DTB #0: Offset 0x000000 | 397,305 B | msm-id = <246 0x30000> (MSM8996 v3.0) | PM8994 v2.0 + PMI8994 v2.0 (Active)
DTB #1: Offset 0x060ff9 | 397,305 B | msm-id = <246 0x30000> (MSM8996 v3.0) | PM8994 v2.0 + PMI8996 v1.3
DTB #2: Offset 0x0c1ff2 | 397,817 B | msm-id = <246 0x30001> (MSM8996 v3.1) | PM8994 v2.0 + PMI8994 v2.0
DTB #3: Offset 0x1231eb | 397,817 B | msm-id = <246 0x30001> (MSM8996 v3.1) | PM8994 v2.0 + PMI8996 v1.3
```

### Cross-Variant Verification:
Comparing the display blocks across all 4 DTBs proved that:
- `somc,sharp_synaptics_cmd_9_panel` has identical SHA256: `4dc567be47885a8f278a5dcfd4eb7af5aeabf5acd6f6d842dae5ce07428e5531`.
- `qcom,mdss_dsi_ctrl0@994000` has identical SHA256: `503561cba5d8d3cc6caf6632bc4898b51f18ffa71e598036776b792f8eb63afc`.
- DTB #0 was copied cleanly to `artifacts/display-audit/keyaki.dts`.

---

## 4. Exact Panel Identification

Target hardware `BH905SX976` execution in `artifacts/logs/twrp-dmesg-full.log` confirms:
```text
<5>[ 0.000000] Kernel command line: ... lcdid_adc=0x270C display_status=off ...
<6>[ 1.442796] mdss_dsi_ctrl_probe: DSI Ctrl name = MDSS DSI CTRL->0
<6>[ 1.443466] mdss_dsi_panel_driver_detection: physical:9996
<6>[ 1.443844] mdss_dsi_panel_init: Panel Name = 9
<6>[ 1.449117] mdss_dsi_ctrl_probe: Dsi Ctrl->0 initialized, DSI rev:0x10040001, PHY rev:0x2
<6>[ 1.462272] mdss_fb_probe: fb0: split_mode:0 left:0 right:0
<6>[ 1.463549] mdss_fb_register: FrameBuffer[0] 1080x1920 registered successfully!
```

- **Active Panel Node:** `somc,sharp_synaptics_cmd_9_panel`
- **Panel Name:** `"9"`
- **Manufacturer:** Sharp (LCD) + Synaptics (Touch IC)
- **ADC Match:** `lcdid_adc=0x270C` $\in [0\text{x}00, 0\text{x}dea8]$ (`somc,lcd-id-adc`)

---

## 5. DSI Topology & Geometry

- **Controller:** Single DSI controller (`DSI0` at `0x00994000`). DSI1 is disabled (`status = "disabled"`).
- **Topology:** Non-split (`split_mode:0 left:0 right:0`).
- **Data Lanes:** 4 high-speed data lanes (`lane-0-state` through `lane-3-state` enabled).
- **Mode:** Command Mode (`dsi_cmd_mode`), using hardware TE on GPIO 10 (`qcom,mdss-dsi-te-using-te-pin`).
- **Dimensions:** Width = 1080 pixels (`0x438`), Height = 1920 lines (`0x780`), BPP = 24 (RGB888).
- **Refresh Rate:** 60 Hz (`0x3c`).

---

## 6. Clock Tree & Timing Calculations

From `somc,sharp_synaptics_cmd_9_panel`:
- $H_{\text{active}} = 1080$, $HFP = 56$, $HPW = 8$, $HBP = 8 \implies H_{\text{total}} = 1152\text{ pixels}$.
- $V_{\text{active}} = 1920$, $VFP = 227$, $VPW = 8$, $VBP = 8 \implies V_{\text{total}} = 2163\text{ lines}$.

### Analytical Formulas:
$$\text{Pixel Clock} = 1152 \times 2163 \times 60 = 149,506,560\text{ Hz } (\approx 149.507\text{ MHz})$$

$$\text{DSI Bit Clock} = \frac{149,506,560 \times 24}{4} = 897,039,360\text{ Hz } (\approx 897.039\text{ MHz})$$

$$\text{DSI Byte Clock} = \frac{897,039,360}{8} = 112,129,920\text{ Hz } (\approx 112.130\text{ MHz})$$

$$\text{Target VCO} = 2 \times 897,039,360 = 1,794,078,720\text{ Hz } (\approx 1.794\text{ GHz})$$

Target VCO of 1.794 GHz falls within the 14nm VCO valid range $[1.30\text{ GHz}, 2.60\text{ GHz}]$.

---

## 7. 14nm DSI PHY & PLL Architecture

- **PLL Base Address:** `0x00994800` (length `0x188`)
- **PHY Base Address:** `0x00994400` (Common `0x100`, Lanes `0x300` starting at `0x00994500`)
- **Status Register:** `RESET_SM_READY_STATUS` at `0x00994850` (`PLL_BASE + 0x50`):
  - **Bit 5 (`0x20`):** `PLL_LOCKED` (1 = Frequency Locked)
  - **Bit 0 (`0x01`):** `PLL_READY` (1 = Reset State Machine Ready)
- **Lane Stride:** `0x80` bytes per lane:
  - Lane 0: `0x00994500`
  - Lane 1: `0x00994580`
  - Lane 2: `0x00994600`
  - Lane 3: `0x00994680`
  - Clock Lane: `0x00994700`
- **Decoded PHY Lane Timings (40 bytes):**
  - Data lanes 0..3: `24 1f 08 09 05 03 04 a0`
  - Clock lane: `24 1b 08 09 05 03 04 a0`
  - `t-clk-pre` = 43 (`0x2b`), `t-clk-post` = 27 (`0x1b`).

---

## 8. Power Rails, Regulators & GPIO Assignments

### Supply Rails:
1. `vddio-supply`: 1.8V LCD logic I/O supply (controlled via GPIO 51).
2. `lab-supply`: +5.5V positive LCD bias rail (PMI8994 QPNP LAB).
3. `ibb-supply`: -5.5V negative LCD bias rail (PMI8994 QPNP IBB).
4. `vdda-supply`: 1.25V DSI analog supply.
5. `vcca-supply`: 0.925V DSI PHY core supply.

### GPIO Mapping:
- **GPIO 8:** `disp_reset_n` (Panel Hardware Reset, Active Low)
- **GPIO 10:** `mdp_vsync` / TE (Tearing Effect Input, Active Low)
- **GPIO 51:** `lcd_vddio_en` (LCD VDDIO Rail Enable, Active High)
- **GPIO 50:** `panel_tvdd` (Touch VDDIO Enable, Active High)
- **GPIO 89:** `touch_reset_n` (Touch Reset, Active Low)
- **GPIO 125:** `touch_int_n` (Touch Interrupt, Active Low)

---

## 9. DCS Command Protocol

### Panel Initialization Sequence:
1. **Sleep Out (`DCS 0x11`):** Short write `0x11`, followed by a **120 ms mandatory delay**.
2. **Set Tear ON (`DCS 0x35`):** Long write `[35 00]` (Mode 0: V-blanking only).
3. **Set Display ON (`DCS 0x29`):** Short write `0x29`.

### Panel Shutdown Sequence:
1. **Set Display OFF (`DCS 0x28`):** Short write `0x28`.
2. **Enter Sleep Mode (`DCS 0x10`):** Short write `0x10`, followed by a **120 ms delay**.
3. **Power Down Window:** Hold unpowered for at least **300 ms** before re-enabling.

---

## 10. MDP5 Routing & Interface Mapping

- **Output Interface:** `INTF_1` (`0x00901000 + 0x6b800 = 0x0096c800`) directly wired to DSI0.
- **Layer Mixer:** `LM0` (`0x00901000 + 0x45000 = 0x00946000`).
- **Control Path:** `CTL_0` (`0x00901000 + 0x2000 = 0x00903000`).
- **PingPong Buffer:** `PP_0` (`0x00901000 + 0x71000 = 0x00972000`).
- **Continuous Splash Memory:** `0x83401000` (length `0x1400000` = 20 MB). Bootloader display is disabled (`display_status=off`, `bootloader display is off`) at OS entry.

---

## 11. Readiness Gate Evaluation

| Readiness Gate | Description | Status | Rationale |
|---|---|:---:|---|
| **`READY_FOR_D8_M3`** | DSI PLL + PHY + Lane Clocks | **READY** | All register offsets, target VCO (1.794 GHz), bit/byte clocks, PHY lane timings, and status register masks are 100% resolved. Core power domains (MDSS GDSC) are proven ON. |
| **`READY_FOR_D8_M5`** | Panel Power & Reset GPIO | **CONDITIONAL_READY** | Pin assignments (GPIO 8, 10, 51) and timing sequences are proven. Requires XNU TLMM GPIO driver and SPMI/PMIC regulator driver (or pre-configuration) for LAB/IBB bias rails. |
| **`READY_FOR_D8_M7`** | Backlight & First Pixels | **BLOCKED** | Blocked on completion of D8-M3, D8-M4, D8-M5, and D8-M6 milestones. |

---

## 12. Next Steps for D8-M3 (NO IMPLEMENTATION UNDERTAKEN)

Milestone **D8-A0 is COMPLETE**.

As mandated:
```text
DO NOT START D8-M3 YET
```

When D8-M3 is authorized:
1. Program DSI0 PLL at `0x00994800` for Target VCO = 1.794 GHz.
2. Program DSI0 PHY lanes at `0x00994500` with the 40-byte timing configuration.
3. Poll `RESET_SM_READY_STATUS` (`0x00994850`) for Lock (bit 5) and Ready (bit 0).
4. Enable MMCC DSI branch clocks (`mdss_byte0`, `mdss_pclk0`, `mdss_esc0`).
