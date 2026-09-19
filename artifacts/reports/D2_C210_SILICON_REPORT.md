# D2-C2.10 Silicon Report

## Git Baseline

- **Baseline Pre-Task Commit:** `feed16a9254d89be4b5b027775437459c77eaf28`
- **Working Tree:** `CLEAN`
- **Remote Branch:** `origin/xzs-bringup`
- **Target Device:** Sony Xperia XZs (`tone-keyaki` / G8231 / MSM8996 v2.2.0, Serial: `BH905SX976`)
- **Execution Checkpoint:** `CP = 0xD2A0`

---

## GCC Reset Audit

An exhaustive audit of genuine Sony kernel binaries (`twrp-kernel.bin`) and MSM8996 GCC reset tables proved:
1. `0x75000` (`GCC_UFS_BCR`) is present in the kernel reset map at file offset `0xfc7e24` and `0x1a81da4`.
2. Zero symbols, descriptors, or references exist for `GCC_UFS_PHY_BCR` in the entire kernel or live device tree.
3. On MSM8996, UFS PHY reset is driven exclusively by the host controller register `REG_UFS_CFG1` bit 1 (`UFS_PHY_SOFT_RESET`).
```text
MSM8996_GCC_UFS_BCR_PRESENT=yes
MSM8996_GCC_UFS_PHY_BCR_PRESENT=no
```
No guessed reset registers were written.

---

## Exact Sony PHY Power Dependency Chain

Disassembly of `ufs_qcom_phy_power_on` (`0xffffffc00049ceb8` - `0xffffffc00049d020`) and `ufs_qcom_power_up_sequence` (`0xffffffc0007262a0`) in `twrp-kernel.bin` reconstructed the exact end-to-end dependency order:

```text
vdda_phy / L28 enable
        ↓
power_control(true) / C04 = 1
        ↓
vdda_pll / L12 enable
        ↓
ref_clk_src / LN_BB enable (clka/8)
        ↓
ref_clk / GCC_UFS_CLKREF enable (0x88008)
        ↓
vddp_ref_clk / L25 enable (if present, uv=1200000, ma=0)
        ↓
ufs_qcom_power_up_sequence:
  - REG_UFS_CFG1 bit 1 assert (soft reset = 1) -> 1 ms delay
  - 76-entry Rate-A table + Rate-B override (0x128 = 0x44)
  - REG_UFS_CFG1 bit 1 deassert (soft reset = 0) -> 1 ms delay
  - UFS_PHY_POWER_DOWN_CONTROL (+0xC04) write 0x01
  - UFS_PHY_PHY_START (+0xC00) write 0x01 (SerDes start)
  - Poll QSERDES_COM_C_READY_STATUS (+0x190) and UFS_PHY_PCS_READY_STATUS (+0xD68)
```

### SONY_PHY_POWER_DEPENDENCY_TABLE

| Dependency | DT Phandle | Resource | Voltage | Load | Linux Request | Current XNU Equivalent | Verification Level |
|---|---|---|---|---|---|---|---|
| `vdda_phy` | `<0x2a>` (`pm8994_l28`) | `ldoa/28` | 925,000 µV | 18 mA | `swen=1, uv=925000, ma=18` | `xzs_rpm_vote_ldo(28, 925000, 18, true)` | `RPM_ACK_ONLY` |
| `power_control(1)` | N/A | MMIO `+0xC04` | N/A | N/A | `writel(0x01, +0xC04)` | `xzs_ufs_phy_write32(0xC04, 0x01)` | `SPMI_STATE` / MMIO |
| `vdda_pll` | `<0x43>` (`pm8994_l12`) | `ldoa/12` | 1,800,000 µV | 9 mA | `swen=1, uv=1800000, ma=9` | `xzs_rpm_vote_ldo(12, 1800000, 9, true)` | `RPM_ACK_ONLY` |
| `ref_clk_src` | `<0x44 0x3ab0b36d>` | `clka/8` (`LN_BB`) | N/A | N/A | `swen=1` (ACTIVE + SLEEP) | `xzs_rpm_vote_clk_buffer(8, set, true)` | `RPM_ACK_ONLY` |
| `ref_clk` | `<0x44 0x92aa126f>` | `gcc_ufs_clkref_clk` | N/A | N/A | `clk_enable()` (0x88008) | `xzs_gcc_enable_and_wait_branch(0x88008)` | `CLOCK_BRANCH_STATE` |
| `vddp_ref_clk` | `<0x121>` (`pm8994_l25`)| `ldoa/25` | 1,200,000 µV | 100 µA | `swen=1, uv=1200000, ma=0` | `xzs_rpm_vote_ldo(25, 1200000, 0, true)` | `RPM_ACK_ONLY` + `SPMI_STATE` |

---

## L28 Evidence

- **Canonical Specification:** `resource = ldoa/28`, `voltage = 925000 uV`, `load = 18 mA`.
- **Pre-Vote SPMI Observation (Peripheral 0x5B00):**
  - `ENABLE (+0x46) = 0x80`
  - `STATUS (+0x08) = 0x80`
- **RPM Vote Transmission & Acknowledgement:**
  - `msg_id = 0x01`, `set = ACTIVE (0)`
  - KVPs: `swen = 1`, `uv = 925000 (0x0e1d48)`, `ma = 18 (0x12)`
  - ACK received: elapsed = 30 µs (`id_ack = 1`)
- **Rollback:**
  - Released in reverse order: `swen = 0, uv = 925000, ma = 18` (`msg_id = 9`, ACK in 10 µs).
- **Physical Classification:**
  ```text
  L28_RPM_ACK=yes
  L28_SPMI_OBSERVABLE=no
  L28_PHYSICAL_STATE=UNPROVEN_BY_SOFTWARE
  ```

---

## L12 Evidence

- **Canonical Specification:** `resource = ldoa/12`, `voltage = 1800000 uV`, `load = 9 mA`.
- **Pre-Vote SPMI Observation (Peripheral 0x4B00):**
  - `ENABLE (+0x46) = 0x80`
  - `STATUS (+0x08) = 0x80`
- **RPM Vote Transmission & Acknowledgement:**
  - `msg_id = 0x02`, `set = ACTIVE (0)`
  - KVPs: `swen = 1`, `uv = 1800000 (0x1b7740)`, `ma = 9 (0x09)`
  - ACK received: elapsed = 40 µs (`id_ack = 2`)
- **Rollback:**
  - Released in reverse order: `swen = 0, uv = 1800000, ma = 9` (`msg_id = 8`, ACK in 10 µs).
- **Physical Classification:**
  ```text
  L12_RPM_ACK=yes
  L12_SPMI_OBSERVABLE=no
  L12_PHYSICAL_1V8_PROVEN=no
  ```

---

## PMIC ADC / Rail Measurement Audit

An audit of the live Sony device tree (`twrp-extracted.dts`) and PM8994 drivers:
1. `vadc@3100` defines 24 ADC channels:
   `die_temp` (0x8), `ref_625mv` (0x9), `ref_1250v` (0xa), `vcoin` (0x5), `vph_pwr` (0x7), `msm_therm`, `emmc_therm`, `pa_therm0/1`, `quiet_therm`, `xo_therm_buf`, `flash_therm`, `chg_temp`, `usb_id_lv`, `usbin`, `dcin`, `usb_dp`, `usb_dm`.
2. Zero channels route to L12, L28, or L25 outputs.
3. `vadc@3400` is `qpnp-adc-tm` (Thermal Monitor).
```text
L12_ADC_MEASURABLE=no
L28_ADC_MEASURABLE=no
L25_ADC_MEASURABLE=no

L12_PHYSICAL_1V8_PROVEN=no (not measurable by current software path)
L28_PHYSICAL_0V925_PROVEN=no (not measurable by current software path)
L25_PHYSICAL_1V2_PROVEN=no (not measurable by current software path)
```
*Note:* "Unproven" denotes lack of non-invasive measurement paths in current software, NOT physical absence of voltage.

---

## L25 / vddp-ref-clk Audit

Audit of live Sony DT (`/soc/ufsphy@627000`) and kernel sources:
- `vddp-ref-clk-supply = <0x121>` (`pm8994_l25`)
- `vddp-ref-clk-max-microamp = <0x64>` (100 µA)
- `vddp-ref-clk-always-on`
- Voltage: `regulator-min-microvolt = <1200000>`, `regulator-max-microvolt = <1200000>` (1.2V)
- Integer conversion in Qualcomm `rpm-smd-regulator.c`:
  `load_mA = load_uA / 1000 = 100 / 1000 = 0 mA`
- Request Parameters:
  - Resource: `ldoa/25`
  - Voltage: 1,200,000 µV (`0x124f80`)
  - Load: `0 mA`
  - Set: `ACTIVE (0)`
- **Always-On & Preexisting State:**
  - SPMI pre-read (`0x5846`) verified: `ENABLE = 0x80` (`PMIC_ENABLE_BIT` set).
  - `L25_PREEXISTING_ACTIVE = yes`.
  - Disassembly of `ufs_qcom_phy_disable_vreg` confirmed that when `is_always_on == true` (checked via `+0x1d`), `regulator_disable` is explicitly **skipped**.
  - **Rollback Safety Policy Applied:** L25 was preserved enabled at rollback (`RESTORED_PREEXISTING_STATE`). Unconditional global `SWEN=0` was **NEVER** transmitted.

---

## Stage A — Exact L25 Replay

- **Executed on Silicon:** YES (`CP = 0xD2A0, 0x40` -> `0x50` -> `0x60`)
- **All Frozen Prerequisites Active:**
  - `UFS_AXI_CLK_SRC` = 200 MHz (`0x75024` latched `CFG = 0x105`, `ROOT_OFF = 0`)
  - `UNIPRO_SRC` = 300 MHz (`0x76014` latched `CFG = 0x103`, `ROOT_OFF = 0`)
  - Branches Running: `UFS_AXI`, `SYS_NOC`, `AGGRE2`, `UFS_AHB`, `CLKREF`, `UNIPRO_CORE`
  - Regulators Voted & ACKed: `L28` (0.925V, 18mA), `L12` (1.800V, 9mA), `LN_BB` (ACTIVE + SLEEP)
  - Interleaved PHY Power: `+0xC04 = 0x01` written between L28 and L12 per Sony call graph
- **L25 Vote Transmitted:**
  - `msg_id = 0x05`, `ldoa/25`, `swen = 1`, `uv = 1200000`, `ma = 0`
  - RPM ACK received in 40 µs (`id_ack = 5`).
- **Exact Sony Calibration Replay:**
  - Soft reset asserted via `REG_UFS_CFG1` bit 1 (1 ms)
  - 76-entry Rate-A table programmed + Rate-B override (`0x128 = 0x44`)
  - Soft reset deasserted (1 ms)
  - `+0xC04 = 0x01`
  - `+0xC00 = 0x01` (SerDes start)
- **Telemetry Checkpoints Across 1.0s (10,000 * 100 µs):**
  - `T = 10,000 µs`: `CR(+0x190) = 0`, `PCS(+0xD68) = 0`, `LOCK(+0x0C8) = 0x01`, `0x160 = 0x00`, `CMN(+0x194) = 0x0E`, `C00 = 0x01`, `C04 = 0x01`
  - `T = 100,000 µs`: `CR(+0x190) = 0`, `PCS(+0xD68) = 0`, `LOCK(+0x0C8) = 0x01`, `0x160 = 0x00`, `CMN(+0x194) = 0x0E`, `C00 = 0x01`, `C04 = 0x01`
  - `T = 500,000 µs`: `CR(+0x190) = 0`, `PCS(+0xD68) = 0`, `LOCK(+0x0C8) = 0x01`, `0x160 = 0x00`, `CMN(+0x194) = 0x0E`, `C00 = 0x01`, `C04 = 0x01`
  - `T = 1,000,000 µs`: `CR(+0x190) = 0`, `PCS(+0xD68) = 0`, `LOCK(+0x0C8) = 0x01`, `0x160 = 0x00`, `CMN(+0x194) = 0x0E`, `C00 = 0x01`, `C04 = 0x01`
- **Result:**
  ```text
  C_READY:   0 (timeout after 1,000,000 µs, CP 0xD2A0, 0x62)
  PCS_READY: 0
  ```
- **Stage A Classification:**
  ```text
  Exact vddp-ref-clk/L25 state was insufficient
  to recover QSERDES common PLL readiness.
  ```

---

## Reference Clock Physical Proof

- **Digital Gate Status:**
  `GCC_UFS_CLKREF_CBCR` (`0x88008`) readback = `0x00000001` (`CLK_OFF = 0`, Running).
- **Physical Observability:**
  `REFCLK_DIGITAL_GATE_PROVEN=yes`
  `REFCLK_19P2MHZ_PHYSICAL_PROVEN=no`
  `MEASUREMENT_METHOD=UNPROVEN_BY_SOFTWARE`
- **GCC Debug Mux Audit:**
  - `gcc_ufs_clkref_clk` is present in the Linux `debugcc-msm8996` clock debug mux table (`qcom,gcc-8996`).
  - Full hardware frequency counter measurement requires CPU APCS counter timer hardware and complex mux routing.
  - In accordance with safety rules against unproven register mutations:
    `DEBUG_MUX_SUPPORTED=yes`
    `REFCLK_DEBUG_MEASUREMENT=NOT_EXECUTED`

---

## HCE Ordering Audit

- Audit of Linux driver `ufshcd.c` verified:
  ```text
  HCE notify PRE_CHANGE
          ↓
  QCOM PHY power-up / calibration / SerDes start
          ↓
  Poll PCS_READY == 1
          ↓
  Lane clocks enable
          ↓
  HCE controller start (HCE = 1)
  ```
- **Finding:** Initial SerDes start and common PLL lock (`C_READY`) occur **BEFORE** `HCE` is asserted.
```text
HCE_REQUIRED_FOR_INITIAL_PHY_READY=no
```
- **Invariants Preserved:**
  `HCE = 0x00`, `HCS = 0x00`, `UICCMD = 0x00`. DMA untouched.

---

## HARDWARE VERIFIED FACTS

1. **Pre-Vote SPMI Readback (Silicon Truth):**
   - L25 (`0x5800`): `TYPE=0x06, SUBTYPE=0x03, STATUS=0x80, ENABLE=0x80, MODE=0x07`.
   - L25 was confirmed **PREEXISTING ACTIVE** in hardware at XNU boot time.
   - L12 (`0x4B00`): `ENABLE=0x80, STATUS=0x80`.
   - L28 (`0x5B00`): `ENABLE=0x80, STATUS=0x80`.
2. **Exact Sony Dependency Sequencing:**
   - Sequential order executed on silicon: L28 -> `+0xC04=1` -> L12 -> LN_BB (ACTIVE+SLEEP) -> CLKREF -> L25 (`ma=0`).
   - Calibration replay executed under soft reset assert, followed by deassert, `+0xC04=1`, and `+0xC00=1`.
3. **RPM Transaction Latencies:**
   - L28 vote: ACK in 30 µs (`msg_id = 1`)
   - L12 vote: ACK in 40 µs (`msg_id = 2`)
   - LN_BB active vote: ACK in 10 µs (`msg_id = 3`)
   - LN_BB sleep vote: ACK in 10 µs (`msg_id = 4`)
   - L25 vote: ACK in 40 µs (`msg_id = 5`)
4. **PLL Readiness:**
   - Even with exact L25 (1.200V, `ma=0`), L28 (0.925V, 18mA), L12 (1.800V, 9mA), LN_BB, CLKREF, AXI 200MHz, and UNIPRO 300MHz all active, `C_READY (+0x190)` remains `0` throughout 1.0 second of polling.
5. **State Restoration & Safe Rollback:**
   - L25 was preserved enabled (`RESTORED_PREEXISTING_STATE`); no `SWEN=0` was transmitted.
   - LN_BB, L12, and L28 released in reverse order (`msg_id = 6..9`, ACKs in 10 µs each).
   - Fastboot warm reset returned cleanly (`CP = 0xD2A0, 0x01`).

---

## SOURCE-AUDITED FACTS

1. `GCC_UFS_PHY_BCR` is completely absent from the MSM8996 GCC driver and device tree.
2. Qualcomm UFS PHY driver explicitly skips `regulator_disable` when `is_always_on == true`.
3. Qualcomm RPM-SMD regulator driver computes `load_mA = load_uA / 1000` via integer division; 100 µA maps to `0 mA`.
4. PM8994 VADC provides 24 channels, none of which monitor L12, L28, or L25.
5. `gcc_ufs_clkref_clk` is present in the MSM8996 debug mux table, but requires APCS hardware counters to measure safely.

---

## UNPROVEN PHYSICAL STATES

1. `L28_PHYSICAL_STATE = UNPROVEN_BY_SOFTWARE` (RPM accepted vote, SPMI shows bit enabled, but physical analog output voltage not measurable via software telemetry).
2. `L12_PHYSICAL_1V8_PROVEN = no` (RPM accepted vote, SPMI shows bit enabled, but physical analog output voltage not measurable via software telemetry).
3. `L25_PHYSICAL_1V2_PROVEN = no` (RPM accepted vote, SPMI shows bit enabled, but physical analog output voltage not measurable via software telemetry).
4. `REFCLK_19P2MHZ_PHYSICAL_PROVEN = no` (CBCR gate confirmed running, but analog 19.2 MHz waveform arrival at QSERDES macro input unproven without oscilloscope/probe).

---

## INFERENCES

1. **Digital Configuration Fully Exhausted:**
   Rates (200 MHz AXI, 300 MHz UniPro), branches (AXI, AHB, SYS_NOC, AGGRE2, CLKREF, UNIPRO_CORE), and digital resets (GCC_UFS_BCR, CFG1 soft reset) are 100% source-identical to Linux.
2. **Analog Configuration Fully Replayed:**
   L28, L12, LN_BB, and L25 are all voted with exact parameters, verified ACKed, and sequenced in the exact order audited from genuine Sony binaries.
3. **Root Cause of C_READY = 0:**
   Because all source-documented software prerequisites (power rails, clock trees, RCG dividers, PHY calibration tables, and sequencing) have now been established on silicon without achieving common PLL lock, the remaining divergence cannot be resolved by guessing additional software knobs.
   The state machine either requires:
   - **Pre-existing firmware initialization state:** Bootloader / TrustZone / ABOOT state establishing analog bias trims or security gating prior to kernel handoff.
   - **Known-Good Runtime Oracle Acquisition:** Differential sampling from an environment where UFS actually links, or auditing the stock Sony bootloader (XBL/SBL1/PBL) UFS bringup path.

---

## D2-C2 Status

```text
D2-C2 STATUS: CASE B / ANALOG_STATE_UNRESOLVED
Causal Elimination:
  - 0x134 preservation:                 RULED OUT
  - Reset / C04 ordering:               RULED OUT
  - UniPro core clock enable alone:     RULED OUT
  - UFS_AXI 200 MHz rate programming:   RULED OUT as sole missing prerequisite
  - UniPro 300 MHz rate programming:    RULED OUT as sole missing prerequisite
  - GCC_UFS_PHY_BCR:                    PROVEN ABSENT (Not a valid knob)
  - Exact L25 / vddp-ref-clk replay:    RULED OUT as sole missing prerequisite
  - RPM Regulators (L28, L12, L25):     Voted & ACKed
  - LN_BB reference clock buffer:       Voted & ACKed (Active + Sleep)
  - GCC_UFS_CLKREF branch:              Verified running
```

---

## Recommended Next Phase

### Phase D2-C2.11: Known-Good Bootloader / Stock Sony Firmware Oracle Audit
1. Stop guessing clocks, resets, bias delays, or regulator settings.
2. Audit the stock Sony bootloader / ABOOT / XBL binary to determine how Sony firmware initializes UFS hardware before handing off to the kernel.
3. Determine whether TrustZone (QSEE) or an early bootloader stage configures an internal analog power gate, clock pad mux, or security firewall for the QMP UFS PHY.
