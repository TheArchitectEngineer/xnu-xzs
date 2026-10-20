# XNU-XZS: Apple iOS Hybrid Architecture Blueprint
## Integrating the Proven iOS 16 Bring-up Strategy from ios-sweet onto Sony Xperia XZs (MSM8996)

---

## 1. Executive Summary

This document establishes the authoritative technical blueprint for bringing authentic Apple iOS userland execution to the **Sony Xperia XZs** (`G8231` / `MSM8996` / `keyaki`) by adopting the proven architecture of **ios-sweet** (`SM7150` / Redmi Note 10 Pro / iOS 16.7.16).

### 1.1 Architectural Thesis
Previous `xnu-xzs` milestones attempted full bare-metal driver re-implementation for every mobile SoC subsystem (notably the complex Qualcomm MDSS/MDP5 DSI command-mode pipeline, PingPong flow control, and in-cell TE handshake). While this achieved critical physical milestones (such as `D8-M8.5` WLED backlight illumination and visible pattern latching), attempting to write full GPU/display drivers from scratch creates an insurmountable barrier to running authentic iOS.

By auditing the breakthrough architecture of `ios-sweet`, we adopt a **Hybrid Layered Architecture**:
1. **Retain Hardware Provenance**: Preserve `xnu-xzs`'s native MSM8996 silicon foundations (Kryo SMP, GICv3, BLSP2 UARTDM, SPMI v2 arbiter, eMMC 5.1 block storage).
2. **Import Apple Compatibility Services (`XZSAppleCompat`)**: Drop in the proven, self-contained C++ IOKit services from `ios-sweet` (`AppleCLCD` / `IOMobileFramebuffer`, `IOCoreSurfaceRoot` / `IOSurface`, `AppleKeyStore`, `IOHIDFamily-1915`, and permissive AMFI/Sandbox MAC policies).
3. **Continuous Framebuffer Scanout Model**: Eliminate the brittle PingPong / TE software synchronization in userspace by exposing a flat, high-performance physical framebuffer directly to Apple's window server (`backboardd`) and compositing engine (`RenderServer`), mirroring the `SweetFramebuffer` design.
4. **Authentic IPSW RootFS Pipeline**: Utilize `mkiosroot.sh` and `sweet-root-tweaks.py` to extract, merge, and neutralize Apple's iPhone root filesystem images into an uncompressed, journaled HFS+ volume residing on internal storage.

---

## 2. Comparative Platform Matrix: `xnu-xzs` vs `ios-sweet`

| Subsystem | `ios-sweet` (Redmi Note 10 Pro) | `xnu-xzs` (Sony Xperia XZs) | Hybrid Integration Strategy |
| :--- | :--- | :--- | :--- |
| **SoC / CPU** | Qualcomm SM7150 (Snapdragon 732G)<br>ARMv8.2-A, 8 cores (6x A55 + 2x A76) | Qualcomm MSM8996 Pro (Snapdragon 820)<br>ARMv8.0-A, 4 cores Kryo (2x Silver + 2x Gold) | Retain `xnu-xzs` Kryo SMP & GICv3; ensure target userland arm64 binaries execute cleanly without v8.2-only mandatory instructions. |
| **DRAM Layout** | 4 GB LPDDR4<br>Highmem: `0x100000000` (above 4GB carveout) | 4 GB LPDDR4<br>Lowmem base: `0x80000000` | Retain `xnu-xzs` DRAM base and memory map (`0x80000000`). |
| **Boot Mechanism** | Xiaomi ABL (`fastboot boot shim-boot.img`) | Sony S1 ABOOT (`fastboot boot boot.img`) | Retain `xnu-xzs` bootshim with compressed Image.gz-dtb. |
| **Display Architecture** | DSI Video Mode (DSC)<br>Continuous Splash at `0x9c000000` | DSI Command Mode (Sharp 1080p)<br>Splash reservation at `0x83401000` / Test FB `0x98000000` | Implement `XZSFramebuffer` modeled on `SweetFramebuffer.cpp`: CPU copy of swapped IOSurfaces into the physical framebuffer. |
| **Window Server / Compositor** | `AppleCLCD` + `IOCoreSurfaceRoot`<br>CPU software composition (20-30 fps) | Raw MMIO scanout / `xzs_fbcon` (1080x1920) | Replace raw scanout dependency with `XZSFramebuffer` & `XZSSurface` IOKit classes. |
| **Storage & RootFS** | UFS 2.1 host controller (`0x1d84000`)<br>HFS+ volume on `userdata` | eMMC 5.1 (SDCC1 `0x07464900`)<br>GPT slices (`/dev/disk0s1..s55`) | Implement HFS+ read/write mount on eMMC `userdata` slice (`/dev/disk0s54`). |
| **Security / AMFI** | Permissive AMFI + Sandbox MAC policy | Minimal entitlement bypass | Port `sweet_amfi.c` to provide full permissive MAC policies for `launchd` and Mach lookups. |
| **KeyStore / SEP** | `SweetKeyStore.cpp` (dummy AKS responses) | None (causes datamigrator / SpringBoard hang) | Port `SweetKeyStore.cpp` as `XZSKeyStore.cpp` to satisfy selectors 17, 35, 101. |
| **Input / Human Interface** | Goodix GT9896 (GENI I2C) + SPMI PON keys<br>`SweetHID.cpp` + `IOHIDFamily-1915` | Synaptics RMI4 (BLSP I2C) + PMIC keys | Port `SweetHID.cpp` and Apple `IOHIDFamily-1915.140.3` userclient wrappers to route touch & buttons to `backboardd`. |

---

## 3. Subsystem Architectural Blueprints

```text
+-----------------------------------------------------------------------------------+
|                            AUTHENTIC APPLE USERLAND                               |
|                                                                                   |
|   SpringBoard.app        Settings.app          Setup.app          MobilePhone     |
|   (Home Screen)       (Preferences)       (PurpleBuddy-skip)     (Apps/Daemons)   |
|         |                   |                     |                    |          |
|         +-------------------+---------------------+--------------------+          |
|                             | (Mach IPC / UIKit / QuartzCore)                     |
|                             v                                                     |
|                  backboardd (Window Server)                                       |
|                  - Matches IOMobileFramebuffer (AppleCLCD)                        |
|                  - Matches IOCoreSurfaceRoot (IOSurface)                          |
|                  - Matches IOHIDEventService (Buttons & Touch)                    |
+-----------------------------------------------------------------------------------+
                                      |
================================ IOKit / BSD Boundary ===============================
                                      |
+-----------------------------------------------------------------------------------+
|                        XZSAppleCompat KERNEL LAYER                                |
|                                                                                   |
|   +-----------------------+  +----------------------+  +----------------------+   |
|   |    XZSFramebuffer     |  |      XZSSurface      |  |      XZSKeyStore     |   |
|   |  (: IOMobileFrame-    |  |  (: IOCoreSurface-   |  |  (: AppleKeyStore    |   |
|   |      buffer)          |  |       Root)          |  |       stub)          |   |
|   |  - iPhone X geometry  |  |  - Wired surface mgmt|  |  - Dummy device-state|   |
|   |  - 30/60Hz VSYNC timer|  |  - User-client maps  |  |  - Unblocks AKS lock |   |
|   |  - CPU copy to FB PA  |  |  - Per-client bounds |  |  - Migration unblock |   |
|   +-----------------------+  +----------------------+  +----------------------+   |
|                                                                                   |
|   +-----------------------+  +----------------------+  +----------------------+   |
|   |      xzs_amfi.c       |  |       XZSHID         |  |   HFS+ VFS Driver    |   |
|   |  - Permissive AMFI    |  |  - IOHIDFamily-1915  |  |  - Journal replay    |   |
|   |  - Permissive Sandbox |  |  - Button event queues|  |  - Root on eMMC slice|   |
|   |  - Unentitled exec    |  |  - Touch digitizer   |  |  - Case-sensitive    |   |
|   +-----------------------+  +----------------------+  +----------------------+   |
+-----------------------------------------------------------------------------------+
                                      |
================================ Hardware Boundary ==================================
                                      |
+-----------------------------------------------------------------------------------+
|                        XZSPlatform SILICON LAYER (MSM8996)                        |
|                                                                                   |
|   - Physical Framebuffer (0x83401000 Sony cont_splash / 0x98000000 Autorefresh)   |
|   - Samsung BJNB4R 32GB eMMC 5.1 Storage Driver (SDCC1 / bdevsw)                 |
|   - Kryo Quad-Core SMP via PSCI / ARM GICv3 Interrupt Controller                  |
|   - PMI8994 SPMI Arbiter v2 (Power & Volume Key Observers, WLED Backlight)        |
|   - Synaptics Touch Controller (BLSP I2C / TLMM GPIO)                             |
+-----------------------------------------------------------------------------------+
```

### 3.1 Display Subsystem: `XZSFramebuffer` & `XZSSurface`
Instead of attempting complex kernel-mode layer composition and waiting for fragile in-cell hardware TE pulses, we implement `XZSFramebuffer` and `XZSSurface` directly in `iokit/Kernel/arm/`:

1. **`XZSSurface` (`IOCoreSurfaceRoot`)**:
   - Implements selectors `0 (Create)`, `4 (Lookup)`, `13 (Limits)`, `34 (LookupFromPort)`, `35 (CreatePort)`.
   - Supports traps `0..4` (`IncrementUse`, `DecrementUse`, `Lock`, `Unlock`, `Release`).
   - Allocates wired kernel pages mapped into the calling process (`backboardd`, `SpringBoard`).
2. **`XZSFramebuffer` (`AppleCLCD` / `IOMobileFramebuffer`)**:
   - Publishes itself under `IODeviceTree:/arm-io/disp0`.
   - Reports display geometry matching the target device profile (1080x1920 native or 1125x2436 iPhone X presentation).
   - Generates synthetic 60 Hz VSYNC notifications (`kSweetFBNotifications / notify(SWEET_FB_NOTIFY_VSYNC)`).
   - **Swap Execution**: On `SwapEnd`, fetches the active layer surface pointer and performs a high-speed CPU raster copy into the physical framebuffer memory (`0x83401000` or `0x98000000`), regulated by `sweetfb_hz`.

### 3.2 Security & Sandboxing: `xzs_amfi.c`
Authentic Apple iOS daemons depend on Mach port lookups and XPC endpoints that are strictly blocked if a kernel Sandbox or AMFI policy returns an error.
1. Implements a permissive MAC policy module named `"Sandbox"`:
   - `mpo_policy_syscall`: returns `0` for initialization, check, and note operations.
   - Container and extension queries return `ENOENT` cleanly rather than throwing fatal policy errors (`ENOPOLICY`).
2. Implements permissive `"AMFI"`:
   - Disables mandatory entitlement enforcement for daemon posix_spawn.
   - Permits execution of binaries with non-Apple signatures or unsigned shared caches.

### 3.3 Hardware Identity: Apple Device Tree (`tools/mkadt.py`)
`MobileGestalt` and UIKit determine device class, button presence, and status-bar configuration strictly through the Apple Device Tree (ADT):
- `compatible`: `D22AP|iPhone10,3|sony,keyaki|qualcomm,msm8996`
- `hw.product`: `iPhone10,3`
- `hw.target`: `D22AP`
- `/buttons`: `home-button-type = 2` (Gesture-based home bar UI, eliminating dependency on physical home button).
- `/chosen`:
  - `display-scale`: `3` (enabling 3x Retina rendering without UI truncation).
  - `certificate-production-status`: `0` (triggers `mobileactivationd` native Hactivation).
  - `unique-chip-id` & `chip-id`: synthetic values allowing `IOPlatformExpert` to generate a stable, valid `IOPlatformUUID` (mandatory for `CFPreferences` ByHost storage and DataMigration bypass).

---

## 4. Multi-Phase Implementation Roadmap

```text
Phase D8 (Complete/Seal Display & Text Console)
  ├── D8-M8.5: Sealed (WLED Backlight + First Visible Pattern)
  └── D8-M10:  In-Progress (Framebuffer Console & Shell)
         ↓
Phase D9 (Apple Compatibility Services Layer — XZSAppleCompat)
  ├── D9-M1: Port XZSSurface & XZSFramebuffer (IOCoreSurfaceRoot + AppleCLCD)
  ├── D9-M2: Port xzs_amfi.c (Permissive AMFI + Sandbox MAC Policies)
  ├── D9-M3: Port XZSKeyStore.cpp (AppleKeyStore stub selectors 17, 35, 101)
  └── D9-M4: Port XZSHID.cpp + Apple IOHIDFamily-1915 integration
         ↓
Phase D10 (Storage, Root Filesystem & IPSW Pipeline)
  ├── D10-M1: Implement HFS+ read/write VFS support in XNU
  ├── D10-M2: Extract iPhone X (16.7.16) / iPhone 7/8 IPSW via mkiosroot.sh
  ├── D10-M3: Apply xzs-root-tweaks.py (DataMigration bypass, Hactivation, Setup skip)
  └── D10-M4: Flash & mount HFS+ root on eMMC userdata partition (rd=emmcroot0)
         ↓
Phase D11 (System Integration & Apple Device Tree)
  ├── D11-M1: Implement tools/mkadt.py for Xperia XZs (MSM8996 MMIO + iPhone X identity)
  └── D11-M2: Build unified bootshim payload (Kernel + ADT + boot-args)
         ↓
Phase D12 (Authentic iOS Userland Execution)
  ├── D12-M1: Launch /sbin/launchd & dyld shared cache execution
  ├── D12-M2: Core daemons startup (logd, configd, containermanagerd)
  ├── D12-M3: Window server bring-up (backboardd matching XZSFramebuffer)
  └── D12-M4: SpringBoard Home Screen execution on physical Xperia XZs
```

---

## 5. Risk Assessment & Mitigations

1. **CPU Architecture Differences (ARMv8.0 vs ARMv8.2)**:
   - *Risk*: iOS 16 userland (compiled for A11 Bionic) might contain ARMv8.2 atomic instructions (LSE: `LDADD`, `CAS`) not supported by Kryo (ARMv8.0).
   - *Mitigation*: The `arm64` slice of iOS 16 remains compatible with ARMv8.0 baselines (Apple A11 still runs legacy arm64 code). If single LSE instructions are encountered, XNU's undefined instruction trap (`sleh.c`) can emulate them transparently, or the pipeline can target iOS 15.7 (which officially supported iPhone 7 / A10 ARMv8.0).
2. **eMMC Storage Throughput**:
   - *Risk*: iOS daemons perform heavy concurrent I/O during boot. Polled or synchronous eMMC access could cause launchd watchdogs to trigger.
   - *Mitigation*: Adopt `BKNoWatchdogs=True` in `com.apple.springboard.plist` and batch disk log flushes, as proven in `ios-sweet` patch `0069`.

---

## 6. Document Sign-Off

* **Author**: Assistant & lechaukha12
* **Reference Artifact**: `ios-sweet/` (commit `11da58b`)
* **Target Project**: `xnu-xzs` (commit `0d762d4`)
* **Status**: APPROVED & ACTIVE ARCHITECTURAL DIRECTIVE
