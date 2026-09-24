# MSM8996 14nm DSI PHY Golden Programming Sequence

## 1. Overview
The MSM8996 Qualcomm 14nm DSI PHY v2 initialization sequence configures the physical analog transceivers for DSI0 (4 data lanes + 1 clock lane):
1. **PHY Regulator Configuration:** Program internal LDO/regulator bias levels across all 5 lane blocks (`mdss_dsi_8996_phy_regulator_enable`).
2. **PHY Software Reset:** Pulse software reset to clear PHY state machines (`mdss_dsi_phy_sw_reset`).
3. **Common Control Setup:** Initialize common analog control registers.
4. **Lane Drive Strength:** Configure drive strength and slew rate for 4 data lanes and clock lane.
5. **DSI PHY Timing Parameters:** Program exact high-speed and low-power timing counters derived from bit clock (897,039,360 Hz).
6. **Lane Enable & Validation:** Power up data lanes 0..3 and clock lane.

---

## 2. Lane Block Address Mapping (PHY Base = `0x00994400`)

| Lane Block | Base Address | Offset Range | Spacing |
|---|---|---|---|
| **Data Lane 0 (DL0)** | `0x00994400` | `+ 0x000` | 128 bytes (`0x80`) |
| **Data Lane 1 (DL1)** | `0x00994480` | `+ 0x080` | 128 bytes (`0x80`) |
| **Data Lane 2 (DL2)** | `0x00994500` | `+ 0x100` | 128 bytes (`0x80`) |
| **Data Lane 3 (DL3)** | `0x00994580` | `+ 0x180` | 128 bytes (`0x80`) |
| **Clock Lane (CLK)** | `0x00994600` | `+ 0x200` | 128 bytes (`0x80`) |

---

## 3. Unpacked PHY Timing Blob (40 Bytes / 10 Words)

From Device Tree & Hardware Debugfs:
`<0x241f0809 0x50304a0 0x241f0809 0x50304a0 0x241f0809 0x50304a0 0x241f0809 0x50304a0 0x241b0809 0x50304a0>`

Live Hardware Register State (`/sys/kernel/debug/mdss_panel_fb0/intf0/dsi_phy_ctrl/timing`):
`0xe6 0x38 0x26 0x00 0x68 0x6e 0x2a 0x3c 0x2c 0x03 0x04 0x00`

### Decoded Timing Breakdown:
| Parameter | Value | Register Field | Purpose |
|---|---|---|---|
| `t_clk_pre` | 43 (`0x2b`) | `DSI_T_CLK_PRE` | Clock lane preamble duration before HS data |
| `t_clk_post` | 27 (`0x1b`) | `DSI_T_CLK_POST` | Clock lane postamble duration after HS data |
| `t_clk_zero` | 38 (`0x26`) | `DSI_T_CLK_ZERO` | Clock zero duration after prepare |
| `t_clk_trail` | 9 (`0x09`) | `DSI_T_CLK_TRAIL` | Clock trail duration after burst |
| `t_clk_prepare` | 8 (`0x08`) | `DSI_T_CLK_PREPARE`| Clock prepare duration |
| `t_hs_prepare` | 8 (`0x08`) | `DSI_T_HS_PREPARE` | Data lane HS prepare duration |
| `t_hs_zero` | 31 (`0x1f`) | `DSI_T_HS_ZERO` | Data lane HS zero duration |
| `t_hs_trail` | 9 (`0x09`) | `DSI_T_HS_TRAIL` | Data lane HS trail duration |
| `t_hs_rqst` | 36 (`0x24`) | `DSI_T_HS_RQST` | HS request transition time |
| `t_hs_exit` | 42 (`0x2a`) | `DSI_T_HS_EXIT` | HS exit transition time |
| `t_ta_go` | 3 (`0x03`) | `DSI_T_TA_GO` | Bus turnaround GO duration |
| `t_ta_sure` | 0 (`0x00`) | `DSI_T_TA_SURE` | Bus turnaround SURE duration |
| `t_ta_get` | 4 (`0x04`) | `DSI_T_TA_GET` | Bus turnaround GET duration |

---

## 4. Ordered PHY Programming Sequence

| Seq | Function | Register | Address | Mask | Value | Delay | Purpose |
|---:|---|---|---|---|---|---|---|
| 0101 | `phy_regulator_en` | `DL0_REGULATOR_CTRL` | `0x00994564` | `0xff` | `0x1d` | - | Data lane 0 regulator bias |
| 0102 | `phy_regulator_en` | `DL1_REGULATOR_CTRL` | `0x009945e4` | `0xff` | `0x1d` | - | Data lane 1 regulator bias |
| 0103 | `phy_regulator_en` | `DL2_REGULATOR_CTRL` | `0x00994664` | `0xff` | `0x1d` | - | Data lane 2 regulator bias |
| 0104 | `phy_regulator_en` | `DL3_REGULATOR_CTRL` | `0x009946e4` | `0xff` | `0x1d` | - | Data lane 3 regulator bias |
| 0105 | `phy_regulator_en` | `CLK_REGULATOR_CTRL` | `0x00994764` | `0xff` | `0x1d` | - | Clock lane regulator bias |
| 0106 | `phy_sw_reset` | `DSI_PHY_SW_RESET` | `0x0099412c` | `0x01` | `0x01` | 10 us | Assert PHY software reset |
| 0107 | `phy_sw_reset` | `DSI_PHY_SW_RESET` | `0x0099412c` | `0x01` | `0x00` | 100 us | De-assert PHY software reset |
| 0108 | `phy_init` | `PHY_CMN_CTRL_0` | `0x00994400` | `0xff` | `0x00` | - | Common control register 0 |
| 0109 | `phy_init` | `PHY_CMN_CTRL_1` | `0x00994404` | `0xff` | `0x00` | - | Common control register 1 |
| 0110 | `phy_init` | `PHY_CMN_CTRL_2` | `0x00994408` | `0xff` | `0x00` | - | Common control register 2 |
| 0111 | `phy_init` | `PHY_CMN_CTRL_3` | `0x0099440c` | `0xff` | `0x00` | - | Common control register 3 |
| 0112 | `phy_init` | `PHY_CMN_CTRL_4` | `0x00994410` | `0xff` | `0x1e` | - | Common control register 4 |
| 0113 | `phy_init` | `PHY_CMN_CTRL_5` | `0x00994414` | `0xff` | `0x00` | - | Common control register 5 |
| 0114 | `phy_init` | `PHY_CMN_CTRL_6` | `0x00994418` | `0xff` | `0x00` | - | Common control register 6 |
| 0115 | `phy_strength` | `DL0_STRENGTH_CTRL` | `0x00994440` | `0xffff` | `0x06ff`| - | Data lane 0 drive strength |
| 0116 | `phy_strength` | `DL1_STRENGTH_CTRL` | `0x009944c0` | `0xffff` | `0x06ff`| - | Data lane 1 drive strength |
| 0117 | `phy_strength` | `DL2_STRENGTH_CTRL` | `0x00994540` | `0xffff` | `0x06ff`| - | Data lane 2 drive strength |
| 0118 | `phy_strength` | `DL3_STRENGTH_CTRL` | `0x009945c0` | `0xffff` | `0x06ff`| - | Data lane 3 drive strength |
| 0119 | `phy_strength` | `CLK_STRENGTH_CTRL` | `0x00994640` | `0xffff` | `0x00ff`| - | Clock lane drive strength |
| 0120 | `phy_timing` | `DSI_T_CLK_POST` | `0x00994018` | `0x3f` | `0x1b` | - | Program t_clk_post (27 cycles) |
| 0121 | `phy_timing` | `DSI_T_CLK_PRE` | `0x009942a0` | `0x3f` | `0x2b` | - | Program t_clk_pre (43 cycles) |
| 0122 | `phy_lanecfg` | `DL0_CFG_0` | `0x00994420` | `0xff` | `0x10` | - | DL0 lane enable / config |
| 0123 | `phy_lanecfg` | `DL1_CFG_0` | `0x009944a0` | `0xff` | `0x10` | - | DL1 lane enable / config |
| 0124 | `phy_lanecfg` | `DL2_CFG_0` | `0x00994520` | `0xff` | `0x10` | - | DL2 lane enable / config |
| 0125 | `phy_lanecfg` | `DL3_CFG_0` | `0x009945a0` | `0xff` | `0x10` | - | DL3 lane enable / config |
| 0126 | `phy_lanecfg` | `CLK_CFG_0` | `0x00994620` | `0xff` | `0x10` | - | Clock lane enable / config |
| 0127 | `phy_lanecfg` | `CLK_CFG_1` | `0x00994624` | `0xff` | `0x8f` | - | Clock lane differential config |

---

## 5. PHY Acceptance Criteria
- Revision Check: `PHY_REVISION` read at `0x00994400` offset or status == `0x2` (14nm v2).
- Lanes Enabled: `DSI_LANE_CTRL` (`0x009941f4`) == `0x03000104`.
- No Lane Faults: `DSI_LANE_STATUS` (`0x009941b8`) == `0x00000006` (Idle LP-11 state).
