# XZS D8-M8 Register Write Plan & Execution Specification

## 1. Executive Summary & Purpose

This document provides the canonical, ordered register write plan for **Milestone D8-M8 (MDP Framebuffer Scanout)** on the Sony Xperia XZs (`G8231` / `keyaki` / MSM8996 v3.0 / `BH905SX976`).

This plan documents the exact sequence of 20 MMIO operations required to display a single full-screen frame to the Sharp + Synaptics command-mode panel without guessing a single bit or register.

**Safety Constraints:**
- This plan is for future staged execution (Milestone D8-M8).
- During the current audit phase, `MDP_MMIO_WRITES = 0` and `MDP_KICKOFF_COUNT = 0`.

---

## 2. Hardware Dependency Graph

```text
[MMCC Clock Controller]
  ├── mmagic_mdss_gdscr = 0xa0222000 (PWR_ON)
  ├── mdss_gdscr = 0xa0222000 (PWR_ON)
  ├── mdss_ahb_cbcr = 0x20008001 (running)
  ├── mdss_axi_cbcr = 0x00006221 (running)
  └── mdss_mdp_cbcr = 0x00006221 (running)
       │
       ▼
[DSI0 PLL & PHY (D8-M3/M4)]
  ├── PLL locked (0x994850 = 0x21)
  ├── mdss_byte0 / mdss_pclk0 / mdss_esc0 running
  └── DSI0 Controller enabled (0x994004 = 0x1f5)
       │
       ▼
[Panel Power & DCS State (D8-M5/M6)]
  ├── VDDIO=1.8V, LAB=+5.6V, IBB=-5.6V, Reset=HIGH
  └── SLPOUT (0x11), TEON (0x35), DISPON (0x29) sent
       │
       ▼
[Step 1: Framebuffer Memory Allocation & Pattern Fill]
  └── Allocate 8,294,400 bytes aligned to 64 bytes; clean to PoC
       │
       ▼
[Step 2: Source Pipe Configuration (SSPP RGB0)]
  └── Configure geometry, stride, physical address, format, unpack
       │
       ▼
[Step 3: Layer Mixer Configuration (LM0)]
  └── Configure output size (1080x1920) and border color (black)
       │
       ▼
[Step 4: PingPong Configuration (PP0)]
  └── Configure command mode buffer, disable tear-check for initial frame
       │
       ▼
[Step 5: DSI Controller MDP Command Stream Configuration]
  └── Program DCS write opcodes (0x2C/0x3C), stream stride, stream total
       │
       ▼
[Step 6: Control Path (CTL0) Routing Configuration]
  └── Select INTF1 on DISP_INTF_SEL, map RGB0 to BASE on LM0, set CMD_MODE on CTL_TOP
       │
       ▼
[Step 7: Flush Shadow Registers]
  └── Write CTL_FLUSH = 0x00020048 (CTL + LM0 + RGB0)
       │
       ▼
[Step 8: Kickoff Frame]
  └── Write CTL_START = 0x00000001
       │
       ▼
[Step 9: Completion Bounded Poll]
  └── Poll MDSS_MDP_REG_INTR_STATUS for BIT(8) (PP_0_DONE) == 1
       │
       ▼
[Step 10: Clear Completion Interrupt]
  └── Write MDSS_MDP_REG_INTR_CLEAR = 0x00000100
```

---

## 3. Canonical Register Write Table (Ordered Execution)

| Step | Block | Register Name | Physical Address | Planned Value | Mask | Rationale & Bitfield Breakdown | Evidence |
|:---:|---|---|---|---|---|---|---|
| **01** | **MDP** | `DISP_INTF_SEL` | `0x00901004` | `0x00000100` | `0x0000ff00` | Route INTF1 to DSI (`0x01` in bits [15:8]) | SOURCE_PROVEN (`mdss_mdp_ctl.c:2525`) |
| **02** | **SSPP**| `RGB0_SRC_SIZE` | `0x00915000` | `0x07800438` | `0xffffffff` | Source image size: H=1920 (`0x780`), W=1080 (`0x438`) | SOURCE_PROVEN (`mdss_mdp_pipe.c`) |
| **03** | **SSPP**| `RGB0_SRC_IMG_SIZE` | `0x00915004` | `0x07800438` | `0xffffffff` | Full buffer size: H=1920, W=1080 | SOURCE_PROVEN (`mdss_mdp_pipe.c`) |
| **04** | **SSPP**| `RGB0_SRC_XY` | `0x00915008` | `0x00000000` | `0xffffffff` | Fetch starting coordinate: X=0, Y=0 | SOURCE_PROVEN (`mdss_mdp_pipe.c`) |
| **05** | **SSPP**| `RGB0_OUT_SIZE` | `0x0091500C` | `0x07800438` | `0xffffffff` | Output display size: H=1920, W=1080 (1:1 no scale) | SOURCE_PROVEN (`mdss_mdp_pipe.c`) |
| **06** | **SSPP**| `RGB0_OUT_XY` | `0x00915010` | `0x00000000` | `0xffffffff` | Output mixer coordinate: X=0, Y=0 | SOURCE_PROVEN (`mdss_mdp_pipe.c`) |
| **07** | **SSPP**| `RGB0_SRC0_ADDR` | `0x00915014` | `<FB_PADDR>` | `0xffffffff` | Direct physical DRAM address of framebuffer | SOURCE_PROVEN (`mdss_mdp_splash_logo.c`) |
| **08** | **SSPP**| `RGB0_SRC_YSTRIDE0` | `0x00915024` | `0x00001100` | `0xffffffff` | Framebuffer stride in bytes: `ALIGN(1080 * 4, 128) = 4352` (`0x1100`) | GOLDEN_HW_PROVEN (`/sys/class/graphics/fb0/stride`) |
| **09** | **SSPP**| `RGB0_SRC_FORMAT` | `0x00915030` | `0x000236AA` | `0xffffffff` | Interleaved 32bpp XRGB, 4 channels 8-bit, tight unpack | SOURCE_PROVEN (`mdss_mdp_formats.h`) |
| **10** | **SSPP**| `RGB0_SRC_UNPACK` | `0x00915034` | `0x03010002` | `0xffffffff` | Component byte mapping: [0]=R, [1]=G, [2]=B, [3]=X | SOURCE_PROVEN (`mdss_mdp_formats.h`) |
| **11** | **SSPP**| `RGB0_SRC_OP_MODE` | `0x00915038` | `0x00000000` | `0xffffffff` | Normal linear fetch, no rotation, no flip, no IGC | SOURCE_PROVEN (`mdss_mdp_pipe.c`) |
| **12** | **LM0** | `LM0_OUT_SIZE` | `0x00945004` | `0x07800438` | `0xffffffff` | Layer Mixer output frame size: 1080x1920 | SOURCE_PROVEN (`mdss_mdp_ctl.c`) |
| **13** | **LM0** | `LM0_BORDER_COLOR_0`| `0x00945008`| `0x00000000` | `0xffffffff` | Mixer backdrop color: Solid Black (`0x00000000`) | SOURCE_PROVEN (`mdss_mdp_ctl.c`) |
| **14** | **PP0** | `PP0_TEAR_CHECK_EN` | `0x00971000` | `0x00000000` | `0x00000001` | Tear-check disabled (no wait for external TE pin) | SOURCE_PROVEN (`mdss_mdp_intf_cmd.c:138`) |
| **15** | **DSI0**| `DSI_CMD_MDP_CTRL` | `0x00994040` | `0x00000008` | `0x0000000f` | Stream destination format: RGB888 (`0x8`), no RGB swap | SOURCE_PROVEN (`mdss_dsi_host.c:334`) |
| **16** | **DSI0**| `DSI_CMD_DCS_CTRL` | `0x00994044` | `0x00013C2C` | `0x0001ffff` | Bit 16=1 (Insert DCS), [15:8]=0x3C (CONT), [7:0]=0x2C (START) | SOURCE_PROVEN (`mdss_dsi_host.c:340`) |
| **17** | **DSI0**| `DSI_STREAM0_CTRL` | `0x00994058` | `0x0CA90039` | `0xffffffff` | `ystride = 3241 (0xCA9)`, `vc = 0`, DataType = `0x39` (DCS) | SOURCE_PROVEN (`mdss_dsi_host.c:1152`) |
| **18** | **DSI0**| `DSI_STREAM0_TOTAL`| `0x0099405c` | `0x07800438` | `0xffffffff` | Lines = 1920 (`0x780`), Pixels = 1080 (`0x438`) | SOURCE_PROVEN (`mdss_dsi_host.c:1154`) |
| **19** | **CTL0**| `CTL_LAYER_0` | `0x00902000` | `0x00000200` | `0x000003ff` | Map RGB0 to BASE stage of LM0 (`1 << 9`) | SOURCE_PROVEN (`mdss_mdp_ctl.c`) |
| **20** | **CTL0**| `CTL_TOP` | `0x00902014` | `0x00020010` | `0x000200f0` | Command Mode (`BIT 17`), Output to INTF1 (`BIT 4`) | SOURCE_PROVEN (`mdss_mdp_ctl.c:3757`) |
| **21** | **CTL0**| `CTL_FLUSH` | `0x00902018` | `0x00020048` | `0xffffffff` | Commit shadow regs for CTL0 (`0x20000`), LM0 (`0x40`), RGB0 (`0x8`) | SOURCE_PROVEN (`mdss_mdp_ctl.c:3893`) |
| **22** | **CTL0**| `CTL_START` | `0x0090201C` | `0x00000001` | `0x00000001` | **KICKOFF**: Trigger single command-mode frame transfer | SOURCE_PROVEN (`mdss_mdp_intf_cmd.c:640`) |

---

## 4. Completion Polling Protocol

1. **Poll Target:** Register `MDSS_MDP_REG_INTR_STATUS` (`0x00901014`).
2. **Mask:** `BIT(8)` = `0x00000100` (`MDSS_MDP_INTR_PING_PONG_0_DONE`).
3. **Condition:** `(read32(0x00901014) & 0x00000100) != 0`.
4. **Timeout:** Hardware cycle loop using `cntvct_el0` with 100,000 µs (100 ms) maximum duration (~6 frames @ 60Hz).
5. **Post-Completion Clear:** Write `0x00000100` to `MDSS_MDP_REG_INTR_CLEAR` (`0x00901018`).
6. **Error Handlers:**
   - If timeout expires: emit `[D8-M8-FAIL] PINGPONG_0_DONE TIMEOUT`. Check `DSI_ACK_ERR_STATUS` (`0x009940ac`) and `DSI_TIMEOUT_STATUS` (`0x009940b0`). Do not loop or retry.
