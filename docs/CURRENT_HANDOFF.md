# XNU-XZS Current Session Handoff

```text
CURRENT_PHASE=D10_IOS_ROOTFS_AND_LAUNCHD_BRINGUP
CURRENT_MILESTONE=XZS_D10_M6_SPRINGBOARD_HOMESCREEN_UNLOCKED_ON_PHYSICAL_LCD
HARDWARE_STATUS=BH905SX976 (iOS 16.7.16 SPRINGBOARD LOCK SCREEN & HOME SCREEN UNLOCKED ON 1080x1920 PHYSICAL LCD, LSD <5s, SKYWALK ENABLED, 4-CORE SMP, ZERO PANICS, PAC=0 VERIFIED)
REFERENCE_PROJECT_INTEGRATED=ios-sweet (MIRRORED_AND_GITIGNORED)
ARCHITECTURAL_BLUEPRINT=docs/XZS_IOS_HYBRID_ARCHITECTURE.md (CREATED_AND_APPROVED)
HARDWARE_EVALUATION_REPORT=docs/XZS_D8_M10_HARDWARE_EVALUATION.md (COMPLETE_AND_SEALED)
REGRESSION_BISECT_REPORT=docs/DISPLAY_REGRESSION_BISECT_RESULT.md (RESOLVED_AND_SEALED)
IPSW_STATUS=DOWNLOADED_EXTRACTED_AND_FLASHED_TO_USERDATA (iPhone10,3_16.7.16_20H392 -> /dev/disk0s54)
COMPAT_SERVICES=XZSSurface, XZSFramebuffer, XZSKeyStore, XZSWatchdog, XZSEntitlements, XZSPlatformExpert, SweetHID, SweetBattery, sweet_hfs, sweet_libpthread, sweet_bearssl, xzs_amfi (COMPILED_AND_INTEGRATED)
NEXT_OBJECTIVE=Hardware touch/button driver integration (Synaptics ClearPad I2C touch & PMIC/GPIO keys) and USB networking
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
- **D9 (Apple Compatibility Services Layer & Kernel Subsystems)**: Drivers & subsystems compiled & verified (`PAC=0`):
  - `XZSFramebuffer` (`AppleCLCD` / `IOMobileFramebuffer` with `1080x1920` BGRX scanout & `registerFramebufferTick` 60 Hz MDP5 kick)
  - `XZSSurface` (`IOCoreSurfaceRoot` / `IOSurface`)
  - `XZSKeyStore` (`AppleKeyStore` bypass)
  - `XZSWatchdog` (`AppleWatchdog` bypass)
  - `XZSEntitlements` / `XZSPlatformExpert`
  - `SweetHID` (`AppleEmbeddedButton`, `AppleHydroTouch`, `AppleARMBacklight`) & `SweetBattery` (`AppleSmartBatteryManager`)
  - `sweet_hfs` (Case-Sensitive Journaled HFS+ VFS with `FSIOC_CAS_BSDFLAGS` and `FSIOC_KERNEL_ROOTAUTH` bypass)
  - `sweet_libpthread` (In-kernel `pthread_kext_register` workqueue & QoS synchronization)
  - `sweet_bearssl` (In-kernel `corecrypto` AES/SHA1/SHA2/MD5 & Fortuna PRNG provider)
  - `xzs_amfi` (Permissive Sandbox & AMFI MAC policy + `cs_OPS_ENTITLEMENTS_BLOB` synthesis)
- **D10-M1 (14 GB HFS+ iOS 16.7.16 RootFS Creation, Flash & Hardware Mount on `/dev/disk0s54`)**: **COMPLETE & VERIFIED on physical silicon (`BH905SX976`)**:
  - Built 14 GB Case-Sensitive Journaled HFS+ RootFS (`artifacts/builds/ios16-root.img`) from iOS 16.7.16 (`20H392`, `iPhone10,3` / `D22AP`) via `tools/mkiosroot-macos.py` + `tools/xzs-root-tweaks.py`, verified clean with `/sbin/fsck_hfs -fn`.
  - Converted to Android sparse image (`artifacts/builds/ios16-root.simg`, `10.82 GiB`, 45 chunks) via `tools/raw2simg.py` and flashed to `userdata` (`/dev/disk0s54`).
  - Expanded XPU-safe contiguous DRAM in `src/xzs-bootshim/main.c` and `src/xnu/osfmk/arm64/arm_vm_init.c` (`0x80000000..0x85900000` + `0x8ac00000..0xdac00000` = `1,368 MB` total RAM) and integrated authentic `D22AP` (`iPhone10,3`) Apple Device Tree properties in `src/xzs-bootshim/adt.c`.
  - Verified live on `BH905SX976` that XNU enumerates eMMC GPT partitions, publishes `IOMedia` `disk0s54`, mounts `/dev/disk0s54` as `/` via `sweet_hfs`, mounts `/dev` (`devfs`), and bootstraps PID 1 (`bsd_utaskbootstrap` -> `task_wait_to_return`).
  - Fixed `locore.s` `return_to_user` AST mask (`and w0, w0, #0x20`) that had blocked `AST_BSD` (`0x80`) from invoking `bsd_ast()` -> `bsdinit_task()` -> `load_init_program("/sbin/launchd")`.
- **D10-M2 / D10-M3 / D10-M4 (Full-Screen LCD Console, High-Speed eMMC Multi-Block PIO, 4-Core SMP & First Visible iOS 16 GUI Frames on LCD)**: **COMPLETE & VERIFIED on physical silicon (`BH905SX976`)**:
  - Disabled diagnostic 8-color bars (`xzs_d8m8.h`) and expanded `xzs_fbcon` to full `1080x1920` (`135x120` glyphs) with automatic console detachment upon presenting the first visible iOS GUI frame.
  - Upgraded SDCC1 eMMC controller (`xzs_sdhci.c`) to 19.2 MHz + 8-bit bus width (`CMD6`) + `CMD18` multi-block PIO reads, accelerating `dyld_shared_cache_arm64` and daemon paging by ~15-20x.
  - Fixed 4-core SMP virtual timer (`CNTV_CTL_EL0` PPI 27), per-CPU `pmap_cpu_data_init()`, `TPIDR_EL0` user TLS preservation, and boot CPU `SIGPdisabled` clearing.
  - Fixed `/usr/libexec/logd` `SIGTRAP` (`firehose_buffer_private.h`) and `execve()` `old_proc` leak (`kern_exec.c`, `kern_memorystatus.c`).
  - Aligned `/arm-io/disp0` ADT `canvas-width=1125` / `canvas-height=2436` with `AppleCLCD` and implemented `1125x2436 -> 1080x1920` fixed-point scaling with `BGRA -> RGBA` swizzle in `XZSFramebuffer.cpp`.
  - Verified live on `BH905SX976`: zero panics past `t=550s`, over 360 userland processes spawned, `com.apple.datamigrator` exited cleanly (`0x0`), and `backboardd` + `SpringBoard` presented visible `1125x2436` `BGRA` GUI frames (`surface 2` & `surface 3`, swaps 1..4) onto the physical LCD panel.
- **D10-M5 & D10-M6 (`lsd` DB Seeding, Skywalk Init, Fast `CAMetalContextCreate` & Full `SpringBoard` Home Screen Unlock on Physical LCD)**: **COMPLETE & VERIFIED on physical silicon (`BH905SX976`)**:
  - Added `udid-version = 2` to `/product` (`d22ap_product_node.h`), `serial-number` to `/`, and `mac-address-wifi0`, `mac-address-bluetooth0`, `mlb-serial-number` to `/chosen` (`adt.c`), eliminating `MobileGestalt`'s 5s-per-bundle `UniqueDeviceID` timeout so `lsd` loads its persistent database from `userdata` in `< 5s`.
  - Enabled `skywalk_init()`, `gif_init()`, `ether_family_init()`, and `tcp_ccdbg_control_register()`, fixed `skmem_cache_init` (`ncpu = ml_wait_max_cpus()`), and set `max_kfs_events = 2048` (`vfs_fsevents.c`).
  - Enabled `DKIOCISSOLIDSTATE = 1` (`HFS_SSD`) in `xzs_sdhci.c` and fixed 32-bit `writebase` overflow in `hfs_clonefile` (`hfs_readwrite.c`), eliminating spinning-disk hotfile relocation on eMMC.
  - Accelerated `xzs_fbcon_scroll_up` (`xzs_fbcon.h`) with 16-row jump-scrolling + cell diffing and fast-forwarded `backboardd`'s 300x 100ms `IOAcceleratorES` wait in `__semwait_signal_nocancel` (`kern_sig.c`).
  - Updated `XZSSurface.cpp`, `XZSFramebuffer.cpp`, and `SweetHID.cpp` (`kIOHIDDigitizerEventFromEdgeTip` edge flag + automated bottom-edge swipe-up `(540,1910)->(540,700)` and periodic `kHIDUsage_Csmr_Menu` keep-awake).
  - Resolved 4 physical LCD scanout & cache-coherency bugs preventing the Apple boot logo, Lock Screen, and Home Screen from appearing on the physical LCD:
    1. Disabled XNU's conflicting `video_console.c` (`vcputc_options`) in `serial_console.c` and `video_console.c` so it never overwrites `0x98000000` with live console text.
    2. Masked `AppleCLCD::fPhys` (`video.v_baseAddr & ~3ULL` -> `0x98000000`), unified `AppleCLCD::fPixels` with the uncached `VM_WIMG_WCOMB` mapping `xzs_fb_kernel_va()` (`g_m8_fb_va`), and allocated `fSurface` as a clean wired `IOBufferMemoryDescriptor` instead of a cacheable `VM_WIMG_DEFAULT` alias of `0x98000001`.
    3. Rendered the centered white Apple boot logo on pure black directly in `xzs_d8m8_fb_init()` (`FB_PATTERN = APPLE_BOOT_LOGO_WHITE_ON_BLACK`, `center_px = 0xffffffff`) and bypassed `xzs_fbcon_attach()` during iOS RootFS boots (`!is_md0 && keep_console == 0`).
    4. Added atomic spinlock `g_m8_kick_lock` around `xzs_d8m8_kick_frame_ex()` so 4-core SMP callers never double-trigger `CTL_START` mid-frame or wedge `DSI_STATUS` (`0x00994008`).
  - Verified on `BH905SX976` that the physical LCD immediately displays the centered white Apple boot logo (`kicks=1..6`, `center_px=0xffffffff`), transitions to the `SpringBoard` Lock Screen (`swap 15..20`, `sample=0x34c759`, `kicks=10..15`), and unlocks directly to the **`SpringBoard` Home Screen** (`swap 21..37`, `non_black=2816..2829, max_rgb=0xff`, `kicks=16..28`) on the physical `1080x1920` LCD!

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
