# Qualcomm MSM8996 UFS QMP 14nm PHY Audit

## 1. Physical Layout & Base Addresses

Derived from `device/reference/msm8996.dtsi:2192`, Linux `drivers/phy/qualcomm/phy-qcom-ufs-qmp-14nm.c` & `phy-qcom-ufs-qmp-14nm.h`.

| Sub-block | Physical Base | Offset Range | Size | Description |
| :--- | :--- | :--- | :--- | :--- |
| **UFS PHY MMIO Aperture** | `0x00627000` | `+0x000 - +0xFFF` | `0x1000` (4 KB) | Entire QMP 14nm UFS PHY block |
| **QSERDES COM (Common)** | `0x00627000` | `+0x000 - +0x3FF` | `0x400` (1 KB) | PLL, VCO, BIAS, Clock Distribution |
| **TX Lane 0** | `0x00627400` | `+0x400 - +0x5FF` | `0x200` (512 B) | Transmit lane 0 analog front-end |
| **RX Lane 0** | `0x00627600` | `+0x600 - +0x7FF` | `0x200` (512 B) | Receive lane 0 analog front-end |
| **TX Lane 1** | `0x00627800` | `+0x800 - +0x9FF` | `0x200` (512 B) | Transmit lane 1 (optional / 2-lane mode) |
| **RX Lane 1** | `0x00627A00` | `+0xA00 - +0xBFF` | `0x200` (512 B) | Receive lane 1 (optional / 2-lane mode) |
| **PCS (Physical Coding Sublayer)** | `0x00627C00` | `+0xC00 - +0xFFF` | `0x400` (1 KB) | Digital control, power-down, ready status |

---

## 2. Clock & Reset Topology

### 2.1 Source Lineage & PHY Input Reference Clocks

* **Source Lineage:** 
  - Mainline device tree: `device/reference/msm8996.dtsi:2196` (`qcom,msm8996-ufs-phy-qmp-14nm`)
  - Sony Tone platform DT: `device/reference/msm8996-sony-xperia-tone.dtsi:461`
  - GCC Clock Binding: `include/dt-bindings/clock/qcom,gcc-msm8996.h:224` (`#define GCC_UFS_CLKREF_CLK 215`)
  - Linux QMP UFS Driver: `drivers/phy/qualcomm/phy-qcom-qmp-ufs.c` (`msm8996_ufsphy_cfg`)

| Clock Name | DTS Name | Provider | Frequency / Control | Register / Mechanism | Description |
| :--- | :--- | :--- | :--- | :--- | :--- |
| **`ref`** | `ref` | `rpmcc RPM_SMD_LN_BB_CLK` | `19.2 MHz` XO | RPM SMD voting channel | Low-noise baseband reference clock (bootloader-initialized, unowned by XNU) |
| **`qref`** | `qref` | `gcc GCC_UFS_CLKREF_CLK` | Branch gate | GCC MMIO `0x00388008` (`GCC + 0x88008`) | GCC reference clock gate for UFS PHY (bit 0: ENABLE, bit 31: CLK_OFF) |

### 2.2 Lane & Symbol Clocks (PHY Outputs -> GCC Inputs)

> [!IMPORTANT]
> The PHY PLL output is the physical clock generator for the TX/RX lane symbol clocks.
> Lane symbol clocks CANNOT run before the PHY is initialized and powered on.

| Symbol Clock | GCC Branch | Source | Description |
| :--- | :--- | :--- | :--- |
| **`tx_lane0_sync_clk`** | `GCC_UFS_TX_SYMBOL_0_CLK` | UFS PHY TX PLL | TX symbol interface clock (Lane 0) |
| **`rx_lane0_sync_clk`** | `GCC_UFS_RX_SYMBOL_0_CLK` | UFS PHY CDR / RX | RX symbol interface clock (Lane 0) |

### 2.3 Reset Topology

* **Host / Core Controller Reset:** `GCC_REG_UFS_BCR` @ `0x00375000` (`GCC + 0x75000`):
  * Source reference: Linux `drivers/clk/qcom/gcc-msm8996.c`: `[GCC_UFS_BCR] = { 0x75000 }`.
  * Reset mechanism: Linux `drivers/clk/qcom/reset.c`: `mask = map->bitmask ? map->bitmask : BIT(map->bit)` -> evaluates to `BIT(0)` (`BCR_BLK_ARES`).
  * Assert semantics: write `BCR | BIT(0)` -> mandatory dummy readback -> hold for ~200 µs (3–4 cycles of 32.768 kHz sleep clock ≈ 125 µs min).
  * Deassert semantics: write `BCR & ~BIT(0)` -> mandatory dummy readback -> settle for ~1000 µs (1 ms).
* **PHY Soft Reset:** Provided via UFS Host Controller extension register `REG_UFS_CFG1` @ `0x006240DC`:
  * `bit 1`: `UFS_PHY_SOFT_RESET` (1 = Reset asserted, 0 = Reset deasserted).
  * In `msm8996.dtsi:2199`: `resets = <&ufshc 0>; reset-names = "ufsphy"`.
  * In Linux `ufs-qcom.c`: Host asserts reset -> powers/programs PHY -> deasserts reset -> starts SerDes.

---

## 3. MSM8996 QMP 14nm Calibration Tables (Source: Linux v4.19 / v5.10)

### 3.1 QSERDES COM Register Map & Calibration Table (`msm8996_ufsphy_serdes`)
Source Reference: Linux `drivers/phy/qualcomm/phy-qcom-ufs-qmp-14nm.h` & `drivers/phy/qualcomm/phy-qcom-qmp.h`

| Register Name | Offset (COM) | Absolute PHY Address | Type | Value (Rate A) | Description / Source Reference |
| :--- | :--- | :--- | :--- | :--- | :--- |
| `QSERDES_COM_ATB_SEL1` | `+0x000` | `0x00627000` | Config | *(Uncalibrated)* | Analog Test Bus Select 1 (NOT CMN_CONFIG!) |
| `QSERDES_COM_BG_TIMER` | `+0x00C` | `0x0062700C` | Config | `0x0A` | Bandgap timer (`phy-qcom-ufs-qmp-14nm.h:35`) |
| `QSERDES_COM_BIAS_EN_CLKBUFLR_EN` | `+0x034` | `0x00627034` | Config | `0x08` | Bias & clock buffer enable (`phy-qcom-ufs-qmp-14nm.h:36`) |
| `QSERDES_COM_SYS_CLK_CTRL` | `+0x03C` | `0x0062703C` | Config | `0x06` | System clock control (`phy-qcom-ufs-qmp-14nm.h:37`) |
| `QSERDES_COM_LOCK_CMP1_MODE0` | `+0x04C` | `0x0062704C` | Config | `0xFF` | Lock compare 1 (Mode 0) |
| `QSERDES_COM_LOCK_CMP2_MODE0` | `+0x050` | `0x00627050` | Config | `0x0C` | Lock compare 2 (Mode 0) |
| `QSERDES_COM_LOCK_CMP3_MODE0` | `+0x054` | `0x00627054` | Config | `0x00` | Lock compare 3 (Mode 0) |
| `QSERDES_COM_CP_CTRL_MODE0` | `+0x078` | `0x00627078` | Config | `0x0B` | Charge pump control (Mode 0) |
| `QSERDES_COM_PLL_RCTRL_MODE0` | `+0x084` | `0x00627084` | Config | `0x16` | PLL R-control (Mode 0) |
| `QSERDES_COM_PLL_CCTRL_MODE0` | `+0x090` | `0x00627090` | Config | `0x28` | PLL C-control (Mode 0) |
| `QSERDES_COM_SYSCLK_EN_SEL` | `+0x0AC` | `0x006270AC` | Config | `0xD7` | System clock enable select (`phy-qcom-ufs-qmp-14nm.h:50`) |
| `QSERDES_COM_RESETSM_CNTRL` | `+0x0B4` | `0x006270B4` | Config | `0x20` | Reset state machine control (`phy-qcom-ufs-qmp-14nm.h:51`) |
| `QSERDES_COM_LOCK_CMP_EN` | `+0x0C8` | `0x006270C8` | Config | `0x01` | Lock compare enable (Config, NOT status!) |
| `QSERDES_COM_LOCK_CMP_CFG` | `+0x0CC` | `0x006270CC` | Config | `0x00` | Lock compare config |
| `QSERDES_COM_DEC_START_MODE0` | `+0x0D0` | `0x006270D0` | Config | `0x82` | Dec start (Mode 0) |
| `QSERDES_COM_DEC_START_MODE1` | `+0x0D4` | `0x006270D4` | Config | `0x98` | Dec start (Mode 1) |
| `QSERDES_COM_VCO_TUNE_CTRL` | `+0x124` | `0x00627124` | Config | `0x10` | VCO tuning control (`phy-qcom-ufs-qmp-14nm.h:58`) |
| `QSERDES_COM_VCO_TUNE_MAP` | `+0x128` | `0x00627128` | Config | `0x54` | VCO tuning map (Rate B: 0x54) |
| `QSERDES_COM_VCO_TUNE1_MODE0` | `+0x12C` | `0x0062712C` | Config | `0x28` | VCO tune 1 (Mode 0) |
| `QSERDES_COM_VCO_TUNE2_MODE0` | `+0x130` | `0x00627130` | Config | `0x02` | VCO tune 2 (Mode 0) |
| `QSERDES_COM_VCO_TUNE_TIMER1` | `+0x144` | `0x00627144` | Config | `0xFF` | VCO tune timer 1 |
| `QSERDES_COM_VCO_TUNE_TIMER2` | `+0x148` | `0x00627148` | Config | `0x3F` | VCO tune timer 2 |
| `QSERDES_COM_CLK_SELECT` | `+0x174` | `0x00627174` | Config | `0x30` | Clock select (`phy-qcom-ufs-qmp-14nm.h:66`) |
| `QSERDES_COM_HSCLK_SEL` | `+0x178` | `0x00627178` | Config | `0x05` | High speed clock select (`phy-qcom-ufs-qmp-14nm.h:67`) |
| `QSERDES_COM_CORECLK_DIV` | `+0x184` | `0x00627184` | Config | `0x0A` | Core clock divider (`phy-qcom-ufs-qmp-14nm.h:68`) |
| `QSERDES_COM_CORE_CLK_EN` | `+0x18C` | `0x0062718C` | Config | `0x00` | Core clock enable (`phy-qcom-ufs-qmp-14nm.h:69`) |
| **`QSERDES_COM_C_READY_STATUS`** | **`+0x190`** | **`0x00627190`** | **Status** | **Read-Only** | **Common block PLL lock/ready status (bit 0 = C_READY)** |
| **`QSERDES_COM_CMN_CONFIG`** | **`+0x194`** | **`0x00627194`** | **Config** | **`0x0E`** | **Common configuration (`phy-qcom-ufs-qmp-14nm.h:70`)** |
| `QSERDES_COM_SVS_MODE_CLK_SEL` | `+0x19C` | `0x0062719C` | Config | `0x05` | SVS mode clock select (`phy-qcom-ufs-qmp-14nm.h:71`) |
| `QSERDES_COM_CORECLK_DIV_MODE1` | `+0x1BC` | `0x006271BC` | Config | `0x0A` | Core clock divider mode 1 (`phy-qcom-ufs-qmp-14nm.h:72`) |

### 3.2 TX Table (`msm8996_ufsphy_tx` @ `+0x400`)
Source Reference: Linux `drivers/phy/qualcomm/phy-qcom-ufs-qmp-14nm.h:80-81`

| Register Name | Offset (PHY) | Offset (TX0) | Type | Value | Description |
| :--- | :--- | :--- | :--- | :--- | :--- |
| `QSERDES_TX_HIGHZ_TRANSCEIVER_BIAS_DRVR_EN` | `+0x468` | `+0x068` | Config | `0x45` | Transceiver bias / driver enable |
| `QSERDES_TX_LANE_MODE` | `+0x494` | `+0x094` | Config | `0x02` | Lane mode |

### 3.3 RX Table (`msm8996_ufsphy_rx` @ `+0x600`)
Source Reference: Linux `drivers/phy/qualcomm/phy-qcom-ufs-qmp-14nm.h:84-94`

| Register Name | Offset (PHY) | Offset (RX0) | Type | Value | Description |
| :--- | :--- | :--- | :--- | :--- | :--- |
| `QSERDES_RX_UCDR_FASTLOCK_FO_GAIN` | `+0x640` | `+0x040` | Config | `0x0B` | UCDR fastlock gain |
| `QSERDES_RX_RX_TERM_BW` | `+0x690` | `+0x090` | Config | `0x5B` | Termination bandwidth |
| `QSERDES_RX_RX_EQ_GAIN1_LSB` | `+0x6C4` | `+0x0C4` | Config | `0xFF` | Equalizer gain 1 LSB |
| `QSERDES_RX_RX_EQ_GAIN1_MSB` | `+0x6C8` | `+0x0C8` | Config | `0x3F` | Equalizer gain 1 MSB |
| `QSERDES_RX_RX_EQ_GAIN2_LSB` | `+0x6CC` | `+0x0CC` | Config | `0xFF` | Equalizer gain 2 LSB |
| `QSERDES_RX_RX_EQ_GAIN2_MSB` | `+0x6D0` | `+0x0D0` | Config | `0x0F` | Equalizer gain 2 MSB |
| `QSERDES_RX_RX_EQU_ADAPTOR_CNTRL2` | `+0x6D8` | `+0x0D8` | Config | `0x0E` | Adaptor control 2 |
| `QSERDES_RX_SIGDET_CNTRL` | `+0x714` | `+0x114` | Config | `0x02` | Signal detect control |
| `QSERDES_RX_SIGDET_LVL` | `+0x718` | `+0x118` | Config | `0x24` | Signal detect level |
| `QSERDES_RX_SIGDET_DEGLITCH_CNTRL` | `+0x71C` | `+0x11C` | Config | `0x18` | Deglitch control |
| `QSERDES_RX_RX_INTERFACE_MODE` | `+0x72C` | `+0x12C` | Config | `0x00` | Interface mode |

### 3.4 PCS Control & Status Registers (`@ +0xC00`)
Source Reference: Linux `drivers/phy/qualcomm/phy-qcom-ufs-qmp-14nm.h:75-77`

| Register Name | Offset (PHY) | Offset (PCS) | Type | Bitfields | Description |
| :--- | :--- | :--- | :--- | :--- | :--- |
| `UFS_PHY_PHY_START` | `+0xC00` | `+0x000` | Config | bit 0: `SERDES_START` | 1 = Start SerDes |
| `UFS_PHY_POWER_DOWN_CONTROL` | `+0xC04` | `+0x004` | Config | bit 0: `SW_PWRDN` | 1 = Powered Up / Active, 0 = Power-down |
| **`UFS_PHY_PCS_READY_STATUS`** | **`+0xD68`** | **`+0x168`** | **Status** | **bit 0: `PCS_READY`** | **1 = PCS Ready, 0 = Not ready / calibrating** |

---

## 3.5 Sony Xperia XZs (MSM8996) Device-Tree & PMIC Supply Mapping Audit
Audited live from device (`BH905SX976`) TWRP recovery (`/sys/firmware/devicetree/base` & `/sys/kernel/debug/regulator`):

| Logical Supply | DT Property | Phandle | PMIC Regulator | Expected Voltage | TWRP Measured State | Note |
| :--- | :--- | :--- | :--- | :--- | :--- | :--- |
| `vdda-phy` | `vdda-phy-supply` | `0x2a` | `pm8994_l28` | 0.925 V (`925000` uV) | **ENABLED** (`use_count=1, voltage=925000`) | Shared with ssphy-vdd |
| `vdda-pll` | `vdda-pll-supply` | `0x43` | `pm8994_l12` | 1.800 V (`1800000` uV) | **ENABLED** (`use_count=3, voltage=1800000`) | Shared with ssphy-vdda18 |
| `vddp-ref-clk` | `vddp-ref-clk-supply` | `0x121` | `pm8994_l25` | 1.200 V (`1200000` uV) | **DISABLED** (`enable=0, use_count=0`) | **KEY ANALOG CANDIDATE** |
| `vcc-supply` | `vcc-supply` | `0x10f` | `pm8994_l20` | 2.950 V (`2950000` uV) | **DISABLED** (`enable=0, use_count=0`) | UFS Flash VCC |
| `vccq-supply` | `vccq-supply` | `0x121` | `pm8994_l25` | 1.200 V (`1200000` uV) | **DISABLED** (`enable=0, use_count=0`) | UFS Flash VCCQ |
| `vccq2-supply` | `vccq2-supply` | `0x2e` | `pm8994_s4` | 1.800 V (`1800000` uV) | **ENABLED** (`use_count=2, voltage=1800000`) | PM8994 SMPS 4 |
| `vdd-hba` | `vdd-hba-supply` | `0x120` | `gdsc_ufs` (`@0x375004`) | Internal GDSC | **ON** in XNU (measured in D2-A.1) | GCC UFS GDSC |

### 3.6 Clock Tree & RPM Resource Lineage Audit
Audited live from device (`BH905SX976`) TWRP recovery (`/sys/kernel/debug/clk`):

| Clock Name | DT Name | Type / Source | TWRP State | Role in MSM8996 UFS Bring-up |
| :--- | :--- | :--- | :--- | :--- |
| `ln_bb_clk` | `ref_clk_src` | RPM SMD Resource (`is_local=0`) | `enable=1, rate=1000` | 19.2 MHz reference clock from PMIC/RPM |
| `gcc_ufs_clkref_clk` | `ref_clk` | GCC MMIO Branch (`is_local=1`, CBCR `0x88008`) | `enable=0, rate=0` | Enabled in XNU by `xzs_ufs_phase_d2c2_probe` |
| `gcc_ufs_ahb_clk` | `iface_clk` | GCC MMIO Branch (CBCR `0x7500C`) | `enable=0` in TWRP | AHB bus clock, enabled in XNU |
| `gcc_ufs_axi_clk` | `core_clk` | GCC MMIO Branch (CBCR `0x75008`) | `enable=0` in TWRP | Core AXI clock, enabled in XNU |
| `gcc_aggre2_ufs_axi_clk`| `bus_aggr_clk`| GCC MMIO Branch (CBCR `0x83014`) | `enable=0` in TWRP | Interconnect clock, enabled in XNU |
| `gcc_sys_noc_ufs_axi_clk`| `bus_clk` | GCC MMIO Branch (CBCR `0x75038`) | `enable=0` in TWRP | System NoC clock, enabled in XNU |

---

## 4. Subphase Strategy

1. **Phase D2-C1 (Current):**
   - READ-ONLY audit of PHY MMIO aperture (`0x00627000`, size `0x1000`).
   - Read and log initial state of COM (`+0x000`), TX (`+0x400`), RX (`+0x600`), and PCS (`+0xC00`).
   - Read and log Controller vendor registers: `REG_UFS_CFG1` (`0x006240DC`), `REG_UFS_CFG2` (`0x006240E0`).
   - Verify that PHY aperture is accessible without ARM64 SError.
   - Absolutely NO writes to PHY or HCE.
2. **Phase D2-C2 (Subsequent):**
   - Execute exact MSM8996 PHY initialization sequence.
3. **Phase D2-C3 (Final of D2-C):**
   - Enable `HCE` and verify controller ready state.

---

## 5. Phase D2-C1 — Hardware-Verified Measurement Results

Executed on Sony Xperia XZs (`BH905SX976`) with automated telemetry pipeline.

### 5.1 Host Controller Vendor Registers (@ `0x00624000`)

| Register | Offset | Raw Silicon Value | Decoded Meaning |
| :--- | :--- | :--- | :--- |
| **`CAP`** | `+0x00` | `0x0107000F` | Verified UFSHCI capabilities (16 transfer req slots, 8 task slots, 64AS=1) |
| **`QCOM_HW_VER`** | `+0xE4` | `0x20020000` | Qualcomm Controller v2.2.0 (`Major=2, Minor=2, Step=0`) |
| **`REG_UFS_CFG1`** | `+0xDC` | `0x1C00052C` | `bit 1: UFS_PHY_SOFT_RESET = 0` (PHY reset **RELEASED**), `bit 0: QUNIPRO = 0` |
| **`REG_UFS_CFG2`** | `+0xE0` | `0x3F011300` | Bootloader-configured internal CGC / timing register |
| **`HCS`** | `+0x30` | `0x00000000` | Device not present, transfer engine idle |
| **`HCE`** | `+0x34` | `0x00000000` | Host Controller disabled |

### 5.2 QMP 14nm PHY Aperture Accessibility & Initial Register Audit (@ `0x00627000`)

* **MMIO Mapping:** Mapped at VA `0xfffffecf7b29f000` (size `0x1000`).
* **Bus Accessibility:** **PASS — 100% accessible, ZERO SError / External Abort.**

| Sub-block | Offset | Register Name | Measured Initial Value | Note |
| :--- | :--- | :--- | :--- | :--- |
| **COM** | `+0x000` | `QSERDES_COM_CMN_CONFIG` | `0x00000001` | Initial uncalibrated state |
| **COM** | `+0x00C` | `QSERDES_COM_BG_TIMER` | `0x0000000A` | Matches calibration table default (0x0A) |
| **COM** | `+0x03C` | `QSERDES_COM_SYS_CLK_CTRL` | `0x00000006` | Matches calibration table default (0x06) |
| **COM** | `+0x0AC` | `QSERDES_COM_SYSCLK_EN_SEL` | `0x00000010` | Initial state (calibration writes `0xD7`) |
| **TX** | `+0x400` | TX block header | `0x00000000` | Default reset |
| **TX** | `+0x404` | `QSERDES_TX_LANE_MODE` | `0x00000000` | Default reset (calibration writes `0x02`) |
| **TX** | `+0x468` | `QSERDES_TX_HIGHZ_TRANSCEIVEREN...` | `0x00000005` | Initial state (calibration writes `0x45`) |
| **RX** | `+0x600` | `QSERDES_RX_RX_INTERFACE_MODE` | `0x0000000A` | Initial state (calibration writes `0x00`) |
| **RX** | `+0x624` | `QSERDES_RX_SIGDET_LVL` | `0x0000000A` | Initial state (calibration writes `0x24`) |
| **RX** | `+0x628` | `QSERDES_RX_SIGDET_CNTRL` | `0x0000000A` | Initial state (calibration writes `0x02`) |
| **PCS** | `+0xC00` | `QPHY_START` | `0x00000000` | SerDes currently inactive (`START=0`) |
| **PCS** | `+0xC04` | `QPHY_POWER_DOWN_CONTROL` | `0x00000000` | Power down control |
| **PCS** | `+0xCA8` | Unidentified PCS internal register | `0x00000001` | Previously mislabeled as PCS_READY; actual PCS_READY_STATUS is at `+0xD68` |

### 5.3 Hardware-Verified Findings & Invariants

1. **UFS PHY MMIO is completely accessible:** Reading registers across all 4 sub-blocks (`COM`, `TX`, `RX`, `PCS`) did not trigger any bus fault or SError.
2. **PHY Soft Reset is ALREADY RELEASED:** `REG_UFS_CFG1` (`0xDC`) bit 1 (`UFS_PHY_SOFT_RESET`) was measured as `0`.
3. **No Controller Reset Required:** The bootloader has already released both `GCC_UFS_BCR` (block reset) and `UFS_PHY_SOFT_RESET` (PHY soft reset).
4. **Automated Recovery Pipeline:** 100% automated with zero manual button presses (`+4s` warm reboot, TWRP dump, Fastboot return).

---

## 6. Phase D2-C2.4A — QSERDES Status & HCI Identity Stability Audit

### 6.1 Corrected QSERDES COM v2 Status Registers (@ `0x00627000`)
- `QSERDES_COM_ATB_SEL1`: `COM + 0x000`
- `QSERDES_COM_C_READY_STATUS`: `COM + 0x190` (Read-only status, bit 0: Common SerDes Ready)
- `QSERDES_COM_CMN_CONFIG`: `COM + 0x194` (Configuration, `0x0E`)
- `UFS_PHY_PCS_READY_STATUS`: `PCS + 0x168` = `PHY + 0xD68` (Read-only status, bit 0: PCS Ready)

### 6.2 3-Point Lifecycle Status Measurement
- **Point A (Pre-Calibration):** `C_READY_STATUS = 0`, `PCS_READY = 0`
- **Point B (Post-Calibration):** `C_READY_STATUS = 0`, `PCS_READY = 0`
- **Point C (Post-Start & Poll):** `C_READY_STATUS = 0`, `PCS_READY = 0`
- **Conclusion (Result A):** Common SerDes PLL layer does not reach lock/ready (`C_READY = 0`).

### 6.3 UFSHCI Identity Stability Audit
- 8 consecutive reads across Stage 1 (Pre-Reset), Stage 2 (Post-Core-Reset), Stage 3 (Post-PHY-Init):
  - `CAP = 0x0107001F`: 32 transfer req slots, 8 task slots, 64AS=1 (100% stable)
  - `VER = 0x00010100`: UFSHCI v1.1.0 (100% stable)
  - `QCOM_HW_VER = 0x20020000`: Controller v2.2.0 (100% stable)
  - `HCPID = 0x01000000`, `HCMID = 0x00010217` (100% stable)

---

## 7. Phase D2-C2.4B — PM8994 L25 / SPMI Read-Only Resource Audit

### 7.1 SPMI Architecture on Qualcomm MSM8996
- **SPMI Arbiter Controller:** PMIC Arbiter Version-2 (v2), physical base `0x0400F000` (Core, 4KB), `0x04C00000` (Observer Channels, 8MB).
- **HW Version Verified:** `spmi_core_read32(0x00) = 0x20010000` (PMIC Arbiter v2.0.1, matching Linux/TWRP kernel).
- **APID Allocation Mechanism:**
  - Table at `core + 0x800 + 4 * apid` (apid 0..511).
  - Channel window: `obsrvr_base + 0x8000 * apid`.
  - Transaction format: `(PMIC_ARB_OP_EXT_READL << 27) | ((addr & 0xFF) << 4) | 0`.

### 7.2 Hardware APID Discovery Results (APPS EE 0)
- **PM8994 REVID (`0x0100`):** `APID = 0x50` (Mapped)
- **PM8994 L25 (`0x5800`):** `APID = 0x82` (Mapped)
- **PM8994 L26 (`0x5900`):** `APID = 0x61` (Mapped)
- **PM8994 L27 (`0x5A00`):** `APID = 0x84` (Mapped)
- **PM8994 L28 (`0x5B00`):** `APID = 0x9C` (Mapped)
- **PM8994 L12 (`0x4B00`):** **UNMAPPED** in APPS EE (EE 0) table. L12 is owned exclusively by RPM SMD.

### 7.3 Sanity Control: PM8994 REVID Verification
- 8 consecutive reads of `0x0104` (TYPE) and `0x0105` (SUBTYPE):
  - 8/8 reads: `TYPE = 0x51`, `SUBTYPE = 0x09` -> **100% PASS (PM8994 MATCH)**.
  - Formally proves the custom XNU SPMI PMIC Arbiter v2 read primitive operates with 100% fidelity on real hardware.

### 7.4 Live Silicon Measurement Table at XNU Runtime

| Peripheral | Base Addr | APID | TYPE | SUBTYPE | STATUS (`+0x08`) | ENABLE (`+0x46`) | VSET (`+0x41`) | MODE (`+0x45`) | Decoded State | Stability |
| :--- | :--- | :--- | :--- | :--- | :--- | :--- | :--- | :--- | :--- | :--- |
| **REVID** | `0x0100` | `0x50` | `0x51` | `0x09` | `0x00` | N/A | N/A | N/A | PM8994 Valid | 8/8 (100%) |
| **L25** | `0x5800` | `0x82` | `0x06` | `0x08` | `0x80` (VREG_OK) | `0x80` (Bit 7=1) | `0x00` | `0x00` | **ENABLED (ON)** | 8/8 (100%) |
| **L28** | `0x5B00` | `0x9C` | `0x06` | `0x0B` | `0x00` | `0x00` (Bit 7=0) | `0x00` | `0x00` | **DISABLED (OFF)** | 8/8 (100%) |
| **L26** | `0x5900` | `0x61` | `0x06` | `0x02` | `0x80` | `0x01` (Bit 7=0) | — | — | **DISABLED (OFF)** | 100% |
| **L27** | `0x5A00` | `0x84` | `0x06` | `0x0C` | `0x85` | `0x80` (Bit 7=1) | — | — | **ENABLED (ON)** | 100% |
| **L12** | `0x4B00` | N/A | N/A | N/A | N/A | N/A | N/A | N/A | **UNMAPPED (RPM)**| N/A |

### 7.5 Phase D2-C2.4B Classification & Architectural Impact
- **Classification:** **CASE B — L25 IS HARDWARE ENABLED AT XNU RUNTIME** (`ENABLE = 0x80`, `STATUS = 0x80` [VREG_OK]).
- **Hypothesis Disproven:** The hypothesis that `PCS_READY` timed out due to PM8994 L25 (`vddp-ref-clk` and `vccq`, 1.2V) being disabled at XNU runtime is **DISPROVEN**. L25 is already active and regulated at runtime.
- **Key Analog Finding:** `L28` (`vdda-phy`, 0.925V) is measured as **DISABLED** (`ENABLE = 0x00`, `STATUS = 0x00`).
- **Control Lineage:** `L12` (`vdda-pll`, 1.8V) is verified to be RPM-managed, with no APPS EE direct SPMI mapping.

---

## 8. Phase D2-C2.4C Hardware Audit: PM8994 L28 SPMI Identity Validation

### 8.1 Objective & Mandatory Corrections
Phase D2-C2.4C executed with strict guardrails:
1. General PMIC Arbiter v2 channel offset formula: `channel_offset = 0x1000 * EE + 0x8000 * APID`.
2. Physical write aperture verified from real DTB: `0x04400000` (`chnls`, 8MB). Zero writes issued through the observer aperture.
3. Command format: `cmd = (opcode << 27) | ((addr & 0xFF) << 4) | (bc & 0x7)` without SID embedding.
4. Mandatory status checking with 1000us timeout and full failure logging (`APID, PPID, addr, cmd, WDATA0, STATUS, elapsed_us`).
5. **Strict Identity Gate**: L28 write permission strictly gated on `TYPE == 0x04 (LDO)` and `SUBTYPE == 0x0B (P600)`. If `TYPE == 0x06` appears at `+0x04`, issue ZERO PMIC writes and halt immediately.
6. Independent proof and decode of raw APID mapping registers (`core + 0x800 + 4*apid`).
7. Byte-for-byte agreement check between multi-byte bulk read (`0x00..0x07`) and single-byte reads.
8. Restoration of deliberate terminal path `xzs_spin_halt()` (confirming ~4-6s watchdog reset vs previous +40s runtime).

### 8.2 Independent APID Mapping Verification (EE 0)
Every target was mapped and decoded directly from SPMI core registers:

| Target Peripheral | Peripheral Base | APID | Raw Value | Decoded PPID | Decoded SID | Decoded PID | Expected PID | Decode Result |
| :--- | :--- | :--- | :--- | :--- | :--- | :--- | :--- | :--- |
| **REVID** | `0x0100` | `0x50` | `0x0100` | `0x0001` | `0` | `0x01` | `0x01` | **VERIFIED** |
| **L25** | `0x5800` | `0x82` | `0x5800` | `0x0058` | `0` | `0x58` | `0x58` | **VERIFIED** |
| **L26** | `0x5900` | `0x61` | `0x5900` | `0x0059` | `0` | `0x59` | `0x59` | **VERIFIED** |
| **L27** | `0x5A00` | `0x84` | `0x5A00` | `0x005A` | `0` | `0x5A` | `0x5A` | **VERIFIED** |
| **L28** | `0x5B00` | `0x9B` | `0x5B00` | `0x005B` | `0` | `0x5B` | `0x5B` | **VERIFIED** |
| **L12** | `0x4B00` | N/A | N/A | N/A | N/A | N/A | `0x4B` | **UNMAPPED (RPM-owned)** |

### 8.3 Bulk Read vs Single-Byte Read Byte-for-Byte Comparison
Multi-byte bulk read (`+0x00..+0x07`) was compared against single-byte reads (`+0x00..+0x08`):

| Peripheral | Bulk Read `+0x00..+0x07` | Single-Byte Reads `+0x00..+0x08` | Byte-for-Byte Agreement |
| :--- | :--- | :--- | :--- |
| **REVID (0x0100)** | `00 00 00 02 51 09 00 00` | `00 00 00 02 51 09 00 00 A0` | **100% MATCH** |
| **L25 (0x5800)** | `01 00 00 01 06 08 00 00` | `01 00 00 01 06 08 00 00 80` | **100% MATCH** |
| **L26 (0x5900)** | `01 02 01 00 06 02 00 00` | `01 02 01 00 06 02 00 00 80` | **100% MATCH** |
| **L27 (0x5A00)** | `04 01 00 00 06 0C 00 00` | `04 01 00 00 06 0C 00 00 85` | **100% MATCH** |
| **L28 (0x5B00)** | `01 00 00 00 06 0B 00 00` | `01 00 00 00 06 0B 00 00 00` | **100% MATCH** |

### 8.4 Repeated Single-Byte Reads (8 Passes)
- **L25 (0x5800)**: All 8 consecutive passes returned identically:
  `+0x00=0x01 +0x01=0x00 +0x02=0x00 +0x03=0x01 +0x04=0x06 +0x05=0x08 +0x06=0x00 +0x07=0x00 +0x08=0x80` (100% stable).
- **L28 (0x5B00)**: All 8 consecutive passes returned identically:
  `+0x00=0x01 +0x01=0x00 +0x02=0x00 +0x03=0x00 +0x04=0x06 +0x05=0x0B +0x06=0x00 +0x07=0x00 +0x08=0x00` (100% stable).

### 8.5 Identity Gate Result & Architectural Finding
- **Gate Evaluation:**
  - L28 `TYPE` at `+0x04`: `0x06` (Expected canonical Qualcomm LDO: `0x04`).
  - L28 `SUBTYPE` at `+0x05`: `0x0B` (Canonical Qualcomm P600: `0x0B`).
  - Bulk vs Single Agreement: `100% MATCH`.
- **Enforcement:**
  - Under Mandatory Rule 4, since `TYPE = 0x06` appeared at `+0x04`:
    `[XZS-SPMI] [C24C0-FAIL] REGULATOR IDENTITY MISMATCH`
    `ZERO PMIC WRITES PERMITTED — ABORTING PROVISIONING`
  - **Zero PMIC writes were issued.**
  - Terminal halt via `xzs_spin_halt()` triggered with breadcrumb `0xEF4C`.
- **Timing Invariant Confirmation:**
  - Fastboot returned in **+5s** (`Automated Return: +5s`), fully confirming the ~4-6s watchdog reset baseline.
- **Hardware Architecture Takeaway:**
  - Across all 4 measured PM8994 LDO regulators (L25, L26, L27, L28), the hardware reports `TYPE = 0x06` at offset `+0x04`, with valid distinct subtypes (`0x08` for P50, `0x02` for N150, `0x0C` for P1200, `0x0B` for P600).
  - Bulk reading proves that `+0x04=0x06` and `+0x05=0x0B` are the exact hardware register contents (no alignment, byte-lane, or MMIO distortion).

---

## 7. Phase D2-C2.4D: RPM/SMD Platform Ownership & Control Architecture

### Clarification of PMIC Control Planes
- **HARDWARE FACT**:
  - APPS can observe these PMIC peripherals through the SPMI Arbiter read path.
  - TYPE register read through APPS SPMI path is stably `0x06` for L25–L28.
- **SOURCE FACT**:
  - Generic direct QPNP/SPMI regulator drivers expect LDO `TYPE = 0x04`.
- **PLATFORM FACT**:
  - The live Sony DT assigns L28 (`vdda-phy`) and L12 (`vdda-pll`) regulator control to the RPM/GLINK regulator provider (`compatible = "qcom,rpm-smd-regulator"` under `/soc/qcom,rpm-smd` / `qcom,rpm-glink`).
- **POLICY**:
  - Direct SPMI regulator writes remain unauthorized.
  - RPM/GLINK is the platform-supported control path.

---

## 8. Phase D2-C2.5: MSM8996 UFS QMP 14nm v2.2.0 Calibration & Power/Clock Sequence Replay

### 8.1 Findings & Hardware Telemetry
- **Hardware Version:** `QCOM_HW_VER` @ `0x6240E4` = `0x20020000` (Qualcomm UFS Host v2.2.0).
- **Calibration Replay:** Full Sony Tone v2.2.0 calibration table (76 Rate-A entries + Rate-B override) programmed and verified byte-for-byte on silicon.
- **Clock Reference:** `GCC_UFS_CLKREF` (`0x88008`) confirmed running (`CBCR = 0x00000001`).
- **Supplies:** L28 (0.925V / 18 mA), L12 (1.800V / 9 mA), LN_BB reference clock (ID 8) voted via RPM SMD V0 over GLINK.
- **Outcome:** `C_READY_STATUS` (`0x190`) remained `0` throughout 1-second bounded polling window.
- **Conclusion:** C_READY failure was not caused by missing power supplies or generic clock gates. A deeper differential audit between reference/firmware state and XNU was required.

---

## 9. Phase D2-C2.6: Read-Only Differential State Audit & Sony Lineage Discovery

### 9.1 Oracle Re-evaluation: TWRP is ORACLE_B
- Audited `artifacts/logs/twrp-dmesg-full.log` lines 1.628257 - 1.629609:
  ```text
  <3>[    1.628257] ufshcd 624000.ufshc: ufshcd_variant_hba_init: variant qcom init failed err -19
  <3>[    1.629609] ufshcd 624000.ufshc: Intialization failed
  ```
- Error `-19` (`-ENODEV`) aborted the Qualcomm UFS driver probe before touching the PHY or host controller.
- TWRP operates 100% from ramdisk. No live known-good Linux UFS oracle exists on target (`ORACLE_B`).

### 9.2 Exact Sony Binary Write Sequence Audited
Disassembly of `artifacts/scratch/twrp-Image` (`ufs_qcom_power_up_sequence` @ `0xffffffc0007262a0` and `phy_calibrate` @ `0xffffffc00049dd20`):
1. **Assert Soft Reset:** `REG_UFS_CFG1` (`0x6240DC`) `|= 0x2` (`UFS_PHY_SOFT_RESET = 1`).
2. **Hold Reset:** `usleep_range(1000, 1100)`.
3. **Calibrate PHY with Soft Reset ASSERTED (`phy_calibrate`):**
   - **Save Quirk:** Read `0x627134` (`QSERDES_COM_VCO_TUNE1_MODE1`) and save (`0x0A` on silicon).
   - **Table Write:** Program 76 Rate-A entries.
   - **Restore Quirk:** Restore saved value (`0x0A`) back to `0x627134` (`QSERDES_COM_VCO_TUNE1_MODE1`), overwriting table entry `0xD6`.
4. **Deassert Soft Reset:** `REG_UFS_CFG1` (`0x6240DC`) `&= ~0x2`.
5. **Settle Reset Release:** `usleep_range(1000, 1100)`.
6. **Power-On PHY (`phy_power_on`):** Write `1` to `0x627C04` (`UFS_PHY_POWER_DOWN_CONTROL`).
7. **Start SerDes (`phy_start`):** Write `1` to `0x627C00` (`UFS_PHY_PHY_START`).
8. **Lock Poll:** Poll `0x627D68` (`UFS_PHY_PCS_READY_STATUS`) or `0x627190` (`QSERDES_COM_C_READY_STATUS`).

### 9.3 Critical Sequence Deltas Identified in XNU D2-C2.5
1. **Quirk `0x134` Clobbered:** Table entry 42 overwrote `0x134` with `0xD6`. Silicon bootloader handoff value was `0x0A`. Sony kernel explicitly restores `0x134` to `0x0A`.
2. **`0xC04` Ordering Inverted:** XNU previously wrote `0xC04 = 1` BEFORE soft-reset assert and calibration. Sony kernel writes `0xC04 = 1` AFTER soft-reset deassert.
3. **Soft Reset Duration:** Sony holds soft reset asserted DURING calibration; XNU deasserted it before calibration.

### 9.4 Device Reference Clock (`REG_UFS_CFG1` Bit 26) Audit
- `ufs_qcom_dev_ref_clk_ctrl` is ONLY called during dynamic bus scaling votes. It is NEVER called during initial PHY bring-up.
- Bootloader already hands off with `REG_UFS_CFG1 = 0x1C00052C` (Bit 26 is already 1).
- Neither `GCC_UFS_BCR` nor `REG_UFS_CFG1` soft reset destroys Bit 26.

---

## 10. Phase D2-C2.7: Isolated Sony PHY Sequence Replay & Hardware Telemetry

### 10.1 Status Register Map Proof
- `QSERDES_COM_LOCK_CMP_EN`: offset `0x0C8` (CONFIG register)
- `QSERDES_COM_C_READY_STATUS`: offset `0x190` (STATUS register, bit 0 = C_READY)
- `UFS_PHY_PCS_READY_STATUS`: offset `0xD68` (STATUS register, bit 0 = PCS_READY)
- Register proof verified on silicon before sequence execution.

### 10.2 Sony 0x134 Vendor Quirk Audit
- Source disassembly of `artifacts/scratch/twrp-Image`:
  - Read: `0xffffffc00049dd4c: ldr w3, [x20]` (`PHY + 0x134`)
  - Saved: `0xffffffc00049dd6c: str w3, [x19, #0x100]` (`phy->vco_tune1_mode1`)
  - Branch controlling read: `0xffffffc00049dd24: tbz w0, #2, #0xffffffc00049dd78`
  - Branch controlling restore: `0xffffffc00049dfd8: tbz w0, #2, #0xffffffc00049e014`
  - Write back: `0xffffffc00049e004: str w1, [x21]` (`PHY + 0x134`)
  - Quirk condition: `quirks = (major == 2 && minor == 0 && step == 0) ? 7 : 0`
  - On MSM8996 v2.2.0 (`major=2, minor=2, step=0`): `quirks = 0` (bit 2 is 0).
  - Verdict: Stock Sony Linux driver does NOT save/restore `0x134` on v2.2.0 hardware (`SONY_2_2_0_SAVE_0x134 = no`, `SONY_2_2_0_RESTORE_0x134 = no`).

### 10.3 Stage A: 0x134 Preservation Isolation
- Retained old C2.6 C04 ordering, isolated `0x134` save/restore:
  - `PRE_CAL_0x134 = 0x0A`
  - `TABLE_0x134 = 0xD6`
  - `RESTORED_0x134 = 0x0A`
- Telemetry across 10ms, 100ms, 500ms, 1000ms:
  - `C_READY (+0x190) = 0`
  - `PCS_READY (+0xD68) = 0`
  - `LOCK_CMP_EN (+0x0C8) = 1`
- Classification: **A2** (`C_READY` remained 0; `0x134` preservation alone is insufficient).

### 10.4 Stage B: Exact Sony Reset / Power Ordering
- Preserved `0x134` and executed exact Sony kernel ordering:
  1. Base: `C04 = 0`, `C00 = 0`.
  2. `REG_UFS_CFG1`: assert soft reset (`bit 1 = 1`). Settle 1000 µs.
  3. Calibrate PHY while soft reset remains asserted: write 76 Rate-A + Rate-B override.
  4. Restore `0x134 = 0x0A`.
  5. Verify: `CFG1 bit 1 = 1`, `0x134 = 0x0A`.
  6. `REG_UFS_CFG1`: deassert soft reset (`bit 1 = 0`). Settle 1000 µs.
  7. `PHY + 0xC04 = 1` (`POWER_DOWN_CONTROL = 1`). Memory barrier.
  8. `PHY + 0xC00 = 1` (`PHY_START = 1`). Memory barrier.
  9. Poll `0x190` and `0xD68` across 10ms, 100ms, 500ms, 1000ms.
- Telemetry:
  - `CR (+0x190) = 0`
  - `PCS (+0xD68) = 0`
  - `LOCK_CMP_EN (+0x0C8) = 0x01`
  - `PHY + 0x160 = 0x00` (`RESET_SM_STATUS`)
  - `CMN_CONFIG (+0x194) = 0x0E`
  - `PHY_START (+0xC00) = 0x01`
  - `POWER_DOWN_CONTROL (+0xC04) = 0x01`
- Classification: **B3** (`C_READY = 0` after exact Sony sequence).

### 10.5 Invariants & Reverse Rollback
- Hard Invariant: `HCE = 0`, `HCS = 0`, `UICCMD = 0`, DMA untouched.
- Clean reverse RPM rollback: `LN_BB` (SLEEP -> ACTIVE release) -> `L12` (ACTIVE release) -> `L28` (ACTIVE release) all returned ACK.
- Automated return to Fastboot via pshold in ~7s.
