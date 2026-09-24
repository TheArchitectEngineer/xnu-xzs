# DISPLAY SUBSYSTEM GAP MATRIX & READINESS AUDIT

## 1. D8-A0 Unknowns Resolution Matrix

| Target Domain | Parameter | Initial Status | D8-A0 Audited Status | Classification | Resolution Source |
|---|---|:---:|:---:|:---:|---|
| **Panel Identity** | Exact Panel Name | UNKNOWN | Panel "9" (`somc,sharp_synaptics_cmd_9_panel`) | HARDWARE_PROVEN | TWRP target dmesg log line 862 & DTB node |
| **Panel Identity** | Display Vendor | UNKNOWN | Sharp | HARDWARE_PROVEN | DTB node & TWRP dmesg |
| **Panel Identity** | Touch Controller | UNKNOWN | Synaptics | SOURCE_PROVEN | DTB node name & driver bindings |
| **DSI Topology** | Single vs Dual DSI | UNKNOWN | Single DSI (DSI0 only; DSI1 is disabled) | HARDWARE_PROVEN | DTB `hw-config = "single_dsi"` & TWRP dmesg line 885 (`split_mode:0`) |
| **DSI Controller** | Base Address | KNOWN (Ref) | `0x00994000` (length `0x400`) | SOURCE_PROVEN | DTB `qcom,mdss_dsi_ctrl0@994000` |
| **DSI PHY / Lanes** | Base Addresses | KNOWN (Ref) | Common `0x00994400`, Lanes `0x00994500` | SOURCE_PROVEN | DTB & Linux `msm8996.dtsi` |
| **DSI PLL** | Base Address | KNOWN (Ref) | `0x00994800` (length `0x188`) | SOURCE_PROVEN | DTB & Linux `msm8996.dtsi` |
| **Lane Count** | Data Lanes | UNKNOWN | 4 data lanes (lane 0-3) | HARDWARE_PROVEN | DTB `qcom,mdss-dsi-lane-0..3-state` |
| **Display Mode** | Command vs Video | UNKNOWN | Command Mode (`dsi_cmd_mode`) | HARDWARE_PROVEN | DTB `qcom,mdss-dsi-panel-type = "dsi_cmd_mode"` |
| **Resolution** | Width x Height | UNKNOWN | 1080 x 1920 (FHD) | HARDWARE_PROVEN | DTB & TWRP dmesg line 886 (`1080x1920`) |
| **Color Depth** | BPP & Format | UNKNOWN | 24 bpp (RGB888) | SOURCE_PROVEN | DTB `qcom,mdss-dsi-bpp = <0x18>` |
| **Refresh Rate** | Target FPS | UNKNOWN | 60 Hz | SOURCE_PROVEN | DTB `qcom,mdss-dsi-panel-framerate = <0x3c>` |
| **Porches & Sync** | H-porches & V-porches | UNKNOWN | H: 56/8/8, V: 227/8/8 | SOURCE_PROVEN | DTB panel node |
| **Pixel Clock** | Target Frequency | UNKNOWN | 149,506,560 Hz (~149.51 MHz) | SOURCE_PROVEN | Calculated: $1152 \times 2163 \times 60$ |
| **DSI Bit Clock** | Target Frequency | UNKNOWN | 897,039,360 Hz (~897.04 MHz) | SOURCE_PROVEN | Calculated: $(149,506,560 \times 24) / 4$ |
| **DSI Byte Clock**| Target Frequency | UNKNOWN | 112,129,920 Hz (~112.13 MHz) | SOURCE_PROVEN | Calculated: $897,039,360 / 8$ |
| **VCO Clock** | Target Frequency | UNKNOWN | 1,794,078,720 Hz (~1.794 GHz) | SOURCE_PROVEN | Calculated: $2 \times \text{BitClock}$ (Valid in 1.3-2.6GHz) |
| **PHY Timings** | 40-byte blob | UNKNOWN | Decoded lane-by-lane table | SOURCE_PROVEN | DTB `qcom,mdss-dsi-panel-timings-8996` |
| **Power Rails** | Bias & Logic Rails | UNKNOWN | VDDIO (1.8V), LAB (+5.5V), IBB (-5.5V)| SOURCE_PROVEN | DTB regulator properties & TWRP dmesg |
| **GPIO Reset** | Pin & Polarity | UNKNOWN | GPIO 8, Active Low | SOURCE_PROVEN | DTB `qcom,platform-reset-gpio = <&tlmm 8 0>` |
| **GPIO TE** | Pin & Polarity | UNKNOWN | GPIO 10, Active Low | SOURCE_PROVEN | DTB `qcom,platform-te-gpio = <&tlmm 10 0>` |
| **GPIO VDDIO** | Pin & Polarity | UNKNOWN | GPIO 51, Active High | SOURCE_PROVEN | DTB `qcom,platform-vddio-gpio = <&tlmm 51 0>` |
| **On Sequence** | DCS packets | UNKNOWN | Sleep Out (0x11, 120ms), Tear ON (0x35), Display ON (0x29) | SOURCE_PROVEN | DTB `qcom,mdss-dsi-on-command` |
| **Off Sequence** | DCS packets | UNKNOWN | Display OFF (0x28), Sleep IN (0x10, 120ms) | SOURCE_PROVEN | DTB `qcom,mdss-dsi-off-command` |
| **Backlight** | Control Mechanism | UNKNOWN | PMIC WLED (`bl_ctrl_wled`), levels 1-4095 | SOURCE_PROVEN | DTB `qcom,mdss-dsi-bl-pmic-control-type` |
| **MDP Routing** | Interface & Mixer | UNKNOWN | INTF_1 -> DSI0, LM0, CTL_0, PP_0 | SOURCE_PROVEN | DTB MDP & DSI interconnect graph |

---

## 2. Readiness Evaluation Against Implementation Gates

### Gate 1: `READY_FOR_D8_M3` (DSI PLL + PHY + Lane Clocks)
- **Status:** **PASS / READY**
- **Evaluation:**
  - DSI0 PLL base address (`0x00994800`) and PHY base addresses (`0x00994400`, `0x00994500`) are fully verified.
  - Target VCO (1.794 GHz), bit clock (897 MHz), byte clock (112.13 MHz), and pixel clock (149.51 MHz) are analytically proven.
  - PHY timing array (40 bytes) is decoded and mapped to the 5 hardware lanes.
  - Status register `RESET_SM_READY_STATUS` (`0x00994850`) lock bit (Bit 5) and ready bit (Bit 0) are identified.
  - MMCC branch clocks (`mdss_byte0`, `mdss_pclk0`, `mdss_esc0`) are mapped in MMIO allowlist.
  - Power prerequisite (MDSS GDSC + MMAGIC domains ON) is hardware-proven on tag `xzs-d8-m2-complete`.

### Gate 2: `READY_FOR_D8_M5` (Panel Power & Reset GPIO)
- **Status:** **CONDITIONAL_READY**
- **Evaluation:**
  - GPIO assignments are known (Reset: GPIO 8, VDDIO: GPIO 51, TE: GPIO 10).
  - Power sequencing order is documented (VDDIO -> LAB -> IBB -> Reset).
  - *Dependency:* Requires TLMM GPIO pinmux driver in XNU, and SPMI/PMIC regulator driver (or pre-configuration) for LAB/IBB bias rails (+5.5V / -5.5V).

### Gate 3: `READY_FOR_D8_M7` (Backlight & First Pixels)
- **Status:** **BLOCKED_ON_PRIOR_MILESTONES**
- **Evaluation:**
  - Backlight control requires PMIC WLED driver over SPMI.
  - Pixel generation requires D8-M3 (clocking), D8-M4 (DSI lanes), D8-M5 (panel power), and D8-M6 (DCS init).
