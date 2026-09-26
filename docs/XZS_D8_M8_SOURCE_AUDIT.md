# XZS D8-M8 Source Audit: MSM8996 MDP / Command-Mode Scanout Architecture

## 1. Executive Summary & Purpose

This document provides the authoritative, source-proven architectural audit of the Qualcomm MSM8996 Mobile Display Processor (MDP5) and MIPI DSI command-mode display scanout pipeline for the Sony Xperia XZs (`G8231` / `keyaki` / MSM8996 v3.0 / `BH905SX976`).

This audit serves as the formal foundation for **Milestone D8-M8 (MDP Framebuffer Scanout)**, converting D8-M8 from an uncertain hardware bring-up task into a deterministic, source-proven register plan.

**Absolute Constraints & Scope Locks:**
- Read-only analysis and dry-run planning only.
- `MDP_KICKOFF_COUNT = 0`
- `CTL_START_COUNT = 0`
- `FRAMEBUFFER_SCANOUT_COUNT = 0`
- `SSPP_ENABLE_COUNT = 0`
- `MDP_MMIO_WRITES = 0`

---

## 2. Authoritative Source Provenance

The findings in this document are derived directly from the exact hardware specifications, device tree definitions, and kernel driver sources:
1. `artifacts/display-audit/keyaki.dts` (Authoritative downstream Sony MSM8996 device tree)
2. `drivers/video/msm/mdss/mdss_mdp.c` (Qualcomm MDP5 core driver)
3. `drivers/video/msm/mdss/mdss_mdp_hwio.h` (Hardware I/O register definitions)
4. `drivers/video/msm/mdss/mdss_mdp_ctl.c` (Control path, mixer routing, and flush management)
5. `drivers/video/msm/mdss/mdss_mdp_pipe.c` (Source pipe / SSPP configuration)
6. `drivers/video/msm/mdss/mdss_mdp_intf_cmd.c` (DSI command-mode interface, pingpong, tearcheck, and kickoff)
7. `drivers/video/msm/mdss/mdss_dsi_host.c` (DSI command stream formatting and DCS stream controller)
8. `drivers/video/msm/mdss/mdss_mdp_splash_logo.c` (Bootloader continuous splash scanout architecture)

---

## 3. Physical Display Block Topology

### 3.1 Primary Hardware Pipeline
The physical data flow from memory to the display glass is:
```text
Framebuffer (Physical DRAM, e.g. 0x83401000)
    │
    ▼ [Direct Physical DMA Fetch / Bypass SMMU]
SSPP RGB0 (0x00915000) [or VIG0 (0x00905000)]
    │
    ▼ [Unscaled 1:1 Pixel Stream]
Layer Mixer 0 (LM0, 0x00945000) [Base Stage Routing]
    │
    ▼ [Full Frame Output: 1080x1920]
PingPong 0 (PP0, 0x00971000) [Command Mode Buffer & Sync]
    │
    ▼ [DSI Command Stream Generator]
INTF1 (0x0096b800) -> DSI0 Controller (0x00994000)
    │
    ▼ [MIPI DSI 4 Data Lanes in Command Mode]
Sharp + Synaptics Command-Mode Panel (somc,sharp_synaptics_cmd_9_panel)
```

### 3.2 Block Base Address Resolution
| Block Name | Role | Offset from 0x00900000 | Physical Base Address | Source Evidence | Candidate List Audit |
|---|---|---|---|---|---|
| **MDSS Top** | Display Subsystem Wrapper | `0x00000` | `0x00900000` | `keyaki.dts:206` | MATCH |
| **MDP5 Core** | Mobile Display Processor Core | `0x01000` | `0x00901000` | `keyaki.dts:226` | MATCH |
| **CTL0** | Control Path 0 (Primary) | `0x02000` | `0x00902000` | `keyaki.dts:246,268` | **CORRECTED** (Candidate had `0x00903000`, +0x1000 error) |
| **VIG0** | Video / Image Pipe 0 (w/ Scaler) | `0x05000` | `0x00905000` | `keyaki.dts:234,268` | MATCH |
| **RGB0** | RGB Pipe 0 (No Scaler) | `0x15000` | `0x00915000` | `keyaki.dts:235,268` | SOURCE_PROVEN |
| **LM0** | Layer Mixer 0 (`LAYER_0`) | `0x45000` | `0x00945000` | `keyaki.dts:247,268` | **CORRECTED** (Candidate had `0x00946000`, which is LM1) |
| **INTF1** | DSI0 Interface Timing/Mapping | `0x6b800` | `0x0096b800` | `keyaki.dts:251,268` | **CORRECTED** (Candidate had `0x0096c800`, which is INTF3/HDMI) |
| **PP0** | PingPong 0 (`PP_0`) | `0x71000` | `0x00971000` | `keyaki.dts:252,268` | **CORRECTED** (Candidate had `0x00972000`, which is PP2) |
| **DSI0** | DSI Host Controller 0 | N/A | `0x00994000` | `keyaki.dts:1997` | MATCH |

---

## 4. Source Pipe (SSPP) Selection & Scaler Analysis

### 4.1 Comparison: RGB0 vs VIG0
MSM8996 provides both RGB pipes and VIG pipes:
1. **SSPP RGB0 (`0x00915000`):**
   - Type: `MDSS_MDP_PIPE_TYPE_RGB` (Hardware index 3)
   - Capabilities: Pure 1:1 scanout for RGB formats (`XRGB8888`, `ARGB8888`, `RGB888`, `RGB565`).
   - Scaler: **Zero scalar hardware** (`mdata->has_non_scalar_rgb = 1`). Cannot scale, rotate, or perform decimation.
   - Benefit: Absolutely zero risk of uninitialized scaler coeff tables, phase errors, or filter artifacts.
   - Flush Bit in `CTL_FLUSH`: `BIT(3)` = `0x00000008`.
2. **SSPP VIG0 (`0x00905000`):**
   - Type: `MDSS_MDP_PIPE_TYPE_VIG` (Hardware index 0)
   - Capabilities: Video/YUV and RGB scanout with QSEED2/3 hardware scaler.
   - Scaler: Scaler can be bypassed by setting `SCALE_CONFIG = 0` and `VIG_OP_MODE = 0`.
   - Flush Bit in `CTL_FLUSH`: `BIT(0)` = `0x00000001`.

**Canonical Conclusion:**
Both pipes are fully documented. For initial bring-up, **`SSPP RGB0`** is the preferred primary scanout pipe because it physically lacks scaler logic, eliminating an entire class of hardware register dependencies.

---

## 5. Framebuffer Memory & Addressing Architecture

### 5.1 Physical Addressing vs SMMU
- **Target Addressing Mode:** `DIRECT PHYSICAL ADDRESSING`
- **SMMU Status:** `SMMU_REQUIRED = NO`
- **Proof:**
  In `mdss_mdp_splash_logo.c`, the Qualcomm bootloader continuous splash screen (`cont_splash_mem@83401000`) is displayed by MDP **prior to SMMU attachment** (`!mdata->mdss_util->iommu_attached()`). The MSM8996 hardware resets with SMMU context banks in bypass mode (`SCTLR.M == 0`).
  Consequently, memory addresses programmed into `SSPP_SRC0_ADDR` (`0x014`) pass directly to the memory controller as raw physical DRAM addresses.

### 5.2 Pixel Format & Geometry
- **Format:** `XRGB8888` (32 bpp, 4 bytes per pixel)
- **Geometry:** `1080 x 1920` (FHD)
- **Stride:** `1080 * 4 = 4320` bytes (`0x10E0`)
- **Total Buffer Size:** `1080 * 1920 * 4 = 8,294,400` bytes (~7.91 MB)
- **Memory Alignment:** 64-byte boundary requirement for MDP DMA burst transfers.
- **Cache Maintenance:** CPU framebuffer writes must be cleaned to the Point of Coherency (`CleanPoC_DcacheRegion_Force` / `dc cvac`) prior to kickoff.

---

## 6. Control Path (CTL0) Routing & Flush Architecture

### 6.1 CTL0 Routing Configuration
1. **Interface Selection (`MDSS_MDP_REG_DISP_INTF_SEL`, `0x00901004`):**
   - Bits [15:8] = `0x01` (`MDSS_INTF_DSI` on INTF1)
   - Register Value: `0x00000100`
2. **CTL Top Mode (`MDSS_MDP_REG_CTL_TOP`, `0x00902014`):**
   - Bit 17 = 1 (`MDSS_MDP_CTL_OP_CMD_MODE`)
   - Bits [7:4] = `0x1` (`OUT_SEL = INTF1`)
   - Register Value: `0x00020010`
3. **Layer Mixer Configuration (`MDSS_MDP_REG_CTL_LAYER_0`, `0x00902000`):**
   - Maps the selected SSPP to `STAGE_BASE` of `LM0`:
     - If `RGB0`: `mixercfg = 1 << (3 * 3) = 1 << 9 = 0x00000200`
     - If `VIG0`: `mixercfg = 1 << (3 * 0) = 1 << 0 = 0x00000001`

### 6.2 CTL0 Flush Mask Decomposition
The `CTL_FLUSH` register (`0x00902018`) commits shadow register configurations to active hardware:
| Bit | Subsystem / Block | Value | Meaning |
|---|---|---|---|
| **Bit 0** | `VIG0` | `0x00000001` | Flushes SSPP VIG0 shadow registers (if used) |
| **Bit 3** | `RGB0` | `0x00000008` | Flushes SSPP RGB0 shadow registers (if used) |
| **Bit 6** | `LM0` | `0x00000040` | Flushes Layer Mixer 0 configuration |
| **Bit 17** | `CTL` | `0x00020000` | Flushes CTL0 top/layer routing |

**Final First-Frame Flush Mask:**
- With `RGB0`: `0x00020000 | 0x00000040 | 0x00000008 = 0x00020048`
- With `VIG0`: `0x00020000 | 0x00000040 | 0x00000001 = 0x00020041`

---

## 7. DSI MDP Command-Mode Stream Configuration

The MDP-to-DSI pixel stream is configured in the DSI controller:
1. **`DSI_COMMAND_MODE_MDP_CTRL` (`0x00994040`):** `0x00000008`
   - Bits [3:0] = `0x8` (`DSI_CMD_DST_FORMAT_RGB888`)
   - Bits [18:16] = `0` (no RGB swap)
2. **`DSI_COMMAND_MODE_MDP_DCS_CMD_CTRL` (`0x00994044`):** `0x00013C2C`
   - Bits [7:0] = `0x2C` (`WRITE_MEMORY_START`)
   - Bits [15:8] = `0x3C` (`WRITE_MEMORY_CONTINUE`)
   - Bit 16 = 1 (`insert_dcs_cmd`)
3. **`DSI_COMMAND_MODE_MDP_STREAM0_CTRL` (`0x00994058`):** `0x0CA90039`
   - Bits [31:16] = `ystride = (1080 * 3) + 1 = 3241 = 0x0CA9`
   - Bits [15:8] = `vc_id = 0`
   - Bits [7:0] = `0x39` (`DCS Long Write`)
4. **`DSI_COMMAND_MODE_MDP_STREAM0_TOTAL` (`0x0099405c`):** `0x07800438`
   - Bits [31:16] = `height = 1920 = 0x0780`
   - Bits [15:0] = `width = 1080 = 0x0438`

---

## 8. Kickoff & Completion Synchronization

### 8.1 Kickoff Mechanism
- **Trigger Register:** `MDSS_MDP_REG_CTL_START` (`0x0090201C`)
- **Trigger Value:** `0x00000001`
- **Execution Effect:** Latches all flushed shadow registers, instructs SSPP DMA engine to begin DRAM fetch, streams pixels through LM0 to PP0, and emits DSI packet stream over DSI0.

### 8.2 Completion Signal
- **Status Register:** `MDSS_MDP_REG_INTR_STATUS` (`0x00901014`)
- **Completion Bit:** `BIT(8)` (`0x00000100`, `MDSS_MDP_INTR_PING_PONG_0_DONE`)
- **Clear Method:** Write `0x00000100` to `MDSS_MDP_REG_INTR_CLEAR` (`0x00901018`).
- **Timeout Window:** Bounded hardware poll with 100 ms timeout (~6 frames @ 60Hz).

### 8.3 Tear-Check (TE) Dependency
- `PP_TEAR_CHECK_EN` (`0x00971000`):
  - Setting `PP_TEAR_CHECK_EN = 0` disables tear-check synchronization, causing kickoff to execute immediately without waiting for GPIO10 TE pulses.
  - Initial bring-up policy: Disable tear-check (`0x00000000`) for the first frame to guarantee zero external signal blockage.

---

## 9. Source Audit Verdict

All gating questions for Milestone D8-M8 scanout architecture are resolved and **SOURCE_PROVEN**:
- Exact SSPP: `RGB0` (`0x00915000`) / `VIG0` (`0x00905000`)
- Exact LM: `LM0` (`0x00945000`)
- Exact CTL: `CTL0` (`0x00902000`)
- Exact PP: `PP0` (`0x00971000`)
- Exact INTF: `INTF1` (`0x0096b800`)
- Exact DSI Stream: `0x00994040..5c`
- Exact CTL Flush Mask: `0x00020048` (`RGB0`) / `0x00020041` (`VIG0`)
- Exact Kickoff: `CTL_START = 1`
- Exact Completion: `INTR_STATUS bit 8 (PP_0_DONE)`
- Framebuffer Address Mode: `Physical` (SMMU not required)

**Verdict: `M8_PREAUDIT_READY`**.
