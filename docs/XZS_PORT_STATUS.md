# Sony Xperia XZs (MSM8996) — Port Status

All bring-up, display, storage, `XZSAppleCompat` services, iOS 16.7.16 RootFS, multi-touch/button HID, and 60 Hz compositor milestones (`Phase A` through `Phase D12`) are **COMPLETE, SEALED, AND HARDWARE PROVEN** on physical Sony Xperia XZs silicon (`BH905SX976`).

```text
Phase A..C = PASS (EL1 bootshim, 16KB MMU, High KVA, GICv3, 4-core Kryo SMP, 19.2MHz timers)
D1..D4     = PASS (BSD/VFS bootstrap, SDCC1 eMMC 5.1 driver, GPT discovery, 55 slices /dev/disk0s1..s55)
D5..D7     = PASS (RAMDisk XZSFS, PID 1 EL0, DWC3 USB console, generic Mach-O demand paging)
D8-M1..M6  = PASS (MDSS clocks, 14nm DSI0 PLL/PHY, Command-Mode Host, TLMM GPIO, PMI8996 LAB/IBB, Panel 9 DCS)
D8-M8..M10 = PASS (MDP5 RGB0+LM0+PP0 scanout, WLED backlight, 1080x1920 boot console & custom splash banner)
D9-M1..M3  = PASS (XZSFramebuffer, XZSSurface, XZSKeyStore, SweetHID, SweetBattery, sweet_hfs, xzs_amfi)
D10-M1..M6 = PASS (14 GB HFS+ iOS 16.7.16 RootFS on /dev/disk0s54, launchd, backboardd, SpringBoard Home Screen)
D11-M1..M3 = PASS (Synaptics RMI4 F12 125Hz multi-touch, 2.5D grip filter, edge gestures, PON/GPIO keys, WLED sleep/wake)
D12-M1..M4 = PASS (60Hz 4-core 1:1 LUT compositor, TTBR1 MDSS MMIO, non-blocking USB console, stock iOS 16 apps & widgets)
```

---

## Target Device Summary

* **Target Device**: Sony Xperia XZs (`Model G8231` / `Platform Tone` / `Board Keyaki`)
* **SoC**: Qualcomm Snapdragon 820 (`MSM8996 Pro`)
* **CPU Architecture**: Quad-core Qualcomm Kryo `ARMv8.0-A` (2x Silver + 2x Gold, `PAC=0`)
* **Storage Device**: Samsung `BJNB4R` 32 GB eMMC 5.1 (`19.2 MHz` 8-bit `CMD18`/`CMD24`/`CMD25`, `/dev/disk0s54` mounted R/W as `/`)
* **Display Panel**: 5.2" `1080x1920` Sharp/Synaptics command-mode MIPI DSI LCD (`somc,sharp_synaptics_cmd_9_panel`)
* **Active Branch**: `main`

---

## Hardware-Verified Subsystems Checklist

### 1. Core Kernel, SMP & Platform (`XZSPlatform`)
- [x] **Sony S1 Bootloader Handoff & `xzs-bootshim`**: Android boot image loading via `fastboot boot`, Qualcomm DTB parsing, `D22AP` (`iPhone10,3`) Apple Device Tree generation at `0x81810000`, and `1,368 MB` XPU-safe contiguous DRAM layout.
- [x] **Low-Level ARM64 MMU**: 16 KB page tables, High KVA (`0xfffffe0000000000`), and permanent TTBR1 MDSS MMIO mapping (`g_xzs_mdss_virt_base`).
- [x] **ARM GICv3 & Generic Timers**: Distributor (`0x09bc0000`) and 4 per-core Redistributors (`0x09c00000..0x09c60000`) in system-register mode; 19.2 MHz virtual (`PPI 27`) and physical (`PPI 30`) timers across all 4 cores.
- [x] **4-Core Kryo Mach SMP**: All 4 cores booted via `PSCI CPU_ON` (`0xC4000003`), servicing SGI 1 reschedule IPIs, preserving per-CPU `pmap` and `TPIDR_EL0` user TLS.
- [x] **Non-Blocking Telemetry**: Qualcomm BLSP2 UARTDM (`0x075b0000`) and non-blocking DWC3 High-Speed USB bulk console (`xzs_usb_console_write`).

### 2. Storage & File Systems
- [x] **High-Speed SDCC1 eMMC 5.1 Driver (`xzs_sdhci.c`)**: 19.2 MHz clock, 8-bit bus width (`CMD6`), `CMD18` multi-block PIO reads, `CMD24`/`CMD25` writes, and `DKIOCISSOLIDSTATE = 1`.
- [x] **GPT Discovery & Slice Publication**: Whole-disk `/dev/disk0` and 55 partition slices (`/dev/disk0s1..s55`) published to `devfs` and IOKit (`IOMedia`).
- [x] **Case-Sensitive Journaled HFS+ (`sweet_hfs`)**: 14 GB iOS 16.7.16 RootFS mounted read-write from `/dev/disk0s54` (`userdata`), supporting `FSIOC_CAS_BSDFLAGS`, `FSIOC_KERNEL_ROOTAUTH`, 64-bit `hfs_clonefile`, and volume quota attributes (`ATTR_VOL_QUOTA_SIZE`, `ATTR_VOL_RESERVED_SIZE`).

### 3. Display Engine & 60 Hz Multi-Threaded Compositor
- [x] **Bare-Metal MDSS MDP5 + DSI0 Command Mode (`xzs_d8m8.h`)**: Full 1080x1920 physical scanout (`CTL0` + `SSPP RGB0` + `LM0` + `PP0` + 4-lane `DSI0` + `PMI8996 LAB/IBB ±5.6V` + `WLED`).
- [x] **Live Boot Console & Custom Splash (`xzs_fbcon.h`)**: Full-screen `1080x1920` boot log console with 16-row jump scrolling and custom `"ĐU MA MAY"` banner (`xzs_draw_du_ma_may_banner`), automatically handing over to the iOS GUI on the first visible `SpringBoard` surface swap.
- [x] **`XZSFramebuffer` (`AppleCLCD`) & `XZSSurface` (`IOCoreSurfaceRoot`)**:
  - Wired multi-plane `IOSurface` allocation and zero-copy user mapping.
  - Precomputed **1:1 square-pixel aspect ratio LUT** (`s_px_to_lx_lut`, `s_py_to_ly_lut`) mapping `1125x2436` (`@3x`) onto `1080x1920` without vertical distortion.
  - **4-Core Parallel 60 Hz Presenter**: `3` worker threads + `1` flush thread (`renderSlice`) blitting into `VM_WIMG_WCOMB` framebuffer memory at 60 Hz (`fFlushTicks = 1`).
  - Procedural iOS 16 wallpaper fallback and `PosterBoard` wallpaper rendering.

### 4. Multi-Touch, Gestures & Physical Buttons (`SweetHID`)
- [x] **Synaptics ClearPad RMI4 F12 Multi-Touch (`SweetHIDTouch`)**:
  - Bit-banged I2C over `GPIO 87` (SDA) / `GPIO 88` (SCL) (`BLSP12 QUP5`) with `GPIO 125` attention interrupt and `GPIO 89` hardware reset.
  - **125 Hz (`8 ms`) active touch polling**, strict `type == 1 || type == 3` finger detection, and **2.5D curved-bezel grip rejection** (`x < 12 || x > 1067` on initial touch-down).
  - **Widened Edge-Gesture Zones** (`96 px` top/bottom, `60 px` left/right with `kIOHIDDigitizerEventFromEdgeTip`) enabling Home Bar swipe-up (unlock & home), swipe-up-and-hold (App Switcher), top-right swipe-down (Control Center), top swipe-down (Notification Center), Home Screen swipe-down (Spotlight), and left/right edge swipe (back navigation).
- [x] **Physical Hardware Buttons (`SweetHIDButtons`)**:
  - **Power Button** (`PM8996 PON KPDPWR_N`): Screen sleep/wake (`0x30`) coupled with `kSweetFBRequestPowerChange` and physical PMI8996 WLED backlight control (`xzs_fb_set_backlight`).
  - **Volume Up (`TLMM GPIO 85`) & Volume Down (`PM8996 PON RESIN_N`)**: Volume control (`0xE9` / `0xEA`) with iOS volume HUD.
  - **Camera Focus (`TLMM GPIO 103`) & Shutter (`PM8996 GPIO2`)**: Dual-stage physical camera key mapped to Home / Menu (`0x40`).

### 5. Authentic iOS 16.7.16 Userland, Security & Applications
- [x] **AMFI, Sandbox & Container Path Support (`xzs_amfi.c`)**: Dual `"Sandbox"` and `"AMFI"` MAC policies, syscalls `0x5a` & `0x60`, container path resolution (`call == 4` -> `CFFIXED_USER_HOME=` or `ENOENT`), and entitlement synthesis.
- [x] **Daemon Respawn Storm Prevention (`kern_exit.c`)**: Parks exiting unsupported hardware daemons after full VM/FD teardown so `launchd` never wastes CPU respawning them.
- [x] **Verified Stock iOS 16 Apps & Widgets**:
  - Lock Screen, Home Screen (two icon pages + Today View & App Library), Control Center, Notification Center, Spotlight Search, App Switcher.
  - **Settings (`Preferences.app`)**, **Clock**, **Calendar**, **Photos (`MobileSlideShow` + `assetsd`)**, **Notes**, **Safari (`MobileSafari` + `WebKit`)**, **Calculator**, **Contacts**, **Reminders**, **Files**, **Voice Memos**, **Stocks**, and **Weather / Calendar Widgets (`chronod` + `pkd`)**.

---

## How to Reproduce

### 1. Build Pipeline
```bash
DEVELOPER_DIR=/Applications/Xcode-beta.app/Contents/Developer \
make -C src/xnu \
  KERNEL_CONFIGS=DEVELOPMENT \
  ARCH_CONFIGS=ARM64 \
  MACHINE_CONFIGS=VMAPPLE \
  RC_DARWIN_KERNEL_VERSION=24.0.0 \
  build -j$(sysctl -n hw.ncpu)

./scripts/check-no-pac.sh src/xnu/BUILD/obj/DEVELOPMENT_ARM64_VMAPPLE/kernel.development.vmapple
./scripts/package-boot.sh
```

### 2. Boot on Sony Xperia XZs
```bash
fastboot boot artifacts/builds/boot.img
```
