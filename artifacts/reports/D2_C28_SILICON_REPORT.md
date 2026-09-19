# D2-C2.8 Silicon Report

## Git Baseline

- **Baseline Commit (Pre-task):** `f37d95c9d8bf353bb2506504ad07906e2515e0da`
- **Working Tree:** Clean at start of phase
- **Execution Target:** Sony Xperia XZs (`tone-keyaki` / G8231 / MSM8996 v2.2.0, serial `BH905SX976`)
- **Git Push Verification:** Baseline verified present on `origin/xzs-bringup`.

---

## RESET_SM_STATUS Semantic Audit

- **HARDWARE VERIFIED:**
  `QSERDES_COM_RESET_SM_STATUS @ PHY + 0x160 = 0x00` throughout all pre-test and post-sequence observations.
- **INTERPRETATION / AUDIT:**
  Qualcomm MSM8996 Linux kernel source, downstream Sony kernel headers (`twrp-Image`), and mainline QMP PHY drivers (`phy-qcom-qmp.h`, `phy-qcom-ufs-qmp-14nm.h`) were thoroughly audited.
  No bitmask definitions, enum states, polling routines, or debug decode routines exist for register `+0x160`.
- **Classification Output:**
  ```text
  RESET_SM_STATUS_OFFSET=0x160
  RESET_SM_STATUS_VALUE=0x00
  RESET_SM_ZERO_SEMANTICS=unknown
  ```
  The exact semantic meaning of raw `0x00` remains strictly unproven.

---

## Live PHY Clock DT

Audited from live Sony device tree (`artifacts/builds/twrp-extracted.dts:5665`, node `/soc/ufsphy@627000`):
- `compatible`: `"qcom,ufs-phy-qmp-14nm"`
- `clock-names`: `"ref_clk_src"`, `"ref_clk"`
- `clocks`: `<&rpm_bus_clocks RPM_SMD_LN_BB_CLK>`, `<&gcc GCC_UFS_CLKREF_CLK>`
- **Interface Clock Gate:**
  ```text
  PHY_HAS_TX_IFACE_CLK=no
  PHY_HAS_RX_IFACE_CLK=no
  ```
  The live Sony DT contains neither `tx_iface_clk` nor `rx_iface_clk`.

---

## Live HOST Clock DT

Audited from live Sony device tree (`artifacts/builds/twrp-extracted.dts:5699`, node `/soc/ufshc@624000`):
- `compatible`: `"qcom,ufshc"`
- `clock-names`:
  1. `core_clk_src`
  2. `core_clk`
  3. `bus_clk`
  4. `bus_aggr_clk`
  5. `iface_clk`
  6. `core_clk_unipro_src`
  7. `core_clk_unipro`
  8. `core_clk_ice`
  9. `ref_clk`
  10. `tx_lane0_sync_clk`
  11. `rx_lane0_sync_clk`

### HOST CLOCK TABLE

| Name | Provider | Clock ID | GCC Register | Source/Parent | Required Rate | Enabled Before PHY? | Enabled After PHY? |
| :--- | :--- | :--- | :--- | :--- | :--- | :--- | :--- |
| `core_clk_src` | GCC | `UFS_AXI_CLK_SRC` | `0x75024` | GPLL0 / XO | 200 MHz | YES | YES |
| `core_clk` | GCC | `GCC_UFS_AXI_CLK` | `0x75008` | `core_clk_src` | 200 MHz | YES | YES |
| `bus_clk` | GCC | `GCC_SYS_NOC_UFS_AXI_CLK` | `0x75038` | SYS_NOC | 0 (bus) | YES | YES |
| `bus_aggr_clk` | GCC | `GCC_AGGRE2_UFS_AXI_CLK` | `0x83014` | AGGRE2 | 0 (bus) | YES | YES |
| `iface_clk` | GCC | `GCC_UFS_AHB_CLK` | `0x7500c` | PERIPH_NOC_AHB | 0 (bus) | YES | YES |
| `core_clk_unipro_src` | GCC | `UFS_ICE_CORE_CLK_SRC` | `0x76014` | GPLL0 (postdiv) | 37.5 MHz | YES | YES |
| `core_clk_unipro` | GCC | `GCC_UFS_UNIPRO_CORE_CLK` | `0x7600c` | `core_clk_unipro_src` | 37.5 MHz | YES | YES |
| `core_clk_ice` | GCC | `GCC_UFS_ICE_CORE_CLK` | `0x76010` | `UFS_ICE_CORE_CMD_RCGR` | 150 MHz | NO (on demand) | YES |
| `ref_clk` | RPM | `RPM_SMD_LN_BB_CLK` | Buffer (clka/8) | PMIC XO | 19.2 MHz | YES | YES |
| `tx_lane0_sync_clk` | GCC | `GCC_UFS_TX_SYMBOL_0_CLK` | `0x75018` | PHY PLL Out | Symbol rate | NO | YES |
| `rx_lane0_sync_clk` | GCC | `GCC_UFS_RX_SYMBOL_0_CLK` | `0x7501c` | PHY PLL Out | Symbol rate | NO | YES |

---

## Exact Sony PHY Clock Init Path

Disassembly of genuine Sony kernel `twrp-Image`:
1. `ufs_qcom_phy_init_clks` (`0xffffffc00049ca80`):
   Acquires `ref_clk_src` and `ref_clk`.
2. `ufs_qcom_phy_enable_ref_clk` (`0xffffffc00049cac8`):
   Enables `ref_clk_src` (LN_BB) and `ref_clk` (`GCC_UFS_CLKREF`).
3. `ufs_qcom_phy_enable_iface_clk` (`0xffffffc00049cb48`):
   Checks `if (phy->tx_iface_clk == NULL) return 0;`.
   Because Sony live DT has no `tx_iface_clk`, this function immediately exits with `0`.
- **Classification Output:**
  ```text
  SONY_CALLS_ENABLE_IFACE_CLK_ON_XZS=no
  NOT REQUIRED ON MSM8996 XZS PATH
  ```

---

## Exact Sony Host Clock Init Path

Disassembly of `ufshcd_setup_clocks` (`0xffffffc000722248`):
- Iterates over host clock list in order of DT appearance.
- Enables:
  1. `core_clk` + `core_clk_src`
  2. `bus_clk` (SYS_NOC AXI)
  3. `bus_aggr_clk` (AGGRE2 AXI)
  4. `iface_clk` (UFS AHB)
  5. `core_clk_unipro` + `core_clk_unipro_src`
  6. `ref_clk` (LN_BB)
- Explicitly leaves `tx_lane0_sync_clk` and `rx_lane0_sync_clk` gated until `ufs_qcom_enable_lane_clks` (`0xffffffc000724efc`), which is executed only after PHY calibration.

---

## Sony Pre-PHY Clock State

```text
SONY_PRE_PHY_CLOCK_STATE:
  core_clk_src:         ON
  core_clk:             ON
  bus_clk:              ON
  bus_aggr_clk:         ON
  iface_clk:            ON
  core_clk_unipro_src:  ON
  core_clk_unipro:      ON
  core_clk_ice:         NOT YET ENABLED
  ref_clk (LN_BB):      ON
  ref_clk (CLKREF):     ON
  tx_lane0_sync_clk:    NOT YET ENABLED (post-PHY)
  rx_lane0_sync_clk:    NOT YET ENABLED (post-PHY)
  tx_cfg_clk:           OFF / UNUSED
  rx_cfg_clk:           OFF / UNUSED
```

---

## TX_CFG/RX_CFG Consumer Audit

- `GCC_UFS_TX_CFG_CLK @ 0x75010`
- `GCC_UFS_RX_CFG_CLK @ 0x75014`
- Audit Results:
  ```text
  TX_CFG_CONSUMER=none (not referenced in live Sony DT or kernel driver)
  RX_CFG_CONSUMER=none (not referenced in live Sony DT or kernel driver)
  TX_CFG_REQUIRED_PRE_PHY=no
  RX_CFG_REQUIRED_PRE_PHY=no
  ```

---

## Lane Symbol Clock Timing

- `GCC_UFS_TX_SYMBOL_0_CLK @ 0x75018`
- `GCC_UFS_RX_SYMBOL_0_CLK @ 0x7501c`
- Audit Results:
  ```text
  LANE_CLOCKS_BEFORE_PHY_READY=no
  ```
  These clocks derive from the PHY's internal PLL VCO and cannot run prior to `C_READY = 1`.

---

## XNU Clock Snapshot

Hardware-verified register readbacks captured on MSM8996 silicon before any mutations:

| Register Name | Offset | Raw Value | Enable Bit | CLK_OFF Bit | State |
| :--- | :--- | :--- | :--- | :--- | :--- |
| `UFS_AXI_CBCR` | `0x75008` | `0x80004220` | `0` | `1` | Gated |
| `UFS_AHB_CBCR` | `0x7500c` | `0x80008000` | `0` | `1` | Gated |
| `SYS_NOC_AXI_CBCR` | `0x75038` | `0x80000000` | `0` | `1` | Gated |
| `AGGRE2_AXI_CBCR` | `0x83014` | `0x80000000` | `0` | `1` | Gated |
| `UFS_CLKREF_CBCR` | `0x88008` | `0x00000001` | `1` | `0` | **Running** |
| `UNIPRO_CORE_CBCR` | `0x7600c` | `0x80014220` | `0` | `1` | Gated |
| `ICE_CORE_CBCR` | `0x76010` | `0x80004220` | `0` | `1` | Gated |
| `TX_SYMBOL_0_CBCR` | `0x75018` | `0x80004000` | `0` | `1` | Gated |
| `RX_SYMBOL_0_CBCR` | `0x7501c` | `0x80004000` | `0` | `1` | Gated |
| `TX_CFG_CBCR` | `0x75010` | `0x80000000` | `0` | `1` | Gated |
| `RX_CFG_CBCR` | `0x75014` | `0x80000000` | `0` | `1` | Gated |
| `UFS_AXI_CMD_RCGR` | `0x75024` | `0x80000000` | `0` | `1` (ROOT_OFF) | Gated |
| `UFS_AXI_CFG_RCGR` | `0x75028` | `0x00000000` | - | - | Gated |
| `UFS_ICE_CMD_RCGR` | `0x76014` | `0x80000000` | `0` | `1` (ROOT_OFF) | Gated |
| `UFS_ICE_CFG_RCGR` | `0x76018` | `0x00000000` | - | - | Gated |

CSV Artifacts generated:
- `artifacts/reports/xnu_clock_state.csv`
- `artifacts/reports/sony_expected_clock_state.csv`

---

## Clock Diff

Comparison between Sony Linux pre-PHY state and XNU pre-mutation state:

| Clock Name | Register | Sony Expected | XNU Pre-Test | Category | Action |
| :--- | :--- | :--- | :--- | :--- | :--- |
| `SYS_NOC_AXI` | `0x75038` | ON | Gated | `EXPECTED_ON_XNU_OFF` | Handled by bus bringup |
| `AGGRE2_AXI` | `0x83014` | ON | Gated | `EXPECTED_ON_XNU_OFF` | Handled by bus bringup |
| `UFS_AXI` | `0x75008` | ON | Gated | `EXPECTED_ON_XNU_OFF` | Handled by bus bringup |
| `UFS_AHB` | `0x7500c` | ON | Gated | `EXPECTED_ON_XNU_OFF` | Handled by bus bringup |
| `UFS_CLKREF` | `0x88008` | ON | ON | `MATCH` | Frozen |
| `UNIPRO_CORE` | `0x7600c` | ON | Gated | `EXPECTED_ON_XNU_OFF` | **Candidate 1** |
| `ICE_CORE` | `0x76010` | OFF (optional) | Gated | `MATCH` | Not needed pre-PHY |
| `TX_CFG` | `0x75010` | OFF | Gated | `MATCH` | Unused |
| `RX_CFG` | `0x75014` | OFF | Gated | `MATCH` | Unused |
| `TX_SYMBOL_0`| `0x75018` | OFF | Gated | `MATCH` | Post-PHY PLL |
| `RX_SYMBOL_0`| `0x7501c` | OFF | Gated | `MATCH` | Post-PHY PLL |

---

## Experimental Candidate(s)

- **Candidate 1:** `GCC_UFS_UNIPRO_CORE_CBCR` (`0x7600c`)
  - Parent RCG: `UFS_ICE_CORE_CMD_RCGR` (`0x76014`, bit 1 `ROOT_EN`).
  - Justification: UniPro Core clock feeds the controller UniPro protocol layer and interface logic to the M-PHY. Source audit proved it is enabled by Linux `ufshcd_setup_clocks` prior to PHY initialization.

---

## Per-Candidate Silicon Result

### Candidate 1 Execution:
1. Enabled `UFS_ICE_CORE_CMD_RCGR` (`0x76014`) `ROOT_EN`:
   Command register root enabled.
2. Enabled `GCC_UFS_UNIPRO_CORE_CBCR` (`0x7600c`):
   Readback: `raw = 0x00014221`, `CLK_OFF = 0`, `rc = 0`.
   **Hardware Verified:** UniPro Core clock is running!
3. Executed exact Sony v2.2.0 sequence (Rate-A 76 entries + Rate-B override, no 0x134 restore per `quirks = 0`).
4. Captured polling checkpoints at 10 ms, 100 ms, 500 ms, 1000 ms:
   - `T = 10,000 µs (0x2710)`: `CR(+0x190) = 0`, `PCS(+0xD68) = 0`, `LOCK(+0x0C8) = 0x01`, `0x160 = 0x00`, `CMN(+0x194) = 0x0E`, `C00 = 0x01`, `C04 = 0x01`
   - `T = 100,000 µs (0x186a0)`: `CR(+0x190) = 0`, `PCS(+0xD68) = 0`, `LOCK(+0x0C8) = 0x01`, `0x160 = 0x00`, `CMN(+0x194) = 0x0E`, `C00 = 0x01`, `C04 = 0x01`
   - `T = 500,000 µs (0x7a120)`: `CR(+0x190) = 0`, `PCS(+0xD68) = 0`, `LOCK(+0x0C8) = 0x01`, `0x160 = 0x00`, `CMN(+0x194) = 0x0E`, `C00 = 0x01`, `C04 = 0x01`
   - `T = 1,000,000 µs (0xf4240)`: `CR(+0x190) = 0`, `PCS(+0xD68) = 0`, `LOCK(+0x0C8) = 0x01`, `0x160 = 0x00`, `CMN(+0x194) = 0x0E`, `C00 = 0x01`, `C04 = 0x01`

---

## C_READY / PCS_READY

```text
C_READY   = 0 (timeout after 1,000,000 µs)
PCS_READY = 0 (timeout after 1,000,000 µs)
```
Classification: **C_READY = 0 AFTER CANDIDATE 1 ENABLE**.
Enabling `GCC_UFS_UNIPRO_CORE_CLK` alone did not cause the QSERDES common PLL to lock.

---

## HARDWARE VERIFIED FACTS

1. **GCC Clock Snapshot Integrity:**
   All 15 UFS-related GCC registers (`0x75xxx` and `0x76xxx`) read back predictably. Prior to controller bus bringup, all branches except `CLKREF` (`0x88008`) are gated (`CLK_OFF = 1`).
2. **UniPro Clock Enablement:**
   `GCC_UFS_UNIPRO_CORE_CBCR` (`0x7600c`) enables cleanly (`raw = 0x00014221`, `CLK_OFF = 0`).
3. **No SError Abort:**
   With prerequisite bus clocks (`SYS_NOC`, `AGGRE2`, `UFS_AXI`, `UFS_AHB`, `CLKREF`) active, reading `0x160` and all controller/PHY apertures proceeds with zero bus faults or exceptions.
4. **QSERDES Reset State Machine Status:**
   `RESET_SM_STATUS @ +0x160 = 0x00` remains at `0x00` even when `UNIPRO_CORE` is active.
5. **Hard Invariants Maintained:**
   `HCE = 0`, `HCS = 0`, `UICCMD = 0`. DMA was untouched.
6. **Rollback & Warm Reset:**
   Reverse-order release of LN_BB (Sleep -> Active), L12, and L28 completed with RPM ACKs. Watchdog bite warm reset returned to Fastboot at +6s.

---

## SOURCE-AUDITED FACTS

1. **Sony Live DT Structure:**
   `/soc/ufsphy@627000` consumes only `ref_clk_src` (LN_BB) and `ref_clk` (`GCC_UFS_CLKREF`). Does not consume interface clocks.
2. **Interface Clock Skip:**
   `ufs_qcom_phy_enable_iface_clk` in `twrp-Image` is a no-op when `tx_iface_clk` is NULL.
3. **TX_CFG / RX_CFG Unused:**
   MSM8996 DT and downstream driver contain no references to `GCC_UFS_TX_CFG_CLK` or `GCC_UFS_RX_CFG_CLK`.
4. **Lane Symbol Clocks:**
   `GCC_UFS_TX_SYMBOL_0_CLK` and `GCC_UFS_RX_SYMBOL_0_CLK` are derived from the PHY PLL and must remain gated until after PHY readiness.
5. **RESET_SM Semantics:**
   No source decode or poll loop exists for `0x160`.

---

## INFERENCES

1. The QSERDES Common PLL lock (`C_READY @ +0x190 = 1`) does not fail due to a missing UniPro core clock.
2. Since power supplies (L28, L12, LN_BB), reference clock (`GCC_UFS_CLKREF`), soft reset sequence, and UniPro core clock are all verified identical to Linux, the remaining difference between the bootloader/Linux working state and XNU must reside in:
   - Analog bias or bandgap configuration / PMIC pin control (e.g. MPM / SPMI PM8994 pin-control / boost supplies).
   - An external hardware reset signal or GPIO (TLMM / pinctrl) holding the PHY analog block in reset.
   - Initial RCG configuration of `UFS_AXI_CLK_SRC` (`0x75024` currently in raw `0x80000000` gated state; Linux configures frequency before variant init).

---

## D2-C2 Status

```text
D2-C2 STATUS: IN PROGRESS (C_READY = 0, PCS_READY = 0)
Causal Elimination:
  - 0x134 preservation:          RULED OUT
  - Reset/C04 ordering:          RULED OUT
  - UniPro core clock enable:    RULED OUT as sole missing prerequisite
  - RPM Regulators (L28/L12):    FROZEN (ACK verified)
  - LN_BB buffer:                FROZEN (ACK verified)
  - Clocks untouched:            TX_CFG / RX_CFG / Lane symbol clocks correctly isolated
```

---

## Recommended Next Phase

### Phase D2-C2.9: UFS_AXI RCG Frequency Configuration & TLMM/Reset Pin State Audit
1. Audit whether `UFS_AXI_CLK_SRC` (`0x75024`) RCG requires active PLL configuration (GPLL0 rate select) to feed the host digital wrapper.
2. Audit live Sony DT and pinctrl driver for UFS reset pins (`ufs_reset`, TLMM GPIOs) or PMIC sleep pin controls that might hold the PHY hardware in reset.
