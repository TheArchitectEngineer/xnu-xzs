# xnu-xzs — Native Apple XNU & iOS 16.7.16 on Sony Xperia XZs (Qualcomm MSM8996)

**xnu-xzs** brings the **Apple XNU kernel** (`xnu-12377.1.9` / Darwin 24.0.0) and an authentic **Apple iOS 16.7.16 (`20H392`)** graphical userland up natively on the **Sony Xperia XZs** (`G8231` / `keyaki`, powered by the **Qualcomm Snapdragon 820 / MSM8996 Pro** SoC).

All phases from bare-metal EL1 kernel entry, 4-core Kryo SMP, eMMC 5.1 storage, and MIPI DSI command-mode display scanout through `launchd`, `backboardd`, `SpringBoard`, multi-touch gestures, physical buttons, and stock iOS 16 applications are **hardware-proven on physical Sony Xperia XZs silicon (`BH905SX976`)**.

---

## 1. Hardware-Verified Capabilities Overview

### 1.1 Graphical iOS 16.7.16 User Experience on Physical Hardware
* **Boot Console & Custom Splash Handover**:
  * Full-screen `1080x1920` hardware framebuffer console (`xzs_fbcon`, `135x120` 8x16 glyphs with 16-row jump scrolling) displays live kernel & daemon logs during early boot, overlaid with the custom `"ĐU MA MAY"` vector banner (`xzs_draw_du_ma_may_banner`).
  * Automatically detaches the boot console and switches to pure GUI scanout the moment `SpringBoard` presents its first visible surface.
* **Lock Screen, Home Screen & Wallpapers**:
  * **iOS 16 Lock Screen**: Live time/date rendering, lock indicator, notifications area, and smooth bottom-edge swipe-up to unlock.
  * **Home Screen (Two Icon Pages + Today View & App Library)**: Full `@3x` Retina icon grid, Dock, status bar, page dots, and folder navigation.
  * **Wallpapers**: Stock iOS 16 wallpapers rendered via `PosterBoard` alongside an in-kernel procedural iOS 16 gradient fallback wallpaper so the desktop never displays a black background.
  * **1:1 Square-Pixel Aspect Ratio**: Precomputed horizontal and vertical lookup tables (`s_px_to_lx_lut`, `s_py_to_ly_lut`) map the logical `1125x2436` (`@3x` iPhone X canvas) onto the physical `1080x1920` (16:9) LCD using uniform 1:1 scaling (`1080/1125 = 0.96`, centered `2070`-pixel vertical window) with zero vertical distortion.
* **System Gestures & Multi-Touch (`SweetHIDTouch`)**:
  * Native **Synaptics ClearPad RMI4 F12** I2C multi-touch driver (`BLSP12 QUP5`, `GPIO 87/88`, `INT GPIO 125`, `RST GPIO 89`) polling at **125 Hz (`8 ms`)** during active touch.
  * **2.5D Curved-Bezel Palm/Grip Rejection**: Filters unintended edge contact (`x < 12` or `x > 1067` on initial touch-down) while preserving full swipe travel across the screen.
  * **Widened Edge-Gesture Zones** (`96 px` top/bottom, `60 px` left/right with `kIOHIDDigitizerEventFromEdgeTip` + `kIOHIDDigitizerEventPosition` flags) enabling all system gestures:
    * **Swipe up from bottom Home Bar**: Unlock from Lock Screen & return to Home Screen from any app.
    * **Swipe up and pause**: Open the **App Switcher** and swipe cards up to close apps.
    * **Swipe down from top-right / top edge**: Open **Control Center** and **Notification Center**.
    * **Swipe down on Home Screen**: Open **Spotlight Search**.
    * **Left/right edge swipe**: System back gesture inside navigation controllers and horizontal page switching.
* **Physical Hardware Buttons (`SweetHIDButtons`)**:
  * **Power Button** (`PM8996 PON KPDPWR_N`): Dispatches `kHIDUsage_Csmr_Power` (`0x30`) for screen lock/unlock and sleep/wake, synchronized with `IOMobileFramebuffer` selector `18` (`kSweetFBRequestPowerChange`) to physically toggle the PMI8996 WLED backlight (`xzs_fb_set_backlight`).
  * **Volume Up (`TLMM GPIO 85`) & Volume Down (`PM8996 PON RESIN_N`)**: Dispatches `kHIDUsage_Csmr_VolumeIncrement` (`0xE9`) and `kHIDUsage_Csmr_VolumeDecrement` (`0xEA`) with native iOS volume HUD feedback.
  * **Camera Shutter & Focus Key (`TLMM GPIO 103` half-press / `PM8996 GPIO2` full-press)**: Dual-stage physical camera button mapped to `kHIDUsage_Csmr_Menu` (`0x40`, Home / Menu action).
* **Verified Stock iOS 16 Applications, Widgets & System Services**:
  * **Settings (`Preferences.app`)**, **Clock (`MobileTimer.app`)**, **Calendar (`MobileCal.app`)**, **Photos (`MobileSlideShow.app` + `assetsd`)**, **Notes (`MobileNotes.app`)**, **Safari (`MobileSafari.app` + `com.apple.WebKit.WebContent` + `com.apple.WebKit.GPU`)**, **Calculator**, **Contacts**, **Reminders**, **Files**, **Voice Memos**, and **Stocks**.
  * **Home Screen & Today View Widgets** (`Weather`, `Calendar`, `Clock`, `Batteries`) powered by `chronod` and `PlugInKit` (`pkd`) extension discovery.

---

## 2. Target Hardware Specifications

| Attribute | Specification |
| :--- | :--- |
| **Device** | Sony Xperia XZs (`G8231`, platform `tone`, board `keyaki`) |
| **SoC** | Qualcomm Snapdragon 820 (`MSM8996 Pro` / `MSM8996SG`) |
| **CPU** | 4-core 64-bit Qualcomm Kryo (`ARMv8.0-A`, no PAC): 2x Kryo Silver (Cluster 0) + 2x Kryo Gold (Cluster 1) |
| **Memory** | 4 GB LPDDR4 (`0x80000000` physical base; `1,368 MB` XPU-safe contiguous DRAM mapped into XNU) |
| **Storage** | 32 GB Samsung `BJNB4R` eMMC 5.1 on Qualcomm `SDCC1` (`0x07464900`, 55 GPT slices `/dev/disk0s1..s55`) |
| **Root Filesystem** | 14 GB Case-Sensitive Journaled HFS+ iOS 16.7.16 RootFS on `userdata` (`/dev/disk0s54`) |
| **Display Panel** | 5.2" 1080x1920 Sharp/Synaptics in-cell command-mode MIPI DSI LCD (`somc,sharp_synaptics_cmd_9_panel`) |
| **Display Engine** | Qualcomm MDSS MDP5 v1.7 (`0x00900000`) + 4-lane DSI0 Host (`0x00994000`) + 14nm DSI PHY + PMI8996 LAB/IBB (`±5.6V`) & WLED |
| **Touch Controller** | Synaptics ClearPad S3330 / RMI4 F12 over BLSP12 QUP5 I2C (`0x075B7000`, `GPIO 87/88`, `INT GPIO 125`, `RST GPIO 89`) |
| **Interrupt Controller** | ARM GICv3 Distributor (`0x09BC0000`) + 4 Per-Core Redistributors (`0x09C00000..0x09C60000`) |
| **Boot Mechanism** | Sony S1 Fastboot (`fastboot boot boot.img`) -> `xzs-bootshim` -> `kernel.development.vmapple` |

---

## 3. System Architecture & Kernel Subsystems

```text
+-----------------------------------------------------------------------------------+
|                      AUTHENTIC APPLE iOS 16.7.16 USERLAND                         |
|                                                                                   |
|   SpringBoard.app    Preferences.app    MobileSafari.app    MobileSlideShow.app   |
|   PosterBoard.app    MobileTimer.app    MobileNotes.app     chronod / pkd         |
|         |                   |                  |                     |            |
|         +-------------------+------------------+---------------------+            |
|                             | (Mach IPC / UIKit / QuartzCore / RenderServer)      |
|                             v                                                     |
|                  backboardd (iOS Window & HID Server)                             |
|                  - Matches IOMobileFramebuffer (AppleCLCD)                        |
|                  - Matches IOCoreSurfaceRoot (IOSurface)                          |
|                  - Matches IOHIDEventService (SweetHIDTouch & SweetHIDButtons)    |
+-----------------------------------------------------------------------------------+
                                      |
================================ IOKit / BSD Boundary ===============================
                                      |
+-----------------------------------------------------------------------------------+
|                        XZSAppleCompat KERNEL LAYER                                |
|                                                                                   |
|   +-----------------------+  +----------------------+  +----------------------+   |
|   |    XZSFramebuffer     |  |      XZSSurface      |  |      XZSKeyStore     |   |
|   |  (: IOMobileFrame-    |  |  (: IOCoreSurface-   |  |  (: AppleKeyStore /  |   |
|   |      buffer)          |  |       Root)          |  |     AppleSEPManager) |   |
|   |  - 1125x2436 @3x      |  |  - Wired IOSurfaces  |  |  - Keybag & SEP stub |   |
|   |  - 1:1 LUT scaler     |  |  - Zero-copy maps    |  |  - Unlocks Home &    |   |
|   |  - 4-core 60Hz blit   |  |  - Plane stride sync |  |    DataMigration     |   |
|   +-----------------------+  +----------------------+  +----------------------+   |
|                                                                                   |
|   +-----------------------+  +----------------------+  +----------------------+   |
|   |      xzs_amfi.c       |  |       SweetHID       |  |      sweet_hfs       |   |
|   |  - AMFI + Sandbox MAC |  |  - IOHIDFamily-1915  |  |  - Journaled HFS+    |   |
|   |  - Syscalls 0x5a/0x60 |  |  - 125Hz RMI4 touch  |  |  - 64-bit clonefile  |   |
|   |  - Container paths    |  |  - PON & GPIO keys   |  |  - Quota/Reserve vol |   |
|   |  - Entitlement synth  |  |  - WLED sleep/wake   |  |  - HFS_SSD solidstate|   |
|   +-----------------------+  +----------------------+  +----------------------+   |
+-----------------------------------------------------------------------------------+
                                      |
================================ Hardware Boundary ==================================
                                      |
+-----------------------------------------------------------------------------------+
|                        XZSPlatform SILICON LAYER (MSM8996)                        |
|                                                                                   |
|   - MDSS MDP5 v1.7 + DSI0 Command-Mode DMA Scanout (PA 0x98000000, TTBR1 MMIO)    |
|   - Synaptics ClearPad RMI4 F12 Bit-Banged I2C + PM8996 SPMI PON & TLMM Buttons   |
|   - Samsung BJNB4R 32GB eMMC 5.1 Driver (SDCC1 19.2MHz 8-bit CMD18/CMD24/CMD25)   |
|   - Kryo Quad-Core SMP (PSCI v1.0) + ARM GICv3 + 19.2MHz Architectural Timers     |
|   - DWC3 High-Speed USB Bulk Console + BLSP2 UARTDM + Ramoops Telemetry           |
+-----------------------------------------------------------------------------------+
```

### 3.1 Bootloader Bridge & Apple Device Tree (`src/xzs-bootshim/`)
* **DTB-to-ADT Translation**: Parses the Qualcomm Flattened Device Tree provided by Sony S1 `aboot`, discovers memory banks, and constructs an Apple Device Tree (`ADT`) at `0x81810000` and `boot_args` at `0x81800000` ([`src/xzs-bootshim/main.c`](file:///Users/lechaukha12/Desktop/xnu-xzs/src/xzs-bootshim/main.c), [`src/xzs-bootshim/adt.c`](file:///Users/lechaukha12/Desktop/xnu-xzs/src/xzs-bootshim/adt.c)).
* **`D22AP` (`iPhone10,3`) Identity & MobileGestalt Contract**: Populates `/product` (`udid-version = 2`), `/chosen` (`display-scale = 3`, `certificate-production-status = 0`, `unique-chip-id`, `mac-address-wifi0`, `mac-address-bluetooth0`, `mlb-serial-number`), `/buttons` (`home-button-type = 2`), and `/arm-io/disp0` (`canvas-width = 1125`, `canvas-height = 2436`) so `MobileGestalt`, `launchservicesd` (`lsd`), `backboardd`, and `SpringBoard` initialize without timeouts.
* **XPU-Safe Contiguous Memory Map**: Maps `1,368 MB` of physical LPDDR4 (`0x80000000..0x85900000` + `0x8ac00000..0xdac00000`) while isolating Qualcomm TrustZone, modem/adsp pil regions, and XPU-protected carveouts ([`src/xnu/osfmk/arm64/arm_vm_init.c`](file:///Users/lechaukha12/Desktop/xnu-xzs/src/xnu/osfmk/arm64/arm_vm_init.c)).

### 3.2 CPU, MMU & 4-Core Kryo SMP (`src/xnu/osfmk/arm64/`)
* **ARMv8.0-A Pure Code Generation**: Built with `-mno-ptrauth` and verified with [`scripts/check-no-pac.sh`](file:///Users/lechaukha12/Desktop/xnu-xzs/scripts/check-no-pac.sh) to guarantee zero `arm64e` Pointer Authentication instructions execute on Qualcomm Kryo cores.
* **Quad-Core SMP via PSCI & GICv3**: Boots all 4 Kryo cores (`MPIDR 0x0, 0x1, 0x100, 0x101`) via `PSCI CPU_ON` (`0xC4000003`), initializes per-core GICv3 Redistributors (`0x09C00000..0x09C60000`) in system-register mode (`ICC_SRE_EL1.SRE = 1`), enables 19.2 MHz virtual/physical timer PPIs (`INTID 27` / `INTID 30`) and SGI 1 reschedule IPIs, and preserves `TPIDR_EL0` user TLS across context switches ([`src/xnu/osfmk/arm64/locore.s`](file:///Users/lechaukha12/Desktop/xnu-xzs/src/xnu/osfmk/arm64/locore.s), [`src/xnu/pexpert/arm/pe_fiq.c`](file:///Users/lechaukha12/Desktop/xnu-xzs/src/xnu/pexpert/arm/pe_fiq.c)).

### 3.3 Display Engine & 60 Hz Multi-Threaded Compositor
* **Bare-Metal Qualcomm MDSS MDP5 + DSI0 Command Mode**:
  * Configures MMCC clocks, 14nm DSI0 PLL & PHY, PMI8996 `LAB` (`0xDE00`) & `IBB` (`0xDC00`) `±5.6V` LCD bias rails, `WLED` (`0xD800`) backlight, and Sharp/Synaptics Panel 9 DCS initialization ([`src/xnu/pexpert/arm/xzs_d8m8.h`](file:///Users/lechaukha12/Desktop/xnu-xzs/src/xnu/pexpert/arm/xzs_d8m8.h), [`src/xnu/pexpert/arm/xzs_d8p1.h`](file:///Users/lechaukha12/Desktop/xnu-xzs/src/xnu/pexpert/arm/xzs_d8p1.h), [`src/xnu/pexpert/arm/xzs_d8p2.h`](file:///Users/lechaukha12/Desktop/xnu-xzs/src/xnu/pexpert/arm/xzs_d8p2.h)).
  * Uses a permanent TTBR1 kernel virtual mapping (`g_xzs_mdss_virt_base` in [`src/xnu/pexpert/arm/xzs_diag.c`](file:///Users/lechaukha12/Desktop/xnu-xzs/src/xnu/pexpert/arm/xzs_diag.c)) for zero-overhead MMIO access during 60 Hz frame kicks (`CTL_FLUSH = 0x40022048` + `CTL_START`).
* **`XZSFramebuffer` (`AppleCLCD` / `IOMobileFramebuffer`) & `XZSSurface` (`IOCoreSurfaceRoot`)**:
  * Implements wired multi-plane `IOSurface` management ([`src/xnu/iokit/Kernel/arm/XZSSurface.cpp`](file:///Users/lechaukha12/Desktop/xnu-xzs/src/xnu/iokit/Kernel/arm/XZSSurface.cpp)) and `IOMobileFramebuffer` swap notifications ([`src/xnu/iokit/Kernel/arm/XZSFramebuffer.cpp`](file:///Users/lechaukha12/Desktop/xnu-xzs/src/xnu/iokit/Kernel/arm/XZSFramebuffer.cpp)).
  * **Multi-Threaded 60 Hz LUT Presenter**: Splits the `1080x1920` output across **4 parallel CPU threads** (`3` worker threads + `1` flush thread, 480 rows each) using branchless 4x-unrolled 1:1 LUT loops (`renderSlice`) directly into write-combining (`VM_WIMG_WCOMB`) physical scanout memory at `0x98000000`.

### 3.4 Human Interface Devices: Multi-Touch & Physical Keys (`SweetHID`)
* **`SweetHIDTouch` (`AppleHydroTouch` / `IOHIDEventService`)**:
  * Drives the Synaptics ClearPad S3330 RMI4 controller over BLSP12 I2C (`GPIO 87` SDA, `GPIO 88` SCL, `GPIO 125` IRQ, `GPIO 89` Reset) in [`src/xnu/iokit/Kernel/arm/hid/SweetHID.cpp`](file:///Users/lechaukha12/Desktop/xnu-xzs/src/xnu/iokit/Kernel/arm/hid/SweetHID.cpp).
  * Parses RMI4 Function `$12` (`F12_2D_DATA01`) 8-byte finger records at 125 Hz (`8 ms`), enforces strict `type == 1 || type == 3` finger state validation, filters 2.5D curved-edge grip touches (`x < 12 || x > 1067` on initial touch-down), and synthesizes `IOHIDEvent` digitizer packets with `kIOHIDDigitizerEventFromEdgeTip` for system edge gestures.
* **`SweetHIDButtons` (`AppleEmbeddedButton` / `IOHIDEventService`)**:
  * Polls `PM8996` SPMI `PON_RT_STS` (`0x0810`), `PM8996 GPIO2` (`0xC110`), and `TLMM` GPIOs (`85`, `103`) at 50 Hz (`20 ms`) to deliver Power (`0x30`), Volume Up (`0xE9`), Volume Down (`0xEA`), and Camera Focus/Shutter (`0x40`) events to `backboardd`.

### 3.5 Storage, HFS+ VFS & Security Layer
* **High-Speed SDCC1 eMMC 5.1 Driver (`src/xnu/pexpert/arm/xzs_sdhci.c`)**:
  * Operates at 19.2 MHz with 8-bit bus width (`CMD6`), `CMD18` multi-block PIO reads, and `CMD24`/`CMD25` writes. Exposes `/dev/disk0` and 55 GPT slices (`/dev/disk0s1..s55`) with `DKIOCISSOLIDSTATE = 1`.
* **Case-Sensitive Journaled HFS+ (`src/xnu/bsd/sweet_hfs/`)**:
  * Mounts `/dev/disk0s54` (`userdata`, 14 GB) read-write as `/`, supporting `FSIOC_CAS_BSDFLAGS`, `FSIOC_KERNEL_ROOTAUTH`, 64-bit `hfs_clonefile`, and volume space quota attributes (`ATTR_VOL_QUOTA_SIZE`, `ATTR_VOL_RESERVED_SIZE` in [`src/xnu/bsd/sweet_hfs/core/hfs_vfsops.c`](file:///Users/lechaukha12/Desktop/xnu-xzs/src/xnu/bsd/sweet_hfs/core/hfs_vfsops.c) and [`src/xnu/bsd/vfs/vfs_attrlist.c`](file:///Users/lechaukha12/Desktop/xnu-xzs/src/xnu/bsd/vfs/vfs_attrlist.c)) required by `MobileSafari`, `WebKit`, `Notes`, and `Photos`.
* **AMFI, Sandbox & Process Management (`src/xnu/bsd/kern/xzs_amfi.c`, `kern_exit.c`)**:
  * Registers dual MAC policies (`"Sandbox"` and `"AMFI"`), implements syscalls `0x5a` and `0x60`, resolves container paths (`call == 4` -> `CFFIXED_USER_HOME=` or `ENOENT`), synthesizes entitlements for `PlugInKit` (`pkd`) and `chronod`, and parks unsupported hardware daemons (`fairplayd.H2`, `bluetoothd`, `watchdogd`, `searchpartyd`, `biometrickitd`, `pearld`, `nfcd`, `wirelessproxd`, `syncdefaultsd`) inside `exit_with_reason()` after full VM/FD teardown so `launchd` never wastes CPU respawning them.

---

## 4. Hardware-Verified Milestone Matrix

| Phase | Milestone Description | Status |
| :--- | :--- | :---: |
| **Phase A** | Native EL1 kernel entry (`xzs-bootshim`, ADT generation, 16KB MMU, High KVA) | **COMPLETE & SEALED** |
| **Phase B** | Platform bring-up (BLSP2 UARTDM, GICv3, 19.2MHz Timers, VM & pmap bootstrap) | **COMPLETE & SEALED** |
| **Phase C** | 4-core Kryo SMP scheduler (PSCI `CPU_ON`, per-core GICR, SGI 1 IPI, AST preemption) | **COMPLETE & SEALED** |
| **Phase D1–D4** | BSD/VFS bootstrap, SDCC1 eMMC 5.1 driver, GPT discovery & 55 `/dev/disk0s*` slices | **COMPLETE & SEALED** |
| **Phase D5–D7** | RAMDisk XZSFS, PID 1 EL0 transition, DWC3 USB console & generic Mach-O demand paging | **COMPLETE & SEALED** |
| **Phase D8** | Physical 1080x1920 MIPI DSI command-mode LCD bring-up (MDP5, LAB/IBB, WLED, `xzs_fbcon`) | **COMPLETE & SEALED** |
| **Phase D9** | `XZSAppleCompat` services (`XZSFramebuffer`, `XZSSurface`, `XZSKeyStore`, `SweetHID`, `sweet_hfs`, `xzs_amfi`) | **COMPLETE & SEALED** |
| **Phase D10** | 14 GB HFS+ iOS 16.7.16 RootFS on `/dev/disk0s54`, `/sbin/launchd`, `backboardd` & `SpringBoard` GUI | **COMPLETE & SEALED** |
| **Phase D11** | Synaptics RMI4 125Hz multi-touch, edge gestures, PON/GPIO keys & WLED sleep/wake | **COMPLETE & SEALED** |
| **Phase D12** | 60Hz multi-threaded 1:1 LUT compositor, non-blocking USB console & stock iOS 16 apps/widgets | **COMPLETE & SEALED** |

---

## 5. Repository Layout

```text
xnu-xzs/
├── src/
│   ├── xnu/                  # Apple XNU 24.0.0 kernel source tree with MSM8996 & XZSAppleCompat layers
│   │   ├── osfmk/arm64/      # Low-level bootstrap, MMU, Kryo SMP, exception vectors & AST handling
│   │   ├── pexpert/arm/      # MSM8996 UARTDM, GICv3, SDCC1 eMMC, DWC3 USB, MDSS MDP5/DSI0 & fbcon
│   │   ├── iokit/Kernel/arm/ # XZSFramebuffer, XZSSurface, XZSKeyStore, SweetHID (touch/buttons), SweetBattery
│   │   └── bsd/              # sweet_hfs (Journaled HFS+), sweet_libpthread, sweet_bearssl, xzs_amfi
│   └── xzs-bootshim/         # EL1 ARM64 bootshim, Qualcomm DTB parser & D22AP Apple Device Tree builder
├── scripts/                  # Build, PAC verification, boot.img packaging & hardware deployment scripts
├── tools/                    # HFS+ iOS 16 RootFS builder (mkiosroot-macos.py, xzs-root-tweaks.py, raw2simg.py)
├── docs/                     # Architecture blueprints, hardware verification reports & register audits
└── artifacts/                # Built boot.img payloads and hardware telemetry logs (gitignored)
```

---

## 6. Build & Hardware Reproduction Guide

### 6.1 Prerequisites
* **Host OS**: macOS (Apple Silicon or Intel) with Xcode / Command Line Tools installed.
* **Target Device**: Sony Xperia XZs (`G8231`) with unlocked bootloader in Fastboot mode (`Volume Down` + USB connect).

### 6.2 Compile the XNU Kernel & Verify Zero PAC
```bash
DEVELOPER_DIR=/Applications/Xcode-beta.app/Contents/Developer \
make -C src/xnu \
  KERNEL_CONFIGS=DEVELOPMENT \
  ARCH_CONFIGS=ARM64 \
  MACHINE_CONFIGS=VMAPPLE \
  RC_DARWIN_KERNEL_VERSION=24.0.0 \
  build -j$(sysctl -n hw.ncpu)

./scripts/check-no-pac.sh src/xnu/BUILD/obj/DEVELOPMENT_ARM64_VMAPPLE/kernel.development.vmapple
```

### 6.3 Package `boot.img` & Boot via Fastboot
```bash
./scripts/package-boot.sh
fastboot boot artifacts/builds/boot.img
```

### 6.4 Build & Flash the iOS 16.7.16 HFS+ RootFS (One-Time Setup)
```bash
sudo python3 tools/mkiosroot-macos.py
python3 tools/raw2simg.py artifacts/builds/ios16-root.img artifacts/builds/ios16-root.simg
fastboot flash userdata artifacts/builds/ios16-root.simg
```

---

## 7. Documentation Index

* [Current Session Handoff](docs/CURRENT_HANDOFF.md) — Executive summary of completed milestones and active configuration.
* [Port Status Overview](docs/XZS_PORT_STATUS.md) — Milestone verification checklist and hardware status.
* [Architecture & Execution Pipeline](docs/XZS_ARCHITECTURE.md) — End-to-end boot flow, memory map, and MMIO register reference.
* [iOS Compatibility Architecture Blueprint](docs/XZS_IOS_HYBRID_ARCHITECTURE.md) — Detailed specification of `XZSAppleCompat` services (`XZSFramebuffer`, `XZSSurface`, `SweetHID`, `xzs_amfi`, `sweet_hfs`).
* [Technical Roadmap](docs/XZS_ROADMAP.md) — Complete Phase A through Phase D12 milestone specifications.
* [Physical Display Evaluation Report](docs/XZS_D8_M10_HARDWARE_EVALUATION.md) — Root-cause resolution and hardware verification of the Sharp/Synaptics 1080x1920 MIPI DSI pipeline.
* [Build Guide](docs/XZS_BUILD.md) — Detailed toolchain and build flags reference.
