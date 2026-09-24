# LINUX GOLDEN DISPLAY REGISTERS AUDIT & SAFETY CLASSIFICATION

## 1. Safety Classification Legend

| Classification | Definition | Access Allowed in D8-M3 Audit |
|---|---|:---:|
| **SAFE_RO** | Read-only configuration or state register with zero hardware side effects | YES |
| **SAFE_STATUS**| Read-only status indicator register (polling allowed with bounded timeout) | YES |
| **CLEAR_ON_READ**| Reading clears interrupt/error bits; alters state | NO |
| **WRITE_ONLY** | Hardware write-only register; read returns undefined/bus stall | NO |
| **UNKNOWN_SIDE_EFFECT** | Register side effect unknown; can stall bus if domain is unclocked | NO |

---

## 2. Whitelist Register Snapshot Table

| Block | Physical Address | Register Name | Expected Working Value | Meaning / Bits | Safety Class |
|---|---|---|---|---|---|
| **GCC** | `0x00309008` | `gcc_mmss_noc_cfg_ahb_cbcr` | `0x20008001` | Bit 0=1 (enabled), Bit 31=0 (running) | SAFE_RO |
| **MMCC**| `0x008c247c` | `mmagic_mdss_gdscr` | `0xa0222000` | Bit 31=1 (PWR_ON), Bit 0=0 (no collapse) | SAFE_RO |
| **MMCC**| `0x008c2304` | `mdss_gdscr` | `0xa0222000` | Bit 31=1 (PWR_ON), Bit 0=0 (no collapse) | SAFE_RO |
| **MMCC**| `0x008c5024` | `mmss_mmagic_ahb_cbcr` | `0x00000001` | Bit 0=1 (enabled), Bit 31=0 (running) | SAFE_RO |
| **MMCC**| `0x008c5054` | `mmss_mmagic_cfg_ahb_cbcr` | `0x20008001` | Bit 0=1 (enabled), Bit 31=0 (running) | SAFE_RO |
| **MMCC**| `0x008c2478` | `mmagic_mdss_noc_cfg_ahb_cbcr`| `0x00000001`| Bit 0=1 (enabled), Bit 31=0 (running) | SAFE_RO |
| **MMCC**| `0x008c2474` | `mmagic_mdss_axi_cbcr` | `0x00000001` | Bit 0=1 (enabled), Bit 31=0 (running) | SAFE_RO |
| **MMCC**| `0x008c2308` | `mdss_ahb_cbcr` | `0x20008001` | Bit 0=1 (enabled), Bit 31=0 (running) | SAFE_RO |
| **MMCC**| `0x008c2310` | `mdss_axi_cbcr` | `0x00006221` | Bit 0=1 (enabled), Bit 31=0 (running) | SAFE_RO |
| **MMCC**| `0x008c231c` | `mdss_mdp_cbcr` | `0x00006221` | Bit 0=1 (enabled), Bit 31=0 (running) | SAFE_RO |
| **MMCC**| `0x008c233c` | `mdss_byte0_cbcr` | `0x00000001` (active) | Bit 0=1 (enabled), Bit 31=0 (running) | SAFE_RO |
| **MMCC**| `0x008c2314` | `mdss_pclk0_cbcr` | `0x00000001` (active) | Bit 0=1 (enabled), Bit 31=0 (running) | SAFE_RO |
| **MMCC**| `0x008c2344` | `mdss_esc0_cbcr` | `0x00000001` (active) | Bit 0=1 (enabled), Bit 31=0 (running) | SAFE_RO |
| **DSI0 PLL**| `0x00994850` | `PLL_RESETSM_READY_STATUS`| `0x00000021` | Bit 5=1 (LOCKED), Bit 0=1 (READY) | SAFE_STATUS |
| **DSI0 CTRL**| `0x00994000` | `DSI_HW_VERSION` | `0x10040001` | DSI controller major/minor revision | SAFE_RO |
| **DSI0 PHY**| `0x00994400` | `DSI_PHY_CMN_HW_VERSION` | `0x00000002` | 14nm PHY revision 2.0 | SAFE_RO |

---

## 3. Comparison: Linux Golden vs Current XNU D8-M2 Baseline

| Subsystem Component | Linux Golden Working State | XNU D8-M2 Baseline State | Comparison | Next Milestone Required |
|---|---|---|---|---|
| `MMAGIC_MDSS_GDSC` | Powered ON (`0xa0222000`) | Powered ON (`0xa0222000`) | **MATCH** | None (sealed) |
| `MDSS_GDSC` | Powered ON (`0xa0222000`) | Powered ON (`0xa0222000`) | **MATCH** | None (sealed) |
| MMAGIC AHB/AXI Branches | All 4 enabled & running | All 4 enabled & running | **MATCH** | None (sealed) |
| `mdss_ahb` branch | Enabled (`0x20008001`) | Enabled (`0x20008001`) | **MATCH** | None (sealed) |
| `mdss_axi` branch | Enabled (`0x00006221`) | Enabled (`0x00006221`) | **MATCH** | None (sealed) |
| `mdss_mdp` branch | Enabled (`0x00006221`) | Enabled (`0x00006221`) | **MATCH** | None (sealed) |
| DSI0 PLL VCO & Dividers | Locked (`0x21` in status) | Unprogrammed (unlocked) | EXPECTED_DIFFERENCE | **D8-M3** |
| `mdss_byte0` branch | Enabled | Halted (`0x80000000`) | EXPECTED_DIFFERENCE | **D8-M3** |
| `mdss_pclk0` branch | Enabled | Halted (`0x80000000`) | EXPECTED_DIFFERENCE | **D8-M3** |
| `mdss_esc0` branch | Enabled | Halted (`0x80000000`) | EXPECTED_DIFFERENCE | **D8-M3** |
| DSI0 PHY Lanes | Configured & LP-11 | Unconfigured | EXPECTED_DIFFERENCE | **D8-M3 / D8-M4** |
| Panel Power Rails & Reset | Powered, Reset=1 | Unpowered / Reset=0 | EXPECTED_DIFFERENCE | **D8-M5** |
| DCS Init Commands | Sent (Display ON) | Not sent | EXPECTED_DIFFERENCE | **D8-M6** |
| Backlight (WLED) | On | Off | EXPECTED_DIFFERENCE | **D8-M7** |
| MDP Scanout Framebuffer | Active scanout | Not configured | EXPECTED_DIFFERENCE | **D8-M8** |
