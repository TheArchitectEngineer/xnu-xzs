# MMCC DSI Clock Sequence: Xperia XZs (Keyaki / MSM8996)

## 1. Overview
The Multimedia Clock Controller (MMCC, base `0x008c0000`) provides clock branches to the DSI0 controller and PHY:
- **`BYTE0` Clock:** Derived from DSI0 PLL VCO post-divider (`dsi0pll_byte_clk_src` = 112.13 MHz).
- **`PCLK0` Clock:** Derived from DSI0 PLL N2 divider (`dsi0pll_pixel_clk_src` = 149.51 MHz).
- **`ESC0` Clock:** Derived from TCXO / `mmsscc_xo` (19.20 MHz).

The MMCC branch clocks must only be un-halted AFTER the DSI0 PLL has reached frequency lock and reset SM ready status!

---

## 2. Register Offsets in MMCC (`0x008c0000`)

| Clock Name | CC Mux/RCG Offset | Branch Offset | CBCR Bit | Halt Status Mask |
|---|---|---|---|---|
| `mdss_byte0_clk` | `0x2120` (`BYTE0_CMD_RCGR`) | `0x233c` (`BYTE0_CBCR`) | Bit 0 (Enable) | Bit 31 (CLK_OFF == 0) |
| `mdss_pclk0_clk` | `0x2000` (`PCLK0_CMD_RCGR`) | `0x2314` (`PCLK0_CBCR`) | Bit 0 (Enable) | Bit 31 (CLK_OFF == 0) |
| `mdss_esc0_clk` | `0x2160` (`ESC0_CMD_RCGR`) | `0x2344` (`ESC0_CBCR`) | Bit 0 (Enable) | Bit 31 (CLK_OFF == 0) |

---

## 3. Ordered Branch Enable Sequence

| Seq | Step | Register | Address | Mask | Value | Delay/Poll | Purpose |
|---:|---|---|---|---|---|---|---|
| 0201 | PLL Locked Check | `PLL_RESET_SM_STATUS` | `0x009948cc` | `0x21` | `0x21` | Check | Ensure PLL is locked before gating MMCC |
| 0202 | Byte0 Mux Config | `BYTE0_CFG_RCGR` | `0x008c2124` | `0x700` | `0x200` | - | Select DSI0 PLL byte clock as source |
| 0203 | Byte0 Update | `BYTE0_CMD_RCGR` | `0x008c2120` | `0x01` | `0x01` | Poll bit 0 == 0 | Trigger and wait for RCG update |
| 0204 | Byte0 Branch Enable | `BYTE0_CBCR` | `0x008c233c` | `0x01` | `0x01` | - | Assert CLK_ENABLE |
| 0205 | Byte0 Halt Poll | `BYTE0_CBCR` | `0x008c233c` | `0x80000000` | `0x00` | Poll bit 31 == 0 | Wait for clock to turn on (un-halt) |
| 0206 | Pclk0 Mux Config | `PCLK0_CFG_RCGR` | `0x008c2004` | `0x700` | `0x200` | - | Select DSI0 PLL pixel clock as source |
| 0207 | Pclk0 Update | `PCLK0_CMD_RCGR` | `0x008c2000` | `0x01` | `0x01` | Poll bit 0 == 0 | Trigger and wait for RCG update |
| 0208 | Pclk0 Branch Enable | `PCLK0_CBCR` | `0x008c2314` | `0x01` | `0x01` | - | Assert CLK_ENABLE |
| 0209 | Pclk0 Halt Poll | `PCLK0_CBCR` | `0x008c2314` | `0x80000000` | `0x00` | Poll bit 31 == 0 | Wait for clock to turn on (un-halt) |
| 0210 | Esc0 Mux Config | `ESC0_CFG_RCGR` | `0x008c2164` | `0x700` | `0x000` | - | Select XO (19.2 MHz) as source |
| 0211 | Esc0 Update | `ESC0_CMD_RCGR` | `0x008c2160` | `0x01` | `0x01` | Poll bit 0 == 0 | Trigger and wait for RCG update |
| 0212 | Esc0 Branch Enable | `ESC0_CBCR` | `0x008c2344` | `0x01` | `0x01` | - | Assert CLK_ENABLE |
| 0213 | Esc0 Halt Poll | `ESC0_CBCR` | `0x008c2344` | `0x80000000` | `0x00` | Poll bit 31 == 0 | Wait for clock to turn on (un-halt) |

---

## 4. Acceptance Criteria
- `BYTE0_CBCR` (`0x008c233c`) & `0x80000001` == `0x00000001` (Enabled and unhalted).
- `PCLK0_CBCR` (`0x008c2314`) & `0x80000001` == `0x00000001` (Enabled and unhalted).
- `ESC0_CBCR` (`0x008c2344`) & `0x80000001` == `0x00000001` (Enabled and unhalted).
