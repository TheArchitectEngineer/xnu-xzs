# XNU-XZS: Apple iOS 16 Compatibility Architecture Blueprint
## Native iOS 16.7.16 Bring-Up Architecture on Sony Xperia XZs (Qualcomm MSM8996)

---

## 1. Executive Summary

This document defines the authoritative architecture for running an authentic **Apple iOS 16.7.16 (`20H392`)** graphical userland natively on the **Sony Xperia XZs** (`G8231` / `MSM8996` / `keyaki`).

### 1.1 Architectural Design
`xnu-xzs` employs a **Hybrid Layered Architecture** combining bare-metal Qualcomm MSM8996 silicon drivers (`XZSPlatform`) with a self-contained C++ IOKit and BSD compatibility layer (`XZSAppleCompat`):
1. **Native MSM8996 Silicon Foundations (`XZSPlatform`)**:
   - Quad-core Qualcomm Kryo `ARMv8.0-A` SMP via PSCI v1.0 `CPU_ON` and ARM GICv3.
   - Bare-metal Qualcomm MDSS MDP5 v1.7 (`CTL0` + `SSPP RGB0` + `LM0` + `PP0`) and 4-lane DSI0 Command Mode (`0x00994000`) with PMI8996 `LAB`/`IBB` (`±5.6V`) and `WLED` backlight control.
   - High-speed SDCC1 eMMC 5.1 block storage (`19.2 MHz` 8-bit `CMD18`/`CMD24`/`CMD25`) exposing 55 GPT slices (`/dev/disk0s1..s55`).
   - Synaptics ClearPad RMI4 F12 multi-touch over BLSP12 QUP5 I2C (`GPIO 87/88/125/89`) and PM8996 PON / TLMM GPIO hardware buttons.
2. **Apple Compatibility Services (`XZSAppleCompat`)**:
   - Exposes standard Apple IOKit and BSD interfaces expected by `launchd`, `backboardd`, `RenderServer`, `SpringBoard`, and stock iOS 16 apps: `AppleCLCD` (`IOMobileFramebuffer`), `IOCoreSurfaceRoot` (`IOSurface`), `AppleKeyStore` / `AppleSEPManager`, `IOHIDEventService` (`IOHIDFamily-1915`), `AppleSmartBatteryManager`, Case-Sensitive Journaled HFS+ (`sweet_hfs`), and permissive `"Sandbox"` / `"AMFI"` MAC policies (`xzs_amfi`).
3. **60 Hz Multi-Threaded 1:1 LUT Framebuffer Compositor**:
   - `backboardd` and `SpringBoard` composite into wired `1125x2436` (`@3x` iPhone X) `IOSurface` buffers.
   - `XZSFramebuffer` scales and swizzles `1125x2436 -> 1080x1920` at 60 Hz across 4 parallel CPU threads using precomputed 1:1 square-pixel lookup tables (`s_px_to_lx_lut`, `s_py_to_ly_lut`) directly into write-combining (`VM_WIMG_WCOMB`) scanout RAM (`0x98000000`), which the MDP5 + DSI0 hardware engine bursts to the physical LCD.
4. **Authentic IPSW RootFS Pipeline**:
   - [`tools/mkiosroot-macos.py`](file:///Users/lechaukha12/Desktop/xnu-xzs/tools/mkiosroot-macos.py) and [`tools/xzs-root-tweaks.py`](file:///Users/lechaukha12/Desktop/xnu-xzs/tools/xzs-root-tweaks.py) build a 14 GB Case-Sensitive Journaled HFS+ volume from iOS 16.7.16 (`iPhone10,3` / `D22AP`) flashed onto internal eMMC `userdata` (`/dev/disk0s54`).

---

## 2. Platform Architecture Matrix

| Subsystem | Sony Xperia XZs Hardware (`XZSPlatform`) | Apple-Facing Contract (`XZSAppleCompat`) | Implementation Status |
| :--- | :--- | :--- | :--- |
| **SoC / CPU** | Qualcomm MSM8996 Pro (Snapdragon 820)<br>`ARMv8.0-A`, 4 cores Kryo (2x Silver + 2x Gold) | Pure `arm64` kernel (`-mno-ptrauth`, `PAC=0`) + 4-core Mach SMP (`pset0`) | **COMPLETE & VERIFIED** |
| **DRAM Layout** | 4 GB LPDDR4 (`0x80000000` base)<br>`1,368 MB` XPU-safe contiguous RAM | Kernel VM / `pmap` / wired `IOBufferMemoryDescriptor` pools | **COMPLETE & VERIFIED** |
| **Boot & Device Tree** | Sony S1 ABOOT (`fastboot boot boot.img`) + Qualcomm DTB | `xzs-bootshim` constructs `D22AP` (`iPhone10,3`) ADT at `0x81810000` | **COMPLETE & VERIFIED** |
| **Display Engine** | Sharp/Synaptics 1080x1920 Command-Mode LCD<br>MDSS MDP5 + DSI0 + PMI8996 LAB/IBB/WLED | `XZSFramebuffer` (`AppleCLCD` / `IOMobileFramebuffer`) + `XZSSurface` (`IOSurface` + `IOSurfaceSharedEvent`) | **COMPLETE & VERIFIED** |
| **GPU & Accelerator** | Qualcomm Adreno 530 (`A530v2`, `0x00b00000`, `chipid = 0x05030002`, 1 MB GMEM, 32 KB CP Ringbuffer `0x90f00000`) | `XZSGPU` (`AppleM2ScalerCSCDriver` `IOSurfaceAccelerator` + `XZSAccelerator` `AGXAcceleratorG10P` / `IOAcceleratorES`) | **COMPLETE & VERIFIED** |
| **Aspect Ratio & Blit** | Physical `1080x1920` (16:9) scanout buffer at `0x98000000` | Logical `1125x2436` (`@3x`) mapped via 64-bit SWAR (`xzs_bgra_to_mdp5_swar2`) across 4 CPU threads @ 60 Hz | **COMPLETE & VERIFIED** |
| **Storage & RootFS** | Samsung `BJNB4R` 32 GB eMMC 5.1 (`SDCC1` `0x07464900`, 19.2 MHz 8-bit) | `sweet_hfs` R/W Journaled HFS+ on `/dev/disk0s54` (`HFS_SSD`, 64-bit `clonefile`, volume quotas) | **COMPLETE & VERIFIED** |
| **Security & Sandbox** | Kernel BSD MAC framework | `xzs_amfi.c`: `"Sandbox"` + `"AMFI"` policies, syscalls `0x5a`/`0x60`, container paths (`call == 4`) | **COMPLETE & VERIFIED** |
| **KeyStore / SEP** | Software stub | `XZSKeyStore.cpp`: `AppleKeyStore` & `AppleSEPManager` userclients unlocking `keybagd` & `datamigrator` | **COMPLETE & VERIFIED** |
| **Touch & Buttons** | Synaptics RMI4 F12 (`BLSP12 I2C` `GPIO 87/88/125/89`) + PM8996 PON & TLMM GPIOs | `SweetHIDTouch` (125 Hz, 2.5D grip filter, edge gestures) + `SweetHIDButtons` (Power, Vol, Camera) | **COMPLETE & VERIFIED** |

---

## 3. Subsystem Architectural Blueprints

### 3.1 Display & GPU Accelerator Subsystem: `XZSFramebuffer`, `XZSSurface` & `XZSGPU`
1. **`XZSSurface` (`IOCoreSurfaceRoot`)** ([`src/xnu/iokit/Kernel/arm/XZSSurface.cpp`](file:///Users/lechaukha12/Desktop/xnu-xzs/src/xnu/iokit/Kernel/arm/XZSSurface.cpp)):
   - Implements userclient external methods `0 (Create)`, `4 (Lookup)`, `5 (SetYCbCrMatrix)`, `9 (SetValue)`, `10 (CopyValue)`, `11 (RemoveValue)`, `13 (Limits)`, `27 (SetBulkAttachments)`, `34 (LookupFromPort)`, `35 (CreatePort)`, and `36..41, 45` (`IOSurfaceSharedEvent` & `IOSurfaceSharedEventListener`) plus fast traps `0..5`.
   - Allocates wired kernel pages mapped into calling processes (`backboardd`, `RenderServer`, `SpringBoard`, apps) with full multi-plane stride/offset metadata and non-bulk value seed synchronization (`state + 0x20`).
2. **`XZSGPU` (`AppleM2ScalerCSCDriver` & `XZSAccelerator`)** ([`src/xnu/iokit/Kernel/arm/XZSGPU.cpp`](file:///Users/lechaukha12/Desktop/xnu-xzs/src/xnu/iokit/Kernel/arm/XZSGPU.cpp), [`src/xnu/pexpert/arm/xzs_gpu.h`](file:///Users/lechaukha12/Desktop/xnu-xzs/src/xnu/pexpert/arm/xzs_gpu.h)):
   - Brings up the physical **Qualcomm Adreno 530 (`A530v2`, `0x00b00000`)** GPU (`VDD_GFX` `pmi8994_s2` on SPMI `SID 3 @ 0x1700`, `GPU_GDSC`, `GPU_GX_GDSC`, `MMCC`/`GCC` GFX3D & BIMC clocks, `a530_hwcg` clock gating, and 32 KB `CP_RB` Ringbuffer at `0x90f00000`).
   - Implements `AppleM2ScalerCSCDriver` (`IOSurfaceAccelerator.framework` backend) with 64-bit SWAR surface blitting, fixed-point scaling, `BGRA <-> RGBA` swap, `NV12` (`420v`/`420f`) -> `BGRA` CSC, and `OSAsyncReference64` completion notifications.
   - Implements `XZSAccelerator` (`AGXAcceleratorG10P` / `IOGraphicsAccelerator2` / `IOAcceleratorES`) on `/arm-io/sgx` with all 4 user client classes (`Device` type 1, `Shared` type 2, `Context` types 0/3, `CommandQueue` types 4/8).
3. **`XZSFramebuffer` (`AppleCLCD` / `IOMobileFramebuffer`)** ([`src/xnu/iokit/Kernel/arm/XZSFramebuffer.cpp`](file:///Users/lechaukha12/Desktop/xnu-xzs/src/xnu/iokit/Kernel/arm/XZSFramebuffer.cpp)):
   - Matches `IODeviceTree:/arm-io/disp0` and advertises `1125x2436` (`@3x` iPhone X canvas).
   - Runs a 60 Hz display presenter loop (`fFlushTicks = 1`) backed by 4 parallel worker threads (`renderSlice`) using 2-pixel-at-a-time 64-bit SWAR (`xzs_bgra_to_mdp5_swar2`) conversion.
   - Triggers hardware MIPI DSI frame transfer via direct TTBR1 MDSS MMIO (`g_xzs_mdss_virt_base` -> `xzs_d8m8_kick_frame_ex`).
   - Handles selector `12` (`kSweetFBRequestPowerChange`) to turn the physical PMI8996 WLED backlight off on sleep and back on upon wake (`xzs_fb_set_backlight`).

### 3.2 Input Subsystem: `SweetHIDTouch` & `SweetHIDButtons`
1. **`SweetHIDTouch` (`AppleHydroTouch`)** ([`src/xnu/iokit/Kernel/arm/hid/SweetHID.cpp`](file:///Users/lechaukha12/Desktop/xnu-xzs/src/xnu/iokit/Kernel/arm/hid/SweetHID.cpp)):
   - Communicates with the Synaptics ClearPad S3330 RMI4 controller over BLSP12 QUP5 bit-banged I2C (`GPIO 87` SDA, `GPIO 88` SCL, `GPIO 125` IRQ, `GPIO 89` Reset).
   - Polls RMI4 Function `$12` (`F12_2D_DATA01`) at 125 Hz (`8 ms`) during active touches.
   - Filters invalid object types (`type == 1 || type == 3` only) and suppresses initial touch-downs on the 2.5D curved glass bezel (`x < 12 || x > 1067`).
   - Attaches `kIOHIDDigitizerEventFromEdgeTip` within widened edge zones (`96 px` top/bottom, `60 px` left/right) so `backboardd` recognizes Home Bar swipe-up, App Switcher, Control Center, Notification Center, and edge-swipe back gestures reliably.
2. **`SweetHIDButtons` (`AppleEmbeddedButton`)**:
   - Polls `PM8996` SPMI `PON_RT_STS` (`0x0810`), `PM8996 GPIO2` (`0xC110`), and `TLMM` GPIOs `85` / `103` every `20 ms`.
   - Maps Power (`0x30`), Volume Up (`0xE9`), Volume Down (`0xEA`), and dual-stage Camera Focus/Shutter (`0x40` Home/Menu) to `IOHIDEvent::buttonEvent`.

### 3.3 Security, Sandbox & Process Management (`xzs_amfi.c`, `kern_exit.c`)
1. **Dual MAC Policies (`"Sandbox"` & `"AMFI"`)** ([`src/xnu/bsd/kern/xzs_amfi.c`](file:///Users/lechaukha12/Desktop/xnu-xzs/src/xnu/bsd/kern/xzs_amfi.c)):
   - Implements syscalls `0x5a` and `0x60`.
   - Implements `sweet_sandbox_container_path` (`call == 4`), returning `ENOENT` when uncontainerized (allowing `containermanagerd` to register the container path) and copying out the process's `CFFIXED_USER_HOME=` environment path when containerized.
   - Grants entitlement queries for `PlugInKit` (`pkd`), `chronod`, `MobileSafari`, `WebKit`, `Photos`, and system daemons.
2. **Hardware-Less Daemon Parking** ([`src/xnu/bsd/kern/kern_exit.c`](file:///Users/lechaukha12/Desktop/xnu-xzs/src/xnu/bsd/kern/kern_exit.c)):
   - Parks exiting daemons whose proprietary Apple hardware is absent (`fairplayd.H2`, `bluetoothd`, `watchdogd`, `searchpartyd`, `nanoregistrylaun`, `voiced`, `biometrickitd`, `pearld`, `nfcd`, `wirelessproxd`, `syncdefaultsd`) inside `exit_with_reason()` after tearing down their user VM map, Mach ports, and file descriptors, eliminating `launchd` respawn CPU storms.
