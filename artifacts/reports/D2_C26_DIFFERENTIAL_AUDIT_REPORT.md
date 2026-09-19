# D2-C2.6 Differential Audit Report

## Git Baseline
- **Pre-task baseline commit:** `8e80e4cfc308cb66ae3848e088e4ba76b4548b5b`
- **Audit implementation commit:** `6cdd152c21ba11cd65ac6b02a415dbe4666bde4d` (*xzs: implement Phase D2-C2.6 UFS read-only differential state audit*)
- **Bus clock prerequisite commit:** `3af19bcf89ce0e7cf23dae24747ebc857790b953` (*xzs: ensure bus clocks enabled before Host/PHY MMIO read in D2-C2.6 audit*)
- **Current HEAD:** `3af19bcf89ce0e7cf23dae24747ebc857790b953`
- **Worktree state:** CLEAN, pushed to `origin/xzs-bringup`.

---

## Oracle Validity
- **TWRP operational UFS:** `NO`  
  - Audited `artifacts/logs/twrp-dmesg-full.log` lines 1.628257 - 1.629609:
    ```text
    <3>[    1.628257] ufshcd 624000.ufshc: ufshcd_variant_hba_init: variant qcom init failed err -19
    <3>[    1.629609] ufshcd 624000.ufshc: Intialization failed
    ```
  - Error `-19` (`-ENODEV`) aborted Qualcomm downstream Linux UFS driver probe before touching the PHY or controller registers.
  - TWRP runs entirely from ramdisk; UFS storage is completely offline and unconfigured.
- **Stock Linux oracle:** None available live.
- **Reference classification:** `ORACLE_B` (failed probe; no live working Linux runtime oracle exists).

---

## Sony Pre-PCS MMIO Write Sequence
Disassembly of Sony downstream kernel `artifacts/scratch/twrp-Image`:
- `ufs_qcom_power_up_sequence` @ `0xffffffc0007262a0`
- `ufs_qcom_phy_qmp_14nm_init` / `phy_calibrate` @ `0xffffffc00049dd20`
- `ufs_qcom_phy_power_control` @ `0xffffffc00049e580`
- `ufs_qcom_phy_start_serdes` @ `0xffffffc00049db00`

### Exact Step-by-Step Sony Sequence:
1. **Assert Soft Reset:**  
   `REG_UFS_CFG1` (`0x6240DC`) `|= 0x2` (`UFS_PHY_SOFT_RESET = 1`).
2. **Hold Reset:**  
   `usleep_range(1000, 1100)` (1 ms reset hold).
3. **Calibrate PHY with Soft Reset ASSERTED (`phy_calibrate`):**  
   a. **Save Quirk:** Read `0x627134` (`QSERDES_COM_VCO_TUNE1_MODE1`) and save value (`0x0A` on silicon).  
   b. **Table Replay:** Write 76 Rate-A calibration table entries across COM (`0x000`), TX (`0x400`), RX (`0x600`).  
   c. **Restore Quirk:** Restore saved value (`0x0A`) back to `0x627134` (`QSERDES_COM_VCO_TUNE1_MODE1`), overwriting table entry `0xD6`.
4. **Deassert Soft Reset:**  
   `REG_UFS_CFG1` (`0x6240DC`) `&= ~0x2` (`UFS_PHY_SOFT_RESET = 0`).
5. **Settle Reset Release:**  
   `usleep_range(1000, 1100)` (1 ms settle).
6. **PHY Power-On (`phy_power_on` -> `ufs_qcom_phy_power_control`):**  
   Write `1` to `0x627C04` (`UFS_PHY_POWER_DOWN_CONTROL`).  
   *(Notice: Sony executes this AFTER soft reset deassert; XNU previously executed this BEFORE soft reset assert!)*
7. **Start SerDes (`phy_start` -> `ufs_qcom_phy_start_serdes`):**  
   Write `1` to `0x627C00` (`UFS_PHY_PHY_START`).
8. **Lock Poll:**  
   Poll `0x627D68` (`UFS_PHY_PCS_READY_STATUS`) or `0x627190` (`QSERDES_COM_C_READY_STATUS`) with 1 s timeout.

---

## Secure Configuration Audit
- **Bus Interconnect Branches:**  
  `SYS_NOC_UFS_AXI` (`0x75038`), `AGGRE2_UFS_AXI` (`0x83014`), `UFS_AXI` (`0x75008`), `UFS_AHB` (`0x7500C`) are fully non-secure and manageable from EL1 APPS (EE0).
- **Handoff State:**  
  The bootloader hands off with these branch clocks gated (`CLK_OFF = 1`). Reading Host MMIO `0x624000` before enabling them causes an AXI SError bus abort. Once branch clocks are enabled (`bit 0 = 1`), Host and PHY MMIO read/write succeed without bus faults.
- **TrustZone / SCM:**  
  No SCM secure calls are required for QMP UFS PHY calibration or basic MMIO access on MSM8996.

---

## Device Reference Clock / REG_UFS_CFG1 Audit
- **BIT1 (`UFS_PHY_SOFT_RESET`):**  
  Active-high soft reset for PHY digital logic.
- **BIT26 (`UFS_DEV_REF_CLK_EN`):**  
  Controls UFS device reference clock output to storage IC.
- **When BIT26 is enabled by Sony:**  
  Disassembly of `twrp-Image` proves that `ufs_qcom_dev_ref_clk_ctrl` (`0xffffffc0007256ec`) has ONLY 4 call sites: inside `ufs_qcom_set_bus_vote` (`0x59bc`, `0x59fc`) and `ufs_qcom_update_bus_bw_vote` (`0x5c94`, `0x5ce4`). It is **NEVER** called during PHY power-on, calibration, or SerDes start!
- **Initial-`C_READY` dependency:**  
  `NONE`. `BIT26_BEFORE_INITIAL_PHY_READY = no`.
- **Silicon Observation:**  
  Bootloader hands off with `REG_UFS_CFG1 = 0x1C00052C` (Bit 26 is already 1!).
- **Reset-Destruction Audit (Section 16 Silicon Readback):**  
  - `CFG1_PRE_BCR`: `0x1C00052C` (BIT1=0, BIT26=1)
  - `CFG1_DURING_BCR`: `0x00000000` (Controller core held in reset)
  - `CFG1_POST_BCR`: `0x1C00052C` (BIT1=0, BIT26=1)
  - `CFG1_POST_RESET_ASSERT`: `0x1C00052E` (BIT1=1, BIT26=1)
  - `CFG1_POST_RESET_DEASSERT`: `0x1C00052C` (BIT1=0, BIT26=1)  
  *Result:* Neither GCC BCR nor PHY soft reset wipes Bit 26.

---

## TLMM/Pinctrl Audit
- Audited `twrp-extracted.dts` and MSM8996 platform pin configurations:
  UFS high-speed differential signals (`TX`, `RX`, `REF_CLK`) use dedicated analog I/O pads routed directly to the QMP PHY and are not multiplexed through TLMM GPIOs.
  The storage IC reset pin (`UFS_RESET`) is GPIO 107, but internal QSERDES PLL lock (`C_READY`) is purely internal to the SoC PHY and does not depend on external storage IC state.

---

## Bootloader Pre-XNU State (`EARLY_D2C26_PRE_MUTATION`)
- **Interconnect Clocks:**  
  `SYS_NOC_UFS_AXI` (`0x75038`) = `0x80000000` (`CLK_OFF=1`)  
  `AGGRE2_UFS_AXI` (`0x83014`) = `0x80000000` (`CLK_OFF=1`)  
  `UFS_AXI` (`0x75008`) = `0x80004220` (`CLK_OFF=1`)  
  `UFS_AHB` (`0x7500C`) = `0x80008000` (`CLK_OFF=1`)  
  `GCC_UFS_CLKREF` (`0x88008`) = `0x00000001` (Running)  
- **Power Domain:**  
  `UFS_GDSC` (`0x75004`) = `0xA0222000` (Powered ON)  
- **Core Controller Reset:**  
  `GCC_UFS_BCR` (`0x75000`) = `0x00000000` (Deasserted)  
- **Host Controller:**  
  `CAP` = `0x0107001F`, `VER` = `0x00010100`, `REG_UFS_CFG1` = `0x1C00052C`, `QCOM_HW_VER` = `0x20020000`  
- **PHY Analog State:**  
  `0x134` (`QSERDES_COM_VCO_TUNE1_MODE1`) = `0x0000000A` (Trim value from bootloader)  
  `0xC04` (`POWER_DOWN_CONTROL`) = `0x00000000`  
  `0x190` (`C_READY_STATUS`) = `0x00000000`

---

## Reference HOST Snapshot
Audited 32 host registers (`0x00` - `0xE4`):
- `CAP` = `0x0107001F`
- `VER` = `0x00010100` (UFSHCI 1.1)
- `HCPID` = `0x01000000`
- `HCMID` = `0x00010217`
- `AHIT` = `0x00000000`
- `IS` = `0x00000000`, `IE` = `0x00000000`
- `HCS` = `0x00000000`, `HCE` = `0x00000000` (Host controller disabled)
- `UECPA` .. `UECDME` = `0x00000000`
- `UTRLBA` .. `UTRLRSR` = `0x00000000`
- `UTMRLBA` .. `UTMRLRSR` = `0x00000000`
- `UICCMD` .. `UICCMDARG3` = `0x00000000` (No UIC commands issued)
- `REG_UFS_CFG1` = `0x1C00052C`
- `REG_UFS_CFG2` = `0x3F011300`
- `QCOM_HW_VER` = `0x20020000` (v2.2.0)

---

## Reference PHY Snapshot
Audited 64 PHY registers across QSERDES COM (`0x000`), TX Lane 0 (`0x400`), RX Lane 0 (`0x600`), and PCS (`0xC00`).  
Complete inventory documented in `artifacts/reports/source_expected.csv`.

---

## XNU HOST Timeline
Across all 5 audited stages (`EARLY_PRE` -> `POST_POWER` -> `POST_CAL` -> `POST_START_10MS` -> `POST_START_1S`):
- Host registers remained completely stable and invariant.
- `HCE` remained `0` throughout (no premature host enable).
- `UICCMD` remained `0` throughout (no premature link negotiation).
- `REG_UFS_CFG1` remained `0x1C00052C`.

---

## XNU PHY Timeline
- **`EARLY_PRE`:**  
  CRC32 = `0xA2F20DC8`. Baseline uncalibrated state. `0x134 = 0x0A`, `0xC04 = 0`, `0xC00 = 0`, `C_READY = 0`.
- **`POST_POWER`:**  
  CRC32 = `0xA347F0D5`. Delta = 1 (`0xC04` 0 -> 1).
- **`POST_CAL`:**  
  CRC32 = `0xB0C67D2A`. Delta = 41 (41 calibration table registers written; `0x134` changed from `0x0A` to `0xD6`).
- **`POST_START_10MS`:**  
  CRC32 = `0x1F82EF6D`. Delta = 1 (`0xC00` 0 -> 1).
- **`POST_START_1S`:**  
  CRC32 = `0x1F82EF6D`. Delta = 0 (100% identical to 10ms; `C_READY = 0`, `PCS_READY = 0`).

---

## GCC/RPM State
- **GLINK/RPM Transport:** `rpm_requests` channel open, V0 protocol negotiated.
- **PM8994 L28:** 0.925V (925000 uV) / 18 mA (ACK received).
- **PM8994 L12:** 1.800V / 9 mA (ACK received).
- **LN_BB Reference Clock:** ID 8 SWEN=1 (ACK received).
- **GCC Clocks:** GDSC ON (`0xA0222000`), BCR=0, CLKREF=1, AXI/AHB/SYS_NOC/AGGRE2 enabled and running (`CLK_OFF = 0`).

---

## Machine Diff
Summary of registers changing across stages or diverging from source expected:

| Domain | Offset | Register Name | Bootloader / Pre | Post Power | Post Cal | Post Start (1s) | Expected | Divergence / Note |
| :--- | :--- | :--- | :--- | :--- | :--- | :--- | :--- | :--- |
| HOST | `0x000DC` | `REG_UFS_CFG1` | `0x1C00052C` | `0x1C00052C` | `0x1C00052C` | `0x1C00052C` | `0x1C00052C` |  |
| HOST | `0x000E0` | `REG_UFS_CFG2` | `0x3F011300` | `0x3F011300` | `0x3F011300` | `0x3F011300` | `0x3F011300` |  |
| GCC | `0x75000` | `GCC_UFS_BCR` | `0x00000000` | `0x00000000` | `0x00000000` | `0x00000000` | `0x00000000` |  |
| GCC | `0x75008` | `GCC_UFS_AXI` | `0x00004221` | `0x00004221` | `0x00004221` | `0x00004221` | `0x00000000` | **DIVERGENT** (Exp 0x00000000) |
| GCC | `0x7500C` | `GCC_UFS_AHB` | `0x20008001` | `0x20008001` | `0x20008001` | `0x20008001` | `0x00000000` | **DIVERGENT** (Exp 0x00000000) |
| GCC | `0x75010` | `GCC_UFS_TX_CFG` | `0x80000000` | `0x80000000` | `0x80000000` | `0x80000000` | `0x80000000` |  |
| GCC | `0x75014` | `GCC_UFS_RX_CFG` | `0x80000000` | `0x80000000` | `0x80000000` | `0x80000000` | `0x80000000` |  |
| GCC | `0x75038` | `GCC_SYS_NOC_UFS_AXI` | `0x00000001` | `0x00000001` | `0x00000001` | `0x00000001` | `0x00000000` | **DIVERGENT** (Exp 0x00000000) |
| GCC | `0x83014` | `GCC_AGGRE2_UFS_AXI` | `0x00000001` | `0x00000001` | `0x00000001` | `0x00000001` | `0x00000000` | **DIVERGENT** (Exp 0x00000000) |
| GCC | `0x88008` | `GCC_UFS_CLKREF` | `0x00000001` | `0x00000001` | `0x00000001` | `0x00000001` | `0x00000001` |  |
| PHY | `0x00034` | `QSERDES_COM_BIAS_EN_CLKBUFLR_EN` | `0x00000000` | `0x00000000` | `0x00000008` | `0x00000008` | `0x00000008` | Mutated (0x00000000 -> 0x00000008) |
| PHY | `0x0003C` | `QSERDES_COM_SYS_CLK_CTRL` | `0x00000006` | `0x00000006` | `0x00000002` | `0x00000002` | `0x00000002` | Mutated (0x00000006 -> 0x00000002) |
| PHY | `0x0004C` | `QSERDES_COM_LOCK_CMP1_MODE0` | `0x00000000` | `0x00000000` | `0x000000FF` | `0x000000FF` | `0x000000FF` | Mutated (0x00000000 -> 0x000000FF) |
| PHY | `0x00050` | `QSERDES_COM_LOCK_CMP2_MODE0` | `0x00000000` | `0x00000000` | `0x0000000C` | `0x0000000C` | `0x0000000C` | Mutated (0x00000000 -> 0x0000000C) |
| PHY | `0x00058` | `QSERDES_COM_LOCK_CMP1_MODE1` | `0x00000000` | `0x00000000` | `0x00000032` | `0x00000032` | `0x00000032` | Mutated (0x00000000 -> 0x00000032) |
| PHY | `0x0005C` | `QSERDES_COM_LOCK_CMP2_MODE1` | `0x00000000` | `0x00000000` | `0x0000000F` | `0x0000000F` | `0x0000000F` | Mutated (0x00000000 -> 0x0000000F) |
| PHY | `0x00078` | `QSERDES_COM_CP_CTRL_MODE0` | `0x0000001B` | `0x0000001B` | `0x0000000B` | `0x0000000B` | `0x0000000B` | Mutated (0x0000001B -> 0x0000000B) |
| PHY | `0x0007C` | `QSERDES_COM_CP_CTRL_MODE1` | `0x0000001B` | `0x0000001B` | `0x0000000B` | `0x0000000B` | `0x0000000B` | Mutated (0x0000001B -> 0x0000000B) |
| PHY | `0x00084` | `QSERDES_COM_PLL_RCTRL_MODE0` | `0x00000010` | `0x00000010` | `0x00000016` | `0x00000016` | `0x00000016` | Mutated (0x00000010 -> 0x00000016) |
| PHY | `0x00088` | `QSERDES_COM_PLL_RCTRL_MODE1` | `0x00000010` | `0x00000010` | `0x00000016` | `0x00000016` | `0x00000016` | Mutated (0x00000010 -> 0x00000016) |
| PHY | `0x00090` | `QSERDES_COM_PLL_CCTRL_MODE0` | `0x00000001` | `0x00000001` | `0x00000028` | `0x00000028` | `0x00000028` | Mutated (0x00000001 -> 0x00000028) |
| PHY | `0x00094` | `QSERDES_COM_PLL_CCTRL_MODE1` | `0x00000001` | `0x00000001` | `0x00000028` | `0x00000028` | `0x00000028` | Mutated (0x00000001 -> 0x00000028) |
| PHY | `0x000AC` | `QSERDES_COM_SYSCLK_EN_SEL` | `0x00000010` | `0x00000010` | `0x00000014` | `0x00000014` | `0x00000014` | Mutated (0x00000010 -> 0x00000014) |
| PHY | `0x000B4` | `QSERDES_COM_RESETSM_CNTRL` | `0x00000000` | `0x00000000` | `0x00000020` | `0x00000020` | `0x00000020` | Mutated (0x00000000 -> 0x00000020) |
| PHY | `0x000C8` | `QSERDES_COM_LOCK_CMP_EN` | `0x00000000` | `0x00000000` | `0x00000001` | `0x00000001` | `0x00000001` | Mutated (0x00000000 -> 0x00000001) |
| PHY | `0x000CC` | `QSERDES_COM_LOCK_CMP_CFG` | `0x00000000` | `0x00000000` | `0x00000000` | `0x00000000` | `0x00000000` |  |
| PHY | `0x000D0` | `QSERDES_COM_DEC_START_MODE0` | `0x0000007F` | `0x0000007F` | `0x00000082` | `0x00000082` | `0x00000082` | Mutated (0x0000007F -> 0x00000082) |
| PHY | `0x000D4` | `QSERDES_COM_DEC_START_MODE1` | `0x0000007F` | `0x0000007F` | `0x00000098` | `0x00000098` | `0x00000098` | Mutated (0x0000007F -> 0x00000098) |
| PHY | `0x000DC` | `QSERDES_COM_DIV_FRAC_START1_MODE0` | `0x00000000` | `0x00000000` | `0x00000000` | `0x00000000` | `0x00000000` |  |
| PHY | `0x000E0` | `QSERDES_COM_DIV_FRAC_START2_MODE0` | `0x00000000` | `0x00000000` | `0x00000000` | `0x00000000` | `0x00000000` |  |
| PHY | `0x000E4` | `QSERDES_COM_DIV_FRAC_START3_MODE0` | `0x00000000` | `0x00000000` | `0x00000000` | `0x00000000` | `0x00000000` |  |
| PHY | `0x000E8` | `QSERDES_COM_DIV_FRAC_START1_MODE1` | `0x00000000` | `0x00000000` | `0x00000000` | `0x00000000` | `0x00000000` |  |
| PHY | `0x000EC` | `QSERDES_COM_DIV_FRAC_START2_MODE1` | `0x00000000` | `0x00000000` | `0x00000000` | `0x00000000` | `0x00000000` |  |
| PHY | `0x000F0` | `QSERDES_COM_DIV_FRAC_START3_MODE1` | `0x00000000` | `0x00000000` | `0x00000000` | `0x00000000` | `0x00000000` |  |
| PHY | `0x00108` | `QSERDES_COM_INTEGLOOP_GAIN0_MODE0` | `0x00000008` | `0x00000008` | `0x00000080` | `0x00000080` | `0x00000080` | Mutated (0x00000008 -> 0x00000080) |
| PHY | `0x00110` | `QSERDES_COM_INTEGLOOP_GAIN0_MODE1` | `0x00000008` | `0x00000008` | `0x00000080` | `0x00000080` | `0x00000080` | Mutated (0x00000008 -> 0x00000080) |
| PHY | `0x00128` | `QSERDES_COM_VCO_TUNE_MAP` | `0x00000004` | `0x00000004` | `0x00000044` | `0x00000044` | `0x00000044` | Mutated (0x00000004 -> 0x00000044) |
| PHY | `0x0012C` | `QSERDES_COM_VCO_TUNE1_MODE0` | `0x00000053` | `0x00000053` | `0x00000028` | `0x00000028` | `0x00000028` | Mutated (0x00000053 -> 0x00000028) |
| PHY | `0x00134` | `QSERDES_COM_VCO_TUNE1_MODE1` | `0x0000000A` | `0x0000000A` | `0x000000D6` | `0x000000D6` | `0x0000000A` | **DIVERGENT** (Exp 0x0000000A) |
| PHY | `0x00138` | `QSERDES_COM_VCO_TUNE2_MODE1` | `0x00000001` | `0x00000001` | `0x00000000` | `0x00000000` | `0x00000000` | Mutated (0x00000001 -> 0x00000000) |
| PHY | `0x00174` | `QSERDES_COM_CLK_SELECT` | `0x00000000` | `0x00000000` | `0x00000030` | `0x00000030` | `0x00000030` | Mutated (0x00000000 -> 0x00000030) |
| PHY | `0x00178` | `QSERDES_COM_HSCLK_SEL` | `0x00000020` | `0x00000020` | `0x00000000` | `0x00000000` | `0x00000000` | Mutated (0x00000020 -> 0x00000000) |
| PHY | `0x0018C` | `QSERDES_COM_CORE_CLK_EN` | `0x00000030` | `0x00000030` | `0x00000000` | `0x00000000` | `0x00000000` | Mutated (0x00000030 -> 0x00000000) |
| PHY | `0x00190` | `QSERDES_COM_C_READY_STATUS` | `0x00000000` | `0x00000000` | `0x00000000` | `0x00000000` | `0x00000000` |  |
| PHY | `0x00194` | `QSERDES_COM_CMN_CONFIG` | `0x00000000` | `0x00000000` | `0x0000000E` | `0x0000000E` | `0x0000000E` | Mutated (0x00000000 -> 0x0000000E) |
| PHY | `0x0019C` | `QSERDES_COM_SVS_MODE_CLK_SEL` | `0x00000000` | `0x00000000` | `0x00000005` | `0x00000005` | `0x00000005` | Mutated (0x00000000 -> 0x00000005) |
| PHY | `0x00468` | `QSERDES_TX_HIGHZ_TRANSCEIVER_BIAS_DRVR_EN` | `0x00000005` | `0x00000005` | `0x00000045` | `0x00000045` | `0x00000045` | Mutated (0x00000005 -> 0x00000045) |
| PHY | `0x00494` | `QSERDES_TX_LANE_MODE` | `0x00000000` | `0x00000000` | `0x00000006` | `0x00000006` | `0x00000006` | Mutated (0x00000000 -> 0x00000006) |
| PHY | `0x00640` | `QSERDES_RX_UCDR_FASTLOCK_FO_GAIN` | `0x00000021` | `0x00000021` | `0x0000000B` | `0x0000000B` | `0x0000000B` | Mutated (0x00000021 -> 0x0000000B) |
| PHY | `0x00690` | `QSERDES_RX_RX_TERM_BW` | `0x00000000` | `0x00000000` | `0x0000005B` | `0x0000005B` | `0x0000005B` | Mutated (0x00000000 -> 0x0000005B) |
| PHY | `0x006C4` | `QSERDES_RX_RX_EQ_GAIN1_LSB` | `0x00000007` | `0x00000007` | `0x000000FF` | `0x000000FF` | `0x000000FF` | Mutated (0x00000007 -> 0x000000FF) |
| PHY | `0x006C8` | `QSERDES_RX_RX_EQ_GAIN1_MSB` | `0x00000000` | `0x00000000` | `0x0000003F` | `0x0000003F` | `0x0000003F` | Mutated (0x00000000 -> 0x0000003F) |
| PHY | `0x006CC` | `QSERDES_RX_RX_EQ_GAIN2_LSB` | `0x00000007` | `0x00000007` | `0x000000FF` | `0x000000FF` | `0x000000FF` | Mutated (0x00000007 -> 0x000000FF) |
| PHY | `0x006D0` | `QSERDES_RX_RX_EQ_GAIN2_MSB` | `0x00000000` | `0x00000000` | `0x0000003F` | `0x0000003F` | `0x0000003F` | Mutated (0x00000000 -> 0x0000003F) |
| PHY | `0x006D8` | `QSERDES_RX_RX_EQU_ADAPTOR_CNTRL2` | `0x00000000` | `0x00000000` | `0x0000000D` | `0x0000000D` | `0x0000000D` | Mutated (0x00000000 -> 0x0000000D) |
| PHY | `0x00714` | `QSERDES_RX_SIGDET_CNTRL` | `0x00000000` | `0x00000000` | `0x0000000F` | `0x0000000F` | `0x0000000F` | Mutated (0x00000000 -> 0x0000000F) |
| PHY | `0x00718` | `QSERDES_RX_SIGDET_LVL` | `0x00000004` | `0x00000004` | `0x00000024` | `0x00000024` | `0x00000024` | Mutated (0x00000004 -> 0x00000024) |
| PHY | `0x0071C` | `QSERDES_RX_SIGDET_DEGLITCH_CNTRL` | `0x00000000` | `0x00000000` | `0x0000001E` | `0x0000001E` | `0x0000001E` | Mutated (0x00000000 -> 0x0000001E) |
| PHY | `0x0072C` | `QSERDES_RX_RX_INTERFACE_MODE` | `0x00000000` | `0x00000000` | `0x00000040` | `0x00000040` | `0x00000040` | Mutated (0x00000000 -> 0x00000040) |
| PHY | `0x00C00` | `UFS_PHY_PHY_START` | `0x00000000` | `0x00000000` | `0x00000000` | `0x00000001` | `0x00000001` | Mutated (0x00000000 -> 0x00000001) |
| PHY | `0x00C04` | `UFS_PHY_POWER_DOWN_CONTROL` | `0x00000000` | `0x00000001` | `0x00000001` | `0x00000001` | `0x00000001` | Mutated (0x00000000 -> 0x00000001) |
| PHY | `0x00D68` | `UFS_PHY_PCS_READY_STATUS` | `0x00000000` | `0x00000000` | `0x00000000` | `0x00000000` | `0x00000001` | **DIVERGENT** (Exp 0x00000001) |
| PHY | `0x00D74` | `UFS_PHY_PCS_READY_STATUS_SONY_ALT` | `0x00000000` | `0x00000000` | `0x00000000` | `0x00000000` | `0x00000001` | **DIVERGENT** (Exp 0x00000001) |

---

## Top Meaningful Divergences

### 1. `QSERDES_COM_VCO_TUNE1_MODE1` (`0x134`) Quirk Clobbered
- **Silicon Pre-value:** `0x0000000A` (Bootloader factory/trim calibration).
- **XNU Actual (Post-Cal & Post-Start):** `0x000000D6`.
- **Sony Expected:** `0x0000000A`.
- **Audit Discovery:** Disassembly of `twrp-Image` (`0xffffffc00049dd20` & `0xffffffc00049dfdc`) confirms Sony downstream driver explicitly reads `0x134` BEFORE writing calibration table, saves it (`0x0A`), and RESTORES it after calibration. In XNU, table entry 42 overwrote `0x134` with `0xD6`, leaving Mode 1 VCO PLL mis-tuned!

### 2. `UFS_PHY_POWER_DOWN_CONTROL` (`0xC04`) Sequence Ordering
- **Sony Downstream Order:** Assert Soft Reset -> Calibrate PHY -> Deassert Soft Reset -> **`0xC04 = 1`** (`phy_power_on`) -> **`0xC00 = 1`** (`phy_start`).
- **XNU Previous Order:** **`0xC04 = 1`** -> Assert Soft Reset -> Deassert Soft Reset -> Calibrate PHY -> **`0xC00 = 1`**.
- **Impact:** Powering on the analog block before resetting and calibrating causes the analog front-end to be calibrated while powered on or reset spuriously.

### 3. PHY Soft Reset Duration During Calibration
- **Sony Downstream Behavior:** Soft reset (`REG_UFS_CFG1` bit 1) is held ASSERTED throughout calibration programming.
- **XNU Previous Behavior:** Soft reset was toggled (asserted and deasserted) BEFORE calibration programming.

### 4. `UFS_PHY_PCS_READY_STATUS` (`0xD68`) / `QSERDES_COM_C_READY_STATUS` (`0x190`)
- **Silicon Actual:** `0x00000000` (Unlocked).
- **Expected:** `0x00000001` (Locked).

### 5. Interconnect Branch Clocks Enable
- **Bootloader Pre-XNU:** Gated (`CLK_OFF = 1`).
- **XNU Hardware State:** Enabled (`CLK_OFF = 0`).

---

## First Diverging Register
- **Chronologically during execution:**  
  `0x00C04` (`UFS_PHY_POWER_DOWN_CONTROL`): Written during power-up before soft-reset assertion and before calibration.
- **Parameter value divergence:**  
  `0x00134` (`QSERDES_COM_VCO_TUNE1_MODE1`): Factory trim `0x0A` clobbered by table value `0xD6` and not restored.

---

## HARDWARE VERIFIED FACTS
1. PM8994 L28 (0.925V / 18 mA), L12 (1.800V / 9 mA), and LN_BB (ID 8) are accepted via RPM SMD V0 protocol over GLINK.
2. Interconnect branch clocks (`SYS_NOC_UFS_AXI`, `AGGRE2_UFS_AXI`, `UFS_AXI`, `UFS_AHB`) are gated by default at bootloader handoff and must be un-gated before accessing Host MMIO `0x624000` to prevent AXI SError aborts.
3. Bootloader leaves `REG_UFS_CFG1 = 0x1C00052C` with bit 26 (`UFS_DEV_REF_CLK_EN`) already set to 1.
4. Neither `GCC_UFS_BCR` assert nor `REG_UFS_CFG1` bit 1 soft reset clears bit 26.
5. All 105 registers in the whitelist read and write without bus fault.
6. Checkpoint progression on silicon completed cleanly: `0xD260, 0x00` -> `0x20` -> `0x30` -> `0x40` -> `0x50` -> `0x60` -> `0x61` -> `0x70` -> `0x71` -> `0x80` -> `0x81` -> `0x82` -> `0x90` -> `0xA0` -> `0x01` (fastboot recovery in ~5 s).

---

## SOURCE-AUDITED FACTS
1. TWRP Linux UFS probe failed with `-ENODEV` (`-19`) at bootloader stage; TWRP is `ORACLE_B`.
2. Disassembly of `twrp-Image` (`0xffffffc0007256ec`) proves bit 26 is NEVER touched during initial PHY power-on, calibration, or SerDes start.
3. Disassembly of `twrp-Image` (`0xffffffc00049dd20` & `0xffffffc00049dfdc`) proves Sony downstream driver reads `0x134` before calibration, saves it, and restores it after calibration.
4. Disassembly of `twrp-Image` (`0xffffffc0007262a0`) proves Sony downstream driver writes `UFS_PHY_POWER_DOWN_CONTROL` (`0xC04 = 1`) AFTER soft reset is deasserted.
5. Soft reset is held asserted while the calibration table is written.

---

## INFERENCES
1. Overwriting `QSERDES_COM_VCO_TUNE1_MODE1` (`0x134`) with `0xD6` instead of preserving the bootloader trim (`0x0A`) is the primary technical reason `C_READY` failed to lock.
2. Inverting the sequence of `UFS_PHY_POWER_DOWN_CONTROL` (`0xC04`) relative to soft-reset deassertion prevented the PCS analog power state machine from sequencing properly.
3. Replaying the exact Sony sequence (Hold Soft Reset -> Calibrate -> Restore 0x134 -> Deassert Soft Reset -> Power Down Control = 1 -> Start SerDes) is the justified candidate to achieve `C_READY` lock.

---

## Unresolved Questions
1. Does restoring `0x134 = 0x0A` immediately yield `C_READY = 1`, or does `C_READY` additionally require the post-reset `0xC04 = 1` ordering?
2. Does `PCS_READY` (`0xD68`) lock synchronously with `C_READY`, or require an additional settling window?

---

## Candidate for D2-C2.7
**Phase D2-C2.7: Exact Sony Sequence Replay (Quirk 0x134 Preservation + Post-Reset 0xC04 Ordering)**
1. Assert soft reset (`REG_UFS_CFG1 |= 2`) and hold for 1 ms.
2. While soft reset is asserted:
   - Read and save `0x134` (`QSERDES_COM_VCO_TUNE1_MODE1`).
   - Write 76 Rate-A calibration entries.
   - Restore saved `0x134` (`0x0A`).
3. Deassert soft reset (`REG_UFS_CFG1 &= ~2`) and settle for 1 ms.
4. Write `UFS_PHY_POWER_DOWN_CONTROL` (`0xC04 = 1`).
5. Write `UFS_PHY_PHY_START` (`0xC00 = 1`).
6. Poll `C_READY_STATUS` (`0x190`) and `PCS_READY_STATUS` (`0xD68`).

---

## D2-C2 Status
`INCOMPLETE`
