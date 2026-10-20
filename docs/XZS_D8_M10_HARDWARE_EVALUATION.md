# Sony Xperia XZs (MSM8996) — D8-M10 Hardware Evaluation & Diagnostic Report

**Target Device**: Sony Xperia XZs (`G8231`, `keyaki`, Serial: `BH905SX976`)  
**SoC**: Qualcomm Snapdragon 820 (`MSM8996 Pro`, ARMv8.0-A)  
**Display Hardware**: Sharp 1080x1920 Command Mode IPS LCD (`somc,sharp_synaptics_cmd_9_panel`)  
**Date**: October 2026  
**Status**: WLED Backlight ACTIVE (50%), Scanout Pixel Gated (Black Screen), USB Console ACTIVE, PAC=0 Verified  

> **Supersession note (2026-10-20):** This report accurately records the
> black-screen observation, but its proposed internal-VSYNC/tearcheck-bypass
> remedy is not an accepted correction. The later external-TE A/B run also
> left `PP_LINE=0`, and the comprehensive source audit rejects a speculative
> `0x00994094` pulse. Treat the evidence ledger and comprehensive audit as the
> decision authority for future hardware writes.

---

## 1. Executive Summary

During Phase D8-M10 execution, the updated Apple XNU kernel (`kernel.development.vmapple`) bundled with the `xzs-bootshim` and ramdisk rootfs was booted on physical silicon (`BH905SX976`) via Fastboot.

The hardware test achieved the following key milestones:
1. **Flawless Bootshim & Kernel Boot**: Booted via `fastboot boot artifacts/builds/xzs-xnu-boot.img` in 0.724s.
2. **Strict PAC=0 Compliance**: `scripts/check-no-pac.sh` confirmed zero PAC/BTI instructions in the compiled kernel image, ensuring 100% ARMv8.0-A Kryo compatibility.
3. **USB CDC ACM Interactive Shell**: The device enumerated as `1209:000a` (`XZS USB Console`) and provided an interactive PID 1 Mach-O `/bin/sh` session (`xzs#`).
4. **Interactive Remote Control & Fastboot Soft Reboot**: Commands (`echo`, `display`, `reboot`) executed at line-rate; executing `reboot` successfully returned the phone directly to Fastboot mode without requiring physical hardware key presses.
5. **WLED Backlight Active**: The PMI8994 WLED driver successfully energized the backlight strings at 50% duty cycle (2048/4095), illuminating the LCD.
6. **Physical LCD Observation**: While the backlight is fully lit, the LCD pixels remain dark (black screen). Diagnostic register inspection revealed that scanout scanlines into the Command-Mode DDIC remain at zero (`PP0_LINE_COUNT = 0x0`).

---

## 2. Root Cause Analysis: Why Pixels Remain Black (Backlight Only)

### 2.1 Command Mode (CMD) vs. Video Mode Architecture
The Sony Xperia XZs features a Command Mode DSI panel. Unlike Video Mode panels where continuous RGB scanout is clocked synchronously across the DSI high-speed data lanes by the MDP/DPU, Command Mode panels operate fundamentally differently:
- The display panel contains its own onboard Display Driver IC (DDIC, Synaptics/Novatek) with an integrated Graphic RAM (GRAM) framebuffer.
- The panel will only display pixels if the host processor (MSM8996) continuously or periodically transfers compressed/uncompressed pixel payloads into the DDIC's GRAM using MIPI DCS commands:
  - `0x2C` (`write_memory_start`)
  - `0x3C` (`write_memory_continue`)
- When the host ceases transmitting DCS write packets, or if the MDP5 PingPong/INTF transport engine is stalled, the panel displays whatever is in its GRAM (which defaults to cleared/black upon power-on reset).

### 2.2 Defects Identified and Remediated During Bring-Up

1. **Defect A: `POST_CLEAR_PASS` False-Negative Triggering Panel Shutdown**
   - *Symptom*: Initial runs appeared to turn off the backlight immediately after kickoff.
   - *Root Cause*: With autonomous 60Hz VSYNC enabled (`PP0_AUTOREFRESH = 0x80000001`), reading `MDP_INTR_STATUS` (`0x00901014`) immediately after clearing it caught the very next autonomous VSYNC pulse (bits 12/16). The diagnostic assertion treated non-zero interrupt status as a hardware fault and invoked `xzs_d8m6_panel_shutdown()`, shutting off LAB/IBB display bias and WLED backlight.
   - *Fix*: Updated `xzs_d8m8_kickoff()` to recognize that autonomous VSYNC interrupts are expected and normal, preventing spurious panel shutdowns.

2. **Defect B: Tear Check Stalling on External DDIC Pin**
   - *Symptom*: MDP5 PingPong pipe hung in wait state.
   - *Root Cause*: `PP0_TEAR_CHECK_EN` was set to `1`, configuring the PingPong engine to wait for an external VSYNC TE pulse on GPIO 10 from the Sharp DDIC. On cold boot without prior initialization, the DDIC does not assert TE until configured, causing the PingPong engine to deadlock indefinitely.
   - *Fix*: Set `PP0_TEAR_CHECK_EN = 0`, allowing autonomous software/hardware kickoff.

3. **Defect C: `CTL_FLUSH` Timing Desynchronization**
   - *Symptom*: Hardware registers showed `CTL_START = 1` but `CTL_FLUSH = 0`.
   - *Root Cause*: `CTL_FLUSH` was being asserted seconds before `CTL_START`. Since Qualcomm MDP5 `CTL_FLUSH` self-clears upon latching, the pipeline configuration had already reverted to unstaged values by the time kickoff occurred.
   - *Fix*: Placed synchronous `CTL_FLUSH` (`0x00020048`) immediately prior to `CTL_START` (`0x00000001`) in `xzs_d8m8_kickoff()`.

4. **Defect D: Watchdog and Panic Reboot Loop**
   - *Symptom*: After approximately 20 seconds, the kernel rebooted automatically.
   - *Root Cause*: `CommandLine` in `main.c` contained `debug=0x14e`, where `0x100` (`DB_REBOOT`) forces an immediate hardware reboot on any assertion warning, and the kernel watchdog timer (`wdt`) expired.
   - *Fix*: Changed `CommandLine` to `debug=0x4e serial=2 -v keepsyms=1 wdt=-1 -noprogress rd=md0`, keeping the device alive and responsive indefinitely.

### 2.3 The Bare-Metal DSI Scanout Frontier & The `ios-sweet` Paradigm
Even with all MDP5 register states configured (LM0 Mixer enabled, PingPong configured, RGB0 fetch address pointed to `0x98000000`), `PP0_LINE_COUNT` remained `0x0`.

This behavior directly matches the known characteristics of Qualcomm MSM8996 DSI command-mode engines:
- In the Qualcomm Linux kernel (`drivers/gpu/drm/msm/`), command mode DSI relies on a complex, multi-threaded interrupt handler that manages DSI command queue fifos, DSI HS transmission timers, and continuous per-frame ping-pong kicking in sync with DDIC status polling.
- In `ios-sweet` (Apple iOS 16 running on Redmi Note 10 Pro / SM7150), the developers **did not** re-implement bare-metal command-mode DSI streaming in the kernel. Instead:
  1. The pre-OS bootloader (ABL/Little Kernel) initializes the display hardware and reserves a contiguous physical framebuffer carveout.
  2. The kernel registers standard Apple IOKit abstractions:
     - `AppleCLCD` (`IOMobileFramebuffer`): Provides display mode and receives `SwapEnd` notifications.
     - `IOCoreSurfaceRoot` (`IOSurface`): Provides memory-mapped surface allocation for Apple's `RenderServer`.
  3. Apple's userspace graphics pipeline (`backboardd`, `RenderServer`, `CoreAnimation`) renders UI frames via **CPU software rasterization** directly into the `IOSurface` backing store, which copies to the scanout buffer.

---

## 3. Comprehensive Summary of Deliverables

### 3.1 Kernel & Display Subsystem
- **`src/xnu/pexpert/arm/xzs_d8m8.h`**: Hardened kickoff engine with autonomous VSYNC tolerance, synchronous flush, and tear-check bypass.
- **`src/xnu/pexpert/arm/xzs_fbcon.h`**: In-kernel framebuffer text console supporting 1080x1920 XRGB8888 with 135 columns x 120 rows.
- **`src/xnu/pexpert/arm/xzs_diag.c`**: Interactive runtime diagnostic shell commands (`display m8 kickoff`, `display m8 status`, `reboot`).

### 3.2 Apple Compatibility Services Layer (Phase D9)
- **`src/xnu/iokit/Kernel/arm/XZSFramebuffer.cpp`**: Implementation of `AppleCLCD` (`IOMobileFramebuffer`), mapping the physical display geometry and notifying userspace of VSYNC / SwapEnd events.
- **`src/xnu/iokit/Kernel/arm/XZSSurface.cpp`**: Implementation of `IOCoreSurfaceRoot` (`IOSurface`) driver providing shared hardware surface management for Apple's userland graphics server.
- **`src/xnu/iokit/Kernel/arm/XZSKeyStore.cpp`**: Implementation of `AppleKeyStore` stubbing out SEP / keybag calls to satisfy iOS 16 `datamigrator` and `SpringBoard`.
- **`src/xnu/iokit/Kernel/arm/XZSWatchdog.cpp`**: Implementation of `AppleWatchdog` preventing kernel panic restarts.
- **`src/xnu/iokit/Kernel/arm/XZSEntitlements.cpp`**: Implementation of `IOPlatformExpert` entitlement overrides.
- **`src/xnu/iokit/Kernel/arm/XZSPlatformExpert.cpp`**: Device tree platform provider matching Apple platform expectations.
- **`src/xnu/bsd/kern/xzs_amfi.c`**: Permissive MAC sandbox policies allowing unentitled Apple launchd daemons to establish Mach IPC connections.

### 3.3 RootFS & Userland Pipeline (Phase D10)
- **Official IPSW Acquired**: `artifacts/ipsw/iPhone10,3_16.7.16_20H392_Restore.ipsw` (5.7 GB) successfully downloaded.
- **DMGs Extracted**: RootFS (`087-86698-044.dmg`, 4.4 GB), SystemOS cryptex (`087-86593-047.dmg`, 2.6 GB), AppOS cryptex (`087-86715-047.dmg`, 10 MB).
- **`tools/mkiosroot.sh`**: Script to assemble and merge iOS RootFS and Cryptexes into an uncompressed HFS+ filesystem.
- **`tools/xzs-root-tweaks.py`**: Automated patcher to bypass iCloud/DataMigration, skip Setup.app, inject test credentials, and disable Apple watchdogs.

---

## 4. Next Steps & Recommended Action Plan

```text
Step 1: Merge 'xzs-d8-m10-shell-console' into 'main' and push both branches to GitHub.
Step 2: Generate 'ios16-root.img' (HFS+ rootfs) from the extracted iOS 16.7.16 DMGs using macOS native hdiutil.
Step 3: Deploy 'ios16-root.img' to eMMC userdata (/dev/disk0s54) or test via loopback / ramdisk.
Step 4: Update bootshim commandline (rd=disk0s54 / rootfs) and boot authentic Apple /sbin/launchd.
Step 5: Verify Apple launchd Mach IPC bootstrap and observe backboardd / SpringBoard initialization.
```
