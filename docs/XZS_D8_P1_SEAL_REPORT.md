# D8-P1 Milestone Seal Report: TLMM GPIO Prerequisite

## 1. Executive Summary

Milestone **D8-P1** (TLMM GPIO Prerequisite for Keyaki Panel Bring-Up: GPIO 8 `disp_reset_n`, GPIO 10 `mdp_vsync` / TE, GPIO 51 `lcd_vddio_en`) has completed full hardware execution and verification on the physical Sony Xperia XZs (`keyaki` / MSM8996 v3.0, serial `BH905SX976`).

Two consecutive, independent cold/fresh-boot hardware runs were executed over the physical USB console, achieving 100% pass rates across all validation checkpoints with zero regressions, zero panics, zero bus faults, and zero premature panel activations or power applications.

All target GPIOs were correctly mapped to MSM8996 TLMM MMIO registers, configured to their audited electrical and functional states, and held in their strictly safe, non-panel-activating states:
- **GPIO 8 (`disp_reset_n`)**: Configured as output (2mA, no pull), held asserted LOW (`0x00000000`).
- **GPIO 10 (`mdp_vsync` / TE)**: Configured as input (2mA, pull-down, function 1 `mdp_vsync`), reading `0x00000000`.
- **GPIO 51 (`lcd_vddio_en`)**: Configured as output (2mA, no pull), held disabled LOW (`0x00000000`).

---

## 2. Git & Binary Provenance

- **Branch**: `xzs-d8-display-p1`
- **Source Audit**: Verified against `artifacts/display-audit/keyaki.dts` (lines 2016–2030, 13676–13745, 17218–17233)
- **Kernel Binary**: `artifacts/builds/kernel.development.vmapple`
- **Kernel SHA256**: `6e547ba1fe81367acd26d1ed3668ed724ce40b30c36e3318b6348c3bdee5b1bf`
- **Boot Image**: `artifacts/builds/xzs-xnu-boot.img`
- **Boot Image SHA256**: `5e9cfffc7df467b687d1ea9b7a711d046f0879833f56cf7308d295c5be0760b8`
- **Rootfs Image**: `artifacts/builds/xzs-rootfs.img`
- **Rootfs SHA256**: `d3ba20dcdebfc87ed1b5aa7e413dc3ae4d639be5e7085c45d08cdd67f839bf95`
- **PAC Violations**: 0 (`pac_check PASS`)

---

## 3. Hardware Execution Verification

### Checkpoint Results (Runs 1 & 2)

```text
D8P1-10 PREREQUISITES:
  M2 Core Clocks & GDSC:          PASS
  M3 Lower Layer (PLL + Clocks):  PASS ([D8-M3] RESULT=PASS_FULL_M3)
  M4 DSI0 Host Controller:        PASS (DSI_CTRL=0x000001f5, LANE_STATUS=0x00001f1f)

D8P1-20 TLMM_MAP_VERIFY:
  TLMM Base Address:              0x01010000 (Verified mapped in TTBR0)
  GPIO8 CFG / IN_OUT:             0x01018000 / 0x01018004
  GPIO10 CFG / IN_OUT:            0x0101a000 / 0x0101a004
  GPIO51 CFG / IN_OUT:            0x01043000 / 0x01043004

D8P1-30 GPIO8_CONFIG_STAGE:
  Pre-OE Latch:                   GPIO_IN_OUT(8)  written 0x0 (LOW)
  CFG Write:                      GPIO_CFG(8)     written 0x00000200 (OE=1, DRV=0, PULL=0, FUNC=0)
  Readback:                       CFG=0x00000200, IN_OUT=0x00000000

D8P1-40 GPIO10_CONFIG_STAGE:
  CFG Write:                      GPIO_CFG(10)    written 0x00000005 (OE=0, DRV=0, PULL=1, FUNC=1)
  Readback:                       CFG=0x00000005, IN_OUT=0x00000000

D8P1-50 GPIO51_CONFIG_STAGE:
  Pre-OE Latch:                   GPIO_IN_OUT(51) written 0x0 (LOW)
  CFG Write:                      GPIO_CFG(51)    written 0x00000200 (OE=1, DRV=0, PULL=0, FUNC=0)
  Readback:                       CFG=0x00000200, IN_OUT=0x00000000

D8P1-60 SAFE_STATE_VERIFY:
  Panel Reset (GPIO8):            ASSERTED LOW (Safe)
  TE Pin (GPIO10):                INPUT PULL-DOWN (Safe)
  VDDIO Enable (GPIO51):          DISABLED LOW (Safe)

D8P1-70 M3_M4_INTEGRITY:
  PLL Status:                     0x0000002f (LOCKED)
  DSI Controller:                 0x000001f5 (ACTIVE)
  DSI Lane Status:                0x00001f1f (STOPSTATE)
```

**Final Result**: `[D8-P1] RESULT=PASS_STAGE_A`

---

## 4. Authoritative Register Verification State

| Signal Name | Register Address | Observed Silicon Value | Decoded Hardware State | Functional Interpretation |
| :--- | :--- | :--- | :--- | :--- |
| **GPIO 8 CFG** | `0x01018000` | `0x00000200` | `mux=0 (gpio), pull=0 (none), drv=0 (2mA), oe=1 (output)` | Panel reset line configured as push-pull output |
| **GPIO 8 IN_OUT** | `0x01018004` | `0x00000000` | `in=0, out=0` | Reset line actively held LOW (Panel held in RESET) |
| **GPIO 10 CFG** | `0x0101a000` | `0x00000005` | `mux=1 (mdp_vsync), pull=1 (pull-down), drv=0 (2mA), oe=0 (input)` | TE line routed to MDP VSYNC engine with pull-down |
| **GPIO 10 IN_OUT** | `0x0101a004` | `0x00000000` | `in=0, out=0` | Input pin in quiescent LOW state |
| **GPIO 51 CFG** | `0x01043000` | `0x00000200` | `mux=0 (gpio), pull=0 (none), drv=0 (2mA), oe=1 (output)` | VDDIO enable line configured as push-pull output |
| **GPIO 51 IN_OUT** | `0x01043004` | `0x00000000` | `in=0, out=0` | Power rail switch actively held LOW (VDDIO OFF) |
| **DSI_CTRL** | `0x00994004` | `0x000001f5` | `enable=1, cmd_mode=1, lanes=4, clk=1` | DSI0 Host controller unperturbed |
| **DSI_LANE_STATUS** | `0x009940a8` | `0x00001f1f` | `DL0-DL3 + CLK stopstate` | MIPI DSI physical lanes remain stable |
| **PLL_PRIMARY_STATUS** | `0x009948cc` | `0x0000002f` | `locked=1` | DSI0 PLL remains locked and running |

---

## 5. Safety Invariant Audit

All safety bounds were strictly observed throughout P1 execution across both independent fresh-boot runs:

| Safety Counter | Measured Value | Boundary Status |
| :--- | :--- | :--- |
| `LAB_WRITES` | 0 | ZERO-TOUCH OBSERVED |
| `IBB_WRITES` | 0 | ZERO-TOUCH OBSERVED |
| `WLED_WRITES` | 0 | ZERO-TOUCH OBSERVED |
| `DCS_PACKETS_SENT` | 0 | ZERO-TOUCH OBSERVED |
| `PANEL_RESET_RELEASE` | 0 | PRESERVED (HELD LOW) |
| `PANEL_POWER_EN` | 0 | PRESERVED (HELD LOW) |
| `BUS_ABORT` | 0 | CLEAN |
| `SError` | 0 | CLEAN |
| `PANIC` | 0 | CLEAN |
| `UNINTENDED_RESET` | 0 | CLEAN |
| `USB_ALIVE` | yes | 100% STABLE |
| `SHELL_ALIVE` | yes | 100% STABLE |

---

## 6. Artifacts Index

- Run 1 Host Transcript: [`artifacts/hw/d8p1/run1/host.txt`](../artifacts/hw/d8p1/run1/host.txt)
- Run 1 Registers Snapshot: [`artifacts/hw/d8p1/run1/xnu_post_p1_registers.json`](../artifacts/hw/d8p1/run1/xnu_post_p1_registers.json)
- Run 1 Metadata: [`artifacts/hw/d8p1/run1/metadata.json`](../artifacts/hw/d8p1/run1/metadata.json)

- Run 2 Host Transcript: [`artifacts/hw/d8p1/run2/host.txt`](../artifacts/hw/d8p1/run2/host.txt)
- Run 2 Registers Snapshot: [`artifacts/hw/d8p1/run2/xnu_post_p1_registers.json`](../artifacts/hw/d8p1/run2/xnu_post_p1_registers.json)
- Run 2 Metadata: [`artifacts/hw/d8p1/run2/metadata.json`](../artifacts/hw/d8p1/run2/metadata.json)

---

## 7. Milestone Conclusion

Milestone **D8-P1** is **COMPLETE**, **SEALED**, and **HARDWARE_PROVEN**.

The Keyaki panel GPIOs are safely and deterministically configured on silicon. The project is ready to merge `xzs-d8-display-p1` into `main` and branch `xzs-d8-display-p2` for the SPMI + PMIC LAB/IBB power rail prerequisite.
