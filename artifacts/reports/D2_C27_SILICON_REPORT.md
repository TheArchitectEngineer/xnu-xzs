# D2-C2.7 Silicon Report

## Git Baseline

- **Baseline Commit (Pre-task):** `3899d5ff6dbb114a5b602de44b2abb315c1646bb`
- **Documentation Correction Commit:** `5a17cd9424c8fb233b827376c9dd81e59c04fe71`
- **Status:** All C2.6 documentation corrections committed and pushed to `origin/xzs-bringup` prior to hardware execution. Worktree clean.

---

## C2.6 Documentation Corrections

1. **L28 Canonical Constants:**
   - Previous erroneous claims of `L28 = 1.200V / 300mA` were eliminated across all documentation and reports.
   - Canonical values verified:
     ```text
     L28: resource = ldoa/28, voltage = 925000 uV (0.925V), load = 18 mA
     L12: resource = ldoa/12, voltage = 1800000 uV (1.800V), load = 9 mA
     ```
2. **Register Map Clarification:**
   - Explicitly decoupled `QSERDES_COM_LOCK_CMP_EN` (`0x0C8`, CONFIG) from `QSERDES_COM_C_READY_STATUS` (`0x190`, STATUS).

---

## Status Register Proof

Verified on silicon via assertions and runtime logging prior to sequence execution:
- `C_READY`: `0x190` (`QSERDES_COM_C_READY_STATUS`, bit 0)
- `LOCK_CMP_EN`: `0x0C8` (`QSERDES_COM_LOCK_CMP_EN`, bit 0)
- `PCS_READY`: `0xD68` (`UFS_PHY_PCS_READY_STATUS`, bit 0)

---

## Sony 0x134 Vendor-Delta Proof

Audited Sony downstream kernel binary `artifacts/scratch/twrp-Image`:
- Function: `ufs_qcom_phy_qmp_14nm_init` (`0xffffffc00049dd20`) & `phy_calibrate` (`0xffffffc00049dfd4`).
- **Exact instruction reading PHY + 0x134:** `0xffffffc00049dd4c: ldr w3, [x20]`
- **Where saved value is stored:** `0xffffffc00049dd6c: str w3, [x19, #0x100]` (`phy->vco_tune1_mode1`)
- **Exact branch controlling read:** `0xffffffc00049dd24: tbz w0, #2, #0xffffffc00049dd78` (tests bit 2 of `quirks` at `[x19, #0xc0]`)
- **Exact branch controlling restore:** `0xffffffc00049dfd8: tbz w0, #2, #0xffffffc00049e014`
- **Whether branch depends on controller revision / quirk flags:**
  - `quirks` are computed in `ufs_qcom_phy_qmp_14nm_advertise_quirks` based on controller version:
    `(major == 2 && minor == 0 && step == 0) => quirks = 7` (bit 2 set).
  - For v2.2.0 (`QCOM_HW_VER = 0x20020000`, major=2, minor=2, step=0): `minor == 2 != 0`, condition is false, leaving `quirks = 0`.
- **Exact instruction writing saved value back to PHY + 0x134:** `0xffffffc00049e004: str w1, [x21]`
- **Evaluation:**
  ```text
  SONY_2_2_0_SAVE_0x134 = no
  SONY_2_2_0_RESTORE_0x134 = no
  CONDITION = (major == 2 && minor == 0 && step == 0) => quirks = 7
  ```
- **Conclusion:** The Sony kernel source lineage restricts VCO tune preservation strictly to v2.0.0 hardware. On v2.2.0, `quirks = 0` and the calibration table value (`0xD6`) is written directly without restoration. However, as required by the C2.7 experimental isolation protocol, both Stage A (retaining `0x134`) and Stage B (exact reset order + retaining `0x134`) were tested on silicon.

---

## Stage A — 0x134 Isolation

- **Purpose:** Test whether retaining the bootloader VCO trim (`0x0A`) alone under the old C2.6 C04 ordering achieves common PLL lock (`C_READY`).
- **Telemetry:**
  - `saved`: `0x0A` (`PRE_CAL_0x134`)
  - `table value`: `0xD6` (`TABLE_0x134`)
  - `restored`: `0x0A` (`RESTORED_0x134`)
  - `C_READY timeline`:
    - 10 ms (2,710 µs): `0`
    - 100 ms (18,6a0 µs): `0`
    - 500 ms (7a,120 µs): `0`
    - 1000 ms (f4,240 µs): `0`
  - `PCS_READY timeline`:
    - 10 ms: `0`
    - 100 ms: `0`
    - 500 ms: `0`
    - 1000 ms: `0`
  - `LOCK_CMP_EN`: `0x01` (CONFIG telemetry)
- **Result:** **A2 — C_READY REMAINED 0** (0x134 preservation alone is insufficient). Proceeded to Stage B.

---

## Stage B — Exact Sony Ordering

- **Executed Sequence:**
  1. Base state zeroed: `PHY + 0xC04 = 0`, `PHY + 0xC00 = 0`.
  2. Soft reset asserted via `REG_UFS_CFG1 |= 0x2`. Settle 1000 µs.
  3. Calibrated PHY while soft reset remained asserted: wrote 76 Rate-A entries + Rate-B override.
  4. Restored `0x134 = 0x0A`.
  5. Verified: `CFG1 bit 1 = 1`, `PHY + 0x134 = 0x0A`.
  6. Soft reset deasserted via `REG_UFS_CFG1 &= ~0x2`. Settle 1000 µs.
  7. Power-down released: `PHY + 0xC04 = 1`. Settle & barrier.
  8. SerDes started: `PHY + 0xC00 = 1`. Settle & barrier.
  9. Polled `0x190` and `0xD68` across 10 ms, 100 ms, 500 ms, 1000 ms checkpoints.
- **Telemetry:**
  - `soft-reset timeline`: asserted for >1000 µs, maintained throughout 76-entry calibration + restore, then deasserted with 1000 µs settling.
  - `C04 timeline`: held at `0` until after soft reset deasserted, then set to `1`.
  - `0x134`: `0x0A` verified after calibration while reset asserted.
  - `C_READY timeline (+0x190)`:
    - 10 ms (2,710 µs): `0`
    - 100 ms (18,6a0 µs): `0`
    - 500 ms (7a,120 µs): `0`
    - 1000 ms (f4,240 µs): `0`
  - `PCS_READY timeline (+0xD68)`:
    - 10 ms: `0`
    - 100 ms: `0`
    - 500 ms: `0`
    - 1000 ms: `0`
  - `Diagnostic Status Snapshot`:
    - `PHY + 0x0C8 (LOCK_CMP_EN) = 0x01`
    - `PHY + 0x160 (RESET_SM_STATUS) = 0x00`
    - `PHY + 0x190 (C_READY_STATUS) = 0x00`
    - `PHY + 0x194 (CMN_CONFIG) = 0x0E`
    - `PHY + 0xC00 (PHY_START) = 0x01`
    - `PHY + 0xC04 (POWER_DOWN_CONTROL) = 0x01`
    - `PHY + 0xD68 (PCS_READY_STATUS) = 0x00`
- **Result:** **B3 — C_READY = 0 AFTER EXACT SONY SEQUENCE**.

---

## HARDWARE VERIFIED FACTS

1. **Register Identity:**
   - Polling `0x190` reads `QSERDES_COM_C_READY_STATUS`.
   - Polling `0xD68` reads `UFS_PHY_PCS_READY_STATUS`.
   - Offset `0x0C8` is `QSERDES_COM_LOCK_CMP_EN` (config, readback `0x01`).
2. **VCO Trim 0x134 Independence:**
   - Retaining bootloader trim `0x0A` vs writing calibration table value `0xD6` produces identical results (`C_READY = 0`).
3. **Reset & Power Ordering Independence:**
   - Asserting soft reset during calibration and releasing it before setting `C04 = 1` and `C00 = 1` produces identical results (`C_READY = 0`).
4. **PLL Reset State Machine Idle:**
   - Status register `PHY + 0x160` reads `0x00`. In the QSERDES architecture, this indicates the PLL reset state machine has not advanced past initial reset/idle state.
5. **Supply & Clock Stability:**
   - L28 (925 mV / 18 mA) and L12 (1800 mV / 9 mA) and LN_BB (clka/8) are voted and acknowledged by RPM SMD V0.
   - GCC reference clock `GCC_UFS_CLKREF` is active.
   - Clean reverse rollback and watchdog reset to Fastboot in ~7s is 100% reliable.
6. **Invariants Preserved:**
   - `HCE = 0`, `HCS = 0`, `UICCMD = 0`, DMA untouched across all executions.

---

## SOURCE-AUDITED FACTS

1. **Sony v2.2.0 Quirk Flag:**
   - Disassembly of `twrp-Image` (`0xffffffc00049dd20`): `quirks` are zero for controller v2.2.0 (`major=2, minor=2, step=0`).
   - Saving/restoring `0x134` was only activated on v2.0.0 hardware.
2. **QSERDES Common Reset State Machine:**
   - In Qualcomm 14nm QMP documentation and source lineage (`phy-qcom-qmp.h` / `phy-qcom-ufs-qmp-14nm.h`):
     - `0x160` corresponds to `QSERDES_COM_RESET_SM_STATUS`.
     - When the PLL sequencer is triggered by `POWER_DOWN_CONTROL = 1` and `PHY_START = 1`, the internal state machine cycles through calibration, bias lock, and frequency lock before asserting `C_READY` at `0x190`.
     - Reading `0x00` from `0x160` confirms the state machine never started.

---

## INFERENCES

1. **Why `RESET_SM_STATUS (0x160)` is 0x00:**
   - The PLL reset state machine requires an active input reference clock or an active digital branch clock to clock the state machine logic.
   - In D2-C2.6 GCC register audit:
     - `GCC_UFS_TX_CFG` (`0x75010`) = `0x80000000` (`CLK_OFF = 1`).
     - `GCC_UFS_RX_CFG` (`0x75014`) = `0x80000000` (`CLK_OFF = 1`).
     - `GCC_UFS_AHB_CBCR` and `AXI_CBCR` are active, but the PHY-adjacent configuration clocks in GCC may be gated or held in reset.
   - Alternatively, the input reference clock `ref` from `LN_BB` (19.2 MHz) or `qref` from `GCC_UFS_CLKREF` (`0x88008`) requires an analog bias enable or additional clock routing in GCC.

---

## D2-C2 Status

- **Classification:** **B3** (`C_READY = 0` after exact 0x134 restore and exact Sony reset/C04 ordering).
- Milestone tag `xzs-d2-c2-phy-ready` is **NOT** created.
- Scope bounds strictly observed: no random registers guessed, no HCE/UICCMD issued.

---

## Recommended Next Phase: PHASE D2-C2.8

### Focus: QSERDES COM Reset State Machine Activation & Reference Clock Gating Audit

1. **Investigate `RESET_SM_STATUS (0x160) = 0x00`:**
   - Determine what specific hardware signal triggers the reset state machine in QMP 14nm.
   - Audit Linux `gcc-msm8996.c` and `phy-qcom-qmp-ufs.c` for any missing GCC branch clock enables:
     - `GCC_UFS_TX_CFG_CLK` (`0x75010`)
     - `GCC_UFS_RX_CFG_CLK` (`0x75014`)
     - `GCC_UFS_TX_SYMBOL_0_CLK_CBCR`
     - `GCC_UFS_RX_SYMBOL_0_CLK_CBCR`
2. **Audit Reference Clock Routing:**
   - Confirm whether `LN_BB` requires a specific pin routing or additional RPM resource (e.g. `rf_clk` or `bb_clk` frequency vote).
   - Check `QSERDES_COM_SYSCLK_EN_SEL` (`0x0AC`) and `QSERDES_COM_SYS_CLK_CTRL` (`0x03C`) in the calibration table to verify reference clock selection.
