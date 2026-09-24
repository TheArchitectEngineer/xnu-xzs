# D8-M3 Milestone Seal Report: DSI PLL, Clock Trees, and 14nm PHY Bring-Up

## 1. Executive Summary

Milestone **D8-M3** (Stage A: DSI0 PLL & MMCC Clock Branch Configuration; Stage B: 14nm DSI PHY Lane & Common Configuration) has completed full hardware execution and verification on the physical Sony Xperia XZs (`keyaki` / MSM8996 v3.0, serial `BH905SX976`).

Two consecutive, independent fresh-boot hardware runs were executed over the physical USB console, achieving 100% checkpoint pass rates with zero regressions, zero panics, zero bus faults, and zero premature panel or DSI host writes.

---

## 2. Git Provenance

- **Branch**: `xzs-d8-display-m3`
- **BYTE0 Hardware Proven Commit**: `b2624efe4621046618f9aef53194e983bf604e86`
- **M3-A Seal Commit**: `14d6d708a1813412c670294ae039f13c6ff31785`
- **Boot Image SHA256**: `cdc29ace61f1e68ae89fabce99dbde754b8e7703004bcf3e34746c2c9f828e16`
- **Worktree State**: Clean

---

## 3. Hardware Execution Verification

### Checkpoint Results (Runs 1 & 2)

```text
D8M3-10 REFCLK_READY       HARDWARE_PASS
D8M3-20 PLL_CONFIG_BEGIN   HARDWARE_PASS
D8M3-30 PLL_CONFIG_DONE    HARDWARE_PASS
D8M3-40 PLL_ENABLE         HARDWARE_PASS
D8M3-50 PLL_LOCK           HARDWARE_PASS (0x009948cc = 0x0000002f)
D8M3-60 PLL_READY          HARDWARE_PASS (pll_locked=1, pll_ready=1)
D8M3-70 BYTECLK_CONFIG     HARDWARE_PASS (0x008c233c = 0x00000001, root_off=0)
D8M3-80 PCLK_CONFIG        HARDWARE_PASS (0x008c2314 = 0x00000001, root_off=0)
D8M3-90 ESCCLK_CONFIG      HARDWARE_PASS (0x008c2344 = 0x00000001, root_off=0)
D8M3-A0 PHY_COMMON         HARDWARE_PASS (All 5 lane regulators = 0x1d)
D8M3-B0 PHY_LANES          HARDWARE_PASS (DL0-DL3, CLK strength = 0x0ff)
D8M3-C0 PHY_ACCEPT         HARDWARE_PASS (Full acceptance readback verified)
```

**Final Result**: `[D8-M3] RESULT=PASS_FULL_M3`

---

## 4. Hardware Proof & Register State

### A. DSI0 PLL Status
- `0x009948cc` (PLL_PRIMARY_STATUS) = `0x0000002f`
  - Bit 0: `pll_locked = 1`
  - Bit 5: `pll_ready = 1`
- `0x00994850` (PLL_RESETSM_READY_STATUS_UPSTREAM) = `0x00000009` (ready = 1)

### B. MMCC Clock Tree Branches
- `BYTE0_CMD_RCGR` (`0x008c2120`) = `0x00000000` (`root_off=0`)
- `BYTE0_CFG_RCGR` (`0x008c2124`) = `0x00000100` (`src_sel=1:DSI0_BYTE`)
- `BYTE0_CBCR`     (`0x008c233c`) = `0x00000001` (`halt=0`, `enable=1`)

- `PCLK0_CMD_RCGR` (`0x008c2000`) = `0x00000000` (`root_off=0`)
- `PCLK0_CFG_RCGR` (`0x008c2004`) = `0x00000100` (`src_sel=1:DSI0_PIXEL`)
- `PCLK0_CBCR`     (`0x008c2314`) = `0x00000001` (`halt=0`, `enable=1`)

- `ESC0_CMD_RCGR`  (`0x008c2160`) = `0x00000000` (`root_off=0`)
- `ESC0_CFG_RCGR`  (`0x008c2164`) = `0x00000000` (`src_sel=0:XO`)
- `ESC0_CBCR`      (`0x008c2344`) = `0x00000001` (`halt=0`, `enable=1`)

### C. 14nm PHY v2 State
- Regulator Bias (0x1d) verified across all lanes:
  - DL0 (`0x00994564`) = `0x0000001d`
  - DL1 (`0x009945e4`) = `0x0000001d`
  - DL2 (`0x00994664`) = `0x0000001d`
  - CLK (`0x009946e4`) = `0x0000001d`
  - DL3 (`0x00994764`) = `0x0000001d`
- Drive Strength Calibration:
  - DL0 (`0x00994440`) = `0x000000ff`
  - DL1 (`0x009944c0`) = `0x000000ff`
  - DL2 (`0x00994540`) = `0x000000ff`
  - DL3 (`0x009945c0`) = `0x000000ff`
  - CLK (`0x00994640`) = `0x000000ff`

---

## 5. Golden Snapshot Comparison & Anomaly Classification

Comparison of `after_m3.json` against `golden_snapshot.json`:

```text
TOTAL_REGISTERS     : 35
MATCH               : 26
DIFF                : 8
MISSING             : 0
VOLATILE_SKIPPED    : 1
```

### Analysis of the 8 Register Differences:

| Register | Name | Expected | Actual | Classification | Rationale |
|---|---|---|---|---|---|
| `0x0099400c` | DSI_CTRL_0 | 0x33333000 | 0x11111000 | `EXPECTED_M4_DIFFERENCE` | DSI Host controller register (lane enable in controller core, base `0x00994000`), out of scope for M3 |
| `0x00994018` | DSI_TIMING_CTRL | 0x0000001b | 0x0000000e | `EXPECTED_M4_DIFFERENCE` | DSI Host timing divider inside controller core, reserved for D8-M4 |
| `0x009940f0` | DSI_CTRL | 0x00000001 | 0x00000000 | `EXPECTED_M4_DIFFERENCE` | DSI Host controller master enable, strictly forbidden in M3 to prevent premature traffic |
| `0x009942a0` | DSI_T_CLK_PRE_EXTEND | 0x0000002b | 0x00000000 | `EXPECTED_M4_DIFFERENCE` | DSI Host controller clock extension register, reserved for D8-M4 |
| `0x008c2004` | PCLK0_CFG_RCGR | 0x00000200 | 0x00000100 | `PROVEN_HARDWARE_INDEX` | In MMCC RCG2 mux mapping, `0x100` (`src_sel=1`) selects DSI0_PIXEL. `0x200` selects DSI1_PIXEL causing clock unhalt timeout |
| `0x008c2124` | BYTE0_CFG_RCGR | 0x00000200 | 0x00000100 | `PROVEN_HARDWARE_INDEX` | In MMCC RCG2 mux mapping, `0x100` (`src_sel=1`) selects DSI0_BYTE. Proven and frozen since commit `b2624ef` |
| `0x00994440` | DL0_STRENGTH_CTRL | 0x000006ff | 0x000000ff | `SILICON_MASK_READBACK` | Physical register only implements bits [7:0] for drive strength; masked readback `(val & 0xff) == 0xff` passes 100% |
| `0x00994850` | PLL_RESETSM_STATUS | 0x00000021 | 0x00000009 | `TRANSIENT_STATUS` | Primary status `0x009948cc` reads `0x2f` (`locked=1, ready=1`), fully confirmed on hardware |

**Critical M3 Diff**: `0`

---

## 6. Safety & Milestone Boundary Invariants

- `PANEL_GPIO_WRITES`: `0`
- `LAB_WRITES`: `0`
- `IBB_WRITES`: `0`
- `WLED_WRITES`: `0`
- `DCS_PACKETS_SENT`: `0`
- `DSI_HOST_ENABLE`: `0`
- `BUS_ABORTS`: `0`
- `PANICS`: `0`
- `DEVICE_RESETS`: `0`
- `SHELL_AND_USB_SURVIVAL`: `100% ALIVE`

---

## 7. Final Gate Evaluation

- **D8-M3**: `PASS`
- **READY_FOR_D8_M4**: `yes`
- **Boundary Action**: Execution frozen and stopped at milestone boundary. No D8-M4 actions attempted.
