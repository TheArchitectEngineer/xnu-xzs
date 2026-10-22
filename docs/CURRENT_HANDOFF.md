# XNU-XZS Current Session Handoff

```text
CURRENT_PHASE=D13_GPU_ADRENO530_IOSURFACEACCELERATOR_AND_IOACCELERATOR_METAL_BRIDGE
CURRENT_MILESTONE=XZS_D13_GPU_ADRENO530_M2SCALER_SHAREDEVENT_AND_SWAR_COMPOSITOR
ACTIVE_BRANCH=xzs-d13-gpu-adreno-metal
HARDWARE_STATUS=BH905SX976 (iOS 16.7.16 LOCK SCREEN, HOME SCREEN, WALLPAPERS, 125HZ RMI4 MULTI-TOUCH, SYSTEM GESTURES, PHYSICAL BUTTONS, WLED SLEEP/WAKE, ADRENO 530 GPU BRING-UP, APPLEM2SCALERCSCDRIVER, IOSURFACESHAREDEVENT, XZSACCELERATOR & 60HZ 64-BIT SWAR COMPOSITOR)
ARCHITECTURAL_BLUEPRINT=docs/XZS_IOS_HYBRID_ARCHITECTURE.md (COMPLETE_AND_VERIFIED)
GPU_ARCHITECTURAL_BLUEPRINT=docs/XZS_D13_GPU_ADRENO_METAL_ARCHITECTURE.md (COMPLETE_AND_VERIFIED)
HARDWARE_EVALUATION_REPORT=docs/XZS_D8_M10_HARDWARE_EVALUATION.md (COMPLETE_AND_SEALED)
REGRESSION_BISECT_REPORT=docs/DISPLAY_REGRESSION_BISECT_RESULT.md (RESOLVED_AND_SEALED)
IPSW_STATUS=FLASHED_AND_MOUNTED_READ_WRITE (iPhone10,3_16.7.16_20H392 -> /dev/disk0s54)
COMPAT_SERVICES=XZSSurface, XZSFramebuffer, XZSGPU (AppleM2ScalerCSCDriver + XZSAccelerator), XZSKeyStore, XZSWatchdog, XZSEntitlements, XZSPlatformExpert, SweetHID (SweetHIDTouch + SweetHIDButtons), SweetBattery, sweet_hfs, sweet_libpthread, sweet_bearssl, xzs_amfi (COMPILED_AND_VERIFIED_PAC0)
```

---

## 1. Executive Status & Active Handoff (October 2026)

### 1.1 Proven & Sealed Hardware Milestones
- **Phases A, B, C & D1..D7**: **COMPLETE & SEALED** on physical silicon (`BH905SX976`):
  - Native EL1 XNU 24.0.0 entry (`xzs-bootshim`), 16 KB page tables, High KVA (`0xfffffe0000000000`), BLSP2 UARTDM, GICv3 Distributor + 4 Redistributors, 4-core Kryo SMP (`PSCI CPU_ON`), SDCC1 eMMC 5.1 driver (55 GPT slices `/dev/disk0s1..s55`), DWC3 USB console (`xzs-d7t1-complete`), and native Mach-O execution (`xzs-d7t2-full-complete`).
- **Phase D8 (D8-M1..M10: Physical 1080x1920 MIPI DSI Command-Mode Display & Boot Console)**: **COMPLETE & SEALED on physical silicon (`BH905SX976`)**:
  - Full `1080x1920` Sharp/Synaptics Panel 9 (`somc,sharp_synaptics_cmd_9_panel`) scanout over Qualcomm MDSS MDP5 v1.7 (`CTL0` + `SSPP RGB0` + `LM0` + `PP0`) and 4-lane DSI0 Command Mode (`0x00994000`), with PMI8996 `LAB` (`0xDE00`) / `IBB` (`0xDC00`) `±5.6V` bias rails and `WLED` (`0xD800`) backlight.
  - Verbose live `1080x1920` boot console (`xzs_fbcon` with 16-row jump scrolling) + custom `"ĐU MA MAY"` vector splash banner (`xzs_draw_du_ma_may_banner`), automatically detaching upon the first visible iOS 16 GUI surface swap.
- **Phase D9 (`XZSAppleCompat` Kernel Services & Subsystems)**: **COMPLETE & VERIFIED (`PAC=0`)**:
  - [`XZSFramebuffer`](file:///Users/lechaukha12/Desktop/xnu-xzs/src/xnu/iokit/Kernel/arm/XZSFramebuffer.cpp) (`AppleCLCD` / `IOMobileFramebuffer` with 60 Hz 4-core parallel presenter & 1:1 aspect ratio LUT)
  - [`XZSSurface`](file:///Users/lechaukha12/Desktop/xnu-xzs/src/xnu/iokit/Kernel/arm/XZSSurface.cpp) (`IOCoreSurfaceRoot` / multi-plane `IOSurface` management)
  - [`XZSKeyStore`](file:///Users/lechaukha12/Desktop/xnu-xzs/src/xnu/iokit/Kernel/arm/XZSKeyStore.cpp) (`AppleKeyStore` & `AppleSEPManager` compatibility)
  - [`XZSWatchdog`](file:///Users/lechaukha12/Desktop/xnu-xzs/src/xnu/iokit/Kernel/arm/XZSWatchdog.cpp), [`XZSEntitlements`](file:///Users/lechaukha12/Desktop/xnu-xzs/src/xnu/iokit/Kernel/arm/XZSEntitlements.cpp), [`XZSPlatformExpert`](file:///Users/lechaukha12/Desktop/xnu-xzs/src/xnu/iokit/Kernel/arm/XZSPlatformExpert.cpp)
  - [`SweetHID`](file:///Users/lechaukha12/Desktop/xnu-xzs/src/xnu/iokit/Kernel/arm/hid/SweetHID.cpp) (`SweetHIDTouch` Synaptics RMI4 F12 125 Hz multi-touch + `SweetHIDButtons` PM8996 PON & TLMM GPIO keys + `AppleARMBacklight`)
  - [`SweetBattery`](file:///Users/lechaukha12/Desktop/xnu-xzs/src/xnu/iokit/Kernel/arm/SweetBattery.cpp) (`AppleSmartBatteryManager`)
  - [`sweet_hfs`](file:///Users/lechaukha12/Desktop/xnu-xzs/src/xnu/bsd/sweet_hfs/core/hfs_vfsops.c) (Case-Sensitive Journaled HFS+ VFS with `FSIOC_CAS_BSDFLAGS`, `FSIOC_KERNEL_ROOTAUTH`, 64-bit `hfs_clonefile`, and `ATTR_VOL_QUOTA_SIZE | ATTR_VOL_RESERVED_SIZE`)
  - [`sweet_libpthread`](file:///Users/lechaukha12/Desktop/xnu-xzs/src/xnu/bsd/sweet_pthread/pthread_kext.c) & [`sweet_bearssl`](file:///Users/lechaukha12/Desktop/xnu-xzs/src/xnu/osfmk/sweet_bearssl/sweet_corecrypto.c)
  - [`xzs_amfi`](file:///Users/lechaukha12/Desktop/xnu-xzs/src/xnu/bsd/kern/xzs_amfi.c) (`"Sandbox"` and `"AMFI"` MAC policies, syscalls `0x5a` & `0x60`, container path resolution `call == 4`, and entitlement synthesis)
- **Phase D10 (14 GB HFS+ iOS 16.7.16 RootFS, `launchd`, `backboardd`, `SpringBoard` & `PosterBoard`)**: **COMPLETE & VERIFIED on physical silicon (`BH905SX976`)**:
  - Mounted `/dev/disk0s54` read-write as `/` via `sweet_hfs` over 19.2 MHz 8-bit SDCC1 eMMC (`CMD18` multi-block PIO reads, `DKIOCISSOLIDSTATE = 1`).
  - Full `D22AP` (`iPhone10,3`) ADT identity (`udid-version = 2`, `display-scale = 3`, `home-button-type = 2`, `canvas-width = 1125`, `canvas-height = 2436`) allowing `lsd` to load its database in `< 5s`.
  - Enabled `skywalk_init()`, `gif_init()`, `ether_family_init()`, and `tcp_ccdbg_control_register()`.
  - Full `SpringBoard` Lock Screen, swipe-to-unlock, two-page Home Screen, `PosterBoard` + procedural iOS 16 wallpaper, Control Center, Spotlight, and App Switcher live on the physical `1080x1920` LCD.
- **Phase D11 (Synaptics RMI4 125 Hz Multi-Touch, Edge Gestures & Physical Buttons)**: **COMPLETE & VERIFIED on physical silicon (`BH905SX976`)**:
  - **Multi-Touch (`SweetHIDTouch`)**: Bit-banged I2C on `GPIO 87` (SDA) / `GPIO 88` (SCL) (`BLSP12 QUP5`) reading Synaptics RMI4 Function `$12` (`F12_2D_DATA01`) at 125 Hz (`8 ms`), with strict `type == 1 || type == 3` finger filtering, 2.5D curved-bezel grip rejection (`x < 12 || x > 1067` on initial touch-down), and widened edge-gesture zones (`96 px` top/bottom, `60 px` left/right) supporting Home Bar swipe-up, App Switcher, Control Center, Notification Center, and edge-swipe back.
  - **Physical Buttons (`SweetHIDButtons`)**:
    - Power key (`PM8996 PON KPDPWR_N`) -> `kHIDUsage_Csmr_Power` (`0x30`) + physical WLED backlight sleep/wake via `kSweetFBRequestPowerChange` (`xzs_fb_set_backlight`).
    - Volume Up (`TLMM GPIO 85`) & Volume Down (`PM8996 PON RESIN_N`) -> `0xE9` / `0xEA`.
    - Camera Focus (`TLMM GPIO 103`) & Camera Shutter (`PM8996 GPIO2`) -> mapped to Home/Menu button (`0x40`).
- **Phase D12 (60 Hz Multi-Threaded Compositor, Non-Blocking Telemetry & Stock iOS 16 Apps)**: **COMPLETE & VERIFIED on physical silicon (`BH905SX976`)**:
  - **Non-Blocking USB Console (`xzs_usb_console_write`)**: Eliminated the 1.0 ms/byte spin-wait when the 16 KB USB TX ring fills after host capture scripts exit, and silenced `/dev/null` (`sweet_null_names="-"`), `IOUserClient` (`sweetiolog="-"`), and post-GUI `[XZS-EXEC]`/`[XZS-EXIT]` logs.
  - **60 Hz 4-Core Parallel Presenter (`XZSFramebuffer`)**: Set `fFlushTicks = 1` (60 Hz), added a branchless 4x-unrolled 1:1 LUT fast-path in `AppleCLCD::renderSlice` across 4 CPU threads, added permanent TTBR1 KVA mapping for MDSS (`g_xzs_mdss_virt_base`), and reduced `xzs_d8m8_wait_frame_idle` poll intervals to `50 µs`.
  - **Daemon Respawn Storm Prevention (`kern_exit.c`)**: Parks exiting unsupported hardware daemons (`fairplayd.H2`, `bluetoothd`, `watchdogd`, `searchpartyd`, `nanoregistrylaun`, `voiced`, `biometrickitd`, `pearld`, `nfcd`, `wirelessproxd`, `syncdefaultsd`) after full VM/task/FD teardown so `launchd` never wastes CPU respawning them.
  - **Stock iOS 16 Apps & Widgets Verified**: Settings (`Preferences.app`), Clock, Calendar, Photos (`MobileSlideShow` + `assetsd`), Notes, Safari (`MobileSafari` + `WebKit`), Weather/Calendar Widgets (`chronod`), and `PlugInKit` (`pkd`) extensions.
- **Phase D13 (Adreno 530 GPU Bring-Up, `AppleM2ScalerCSCDriver`, `IOSurfaceSharedEvent`, `XZSAccelerator` & 64-Bit SWAR Compositor)**: **COMPLETE & VERIFIED (`PAC=0`, branch `xzs-d13-gpu-adreno-metal`)**:
  - **Bare-Metal Qualcomm Adreno 530 (`A530v2`, `0x00b00000`) Driver** ([`src/xnu/pexpert/arm/xzs_gpu.h`](file:///Users/lechaukha12/Desktop/xnu-xzs/src/xnu/pexpert/arm/xzs_gpu.h)): Enables `VDD_GFX` (`pmi8994_s2` on SPMI `SID 3 @ 0x1700`), uncollapses `GPU_GDSC` (`0x008c4034`) & `GPU_GX_GDSC` (`0x008c4024`), enables `MMCC`/`GCC` GFX3D & BIMC clocks, programs `VBIF`, `RBBM`, `UCHE`, and all 25 `a530_hwcg` clock-gating registers, and initializes a 32 KB `CP_RB` Ringbuffer at `0x90f00000`.
  - **`AppleM2ScalerCSCDriver` (`IOSurfaceAccelerator` Backend)** ([`src/xnu/iokit/Kernel/arm/XZSGPU.cpp`](file:///Users/lechaukha12/Desktop/xnu-xzs/src/xnu/iokit/Kernel/arm/XZSGPU.cpp)): Implements selectors `0..9` (`TransformSurface`, `CaptureSurface`, `FillSurface`) with 64-bit SWAR surface blitting, scaling (`srcX/Y >> 16`, `srcW/H >> 14`), `BGRA <-> RGBA` swap, `NV12` (`420v`/`420f`) -> `BGRA` CSC, and `OSAsyncReference64` completion callbacks.
  - **`XZSSurface` Value Cache, Bulk Attachments & `IOSurfaceSharedEvent`** ([`src/xnu/iokit/Kernel/arm/XZSSurface.cpp`](file:///Users/lechaukha12/Desktop/xnu-xzs/src/xnu/iokit/Kernel/arm/XZSSurface.cpp)): Implements `SetYCbCrMatrix` (`sel 5`), `SetValue`/`CopyValue`/`RemoveValue` (`sel 9/10/11`), `SetBulkAttachments` (`sel 27`), and `IOSurfaceSharedEvent` + `IOSurfaceSharedEventListener` (`sel 36..41, 45`).
  - **`XZSAccelerator` (`AGXAcceleratorG10P` / `IOAcceleratorES` / Metal Bridge)** ([`src/xnu/iokit/Kernel/arm/XZSGPU.cpp`](file:///Users/lechaukha12/Desktop/xnu-xzs/src/xnu/iokit/Kernel/arm/XZSGPU.cpp)): Implements `Device` (type 1), `Shared` (type 2), `Context` (types 0/3), and `CommandQueue` (types 4/8) user clients bridged to the Adreno 530 CP Ringbuffer.
  - **64-Bit SWAR 60 Hz `XZSFramebuffer`** ([`src/xnu/iokit/Kernel/arm/XZSFramebuffer.cpp`](file:///Users/lechaukha12/Desktop/xnu-xzs/src/xnu/iokit/Kernel/arm/XZSFramebuffer.cpp)): Accelerates `AppleCLCD::renderSlice` with 2-pixel-at-a-time 64-bit SWAR `xzs_bgra_to_mdp5_swar2` conversion across 4 worker threads at 60 Hz.

---

## 2. Display Pipeline Architecture (CPU Compositor + Hardware Display Engine)
- **Pixel Composition & 1:1 Scaling (RAM Framebuffer `0x98000000`)**:
  - `backboardd`, `RenderServer`, and `SpringBoard` render into shared wired `IOSurface` buffers (`1125x2436` `BGRA`).
  - `AppleCLCD::flushDirty` dispatches 4 parallel threads (`3` workers + `1` flush thread) to scale and swizzle `1125x2436 -> 1080x1920` via precomputed 1:1 LUTs (`s_px_to_lx_lut`, `s_py_to_ly_lut`) directly into write-combining (`VM_WIMG_WCOMB`) memory at `0x98000000`.
- **Hardware Frame Scanout & MIPI DSI Transport (`MDSS MDP5 v1.7 + DSI0 Host + 14nm PHY`)**:
  - `SSPP RGB0` (`0x00915000`) fetches scanout lines via DMA over the `MMAGIC` AXI bus.
  - `Layer Mixer 0` (`LM0`, `0x00945000`) composites at `1080x1920`.
  - `PingPong 0` (`PP0`, `0x00971000`) synchronizes with the `60 Hz` physical `TE` pulse (`GPIO 10`).
  - `DSI0 Host` (`0x00994000`) bursts high-speed MIPI DCS `0x2C`/`0x3C` packets over 4 data lanes into the Sharp/Synaptics panel GRAM.

---

## 3. Reference Files & Documentation Index
- Root Guide: [`README.md`](../README.md)
- Master Architectural Blueprint: [`docs/XZS_IOS_HYBRID_ARCHITECTURE.md`](XZS_IOS_HYBRID_ARCHITECTURE.md)
- GPU, Adreno 530 & Metal Architecture: [`docs/XZS_D13_GPU_ADRENO_METAL_ARCHITECTURE.md`](XZS_D13_GPU_ADRENO_METAL_ARCHITECTURE.md)
- Main Port Status: [`docs/XZS_PORT_STATUS.md`](XZS_PORT_STATUS.md)
- Architecture & Execution Pipeline: [`docs/XZS_ARCHITECTURE.md`](XZS_ARCHITECTURE.md)
- Technical Roadmap: [`docs/XZS_ROADMAP.md`](XZS_ROADMAP.md)
- Hardware Display Evaluation Report: [`docs/XZS_D8_M10_HARDWARE_EVALUATION.md`](XZS_D8_M10_HARDWARE_EVALUATION.md)
- Display Bisect & Root-Cause Resolution: [`docs/DISPLAY_REGRESSION_BISECT_RESULT.md`](DISPLAY_REGRESSION_BISECT_RESULT.md)
