# XZS D8-M8 Golden State & Register Comparison Audit

## 1. Executive Summary

This document captures and analyzes the Linux/TWRP golden working display state against the planned XNU D8-M8 scanout register state on physical Sony Xperia XZs (`G8231` / `keyaki` / MSM8996 v3.0 / `BH905SX976`).

- **Operating Environment:** TWRP 3.18 kernel, 1080x1920 command mode, Sharp + Synaptics cmd_9 panel.
- **Reference Sources:** Live debugfs MMIO dumps, `linux_golden_registers.md`, `golden_snapshot.json`, and authoritative downstream driver state.

---

## 2. Register Classification Legend

| Classification | Definition | Application to XNU Programming |
|---|---|---|
| **STABLE_STATIC** | Fixed hardware configuration register that remains constant during display active state | Programmed during setup |
| **DYNAMIC_COUNTER** | Free-running hardware counter or line position | Read-only observation |
| **IRQ_STATUS** | Hardware interrupt or completion status bit | Polled for completion; cleared via clear register |
| **TRIGGER_ONLY** | Self-clearing hardware trigger register | Written to start operation |
| **POWER_DEPENDENT** | Register only accessible when MDSS power domain and clocks are active | Requires GDSC + MMCC clocks active |

---

## 3. Comprehensive Register Map & Comparison Table

| Block | Register Name | Physical Address | Golden Linux Value | Planned XNU D8-M8 Value | Role & Bitfield Breakdown | Classification |
|---|---|---|---|---|---|---|
| **MDSS** | `MDSS_HW_VERSION` | `0x00900000` | `0x10070000` | `0x10070000` (read-only) | MDSS v1.7.0 hardware identifier | STABLE_STATIC |
| **MDP** | `DISP_INTF_SEL` | `0x00901004` | `0x00000100` | `0x00000100` | Bits [15:8]=1 (INTF1 = DSI) | STABLE_STATIC |
| **MDP** | `INTR_EN` | `0x00901010` | `0x00000100` | `0x00000000` (polling used) | Bit 8 = PingPong 0 Done IRQ Enable | STABLE_STATIC |
| **MDP** | `INTR_STATUS` | `0x00901014` | `0x00000000` / `0x100` | Poll for `BIT(8) == 1` | Bit 8 = PingPong 0 Done status | IRQ_STATUS |
| **MDP** | `INTR_CLEAR` | `0x00901018` | Write-only | Write `0x00000100` | Clears Bit 8 in `INTR_STATUS` | TRIGGER_ONLY |
| **CTL0** | `CTL_LAYER_0` | `0x00902000` | `0x00000200` | `0x00000200` | Bits [11:9]=1 (RGB0 on BASE stage of LM0) | STABLE_STATIC |
| **CTL0** | `CTL_TOP` | `0x00902014` | `0x00020010` | `0x00020010` | Bit 17=1 (Cmd Mode), Bits [7:4]=1 (INTF1) | STABLE_STATIC |
| **CTL0** | `CTL_FLUSH` | `0x00902018` | Write-only | Write `0x00020048` | Flushes CTL0 (`0x20000`), LM0 (`0x40`), RGB0 (`0x8`) | TRIGGER_ONLY |
| **CTL0** | `CTL_START` | `0x0090201C` | Write-only | Write `0x00000001` | Initiates single command-mode frame kickoff | TRIGGER_ONLY |
| **SSPP** | `RGB0_SRC_SIZE` | `0x00915000` | `0x07800438` | `0x07800438` | H=1920 (`0x780`), W=1080 (`0x438`) | STABLE_STATIC |
| **SSPP** | `RGB0_SRC_IMG_SIZE`| `0x00915004`| `0x07800438` | `0x07800438` | H=1920 (`0x780`), W=1080 (`0x438`) | STABLE_STATIC |
| **SSPP** | `RGB0_SRC_XY` | `0x00915008` | `0x00000000` | `0x00000000` | Y=0, X=0 | STABLE_STATIC |
| **SSPP** | `RGB0_OUT_SIZE` | `0x0091500C` | `0x07800438` | `0x07800438` | Dst H=1920, Dst W=1080 (1:1 unscaled) | STABLE_STATIC |
| **SSPP** | `RGB0_OUT_XY` | `0x00915010` | `0x00000000` | `0x00000000` | Dst Y=0, Dst X=0 | STABLE_STATIC |
| **SSPP** | `RGB0_SRC0_ADDR` | `0x00915014` | DRAM IOVA / Phys | Physical Framebuffer | Direct physical DRAM address of buffer | STABLE_STATIC |
| **SSPP** | `RGB0_SRC_YSTRIDE0`| `0x00915024`| `0x00001100` | `0x00001100` | Stride in bytes: `ALIGN(1080 * 4, 128) = 4352` (`0x1100`) | STABLE_STATIC |
| **SSPP** | `RGB0_SRC_FORMAT` | `0x00915030` | `0x000236aa` | `0x000236aa` | 32bpp XRGB, 4 channels 8-bit, tight unpack | STABLE_STATIC |
| **SSPP** | `RGB0_SRC_UNPACK` | `0x00915034` | `0x03010002` | `0x03010002` | Byte order: [0]=R, [1]=G, [2]=B, [3]=X | STABLE_STATIC |
| **SSPP** | `RGB0_SRC_OP_MODE`| `0x00915038` | `0x00000000` | `0x00000000` | Default linear, no flip, no IGC | STABLE_STATIC |
| **LM0** | `LM0_OP_MODE` | `0x00945000` | `0x00000000` | `0x00000000` | Default layer mixer operation mode | STABLE_STATIC |
| **LM0** | `LM0_OUT_SIZE` | `0x00945004` | `0x07800438` | `0x07800438` | Output H=1920, Output W=1080 | STABLE_STATIC |
| **LM0** | `LM0_BORDER_COLOR0`| `0x00945008`| `0x00000000` | `0x00000000` | Solid black background (`0x00000000`) | STABLE_STATIC |
| **PP0** | `PP0_TEAR_CHECK_EN`| `0x00971000`| `0x00000001` (Linux) | `0x00000000` (First scanout) | 0=No TE wait (immediate), 1=Wait for TE | STABLE_STATIC |
| **PP0** | `PP0_SYNC_CONFIG_VSYNC`| `0x00971004`| `0x00080000` | `0x00000000` | Vsync clock sync configuration | STABLE_STATIC |
| **DSI0** | `DSI_CMD_MDP_CTRL` | `0x00994040` | `0x00000008` | `0x00000008` | Bits [3:0]=8 (RGB888 stream destination) | STABLE_STATIC |
| **DSI0** | `DSI_CMD_DCS_CTRL` | `0x00994044` | `0x00013c2c` | `0x00013c2c` | Bit 16=1 (Insert DCS), [15:8]=0x3c, [7:0]=0x2c | STABLE_STATIC |
| **DSI0** | `DSI_STREAM0_CTRL` | `0x00994058` | `0x0ca90039` | `0x0ca90039` | Stride=3241 (`0x0ca9`), VC=0, DataType=`0x39` | STABLE_STATIC |
| **DSI0** | `DSI_STREAM0_TOTAL`| `0x0099405c` | `0x07800438` | `0x07800438` | Total lines=1920 (`0x780`), pixels=1080 (`0x438`)| STABLE_STATIC |
| **DSI0** | `DSI_CTRL` | `0x00994004` | `0x000001f5` | `0x000001f5` | Controller enabled, 4 lanes + clk lane enabled | STABLE_STATIC |

---

## 4. Architectural Analysis: Why Linux Values are Deterministic

1. **Geometry Consistency:**
   Every geometry register across SSPP, LM0, and DSI is identical:
   `0x07800438` = `(1920 << 16) | 1080`.
   This demonstrates a perfectly matched 1:1 scanout pipeline with zero spatial scaling or clipping.

2. **DSI DCS Command Encapsulation:**
   The DSI controller's MDP stream engine wraps each line into a MIPI DCS Long Write packet:
   - Packet Data Type: `0x39` (`DCS Long Write`)
   - Word Count: `1080 pixels * 3 bytes/pixel = 3240 bytes` + 1 byte DCS opcode = `3241 bytes` (`0x0CA9`).
   - Frame Start DCS: `0x2C` (`WRITE_MEMORY_START`)
   - Frame Continuation DCS: `0x3C` (`WRITE_MEMORY_CONTINUE`)
   The DSI controller hardware automatically substitutes `0x2C` on the first line and `0x3C` on all subsequent lines of the frame.

3. **Tear-Check Decoupling:**
   In Linux, `PP0_TEAR_CHECK_EN` is enabled (`0x1`) to avoid display tearing under interactive userland UI rendering. For initial XNU bring-up, disabling tear-check (`0x0`) ensures that frame transmission begins immediately without requiring external synchronization pulses from the panel controller, eliminating hardware deadlock risks.
