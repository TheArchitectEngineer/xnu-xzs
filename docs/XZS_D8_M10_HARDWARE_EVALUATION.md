# Sony Xperia XZs (MSM8996) — D8-M10 Hardware Evaluation & Closure Report

**Target Device**: Sony Xperia XZs (`G8231`, `keyaki`, Serial: `BH905SX976`)  
**SoC**: Qualcomm Snapdragon 820 (`MSM8996 Pro`, ARMv8.0-A)  
**Display Hardware**: Sharp + Synaptics 1080x1920 Command Mode In-Cell IPS LCD (`somc,sharp_synaptics_cmd_9_panel`, DDIC ID `84 72 09`)  
**Date**: October 2026  
**Status**: **COMPLETE & SEALED ON HARDWARE** (`LCD_PIXEL_VISIBLE=YES`, `TEST_PATTERN_OR_CONSOLE_VISIBLE=YES`, `PP_LINE_MAX=0x00000780`, `280+` frames scanned out per run, `0` DSI/DDIC errors, `PAC=0` verified)

---

## 1. Executive Summary

Milestone **D8-M10** (Early Boot Framebuffer Console & Interactive Shell Display) is **COMPLETE and SEALED** on physical Sony Xperia XZs silicon (`BH905SX976`) strictly via non-destructive RAM boot (`fastboot boot artifacts/builds/xzs-xnu-boot.img`).

Across consecutive back-to-back RAM-boot runs (`artifacts/hw/display-regression-bisect/p9-glyph-fix-run1/` and `p9-glyph-fix-run2/`):
1. **Physical LCD Display Verified**:
   - **Top Viewport (Rows 0..38, Lines 0..623)**: Displays the live, upright XNU kernel boot log and interactive shell (`xzs#`) at `135x120` character resolution (`8x16` ISO font).
   - **Divider Bar (Row 39, Lines 624..639)**: Displays the status separator banner.
   - **Bottom Viewport (Rows 40..119, Lines 640..1919)**: Displays 8 vertical color bars (`White`, `Yellow`, `Cyan`, `Green`, `Magenta`, `Red`, `Blue`, `Dark Slate`) with a white border and centered black/white checkerboard test badge.
2. **Hardware Scanout & DDIC BTA Telemetry Verified**:
   - `READ_0x04_RAW = 84 72 09` (`VALID=YES`)
   - `XNU_DDIC_0x05 = 0x00` (Zero DSI protocol/ECC/CRC errors)
   - `XNU_DDIC_0x0A = 0x1c` (`SLPOUT=1 | NORON=1 | DISPON=1`)
   - `XNU_DDIC_0x0C = 0x77` (`24bpp RGB888`)
   - `XNU_DDIC_0x0E = 0x80` (`TEON`)
   - `XNU_DDIC_0x0F = 0x40` (`RDDSDR = NORMAL`)
   - `PP_LINE_MAX = 0x00000780` (`1920` lines), `PP0_DONE = YES`, `CMD_MDP_DONE = YES`
   - `CTL_START_COUNT = 280`, `MDP_KICKOFF_COUNT = 280`, `FRAMEBUFFER_SCANOUT_COUNT = 280`
   - `DSI_ACK_ERR = 0x00000000`, `DSI_TIMEOUT = 0x00000000`
3. **Interactive Shell & Line-Rate Scrolling**:
   - Verified `fb`, `help`, `pwd`, `echo hello`, `ls /`, backspace editing, tab completion, and `>120` lines of continuous terminal scrolling without crash, tearing, or FIFO overflow.
   - Clean return to `fastboot` mode after every run.

---

## 2. Root Causes Identified & Resolved

By disassembling both the stock Sony Little Kernel bootloader (`artifacts/firmware/stock/aboot.img`) and the working TWRP recovery kernel (`artifacts/builds/twrp-kernel.bin`), we identified and fixed all 6 hardware/software root causes that previously left the panel in a backlight-only state:

1. **True-Cold DDIC Wakeup After `aboot` `msm_display_off()` (`src/xnu/pexpert/arm/xzs_d8m8.h`)**:
   - Sony `aboot` calls `msm_display_off()` (`0xaa01cb88`) before booting the kernel, sending `DISPOFF (0x28)` + `SLPIN (0x10)` and pulling `GPIO8` (`DISP_RESET_N`) LOW.
   - Fixed `xzs_d8m8_panel_prepare()` to execute `SLPOUT (0x11)` + 120ms delay first, then pulse `GPIO89` (`TOUCH_RESET_N`) LOW 2ms -> HIGH 10ms per `somc,ewu-rst-seq`, followed by authentic Panel 9 commands (`B0 04`, `D6 01`, `C6 59`, `TEON 35 00`, `DISPON 29` + 20ms), removing foreign non-Panel-9 commands.

2. **PMI8994 `LAB` (`0xDE00`) & `IBB` (`0xDC00`) Security Unlock & Current Limits (`src/xnu/pexpert/arm/xzs_d8p2.h`)**:
   - Unlocked `SEC_ACCESS` (`0xDED0 = 0xA5`, `0xDCD0 = 0xA5`) before writing protected registers and programmed Panel 9 current limits and power-up controls (`0xDE4B = 0x88`, `0xDE49 = 0x80`, `0xDC4B = 0x83`, `0xDC58 = 0xC7`).
   - Verified `LAB STATUS1 = 0xA0` and `IBB STATUS1 = 0x80` (`VREG_OK = 1`, zero overload/short-circuit fault bits).

3. **`MDP v1.7` (`0x1007`) `SSPP RGB0` 8-Bit BPC Format & Pixel Extension (`src/xnu/pexpert/arm/xzs_d8m8.h`)**:
   - Corrected `RGB0_SRC_FORMAT` (`0x00915030`) from `0x000236aa` (6-bit BPC `2,2,2,2`) to **`0x000236ff`** (8-bit BPC `3,3,3,3` for `XRGB8888`, matching `twrp-kernel.bin` `mdss_mdp_format_setup` at `0x480090..0x48013c`).
   - Programmed `RGB0_SRC_OP_MODE` (`0x00915038 = 0x80000000`, `MDSS_MDP_OP_PE_OVERRIDE`), `RGB0_FETCH_CONFIG` (`0x00915048 = 0x00080087`), `RGB0_FIFO_WM0..2` (`0x00915050..0x58 = 0x0000ffff`), `RGB0_DECIMATION` (`0x00915070 = 0x00000000`), `RGB0_EXT_134` (`0x00915134 = 0x00000009`), and `RGB0_EXT_138` (`0x00915138 = 0x80000000`).

4. **`CTL_FLUSH` (`0x00902018`) `INTF_1` (`BIT(30)`) & `DSPP_0` (`BIT(13)`) Commit (`src/xnu/pexpert/arm/xzs_d8m8.h`)**:
   - Updated `CTL_FLUSH` from `0x00020048` to **`0x40022048`** (`BIT(30) INTF_1 | BIT(17) CTL_0 | BIT(13) DSPP_0 | BIT(6) LM_0 | BIT(3) RGB_0`), matching `aboot.img` `mdp_get_ctl_flush_bits` (`0xaa01c884`) and `mdp_dsi_cmd_on` (`0xaa01e7c0`) for MSM8996 (`MDP_HW_VERSION = 0x10070002`).

5. **Synchronized Multi-Frame Command-Mode Kickoff (`src/xnu/pexpert/arm/xzs_d8m8.h`)**:
   - Implemented `xzs_d8m8_wait_frame_idle()` and `xzs_d8m8_kick_frame_ex()` so `fbcon` waits for `PP0_DONE` (`MDP_INTR_STATUS BIT(8)`) and `DSI_STATUS` idle before clearing status and issuing the next `CTL_FLUSH` + `CTL_START`, preventing mid-frame DSI FIFO overflow across 280+ frames.

6. **LSB-First Glyph Rendering for XNU `iso_font` (`src/xnu/pexpert/arm/xzs_fbcon.h`)**:
   - Fixed `xzs_fbcon_draw_glyph_raw()` to render bits `0x01` (leftmost pixel `line_ptr[0]`) through `0x80` (rightmost pixel `line_ptr[7]`), matching `osfmk/console/video_console.c` (`vc_render_char`).

---

## 3. Display Rendering Architecture: CPU vs. GPU / Hardware Display Engine

1. **Pixel Rendering into Framebuffer Memory (`PA = 0x98000000`) — CPU**:
   - In early boot and `xzs_fbcon`, the **ARM64 Kryo CPU** rasterizes 8x16 text glyphs and the 32-bit `XRGB8888` test pattern directly into the physical framebuffer carveout at `0x98000000`.
   - Under the `XZSAppleCompat` iOS architecture (`XZSFramebuffer` / `IOMobileFramebuffer` + `XZSSurface` / `IOSurface`), Apple's userspace (`backboardd`, `RenderServer`, `SpringBoard`) likewise performs **CPU software rasterization** into shared `IOSurface` buffers. The Adreno 530 GPU is not required for bring-up or SpringBoard UI rendering.
2. **Frame Scanout, Mixing & MIPI DSI Transport — Hardware Display Engine (MDSS MDP5 + DSI0)**:
   - Once pixels are in RAM at `0x98000000`, **zero CPU cycles** are spent copying pixels to the panel:
     - **MDP5 `SSPP RGB0` DMA (`0x00915000`)** fetches pixel lines over the `MMAGIC` AXI bus.
     - **MDP5 `Layer Mixer 0` (`0x00945000`)** composites the `1080x1920` frame in hardware.
     - **MDP5 `PingPong 0` (`0x00971000`)** synchronizes frame release to the physical `60Hz` `TE` signal on `GPIO10`.
     - **DSI0 Host & 14nm PHY (`0x00994000`)** packetizes the pixel stream into High-Speed MIPI DCS `0x2C`/`0x3C` bursts across 4 data lanes directly into the Sharp/Synaptics DDIC GRAM.
