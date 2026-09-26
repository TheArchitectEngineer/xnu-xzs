# XZS D8-M8 Deferred Report: MDP Command-Mode Scanout

## 1. Executive Summary

Milestone **D8-M8 (MDP Framebuffer Scanout)** on the Sony Xperia XZs (`G8231` / `keyaki` / MSM8996 v3.0 / Serial `BH905SX976`) is formally **FROZEN**, **DEFERRED**, and marked **INCOMPLETE / UNSEALED**.

The user has explicitly instructed to stop D8-M8 debugging at the current hardware-proven boundary, with **NO Retry #11** and **NO further hardware experimentation** on this branch. The current implementation, forensic evidence, and identified blocker are preserved, committed, merged to `main`, and tagged with `xzs-d8m8-deferred`.

```text
D8_M8_STATUS              = DEFERRED / INCOMPLETE / UNSEALED
FIRST_MDP_FRAME           = no
FIRST_SCANOUT_TRANSPORT   = no
FIRST_VISIBLE_PIXELS      = no
FINAL_BLOCKER             = PP0_TO_DSI_COMMAND_MODE_HANDSHAKE
```

---

## 2. Build Identity & Hardware Execution

All testing strictly adhered to non-invasive hardware constraints (no NAND/eMMC flash; RAM-only fastboot boot):

| Property | Value |
|---|---|
| **Target Device** | Sony Xperia XZs (`G8231` / `keyaki` / `tone`) |
| **SoC** | Qualcomm Snapdragon 820 (`MSM8996` v3.0) |
| **Device Serial** | `BH905SX976` |
| **Feature Branch** | `xzs-d8-display-m8-audit` |
| **Retry #10 Commit** | `02435720864946ba6aecfe4be21d400a75ff43ff` |
| **Kernel Binary** | `src/xnu/BUILD/obj/DEVELOPMENT_ARM64_VMAPPLE/kernel.development.vmapple` |
| **KERNEL_SHA256** | `ef73f8905d9e5284dfa12893be09b3165a40c644006ac1b9df8b94db0b168860` |
| **Boot Image** | `artifacts/builds/xzs-xnu-boot.img` |
| **BOOT_SHA256** | `4c8b6d2e401eb35e89c7fa1ccfab7fefe1d6c473cd3a0cf15d5bd43560cc102e` |
| **PAC Compliance** | `PAC=0` (0 executable PAC instructions; 100% ARMv8.0-A compliant) |
| **Flash Policy** | `NO FLASH` (`fastboot boot artifacts/builds/xzs-xnu-boot.img`) |
| **WLED Backlight** | `WLED_WRITES=0` (Backlight remained OFF) |

---

## 3. Independently Hardware-Proven Subsystems

The display bring-up stack preceding D8-M8 remains 100% hardware-proven, sealed, and verified across fresh-boot runs:

- **D8-M3 (DSI PLL / Clocks / 14nm PHY Stage B)**: `SEALED / HW_PROVEN`. DSI0 PLL locked (`0x009948cc = 0x2f`), MMCC BYTE0/PCLK0/ESC0 branches unhalted (`CBCR=1, halt=0`), 14nm PHY lanes calibrated (`LANE_STATUS=0x1f1f`).
- **D8-M4 (DSI0 Host Controller)**: `SEALED / HW_PROVEN`. Command mode, 4 data lanes enabled, `HW_VERSION=0x10040001`, `ACK_ERR=0`, `TIMEOUT=0`.
- **D8-P1 (TLMM Display GPIOs)**: `SEALED / HW_PROVEN`. GPIO8 (panel reset), GPIO10 (TE input), GPIO51 (VDDIO rail).
- **D8-P2 (SPMI LAB/IBB Power Rails)**: `SEALED / HW_PROVEN`. SPMI Arbiter v2 communication, PMI8996 LAB (+5.60V) and IBB (-5.60V) display bias regulators.
- **D8-M5 (Panel Power & Reset Lifecycle)**: `SEALED / HW_PROVEN`. Cycle-accurate hardware reset sequence, powered-idle verification, clean power-down.
- **D8-M6 (Panel Vendor / DCS Command Transmission)**: `SEALED / HW_PROVEN`. DCS Sleep Out (`0x11`), Tear On (`0x35 0x00`), Display On (`0x29`), Display Off (`0x28`), Sleep In (`0x10`) transmitted cleanly with `ACK_ERR=0`, `TIMEOUT=0`.

---

## 4. D8-M8 Proven Subsystem State

During D8-M8 Retries #1 through #10, the following MDP subsystem configurations were validated on physical hardware:

### 4.1 Framebuffer & Memory
- `FB_PA = 0x98000000`, `FB_SIZE = 8355840` bytes (1080 × 1920 × 4 with 4352-byte stride alignment).
- `FB_FORMAT = XRGB8888`, `FB_PATTERN = RED` (0x00FF0000).
- Cache maintenance: `POC_CLEAN` executed before kickoff (`dc cvac` across entire framebuffer PA range).
- SMMU state: `SMMU_STATE=BYPASS`, `DIRECT_PA_SAFE=yes`.

### 4.2 RGB0 / SSPP (Source Surface Pipe)
- Geometry: `SRC_SIZE = 0x07800438` (1080x1920), `SRC_IMG_SIZE = 0x07800438`, `SRC_XY = 0x00000000`, `OUT_SIZE = 0x07800438`, `OUT_XY = 0x00000000`.
- Format & Unpack: `SRC_FORMAT = 0x000236aa` (XRGB8888, 8-bit per component), `SRC_UNPACK = 0x03010002` (B-G-R-A component ordering).
- Address & Stride: `SRC0_ADDR = 0x98000000`, `YSTRIDE0 = 0x00001100` (4352 bytes).
- Fetch & QoS: `FETCH_CONFIG = 0x00000087`, `QOS_CTRL = 0x00000001`.
- **Hardware Proof**: `RGB0_CURRENT_SRC0_ADDR` latched from `0x00000000` to `0x98000000` upon kickoff. SSPP successfully registered the active frame pointer.

### 4.3 LM0 (Layer Mixer)
- Layer routing: `CTL_LAYER_0 = 0x00000200` (RGB0 routed to Layer Mixer Stage BASE).
- Blend mode: `LM0_OP_MODE = 0x00000000`, `LM0_OUT_SIZE = 0x07800438`.
- Source audit confirmed that Stage BASE bypasses blending and does not require `LM_BLEND0` registers.

### 4.4 CTL0 (Controller Engine & Flush)
- Routing: `DISP_INTF_SEL = 0x00000100` (DSI_CMD), `CTL_TOP = 0x00020020` (INTF_1 active, LM0 active).
- Flush register: `CTL_FLUSH = 0x00020048` (flushes LM0 bit 3, SSPP RGB0 bit 6, and INTF1 bit 17).
- Note: Experimental bit 30 (`0x40000000`, video timing generator flush) was rejected because it belongs exclusively to video mode and remains unconsumed in command mode.
- **Hardware Proof**: `CTL_FLUSH` (`0x00020048`) was fully consumed to `0x00000000` on kickoff. `CTL_START` write (`0x00000001`) was consumed to `0x00000000`.

### 4.5 PP0 / Tearcheck & Internal VSYNC Override
Retry #10 implemented the Qualcomm software-TE / internal VSYNC override model (`mdss_panel_override_te_params` / `mdss_mdp_cmd_tearcheck_setup`):
- `PP_TEAR_CHECK_EN = 0x00000001`
- `PP_SYNC_CONFIG_VSYNC = 0x00080093` (`BIT19=1` internal vsync enabled, `BIT20=0` external TE disabled, `vclks_line=0x93` / 147 cycles)
- `PP_SYNC_CONFIG_HEIGHT = 0x00000873` (2163 = VTOTAL = 1920 + 8 + 227 + 8)
- `PP_SYNC_WRCOUNT = 0x00000785` (`START_POS (1920) + THRESH_START (4) + 1 = 1925 = 0x785`)
- `PP_VSYNC_INIT_VAL = 0x00000780` (1920)
- `PP_SYNC_THRESH = 0x00040004` (start=4, cont=4)
- `PP_START_POS = 0x00000780` (1920)
- `PP_RD_PTR_IRQ = 0x00000781` (1921)
- `PP_WR_PTR_IRQ = 0x00000000`
- Clock: `MDSS_VSYNC_CLK = 19.2 MHz`. Calculated internal frame period: `2163 * 147 / 19,200,000` = `16,560` µs (~16.56 ms).
- **Hardware Proof**: High-frequency sampling (0–50 ms) proved:
  - Internal VSYNC counter (`PP0_INT_COUNT_VAL`) cycled with ~16.56 ms periodicity (rolled over past 0x873).
  - Interrupt status transitioned: clean `0x00000000` -> `0x00010000` (WR_PTR at T=0 µs) -> `0x00011000` (RD_PTR at T=10,000 µs / 10 ms).
  - Both WR_PTR and RD_PTR interrupts are active and driven by internal VSYNC.
  - `SW_TE_OVERRIDE_HARDWARE_EFFECT = yes`.

---

## 5. Physical TE Status (GPIO10)

Retry #9 tested physical TE synchronization using the panel DDIC TE pin on TLMM GPIO10:
```text
TE_SAMPLE_WINDOW_US  = 50001
TE_TRANSITIONS       = 0
TE_HIGH_SAMPLES      = 0
TE_LOW_SAMPLES       = 29920
TE_FINAL_LEVEL       = LOW
PHYSICAL_TE_PROVEN   = no
```
Although DCS Tear On (`0x35 0x00`) transmitted cleanly over DSI, the panel DDIC did not generate electrical pulses on GPIO10. Physical TE remains unproven without vendor-specific DDIC initialization or unmasking. Retry #10 successfully bypassed this dependency via Qualcomm's internal VSYNC override model.

---

## 6. Retries History (M8-7 Lifecycle)

| Attempt | Branch / Commit | Result | Key Hardware Findings |
|---|---|---|---|
| **Retry #1** | `9154f24` | FAIL_CLEAN | Initial single-kickoff implementation. Poll loop timed out after 100 ms with `PP0_DONE=0`. |
| **Retry #2** | `7bc2f68` | FAIL_CLEAN | Tearcheck geometry and DSI MDP stream configuration tuned. `PP0_DONE` still timed out. |
| **Retry #3** | `bfde555` | FAIL_CLEAN | Evaluated zero-tearcheck / free-run model. Confirmed command-mode requires PingPong timing. |
| **Retry #4** | `d39ee25` | FAIL_CLEAN | `MDSS_VSYNC_CLK` unhalted at 19.2 MHz. Proved `RGB0_CURRENT_SRC0_ADDR` latched `0x98000000`. |
| **Retry #5** | `132b132` | FAIL_CLEAN | Corrected CTL routing (`DISP_INTF_SEL=0x100`, `CTL_TOP=0x20020`). `PP0_DONE` timed out. |
| **Retry #6** | `43fb036` | FAIL_CLEAN | Integrated real physical panel power-on & DCS sequence (`m8-panel-prepare`). Panel ready. |
| **Retry #7** | `dd4641f` | FAIL_CLEAN | Full SSPP RGB0 and LM BASE register validation. Proved `CTL_FLUSH` (`0x00020048`) consumed to 0. |
| **Retry #8** | `79647b1` | FAIL_CLEAN | Enabled `PP_TEAR_CHECK_EN=1`. Proved WR_PTR interrupt activity (`INTR_STATUS=0x00010000`). |
| **Retry #9** | `8fb8e2d` | ABORT_CLEAN | 50ms TE sampling proved GPIO10 silent (0 transitions). Clean abort without `CTL_START`. |
| **Retry #10** | `0243572` | FAIL_CLEAN | Qualcomm SW-TE override active (`PP_SYNC_CONFIG_VSYNC=0x00080093`). WR_PTR (0 us) and RD_PTR (10 ms) interrupts proven. Counter cycled at ~16.56 ms. `PP0_LINE_COUNT` remained 0; PP→DSI transport blocked. |

---

## 7. Canonical Failure Diagnostics & Final Blocker

### 7.1 Symptoms
Across 100 ms of polling during Retry #10:
- `MAX_LINE_COUNT_OBSERVED = 0x00000000`
- `MAX_OUT_LINE_COUNT_OBSERVED = 0x00000000`
- `PP0_DONE_OBSERVED = no`
- `DSI_STREAM_ACTIVITY = no`
- `FRAMEBUFFER_SCANOUT_COUNT = 0`
- `DSI_ACK_ERR = 0`, `DSI_TIMEOUT = 0`, `DSI_STATUS = 0`, `DSI_FIFO_STATUS = 0x11111000`

### 7.2 Root Cause Analysis
The tearcheck timing engine is fully operational in internal mode. However, pixel rasterization in PingPong never started (`PP0_LINE_COUNT = 0`).

In Qualcomm MDSS DSI command mode, the MDP PingPong block and DSI host controller operate under a strict flow-control contract:
1. `DSI_TRIG_CTRL` (0x00994084) is set to `0x80000004` (`te_sel=1`, `dma_trigger=SW`, `mdp_trigger=NONE`).
2. When `mdp_trigger=NONE` (bits [7:4] = 0), the DSI controller does not autonomously launch MDP packet transmission from hardware VSYNC.
3. The DSI command engine requires either:
   - An explicit software trigger write to `DSI_CMD_MODE_MDP_SW_TRIGGER` (`0x00994094 = 1`) upon kickoff; OR
   - Configuration of `mdp_trigger` in `DSI_TRIG_CTRL`; AND
   - A complete command-mode stream packet contract in `DSI_CMD_MDP_CTRL` (`0x00994040`) matching MSM8996 command-mode packet generation.
4. Without the DSI host asserting flow credit, the PingPong block stalls and never fetches raster scanlines.

```text
CANONICAL BLOCKER:
PingPong0 → DSI Command-Mode Transport / Handshake Boundary
```

---

## 8. Preserved Hardware Artifacts

All forensic test outputs and hardware telemetry logs across Retries #1 through #10 are preserved immutably in:

```text
artifacts/hw/d8m8/m8-7/
artifacts/hw/d8m8/m8-7-retry2/
artifacts/hw/d8m8/m8-7-retry3/
artifacts/hw/d8m8/m8-7-retry4/
artifacts/hw/d8m8/m8-7-retry5/
artifacts/hw/d8m8/m8-7-retry6/
artifacts/hw/d8m8/m8-7-retry7/
artifacts/hw/d8m8/m8-7-retry8/
artifacts/hw/d8m8/m8-7-retry9/
artifacts/hw/d8m8/m8-7-retry10/
```

Key Retry #10 logs in `artifacts/hw/d8m8/m8-7-retry10/`:
- `host.txt` — Full serial session transcript.
- `panel-prepare.txt` — Panel power and DCS initialization log.
- `pre-kick.txt` — Telemetry of all MDP, SSPP, LM, PP, CTL, and DSI registers prior to kickoff.
- `kickoff.txt` — 13-point high-frequency sampling (0–50 ms) and 100 ms timeout forensics.
- `completion.txt` — Final completion status and register dump.
- `post-frame.txt` — DSI host post-frame status.

---

## 9. Future Resume Specification

If and when the Sony Xperia XZs display bring-up project resumes:

1. **Resume Point**: Resume directly from commit `02435720864946ba6aecfe4be21d400a75ff43ff` plus the freeze/documentation commit on `main`.
2. **Do NOT Re-investigate**:
   - Panel power / reset rails (D8-P1, D8-P2, D8-M5).
   - DSI PLL / clock tree / 14nm PHY (D8-M3).
   - DSI0 host basic enable (D8-M4).
   - Panel DCS initialization (D8-M6).
   - SSPP RGB0 geometry, unpack format, or stride (M8-2).
   - LM0 blending / stage routing (M8-3).
   - CTL0 routing or flush mask (M8-5, M8-6).
   - PP0 internal VSYNC override parameters (M8-4).
3. **Investigation Boundary**: Focus strictly on the **PingPong → DSI command-mode trigger / flow-control handshake**:
   - Audit downstream Qualcomm Linux `mdss_dsi_host.c` command-mode kickoff path: `mdss_dsi_cmd_mdp_busy()`, `DSI_CMD_MODE_MDP_SW_TRIGGER` (`0x00994094`), and `DSI_TRIG_CTRL` (`0x00994084`).
   - Determine whether `DSI_CMD_MODE_MDP_SW_TRIGGER` must be pulsed concurrently with `CTL_START`.
   - Verify `DSI_CMD_MDP_CTRL` (`0x00994040`) bitfield definitions for MSM8996 command-mode packet header generation.
   - Do NOT blindly pulse registers without verified downstream driver source reference.

---

## 10. Final Milestone Acceptance Record

```text
D8-M8-7-RETRY10 = FAIL_CLEAN
FIRST_MDP_FRAME = no
FIRST_SCANOUT_TRANSPORT = no
FIRST_VISIBLE_PIXELS = no
D8_M8_STATUS = DEFERRED / INCOMPLETE / UNSEALED
```
