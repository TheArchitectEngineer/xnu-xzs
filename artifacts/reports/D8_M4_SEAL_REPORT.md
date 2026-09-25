# D8-M4 Milestone Seal Report: DSI0 Host Controller Bring-Up

## 1. Executive Summary

Milestone **D8-M4** (DSI0 Host Controller Configuration and Master Enable in Command Mode with 4 active data lanes) has completed full hardware execution and verification on the physical Sony Xperia XZs (`keyaki` / MSM8996 v3.0, serial `BH905SX976`).

Two consecutive, independent cold/fresh-boot hardware runs were executed over the physical USB console, achieving 100% checkpoint pass rates with zero regressions, zero panics, zero bus faults, and zero premature panel or DCS packet transmissions.

The DSI0 host controller successfully achieved the expected idle stopstate (`0x00001f1f`) across all 4 data lanes and the clock lane, with internal clock gates unhalted (`0x0000234f`) and transmit queues idle and ready (`0x11111000`).

---

## 2. Git & Binary Provenance

- **Branch**: `xzs-d8-display-m4`
- **Audit & Phase A Commit**: `e95474e9b16f5045fc9b2ed6ad0ae4daedc19d60`
- **Kernel Binary**: `artifacts/builds/kernel.development.vmapple`
- **Kernel SHA256**: `6e547ba1fe81367acd26d1ed3668ed724ce40b30c36e3318b6348c3bdee5b1bf`
- **Boot Image**: `artifacts/builds/xzs-xnu-boot.img`
- **Boot Image SHA256**: `ac6b5f4cb8bbd7d062b985a8cc1312918568327e7c5d11467c4d1e79ba5a3fdb`
- **PAC Violations**: 0 (`pac_check PASS`)

---

## 3. Hardware Execution Verification

### Checkpoint Results (Runs 1 & 2)

```text
PREREQUISITES:
  M2 Core Clocks & GDSC:          PASS
  M3 Lower Layer (PLL + Clocks + PHY): PASS ([D8-M3] RESULT=PASS_FULL_M3)
  M4 Trace-Only Dry-Run:          PASS ([D8-M4] RESULT=PASS_DRYRUN)

MODE 1 REPRODUCTION:
  DSI_CLKOUT_TIMING_CTRL (0x009940c4) = 0x00001b2b  (Keyaki t_clk_post=0x1b, t_clk_pre=0x2b)
  DSI_EOT_PACKET_CTRL    (0x009940cc) = 0x00000011
  DSI_LANE_CTRL          (0x009940ac) = 0x00000000
  DSI_LANE_SWAP_CTRL     (0x009940b0) = 0x00000000
  DSI_CLK_CTRL           (0x0099411c) = 0x0000023f
  MODE1_REPRODUCED                    = yes

MODE 2 STAGED HOST ENABLE:
  STAGE 2A (Pre-Enable Config):  DSI_CTRL @ 0x00994004 = 0x000001f4
  STAGE 2B (Pre-Enable Status):  DSI_STATUS=0x0, FIFO_STATUS=0x11111000
  STAGE 2C (Enable Controller):  DSI_CTRL |= BIT(0) -> 0x000001f5
  STAGE 2D (Post-Enable Status): DSI_LANE_STATUS = 0x00001f1f (Stopstate active on DL0-DL3 & CLK)
                                 DSI_CLK_STATUS  = 0x0000234f (All core clock gates active)
```

**Final Result**: `[D8-M4] RESULT=PASS_MODE2`

---

## 4. Authoritative Register Verification State

| Register Offset | Register Name | Observed Silicon Value | Status / Interpretation |
| :--- | :--- | :--- | :--- |
| `0x00994000` | `DSI_HW_VERSION` | `0x10040001` | MSM8996 v3.0 DSI Host Core v1.4.1 |
| `0x00994004` | `DSI_CTRL` | `0x000001f5` | Enabled, Command Mode, 4 data lanes + CLK |
| `0x00994008` | `DSI_STATUS` | `0x00000000` | Clean / Idle |
| `0x0099400c` | `DSI_FIFO_STATUS` | `0x11111000` | Transmit FIFO empty / ready |
| `0x00994068` | `DSI_ACK_ERR_STATUS` | `0x00000000` | No ACK errors |
| `0x009940a8` | `DSI_LANE_STATUS` | `0x00001f1f` | All 4 data lanes + CLK in MIPI Stopstate |
| `0x009940ac` | `DSI_LANE_CTRL` | `0x00000000` | Default lane order |
| `0x009940b0` | `DSI_LANE_SWAP_CTRL` | `0x00000000` | Unswapped (Keyaki routing) |
| `0x009940c0` | `DSI_TIMEOUT_STATUS` | `0x00000000` | No timeouts |
| `0x009940c4` | `DSI_CLKOUT_TIMING_CTRL` | `0x00001b2b` | Audited Keyaki panel timing (`t_clk_pre=0x2b`, `t_clk_post=0x1b`) |
| `0x009940cc` | `DSI_EOT_PACKET_CTRL` | `0x00000011` | EoT packets enabled |
| `0x0099411c` | `DSI_CLK_CTRL` | `0x0000023f` | Clocks enabled |
| `0x00994120` | `DSI_CLK_STATUS` | `0x0000234f` | Core clock gates active |
| `0x009942a0` | `DSI_VIDEO_COMPRESSION_MODE_CTRL` | `0x00000b00` | DSC unused (Keyaki uncompressed command mode) |
| `0x009948cc` | `PLL_PRIMARY_STATUS` | `0x0000002f` | DSI0 PLL locked & ready |
| `0x008c2314` | `PCLK0_CBCR` | `0x00000001` | PCLK0 unhalted and running |
| `0x008c233c` | `BYTE0_CBCR` | `0x00000001` | BYTE0 unhalted and running |
| `0x008c2344` | `ESC0_CBCR` | `0x00000001` | ESC0 unhalted and running |

---

## 5. Comparator Acceptance

Against the audited Linux golden reference snapshot (`artifacts/display-audit-a1/golden_snapshot.json`):
- `VERDICT`: **PASS**
- `CRITICAL_M4_DIFF`: **0**
- `DIFF`: **0**
- Obsolete golden mappings (`0x994018`, `0x9940f0`) successfully classified as `IGNORED_OBSOLETE_GOLDEN`.
- Offset `0x2a0` classified as `EXPECTED_DIFFERENCE` (Keyaki panel uncompressed command mode, DSC unused).

---

## 6. Safety Invariant Audit

All safety bounds were strictly observed throughout Phase A, Mode 1, and Mode 2 execution:

| Safety Counter | Measured Value | Boundary Status |
| :--- | :--- | :--- |
| `OFFSET_0x2A0_WRITE_COUNT` | 0 | PRESERVED |
| `DCS_PACKETS_SENT` | 0 | ZERO-TOUCH OBSERVED |
| `DMA_TRIGGER_COUNT` | 0 | ZERO-TOUCH OBSERVED |
| `BTA_TRIGGER_COUNT` | 0 | ZERO-TOUCH OBSERVED |
| `PANEL_GPIO_WRITES` | 0 | FORBIDDEN / ZERO |
| `LAB_WRITES` | 0 | FORBIDDEN / ZERO |
| `IBB_WRITES` | 0 | FORBIDDEN / ZERO |
| `WLED_WRITES` | 0 | FORBIDDEN / ZERO |
| `BUS_ABORT` | 0 | CLEAN |
| `SError` | 0 | CLEAN |
| `PANIC` | 0 | CLEAN |
| `UNINTENDED_RESET` | 0 | CLEAN |
| `USB_ALIVE` | yes | 100% STABLE |
| `SHELL_ALIVE` | yes | 100% STABLE |

---

## 7. Artifacts Index

- Run 1 Host Transcript: [`artifacts/hw/d8m4/mode2/run1/host.txt`](../artifacts/hw/d8m4/mode2/run1/host.txt)
- Run 1 Registers Snapshot: [`artifacts/hw/d8m4/mode2/run1/xnu_post_m4_registers.json`](../artifacts/hw/d8m4/mode2/run1/xnu_post_m4_registers.json)
- Run 1 Diff Report: [`artifacts/hw/d8m4/mode2/run1/diff_report.txt`](../artifacts/hw/d8m4/mode2/run1/diff_report.txt)
- Run 1 Metadata: [`artifacts/hw/d8m4/mode2/run1/metadata.json`](../artifacts/hw/d8m4/mode2/run1/metadata.json)

- Run 2 Host Transcript: [`artifacts/hw/d8m4/mode2/run2/host.txt`](../artifacts/hw/d8m4/mode2/run2/host.txt)
- Run 2 Registers Snapshot: [`artifacts/hw/d8m4/mode2/run2/xnu_post_m4_registers.json`](../artifacts/hw/d8m4/mode2/run2/xnu_post_m4_registers.json)
- Run 2 Diff Report: [`artifacts/hw/d8m4/mode2/run2/diff_report.txt`](../artifacts/hw/d8m4/mode2/run2/diff_report.txt)
- Run 2 Metadata: [`artifacts/hw/d8m4/mode2/run2/metadata.json`](../artifacts/hw/d8m4/mode2/run2/metadata.json)

---

## 8. Milestone Conclusion

Milestone **D8-M4** is **COMPLETE**, **SEALED**, and **HARDWARE_PROVEN**.

The DSI0 host controller is now in a fully configured, active, and idle stopstate. The project is ready to advance to **D8-P1** (TLMM GPIO prerequisite) and **D8-P2** (SPMI LAB/IBB power rail driver) for panel power and reset bring-up.
