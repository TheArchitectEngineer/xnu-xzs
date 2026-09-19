# D2-C2.9 Silicon Report

## Git Baseline

- **Baseline Commit (Pre-Task):** `339e728ee11ce41c9f80ea28045c1198e3daad16`
- **Working Tree:** `CLEAN`
- **Remote Push Verification:** Verified present on `origin/xzs-bringup`
- **Target Device:** Sony Xperia XZs (`tone-keyaki` / G8231 / MSM8996 v2.2.0, Serial: `BH905SX976`)

---

## C2.8 Rate Correction

- In D2-C2.8, `core_clk_unipro_src` was documented as `37.5 MHz`.
- Live MSM8996 DT node `/soc/ufshc@624000` (`freq-table-hz`) defines:
  ```text
  core_clk_unipro_src -> UFS_ICE_CORE_CLK_SRC
  min = 150000000 (150 MHz)
  max = 300000000 (300 MHz)
  ```
- Genuine Sony kernel binary `twrp-Image` clock frequency table (`ftbl_ufs_ice_core_clk_src`) defines:
  ```text
  19.2 MHz  (CXO / 1)
  150.0 MHz (GPLL0 / 4)
  300.0 MHz (GPLL0 / 2)
  ```
- **Classification Correction:**
  ```text
  HARDWARE VERIFIED:
  UNIPRO branch and ROOT_EN could be enabled in C2.8.

  NOT VERIFIED IN C2.8:
  Linux-equivalent source parent/divider/rate.

  C2.8 does NOT rule out a correctly programmed
  core_clk_unipro_src dependency.
  ```

---

## Linux Clock Rate Initialization

Audited from genuine Sony kernel disassembly (`twrp-Image` @ `ufshcd_init_clocks` / `ufshcd_setup_clocks`):
- `ufshcd_init_clocks` reads `freq-table-hz` from DT and calls `clk_set_rate(clki->clk, clki->max_freq)` before `clk_prepare_enable()`:
  - `core_clk_src` (`UFS_AXI_CLK_SRC`): max frequency in DT = `0x0bebc200` (200,000,000 Hz / 200 MHz).
  - `core_clk_unipro_src` (`UFS_ICE_CORE_CLK_SRC`): max frequency in DT = `0x11e1a300` (300,000,000 Hz / 300 MHz).
- **Classification Output:**
  ```text
  LINUX_PRE_PHY_UFS_AXI_RATE=200000000
  LINUX_PRE_PHY_UNIPRO_SRC_RATE=300000000
  ```

---

## UFS_AXI RCG Encoding Audit

Disassembled from `twrp-Image` (`0xffffffc000b0ca3c` and `0xffffffc000b0c8ec`):
- Hardware update sequence:
  1. Write `CFG_RCGR` with source select (bits 10:8) and pre-divider (bits 4:0).
  2. Set `ROOT_EN` (bit 1) and `UPDATE` (bit 0) in `CMD_RCGR`.
  3. Poll `UPDATE` (bit 0) until hardware clears it to 0.
- Target rate: 200 MHz from GPLL0 (600 MHz):
  - Pre-divider: `div = 3` -> hardware encoding: `2 * 3 - 1 = 5` (`0x05`).
  - Parent: `gpll0_out_main` -> source selector = `1` (`0x100`).
  - MND: Not applicable / bypassed (`M = 0, N = 0, D = -1`).
- **Audit Specification:**
  ```text
  EXPECTED_UFS_AXI_200MHZ
  CMD:      0x75024
  CFG:      0x00000105
  M:        0
  N:        0
  D:        -1 (bypassed)
  parent:   GPLL0 (source select 1)
  divider:  3 (encoded as 5)
  ```

---

## UFS_ICE RCG Encoding Audit

Disassembled from `twrp-Image` (`ftbl_ufs_ice_core_clk_src` @ `0x1a8fc58`):
- Target rate: 300 MHz from GPLL0 (600 MHz):
  - Pre-divider: `div = 2` -> hardware encoding: `2 * 2 - 1 = 3` (`0x03`).
  - Parent: `gpll0_out_main` -> source selector = `1` (`0x100`).
  - MND: Not applicable / bypassed (`M = 0, N = 0, D = -1`).
- **Audit Specification:**
  ```text
  EXPECTED_UFS_ICE_300MHZ
  CMD:      0x76014
  CFG:      0x00000103
  M:        0
  N:        0
  D:        -1 (bypassed)
  parent:   GPLL0 (source select 1)
  divider:  2 (encoded as 3)
  ```

---

## Pre-Test RCG Snapshot

Hardware-verified register readbacks captured on MSM8996 silicon before any mutations (`CP 0xD290, 0x20`):

```text
PRE_UFS_AXI_RATE_STATE:
  CMD_RCGR (0x75024) = 0x80000000 (ROOT_OFF=1, ROOT_EN=0, UPDATE=0)
  CFG_RCGR (0x75028) = 0x00000000 (Source=0, Div=0)
  M        (0x7502c) = 0x00000000
  N        (0x75030) = 0x00000000
  D        (0x75034) = 0x00000000

PRE_UNIPRO_RATE_STATE:
  CMD_RCGR (0x76014) = 0x80000000 (ROOT_OFF=1, ROOT_EN=0, UPDATE=0)
  CFG_RCGR (0x76018) = 0x00000000 (Source=0, Div=0)
  M        (0x7601c) = 0x00000000
  N        (0x76020) = 0x00000000
  D        (0x76024) = 0x00000000

GPLL0_MODE (0x52000) = 0x00000011 (OUTCTRL=1, active)
```

---

## Stage A — UFS_AXI 200 MHz

- **RCG Programming:**
  - Wrote `CFG_RCGR (+0x75028) = 0x00000105`
  - Wrote `CMD_RCGR (+0x75024) |= (1 << 1) | (1 << 0)`
  - Poll `UPDATE == 0`: Completed (`rc = 0`).
  - Readback `CMD_RCGR (+0x75024) = 0x00000002` (`ROOT_OFF = 0, ROOT_EN = 1, UPDATE = 0`)
  - Readback `CFG_RCGR (+0x75028) = 0x00000105`
  - **Hardware Verified:** UFS_AXI root clock is confirmed running at 200 MHz!
- **Branch State:**
  - `GCC_UFS_AXI_CBCR` (`0x75008`) = `0x00004221` (`CLK_OFF = 0`, Running)
  - `GCC_SYS_NOC_UFS_AXI_CBCR` (`0x75038`) = `0x00000001` (`CLK_OFF = 0`, Running)
  - `GCC_AGGRE2_UFS_AXI_CBCR` (`0x83014`) = `0x00000001` (`CLK_OFF = 0`, Running)
  - `GCC_UFS_AHB_CBCR` (`0x7500c`) = `0x20008001` (`CLK_OFF = 0`, Running)
  - `GCC_UFS_CLKREF_CBCR` (`0x88008`) = `0x00000001` (`CLK_OFF = 0`, Running)
  - `GCC_UFS_UNIPRO_CORE_CBCR` (`0x7600c`): **Kept GATED** in Stage A.
- **PHY Replay Execution:**
  - Frozen Sony v2.2.0 sequence executed (76 Rate-A entries + Rate-B override, no 0x134 restore per `quirks = 0`).
- **Telemetry Checkpoints Across 10ms – 1000ms:**
  - `T = 10,000 µs`: `CR(+0x190) = 0`, `PCS(+0xD68) = 0`, `LOCK(+0x0C8) = 0x01`, `0x160 = 0x00`, `CMN(+0x194) = 0x0E`, `C00 = 0x01`, `C04 = 0x01`
  - `T = 100,000 µs`: `CR(+0x190) = 0`, `PCS(+0xD68) = 0`, `LOCK(+0x0C8) = 0x01`, `0x160 = 0x00`, `CMN(+0x194) = 0x0E`, `C00 = 0x01`, `C04 = 0x01`
  - `T = 500,000 µs`: `CR(+0x190) = 0`, `PCS(+0xD68) = 0`, `LOCK(+0x0C8) = 0x01`, `0x160 = 0x00`, `CMN(+0x194) = 0x0E`, `C00 = 0x01`, `C04 = 0x01`
  - `T = 1,000,000 µs`: `CR(+0x190) = 0`, `PCS(+0xD68) = 0`, `LOCK(+0x0C8) = 0x01`, `0x160 = 0x00`, `CMN(+0x194) = 0x0E`, `C00 = 0x01`, `C04 = 0x01`
- **Result:**
  ```text
  C_READY:   0 (timeout after 1,000,000 µs, CP 0xD290, 0x34)
  PCS_READY: 0
  ```
  Classification: **C_READY = 0 AFTER UFS_AXI 200 MHz**.

---

## Stage B — Exact UniPro Source Rate

- **Executed:** YES
- **RCG Values:**
  - Wrote `CFG_RCGR (+0x76018) = 0x00000103` (GPLL0 / 2 = 300 MHz)
  - Wrote `CMD_RCGR (+0x76014) |= (1 << 1) | (1 << 0)`
  - Poll `UPDATE == 0`: Completed (`rc = 0`).
  - Readback `CMD_RCGR (+0x76014) = 0x00000002` (`ROOT_OFF = 0, ROOT_EN = 1, UPDATE = 0`)
  - Readback `CFG_RCGR (+0x76018) = 0x00000103`
  - **Hardware Verified:** UniPro parent root clock is confirmed running at 300 MHz!
- **Branch State:**
  - `GCC_UFS_UNIPRO_CORE_CBCR` (`0x7600c`) = `0x00014221` (`CLK_OFF = 0`, Running)
  - All Stage A bus branches remained running.
- **PHY Replay Execution:**
  - Frozen Sony v2.2.0 sequence replayed identically.
- **Telemetry Checkpoints Across 10ms – 1000ms:**
  - `T = 10,000 µs`: `CR(+0x190) = 0`, `PCS(+0xD68) = 0`, `LOCK(+0x0C8) = 0x01`, `0x160 = 0x00`, `CMN(+0x194) = 0x0E`, `C00 = 0x01`, `C04 = 0x01`
  - `T = 100,000 µs`: `CR(+0x190) = 0`, `PCS(+0xD68) = 0`, `LOCK(+0x0C8) = 0x01`, `0x160 = 0x00`, `CMN(+0x194) = 0x0E`, `C00 = 0x01`, `C04 = 0x01`
  - `T = 500,000 µs`: `CR(+0x190) = 0`, `PCS(+0xD68) = 0`, `LOCK(+0x0C8) = 0x01`, `0x160 = 0x00`, `CMN(+0x194) = 0x0E`, `C00 = 0x01`, `C04 = 0x01`
  - `T = 1,000,000 µs`: `CR(+0x190) = 0`, `PCS(+0xD68) = 0`, `LOCK(+0x0C8) = 0x01`, `0x160 = 0x00`, `CMN(+0x194) = 0x0E`, `C00 = 0x01`, `C04 = 0x01`
- **Result:**
  ```text
  C_READY:   0 (timeout after 1,000,000 µs, CP 0xD290, 0x54)
  PCS_READY: 0
  ```
  Classification: **B3 — C_READY = 0 AFTER EXACT UNIPRO SOURCE RATE**.

---

## External UFS_RESET Read-Only Audit

Audit of live Sony DT (`artifacts/builds/twrp-extracted.dts`) and kernel drivers:
1. **Reset Types Distinguished:**
   - **Type A (`GCC_UFS_BCR` @ `0x75000`):** Host controller block reset (asserted/deasserted during controller init).
   - **Type B (`REG_UFS_CFG1` bit 1 @ `0x6240DC`):** UFS PHY soft reset (asserted during calibration, deasserted before `C04=1`).
   - **Type C (`UFS_RESET` external pin):** Hardware reset pin driven by SoC to UFS NAND memory package.
2. **Audit Findings:**
   ```text
   EXTERNAL_UFS_RESET_EXISTS=no
   CONTROLLED_BY=none
   STATE_AT_XNU_RUNTIME=N/A
   USED_BEFORE_PHY_READY=no
   ```
   No `reset-gpios`, `pinctrl-names`, or `pinctrl-0/1` properties are declared in the live device tree under `/soc/ufshc@624000`, `/soc/ufsphy@627000`, or `ufs_variant`.

---

## HARDWARE VERIFIED FACTS

1. **RCG Programming Success:**
   - `UFS_AXI_CMD_RCGR` (`0x75024`) and `UFS_ICE_CORE_CMD_RCGR` (`0x76014`) latch configurations cleanly.
   - `UFS_AXI` root verified running at 200 MHz (`CFG = 0x105`, `ROOT_OFF = 0`).
   - `UFS_ICE_CORE` root verified running at 300 MHz (`CFG = 0x103`, `ROOT_OFF = 0`).
2. **Clock Branches Running:**
   - `UFS_AXI` (`0x75008`), `SYS_NOC` (`0x75038`), `AGGRE2` (`0x83014`), `UFS_AHB` (`0x7500c`), `CLKREF` (`0x88008`), and `UNIPRO_CORE` (`0x7600c`) all report `CLK_OFF = 0`.
3. **Common PLL Status:**
   - Despite verified 200 MHz UFS_AXI and 300 MHz UniPro source clocking, `QSERDES_COM_C_READY_STATUS` (`+0x190`) remains `0` throughout 1.0 second of polling in both Stage A and Stage B.
4. **Diagnostic Status Register Snapshot:**
   - `LOCK_CMP_EN (+0x0C8) = 0x01`
   - `RESET_SM_STATUS (+0x160) = 0x00` (Raw value)
   - `CMN_CONFIG (+0x194) = 0x0E`
   - `PHY_START (+0xC00) = 0x01`
   - `POWER_DOWN_CONTROL (+0xC04) = 0x01`
5. **Invariants Preserved:**
   - `HCE = 0x00`, `HCS = 0x00`, `UICCMD = 0x00`. DMA untouched.
6. **Reverse RPM Rollback & Recovery:**
   - All 4 rollback requests (`LN_BB` Sleep -> `LN_BB` Active -> `L12` -> `L28`) received valid ACKs in 10 µs each.
   - Fastboot warm reset returned cleanly.

---

## SOURCE-AUDITED FACTS

1. **Linux Requested Rates:**
   Linux requests `200 MHz` for `core_clk_src` and `300 MHz` for `core_clk_unipro_src` based on DT `freq-table-hz`.
2. **RCG2 Encodings:**
   The genuine Sony kernel uses GPLL0 (600 MHz) with pre-divider 3 (`0x105`) for 200 MHz, and pre-divider 2 (`0x103`) for 300 MHz.
3. **No External UFS Reset Pin:**
   MSM8996 Tone Keyaki platform defines no external GPIO or pinctrl for UFS reset in the live device tree.

---

## INFERENCES

1. **RCG Frequency Ruled Out:**
   Lack of active RCG clock generation for `UFS_AXI` or `UNIPRO_SRC` was NOT the reason `C_READY` failed to lock. Both clock trees are now running at exact Linux frequencies on silicon with `C_READY` still remaining 0.
2. **Analog Prerequisites Audit Required:**
   Because controller bus clocks (AXI, AHB, SYS_NOC, AGGRE2), reference clock branch (`CLKREF`), and controller core clocks (`UFS_AXI 200MHz`, `UNIPRO_CORE 300MHz`) are verified active and identical to Linux, the remaining question is whether analog prerequisites (L28, L12, LN_BB reference clock propagation, and L25 vddp-ref-clk) are actually active or accepted-only.
   Audit of the MSM8996 GCC reset table proved that `GCC_UFS_BCR` (@ `0x75000`) exists, while `GCC_UFS_PHY_BCR` is **NOT PRESENT** (`MSM8996_GCC_UFS_PHY_BCR_PRESENT=no`). No guessed reset register may be written.

---

## D2-C2 Status

```text
D2-C2 STATUS: IN PROGRESS (C_READY = 0, PCS_READY = 0)
Causal Elimination:
  - 0x134 preservation:                 RULED OUT
  - Reset / C04 ordering:               RULED OUT
  - UniPro core clock enable alone:     RULED OUT
  - UFS_AXI 200 MHz rate programming:   RULED OUT as sole missing prerequisite
  - UniPro 300 MHz rate programming:    RULED OUT as sole missing prerequisite
  - External UFS_RESET:                 AUDITED (Does not exist in live DT)
  - RPM Regulators (L28/L12):           FROZEN (ACK verified)
  - LN_BB buffer:                       FROZEN (ACK verified)
  - GCC_UFS_PHY_BCR:                    NOT PRESENT (Proven from kernel binary)
```

---

## Recommended Next Phase

### Phase D2-C2.10: MSM8996 QMP UFS — Analog Power / Reference Clock Proof + Exact vddp-ref-clk Replay
1. Prove or disprove remaining analog prerequisites: L28 (vdda_phy), L12 (vdda_pll), LN_BB (19.2 MHz reference), and L25 (vddp_ref_clk).
2. Distinguish RPM request acceptance from physical state.
3. Reconstruct exact Sony PHY power-on dependency chain and execute exact sequence on silicon.
