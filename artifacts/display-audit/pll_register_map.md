# QUALCOMM MSM8996 14NM DSI PLL REGISTER MAP & PROGRAMMING AUDIT

## 1. Architectural Summary

| Parameter | Value | Evidence Class | Source |
|---|---|---|---|
| **PLL Model** | Qualcomm 14nm DSI PLL (v2) | SOURCE_PROVEN | `msm8996.dtsi`, `twrp-extracted.dts` (`qcom,mdss_dsi_pll_8996_v2`) |
| **DSI0 PLL Base Address** | `0x00994800` | SOURCE_PROVEN | `msm8996.dtsi` (`reg = <0x00994800 0x188>`), `xzs_diag.c:199` |
| **DSI1 PLL Base Address** | `0x00996800` | SOURCE_PROVEN | `msm8996.dtsi` (DSI1 disabled on Keyaki) |
| **PLL Reference Clock** | 19,200,000 Hz (19.2 MHz) | SOURCE_PROVEN | Linux `dsi_phy_14nm.c:VCO_REF_CLK_RATE` |
| **VCO Minimum Rate** | 1,300,000,000 Hz (1.30 GHz) | SOURCE_PROVEN | Linux `dsi_phy_14nm.c:VCO_MIN_RATE` |
| **VCO Maximum Rate** | 2,600,000,000 Hz (2.60 GHz) | SOURCE_PROVEN | Linux `dsi_phy_14nm.c:VCO_MAX_RATE` |
| **Keyaki Target VCO** | 1,794,078,720 Hz (1.794 GHz)| SOURCE_PROVEN | Calculated: 2 * BitClock (897,039,360 Hz) |
| **PLL Lock Status Register** | `RESET_SM_READY_STATUS` | SOURCE_PROVEN | Linux `dsi_phy_14nm.c:pll_14nm_poll_for_ready` |
| **PLL Lock Bit** | Bit 5 (`0x20`) | SOURCE_PROVEN | `val & BIT(5)` (1 = Frequency Locked) |
| **PLL Ready Bit** | Bit 0 (`0x01`) | SOURCE_PROVEN | `val & BIT(0)` (1 = Reset SM Ready) |

---

## 2. 14nm DSI PLL Register Layout (Relative to `0x00994800`)

| Register Name | Offset | Purpose | Safe Read? |
|---|---|---|---|
| `PLL_SYSCLK_EN_RESET` | `0x000` | System clock enable & soft reset | SAFE_RO |
| `PLL_TXCLK_EN` | `0x004` | Transmitter clock enable | SAFE_RO |
| `PLL_RESETSM_CNTRL` | `0x008` | Reset State Machine Control | SAFE_RO |
| `PLL_RESETSM_CNTRL2` | `0x00c` | Reset State Machine Control 2 | SAFE_RO |
| `PLL_RESETSM_CNTRL3` | `0x010` | Reset State Machine Control 3 | SAFE_RO |
| `PLL_RESETSM_CNTRL4` | `0x014` | Reset State Machine Control 4 | SAFE_RO |
| `PLL_RESETSM_CNTRL5` | `0x018` | Reset State Machine Control 5 | SAFE_RO |
| `PLL_KVCO_DIV_REF1` | `0x01c` | Kvco divider reference part 1 | SAFE_RO |
| `PLL_KVCO_DIV_REF2` | `0x020` | Kvco divider reference part 2 | SAFE_RO |
| `PLL_KVCO_COUNT1` | `0x024` | Kvco count value part 1 | SAFE_RO |
| `PLL_KVCO_COUNT2` | `0x028` | Kvco count value part 2 | SAFE_RO |
| `PLL_VCO_DIV_REF1` | `0x02c` | VCO divider reference part 1 | SAFE_RO |
| `PLL_VCO_DIV_REF2` | `0x030` | VCO divider reference part 2 | SAFE_RO |
| `PLL_VCO_COUNT1` | `0x034` | VCO count value part 1 | SAFE_RO |
| `PLL_VCO_COUNT2` | `0x038` | VCO count value part 2 | SAFE_RO |
| `PLL_PLLLOCK_CMP1` | `0x03c` | Lock comparison value part 1 | SAFE_RO |
| `PLL_PLLLOCK_CMP2` | `0x040` | Lock comparison value part 2 | SAFE_RO |
| `PLL_PLLLOCK_CMP3` | `0x044` | Lock comparison value part 3 | SAFE_RO |
| `PLL_PLLLOCK_CMP_EN` | `0x048` | Lock comparison enable | SAFE_RO |
| `PLL_PLL_LOCK_MIN_DELAY` | `0x04c` | Minimum delay for lock assertion | SAFE_RO |
| `PLL_RESETSM_READY_STATUS`| `0x050`| Status register: Bit 5 = LOCK, Bit 0 = READY | SAFE_STATUS |
| `PLL_SSC_ADJ_PER1` | `0x054` | Spread spectrum adjustment period 1 | SAFE_RO |
| `PLL_SSC_ADJ_PER2` | `0x058` | Spread spectrum adjustment period 2 | SAFE_RO |
| `PLL_SSC_PER1` | `0x05c` | Spread spectrum period part 1 | SAFE_RO |
| `PLL_SSC_PER2` | `0x060` | Spread spectrum period part 2 | SAFE_RO |
| `PLL_SSC_STEP_SIZE1` | `0x064` | Spread spectrum step size part 1 | SAFE_RO |
| `PLL_SSC_STEP_SIZE2` | `0x068` | Spread spectrum step size part 2 | SAFE_RO |
| `PLL_DEC_START` | `0x06c` | Decimation start integer divider | SAFE_RO |
| `PLL_DIV_FRAC_START1` | `0x070` | Fractional divider start part 1 | SAFE_RO |
| `PLL_DIV_FRAC_START2` | `0x074` | Fractional divider start part 2 | SAFE_RO |
| `PLL_DIV_FRAC_START3` | `0x078` | Fractional divider start part 3 | SAFE_RO |

---

## 3. Keyaki PLL Target Programming Values

### Derived parameters for Target VCO = 1,794,078,720 Hz (fref = 19,200,000 Hz):
- Multiplier = $2^{20} = 1,048,576$
- `dec_start_multiple` = $(1,794,078,720 \times 1,048,576) / 19,200,000 = 97,998,064$
- **`dec_start`** = $97,998,064 / 1,048,576 = \mathbf{93}$ (`0x5d`)
- **`div_frac_start`** = $97,998,064 \pmod{1,048,576} = \mathbf{475,376}$ (`0x740f0`)
- `plllock_cmp` (with duration=256 for plllock_cnt=1):
  - $256 \times 97,998,064 / 1,048,576 / 10 = \mathbf{2391}$ (`0x957`)

### Read-Only Status Check Usable in D8-M3:
Address: `0x00994850` (`DSI0_PLL_BASE + 0x50`):
- Mask `0x20` (Bit 5): PLL Lock bit (1 = LOCKED)
- Mask `0x01` (Bit 0): PLL Ready bit (1 = READY)
Acceptance Criterion: `(read32(0x00994850) & 0x21) == 0x21`.
