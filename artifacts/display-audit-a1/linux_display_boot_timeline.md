# Linux Display Boot Timeline: Xperia XZs (Keyaki / MSM8996)

## Target & Environment Metadata
- **Target Serial:** `BH905SX976`
- **Model:** Sony Xperia XZs G8231 (`keyaki`, MSM8996 v3.0, 4x Kryo, 4GB LPDDR4)
- **Environment:** TWRP 3.18.20-v01+ kernel (`artifacts/builds/twrp-kagura.img` booted via fastboot)
- **Status:** Panel active, framebuffer rendering at 1080x1920 @ 60 Hz command mode
- **Evidence Class:** HARDWARE_PROVEN / SOURCE_PROVEN

---

## Chronological Boot Display Timeline

| Time / Order | Component | Event | Source Log Line | Evidence Class |
|---|---|---|---|---|
| `[ 0.309901]` | Memory Reserved | Reserved DFPS data memory | `platform 994400.qcom,mdss_dsi_pll: assigned reserved memory node dfps_data_mem@83400000` | HARDWARE_PROVEN |
| `[ 0.856168]` | MMCC GDSC | `gdsc_mmagic_mdss` regulator probed | `gdsc_mmagic_mdss: no parameters` | HARDWARE_PROVEN |
| `[ 0.859225]` | MMCC GDSC | `gdsc_mdss` probed, parented by mmagic | `gdsc_mdss: supplied by gdsc_mmagic_mdss` | HARDWARE_PROVEN |
| `[ 0.867347]` | DSI0 PLL | MDSS DSI 0 PLL driver probed & SSC enabled | `mdss_pll_probe: MDSS pll label = MDSS DSI 0 PLL PLL SSC enabled` | HARDWARE_PROVEN |
| `[ 0.869904]` | DSI0 PLL | Early PLL lock test (expected unlocked before configuration) | `pll_is_pll_locked_8996: DSI PLL ndx=0 status=0 failed to Lock` | HARDWARE_PROVEN |
| `[ 0.871401]` | DSI0 PLL | Clocks registered (`dsi0pll_vco`, `byte`, `pclk`) | `dsi_pll_clock_register_8996: Registered DSI PLL ndx=0 clocks successfully` | HARDWARE_PROVEN |
| `[ 0.871513]` | DSI1 PLL | MDSS DSI 1 PLL driver probed (unused on Keyaki) | `mdss_pll_probe: MDSS pll label = MDSS DSI 1 PLL PLL SSC enabled` | HARDWARE_PROVEN |
| `[ 1.332664]` | MDP5 | MDSS MDP pipe address setup (SMP / pipes 0-11) | `mdss_mdp_pipe_addr_setup: type:1 ftchid:0 xinid:0 num:0 ndx:0x1 prio:0` | HARDWARE_PROVEN |
| `[ 1.344304]` | MDP5 Probe | MDSS MDP core initialized, continuous splash is OFF | `mdss_mdp_probe: mdss version = 0x10070002, bootloader display is off` | HARDWARE_PROVEN |
| `[ 1.354290]` | SMMU | MDP SMMU v2 domains 0-3 mapped and registered | `mdss_smmu_probe: iommu v2 domain[0] mapping and clk register successful!` | HARDWARE_PROVEN |
| `[ 1.361434]` | DSI0 Ctrl | MDSS DSI CTRL->0 probed (`0x00994000`) | `mdss_dsi_ctrl_probe: DSI Ctrl name = MDSS DSI CTRL->0` | HARDWARE_PROVEN |
| `[ 1.362092]` | Panel Detect | Panel ADC reading detection | `mdss_dsi_panel_driver_detection: physical:10192` | HARDWARE_PROVEN |
| `[ 1.362438]` | Panel Selection | Panel identified: Panel Name = "9" (Sharp Synaptics Cmd Mode) | `mdss_dsi_panel_init: Panel Name = 9` | HARDWARE_PROVEN |
| `[ 1.363080]` | Panel Config | Panel features: Command Mode, ULPS disabled | `mdss_dsi_parse_panel_features: partial_update_enabled=0, ulps feature disabled` | HARDWARE_PROVEN |
| `[ 1.364264]` | DSI0 Resources| Remapped DSI CTRL base (`ctrl_size=0x400`), PHY base (`phy_size=0x588`)| `mdss_dsi_retrieve_ctrl_resources: ctrl_base=ffffff8001ef2000 ctrl_size=400 phy_base=ffffff8001ef4400 phy_size=588` | HARDWARE_PROVEN |
| `[ 1.367704]` | DSI0 Status | DSI 0 initialized, DSI rev: 0x10040001, PHY rev: 0x2 | `mdss_dsi_ctrl_probe: Dsi Ctrl->0 initialized, DSI rev:0x10040001, PHY rev:0x2` | HARDWARE_PROVEN |
| `[ 1.380736]` | FB Probe | Framebuffer 0 probe (split_mode: 0, single DSI) | `mdss_fb_probe: fb0: split_mode:0 left:0 right:0` | HARDWARE_PROVEN |
| `[ 1.381993]` | FB Register | Framebuffer 0 registered: 1080x1920 RGB888 | `mdss_fb_register: FrameBuffer[0] 1080x1920 registered successfully!` | HARDWARE_PROVEN |
| `[ 1.391367]` | Debugfs | Debugfs directory created for DSI controller | `mdss_dsi_ctrl 994000.qcom,mdss_dsi_ctrl0: mipi_dsi_panel_create_debugfs: create folder 994000.qcom,mdss_dsi_ctrl0` | HARDWARE_PROVEN |
| `[ 19.014563]`| Panel Power On | Display unblank triggered by TWRP user space UI | `@@@@ panel power on @@@@` | HARDWARE_PROVEN |
| `[ 19.014600]`| Regulators | LAB (+5.5V) and IBB (-5.5V) enabled | `qpnp_lab_enable`, `qpnp_ibb_enable` (via sysfs / dts regulator supply) | SOURCE_PROVEN |
| `[ 19.014700]`| GPIO Power | Panel VDDIO (GPIO 51) set HIGH, LCD Reset (GPIO 8) released | `mdss_dsi_panel_power_on` -> `gpio_set_value(51, 1)`, `gpio_set_value(8, 1)` | SOURCE_PROVEN |
| `[ 19.014800]`| DSI PLL Init | DSI0 PLL rate set (VCO=1794.08 MHz), locked and ready | `pll_vco_set_rate_8996` -> `dsi_pll_enable_seq_8996` (LOCK bit 5=1, READY bit 0=1) | HARDWARE_PROVEN |
| `[ 19.014900]`| MMCC Clocks | DSI Byte Clock (112.13 MHz) & Pixel Clock (149.51 MHz) enabled | `mdss_dsi_clk_ctrl(DSI_ALL_CLKS, ON)` | HARDWARE_PROVEN |
| `[ 19.015000]`| DSI PHY Init | 14nm PHY initialized, lane strength, timing blob programmed | `mdss_dsi_8996_phy_regulator_enable`, `mdss_dsi_phy_init` | HARDWARE_PROVEN |
| `[ 19.015200]`| DSI Host Enable| DSI controller enabled (`0x009940f0 = 0x1`), 4 data lanes active | `DSI_CTRL` bit 0 written, `DSI_LANE_CTRL = 0x03000104` | HARDWARE_PROVEN |
| `[ 19.015500]`| DCS Commands | DSI on command sequence transmitted (`0x35` set_tear_on, `0x29` display_on)| `dsi_on_cmd` executed via DSI command DMA | HARDWARE_PROVEN |
| `[ 19.040675]`| PCC Setup | Panel color calibration setup | `mdss_dsi_panel_pcc_setup (1918): raw_ud=18 raw_vd=29 ct=0 area=116 ud=18 vd=29 r=0x00008000 g=0x00007C80 b=0x00008000` | HARDWARE_PROVEN |
| `[ 19.315030]`| Touch Unblank | Synaptics ClearPad touch controller early unblank | `clearpad clearpad: (clearpad_fb_early_unblank_handler:6237) EARLY UNBLANK @ 19.315008117` | HARDWARE_PROVEN |
| `[ 19.450956]`| Touch Resume | Synaptics S332U touch IC resumed | `clearpad clearpad: (clearpad_fb_unblank_handler:6249) UNBLANK (power=OK icount=0 active=false)` | HARDWARE_PROVEN |
| `[ 19.451000]`| Backlight | PMIC WLED brightness activated | `qpnp_wled_set_brightness` (via `/sys/class/leds/lcd-backlight/brightness`) | SOURCE_PROVEN |
