# MSM8996 14nm DSI PLL Golden Programming Sequence

## 1. Overview
The MSM8996 Qualcomm 14nm DSI PLL v2 programming sequence consists of:
1. **Prerequisites & Clock Gates:** Verification of reference clock (19.2 MHz XO) and GDSC power.
2. **Frequency-Dependent Calculation:** Calculating decimation start (`dec_start`), fractional start (`div_frac_start`), and lock comparison thresholds (`plllock_cmp`) for the target VCO frequency (1,794,078,720 Hz).
3. **Database Commit (`pll_db_commit_8996`):** Writing calibration, charge pump, loop filter, and divider registers.
4. **PLL Enable & Kick (`dsi_pll_enable_seq_8996`):** Asserting common config and reset state machine enable.
5. **Lock & Ready Acceptance (`pll_is_pll_locked_8996`):** Bounded polling on status register for frequency lock (bit 5) and reset SM ready (bit 0).

---

## 2. Parameter Derivations (1,794,078,720 Hz Target VCO)

| Parameter | Formula | Calculated Value | Register(s) |
|---|---|---|---|
| Reference Clock ($f_{ref}$) | XO Input | 19,200,000 Hz | - |
| Multiplier | $2^{20}$ | 1,048,576 | - |
| Dec Start Multiple | $(f_{vco} \times 2^{20}) / f_{ref}$ | 97,998,064 | - |
| **`dec_start`** | $\lfloor \text{multiple} / 2^{20} \rfloor$ | **93 (`0x5d`)** | `PLL_DEC_START` (`0x0099486c`) |
| **`div_frac_start`** | $\text{multiple} \pmod{2^{20}}$ | **475,376 (`0x740f0`)** | `PLL_DIV_FRAC_START1..3` (`0x00994870..78`) |
| **`plllock_cmp`** | $(\text{duration} \times \text{dec\_start}) / 10$ | **2391 (`0x957`)** | `PLL_PLLLOCK_CMP1..2` (`0x0099483c..40`) |
| Post N1 Divider | Target Bit Clock = VCO / 2 | **2 (`0x01` / div-1)** | `PLL_POST_N1_DIV` (`0x0099444c`) |
| N2 Divider | Target Pixel Clock = Bit Clock / 6 | **3 (`0x02` / div-1)** | `PLL_N2_DIV` (`0x00994428`) |

---

## 3. Ordered Register Programming Sequence

| Seq | Function | Register | Address | Mask | Value | Delay/Poll | Purpose | Type |
|---:|---|---|---|---|---|---|---|---|
| 0001 | `pll_vco_set_rate` | `PHY_CMN_CLK_CFG1` | `0x0099444c` | `0xff` | `0x01` | - | Set post-N1 divider = 2 (Bit Clock) | Freq-Dep |
| 0002 | `pll_db_commit` | `PHY_CMN_CLK_CFG0` | `0x00994448` | `0xff` | `0x00` | - | Disable external clock override | Constant |
| 0003 | `pll_db_commit` | `PLL_SYSCLK_EN_RESET` | `0x00994800` | `0xff` | `0x04` | - | Sysclk enable & reset config | Constant |
| 0004 | `pll_db_commit` | `PLL_TXCLK_EN` | `0x00994804` | `0xff` | `0x07` | - | Enable Tx clock channels | Constant |
| 0005 | `pll_db_commit` | `PLL_RESETSM_CNTRL3` | `0x00994810` | `0xff` | `0x05` | - | Reset SM control 3 | Constant |
| 0006 | `pll_db_commit` | `PLL_RESETSM_CNTRL` | `0x00994828` | `0xff` | `0x20` | - | Reset SM counter initial | Constant |
| 0007 | `pll_db_commit` | `PLL_BKUP_CTRL` | `0x0099482c` | `0xff` | `0x00` | - | Clear backup control | Constant |
| 0008 | `pll_db_commit` | `PLL_KVCO_DIV_REF1` | `0x00994830` | `0xff` | `0x38` | - | Kvco divider ref part 1 | Constant |
| 0009 | `pll_db_commit` | `PLL_PLLLOCK_CMP1` | `0x0099483c` | `0xff` | `0x57` | - | Lock cmp low byte (0x957 & 0xff) | Freq-Dep |
| 0010 | `pll_db_commit` | `PLL_PLLLOCK_CMP2` | `0x00994840` | `0xff` | `0x09` | - | Lock cmp high byte (0x957 >> 8) | Freq-Dep |
| 0011 | `pll_db_commit` | `PLL_PLLLOCK_CMP3` | `0x00994844` | `0xff` | `0x00` | - | Lock cmp upper byte | Freq-Dep |
| 0012 | `pll_db_commit` | `PLL_PLLLOCK_CMP_EN` | `0x00994848` | `0xff` | `0x01` | - | Enable lock comparison | Constant |
| 0013 | `pll_db_commit` | `PLL_PLL_LOCK_MIN_DELAY` | `0x0099484c` | `0xff` | `0x19` | - | Min lock delay cycles | Constant |
| 0014 | `pll_db_commit` | `PLL_DEC_START` | `0x0099486c` | `0xff` | `0x5d` | - | Integer decimation start (93) | Freq-Dep |
| 0015 | `pll_db_commit` | `PLL_DIV_FRAC_START1` | `0x00994870` | `0xff` | `0xf0` | - | Frac start byte 0 (0x740f0 & 0xff)| Freq-Dep |
| 0016 | `pll_db_commit` | `PLL_DIV_FRAC_START2` | `0x00994874` | `0xff` | `0x40` | - | Frac start byte 1 (0x740f0 >> 8) | Freq-Dep |
| 0017 | `pll_db_commit` | `PLL_DIV_FRAC_START3` | `0x00994878` | `0xff` | `0x07` | - | Frac start byte 2 (0x740f0 >> 16)| Freq-Dep |
| 0018 | `pll_db_commit` | `PLL_VCO_DIV_REF1` | `0x0099487c` | `0xff` | `0x33` | - | VCO divider ref part 1 | Constant |
| 0019 | `pll_db_commit` | `PLL_VCO_DIV_REF2` | `0x00994880` | `0xff` | `0x03` | - | VCO divider ref part 2 | Constant |
| 0020 | `pll_db_commit` | `PLL_CP_SET` | `0x00994884` | `0xff` | `0x0b` | - | Charge pump current setting | Constant |
| 0021 | `pll_db_commit` | `PLL_LPF_RES1` | `0x00994888` | `0xff` | `0x1f` | - | Loop filter resistor 1 | Constant |
| 0022 | `pll_db_commit` | `PLL_LPF_CAP1` | `0x00994890` | `0xff` | `0x00` | - | Loop filter capacitor 1 | Constant |
| 0023 | `pll_db_commit` | `PLL_LPF_CAP2` | `0x00994894` | `0xff` | `0x30` | - | Loop filter capacitor 2 | Constant |
| 0024 | `pll_db_commit` | `PLL_BIST_MODE_LANES` | `0x00994898` | `0xff` | `0x00` | - | Disable BIST mode on lanes | Constant |
| 0025 | `pll_db_commit` | `PLL_RESETSM_CNTRL4` | `0x0099489c` | `0xff` | `0x02` | - | Reset SM control 4 | Constant |
| 0026 | `pll_db_commit` | `PLL_RESETSM_CNTRL5` | `0x009948a0` | `0xff` | `0x00` | - | Reset SM control 5 | Constant |
| 0027 | `pll_db_commit` | `PLL_KVCO_COUNT1` | `0x009948a4` | `0xff` | `0x00` | - | Kvco count register 1 | Constant |
| 0028 | `pll_db_commit` | `PLL_KVCO_COUNT2` | `0x009948a8` | `0xff` | `0x00` | - | Kvco count register 2 | Constant |
| 0029 | `pll_db_commit` | `PLL_CAL_CFG0` | `0x009948ac` | `0xff` | `0x14` | - | Calibration configuration 0 | Constant |
| 0030 | `pll_db_commit` | `PLL_CAL_CFG1` | `0x009948b4` | `0xff` | `0x00` | - | Calibration configuration 1 | Constant |
| 0031 | `pll_db_commit` | `PLL_CAL_CFG2` | `0x009948b8` | `0xff` | `0x00` | - | Calibration configuration 2 | Constant |
| 0032 | `pll_db_commit` | `PLL_CAL_CFG3` | `0x009948bc` | `0xff` | `0x00` | - | Calibration configuration 3 | Constant |
| 0033 | `pll_db_commit` | `PLL_CAL_CFG4` | `0x009948c0` | `0xff` | `0x00` | - | Calibration configuration 4 | Constant |
| 0034 | `pll_db_commit` | `PLL_CAL_CFG5` | `0x009948c4` | `0xff` | `0x00` | - | Calibration configuration 5 | Constant |
| 0035 | `pll_db_commit` | `PLL_CAL_CFG6` | `0x009948e8` | `0xff` | `0x00` | - | Calibration configuration 6 | Constant |
| 0036 | `pll_db_commit` | `PLL_CAL_CFG7` | `0x009948f0` | `0xff` | `0x00` | - | Calibration configuration 7 | Constant |
| 0037 | `pll_db_commit` | `PLL_CAL_CFG8` | `0x009948f4` | `0xff` | `0x00` | - | Calibration configuration 8 | Constant |
| 0038 | `pll_db_commit` | `PLL_CAL_CFG9` | `0x009948f8` | `0xff` | `0x00` | - | Calibration configuration 9 | Constant |
| 0039 | `pll_db_commit` | `PLL_CAL_CFG10` | `0x009948fc` | `0xff` | `0x00` | - | Calibration configuration 10 | Constant |
| 0040 | `pll_db_commit` | `PLL_CAL_CFG11` | `0x00994900` | `0xff` | `0x00` | - | Calibration configuration 11 | Constant |
| 0041 | `pll_db_commit` | `PLL_EFUSE_1` | `0x00994904` | `0xff` | `0x20` | - | Efuse configuration 1 | Constant |
| 0042 | `pll_db_commit` | `PHY_CMN_CTRL_0` | `0x00994410` | `0xff` | `0x1e` | - | PHY common control 0 | Constant |
| 0043 | `pll_db_commit` | `PHY_CMN_CTRL_1` | `0x00994414` | `0xff` | `0x00` | - | PHY common control 1 | Constant |
| 0044 | `pll_db_commit` | `PHY_CMN_CTRL_2` | `0x0099441c` | `0xff` | `0x00` | - | PHY common control 2 | Constant |
| 0045 | `pll_db_commit` | `PHY_CMN_CTRL_3` | `0x00994420` | `0xff` | `0x00` | - | PHY common control 3 | Constant |
| 0046 | `pll_enable_seq` | `PLL_CMN_CONFIG` | `0x0099485c` | `0xff` | `0x10` | - | Enable PLL common output buffer | Constant |
| 0047 | `pll_enable_seq` | `PHY_CMN_CLK_CFG0` | `0x00994448` | `0xff` | `0x01` | - | Enable PLL clock generation kick | Constant |
| 0048 | `pll_is_locked` | `PLL_RESET_SM_STATUS`| `0x009948cc` | `0x20` | `0x20` | Poll 10us (max 1000us) | Wait for PLL Frequency Lock | Status Poll |
| 0049 | `pll_is_locked` | `PLL_RESET_SM_STATUS`| `0x009948cc` | `0x01` | `0x01` | Poll 10us (max 1000us) | Wait for Reset State Machine Ready| Status Poll |

---

## 4. Bounded Polling Policy
- **Poll Interval:** 10 microseconds (`display_delay_us(10)`)
- **Timeout Duration:** 1,000 microseconds (1 millisecond)
- **Lock Failure Action:** Abort display enable, log failure checkpoint `D8M3-50`, assert soft reset, return `KERN_FAILURE`.
