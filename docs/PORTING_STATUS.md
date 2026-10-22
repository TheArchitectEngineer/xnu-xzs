# Porting Status: XNU & iOS 16.7.16 on Sony Xperia XZs (MSM8996)

> [!NOTE]
> See [`docs/XZS_PORT_STATUS.md`](XZS_PORT_STATUS.md) and [`docs/CURRENT_HANDOFF.md`](CURRENT_HANDOFF.md) for the detailed milestone matrix and subsystem breakdown.

---

## Device Specification
- **Device**: Sony Xperia XZs (`G8231` / `tone` / `keyaki`)
- **SoC**: Qualcomm MSM8996 Snapdragon 820 (Kryo quad-core, `ARMv8.0-A`)
- **Display**: 5.2" `1080x1920` Sharp/Synaptics command-mode MIPI DSI LCD
- **Userland**: Authentic Apple iOS 16.7.16 (`20H392`, `iPhone10,3` / `D22AP`) on 14 GB Case-Sensitive Journaled HFS+ (`/dev/disk0s54`)

---

## Current Status Summary
* **Phase A (Native Kernel Entry & `xzs-bootshim`)**: **COMPLETE & HARDWARE PROVEN**
* **Phase B (Platform Bring-up: UARTDM, GICv3, Timers, VM)**: **COMPLETE & HARDWARE PROVEN**
* **Phase C (4-Core Kryo Mach SMP Scheduler)**: **COMPLETE & HARDWARE PROVEN**
* **Phase D1–D4 (BSD/VFS Bootstrap, SDCC1 eMMC 5.1 & 55 GPT Slices)**: **COMPLETE & HARDWARE PROVEN**
* **Phase D5–D7 (RAMDisk XZSFS, PID 1 EL0, DWC3 USB Console & Generic Mach-O Exec)**: **COMPLETE & HARDWARE PROVEN**
* **Phase D8 (Physical 1080x1920 MIPI DSI Display Scanout, Boot Console & Custom Banner)**: **COMPLETE & HARDWARE PROVEN**
* **Phase D9 (`XZSAppleCompat` Kernel Layer: `XZSFramebuffer`, `XZSSurface`, `XZSKeyStore`, `SweetHID`, `sweet_hfs`, `xzs_amfi`)**: **COMPLETE & HARDWARE PROVEN**
* **Phase D10 (iOS 16.7.16 RootFS Mount, `launchd`, `backboardd`, `SpringBoard` Lock Screen & Home Screen)**: **COMPLETE & HARDWARE PROVEN**
* **Phase D11 (Synaptics RMI4 125 Hz Multi-Touch, Edge Gestures, Physical Keys & WLED Sleep/Wake)**: **COMPLETE & HARDWARE PROVEN**
* **Phase D12 (60 Hz 4-Core 1:1 LUT Compositor, Non-Blocking Telemetry & Stock iOS 16 Apps/Widgets)**: **COMPLETE & HARDWARE PROVEN**
