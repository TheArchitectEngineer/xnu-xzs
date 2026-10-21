# XNU-XZS Current Session Handoff

```text
CURRENT_PHASE=D10_IOS_ROOTFS_AND_LAUNCHD_BRINGUP
CURRENT_MILESTONE=XZS_D8_M10_COMPLETE_AND_SEALED_ON_HARDWARE
HARDWARE_STATUS=BH905SX976 (1080x1920 PHYSICAL LCD ACTIVE: CONSOLE LOG + 8 COLOR BARS VISIBLE, WLED 50%, USB CDC CONSOLE OK, PAC=0 VERIFIED)
REFERENCE_PROJECT_INTEGRATED=ios-sweet (MIRRORED_AND_GITIGNORED)
ARCHITECTURAL_BLUEPRINT=docs/XZS_IOS_HYBRID_ARCHITECTURE.md (CREATED_AND_APPROVED)
HARDWARE_EVALUATION_REPORT=docs/XZS_D8_M10_HARDWARE_EVALUATION.md (COMPLETE_AND_SEALED)
REGRESSION_BISECT_REPORT=docs/DISPLAY_REGRESSION_BISECT_RESULT.md (RESOLVED_AND_SEALED)
IPSW_STATUS=DOWNLOADED_AND_EXTRACTED (iPhone10,3_16.7.16_20H392)
COMPAT_SERVICES=XZSSurface, XZSFramebuffer, XZSKeyStore, XZSWatchdog, XZSEntitlements, XZSPlatformExpert, xzs_amfi (COMPILED_AND_INTEGRATED)
NEXT_OBJECTIVE=Generate HFS+ userdata rootfs from extracted DMGs and boot to iOS launchd
```

---

## 1. Executive Status & Active Handoff (October 2026)

### 1.1 Proven & Sealed Milestones
- **D7-T1 (USB Console Transport)**: COMPLETE & SEALED, tag `xzs-d7t1-complete`.
- **D7-T2 (Native Generic Mach-O Execution)**: COMPLETE & SEALED, tag `xzs-d7t2-full-complete`. DEBT-001 RESOLVED.
- **D8-M1..M6, D8-P1, D8-P2**: COMPLETE & SEALED on physical silicon (`BH905SX976`).
- **D8-M8 / D8-M8.5 / D8-M10 (Physical LCD Framebuffer Console & Interactive Shell Display)**: **COMPLETE & SEALED on physical silicon (`BH905SX976`)**.
  - Full 1080x1920 Sharp + Synaptics Panel 9 (`somc,sharp_synaptics_cmd_9_panel`) physical pixel scanout verified across consecutive cold RAM-boots (`p9-glyph-fix-run1`, `p9-glyph-fix-run2`).
  - Top viewport (rows 0..38) renders live upright XNU kernel log and interactive shell (`xzs#`); bottom viewport (rows 40..119) renders 8 color bars + contrasting checkerboard badge.
  - All 6 root causes resolved via binary disassembly of `aboot.img` and `twrp-kernel.bin`:
    1. True-cold `SLPOUT (0x11)` + 120ms wakeup sequence after `msm_display_off()` GPIO8 reset, followed by `GPIO89` touch reset pulse and Panel 9 post-on commands (`B0 04`, `D6 01`, `C6 59`, `35 00`, `29`).
    2. PMI8994 `LAB` (`0xDE00`) & `IBB` (`0xDC00`) `SEC_ACCESS` (`0xA5`) unlock + current limit & power-up control (`0xDE4B=0x88`, `0xDE49=0x80`, `0xDC4B=0x83`, `0xDC58=0xC7`).
    3. `MDP v1.7` (`0x1007`) `SSPP RGB0` 8-bit BPC format (`RGB0_SRC_FORMAT = 0x000236ff` instead of 6-bit `0x000236aa`) plus `RGB0_FETCH_CONFIG = 0x00080087`, `RGB0_FIFO_WM0..2 = 0x0000ffff`, `RGB0_EXT_134 = 0x00000009`, `RGB0_EXT_138 = 0x80000000`, `RGB0_SRC_OP_MODE = 0x80000000` (`PE_OVERRIDE`).
    4. `CTL_FLUSH = 0x40022048` (`BIT(30) INTF_1 | BIT(17) CTL_0 | BIT(13) DSPP_0/PP_0 | BIT(6) LM_0 | BIT(3) RGB_0`).
    5. Synchronized `xzs_d8m8_kick_frame_ex()` waiting for `PP0_DONE` + `DSI_STATUS` idle before re-triggering `CTL_START`.
    6. `iso_font` LSB-first glyph rendering (`0x01..0x80`) in `xzs_fbcon_draw_glyph_raw()`.
- **D9 (Apple Compatibility Services Layer)**: Drivers compiled & verified without PAC violations:
  - `XZSFramebuffer` (`AppleCLCD` / `IOMobileFramebuffer`)
  - `XZSSurface` (`IOCoreSurfaceRoot` / `IOSurface`)
  - `XZSKeyStore` (`AppleKeyStore` bypass)
  - `XZSWatchdog` (`AppleWatchdog` bypass)
  - `XZSEntitlements` / `XZSPlatformExpert`
  - `xzs_amfi` (permissive sandbox MAC policy)

### 1.2 Display Pipeline Architecture (CPU vs. GPU / Hardware Display Engine)
- **Pixel Rendering (vẽ vào RAM Framebuffer `0x98000000`)**: Hiện thực hiện bằng **CPU** (nhân ARM64 Kryo ghi font chữ 8x16 và dải màu 32-bit `XRGB8888` vào vùng nhớ Framebuffer; ở tầng iOS `backboardd` / `SpringBoard` / `RenderServer` theo kiến trúc `ios-sweet` cũng sử dụng CPU software rasterization qua `IOSurface` + `IOMobileFramebuffer`). GPU Adreno 530 chưa cần dùng ở giai đoạn này.
- **Frame Scanout & Transport (đọc RAM và đẩy ra tấm nền LCD)**: Thực hiện hoàn toàn bằng **phần cứng chuyên dụng Qualcomm MDSS MDP5 v1.7 + DSI0 Host + 14nm PHY**:
  - DMA phần cứng `SSPP RGB0` (`0x00915000`) tự động đọc vùng nhớ `0x98000000` qua bus AXI (`MMAGIC`).
  - `Layer Mixer 0` (`LM0`, `0x00945000`) trộn lớp ở độ phân giải gốc `1080x1920`.
  - `PingPong 0` (`PP0`, `0x00971000`) đồng bộ phần cứng với xung `TE` vật lý (`GPIO10` @ 60Hz) từ IC Sharp/Synaptics.
  - `DSI0 Host` (`0x00994000`) đóng gói luồng DCS `0x2C`/`0x3C` tốc độ cao qua 4 lane MIPI DSI xuống GRAM của màn hình.

---

## 2. Active Technical Road & Execution Sequence

```text
Step 1: Seal D8-M10 physical display completion on 'main'. (DONE)
Step 2: Run 'tools/mkiosroot.sh' and 'tools/xzs-root-tweaks.py' on extracted iOS 16.7.16 DMGs.
Step 3: Deploy HFS+ root image and boot authentic iOS userland (PID 1 /sbin/launchd -> backboardd -> SpringBoard).
```

---

## 3. Reference Files & Documentation Index
- Master Architectural Blueprint: [`docs/XZS_IOS_HYBRID_ARCHITECTURE.md`](XZS_IOS_HYBRID_ARCHITECTURE.md)
- Hardware Evaluation Report: [`docs/XZS_D8_M10_HARDWARE_EVALUATION.md`](XZS_D8_M10_HARDWARE_EVALUATION.md)
- Display Bisect & Root-Cause Resolution: [`docs/DISPLAY_REGRESSION_BISECT_RESULT.md`](DISPLAY_REGRESSION_BISECT_RESULT.md)
- Main Port Status: [`docs/XZS_PORT_STATUS.md`](XZS_PORT_STATUS.md)
- Roadmap: [`docs/XZS_ROADMAP.md`](XZS_ROADMAP.md)
- Root Guide: [`README.md`](../README.md)
