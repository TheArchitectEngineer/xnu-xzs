# Sony Xperia XZs (MSM8996 / Tone Keyaki / G8231) Physical Memory Map

This document defines and validates all known physical memory regions on the Sony Xperia XZs based on Sony kernel source trees (`tone.dtsi`, `msm8996.dtsi`), Sony S1 Little Kernel (LK) bootloader sources, and empirical device tree analysis.

Total DRAM on target device: **4,294,967,296 bytes (4 GB)** from `0x80000000` to `0x17fffffff`.

---

## 1. MMIO Peripheral Space (`0x00000000` - `0x07ffffff`)

| Physical Range | Size | Component / Peripheral | Evidence / Source | Classification |
|---|---|---|---|---|
| `0x00300000 - 0x003fffff` | 1 MB | GCC (Global Clock Controller) | `msm8996.dtsi:666` | Safe MMIO |
| `0x004ab000 - 0x004abfff` | 4 KB | PSHOLD (Power Hold / SoC Reset) | `aboot.c:101` | Safe MMIO |
| `0x01010000 - 0x0101ffff` | 64 KB | TLMM (Top Level Mode Multiplexer / GPIO) | `msm8996.dtsi:831` | Safe MMIO |
| `0x0400f000 - 0x0401ffff` | 64 KB | SPMI Bus Arbiter (PMIC interface) | `msm8996.dtsi:1234` | Safe MMIO |
| `0x06a00000 - 0x06a0cbff` | 51 KB | Synopsys DWC3 USB 3.0 Controller | `msm8996.dtsi:3170` | Safe MMIO |
| `0x07410000 - 0x07410fff` | 4 KB | QMP USB 3.0 PHY | `msm8996.dtsi:3185` | Safe MMIO |
| `0x07411000 - 0x07411fff` | 4 KB | QUSB2 High-Speed USB PHY | `msm8996.dtsi:3209` | Safe MMIO |
| `0x075b0000 - 0x075b0fff` | 4 KB | Qualcomm BLSP2 UART2 (Debug Console) | `msm8996.dtsi:1900` | Safe MMIO |
| `0x09830000 - 0x09830fff` | 4 KB | Qualcomm APCS Watchdog | `msm8996.dtsi:2500` | Safe MMIO |

---

## 2. Low DRAM & Early Boot Memory (`0x80000000` - `0x857fffff`)

This 88 MB region represents general application DRAM preceding the Qualcomm Secure World (TrustZone).

| Physical Range | Size | Owner / Purpose | Evidence / Source | Status & Safety |
|---|---|---|---|---|
| `0x80000000 - 0x8005ffff` | 384 KB | Low DRAM Unallocated | Reserved DRAM | **SAFE**: Free DRAM |
| `0x80060000 - 0x8006ffff` | 64 KB | **XZS Telemetry Buffer (`xzs_debug_log`)** | `uart.h:7` | **SAFE & ISOLATED**: Zero overlap with any firmware, kernel, or DTB |
| `0x80070000 - 0x8007ffff` | 64 KB | Guard Space / Early Page Tables | Bootstrap memory | **SAFE**: Guard |
| `0x80080000 - 0x800fffff` | 512 KB | Bootshim / Debug Dumper Image | `package-boot.sh` | **OCCUPIED**: Bootloader execution base |
| `0x80100000 - 0x81ffffff` | ~31 MB | Free DRAM Workspace | Application memory | **SAFE**: Free DRAM |
| `0x82000000 - 0x837fffff` | 24 MB | XNU Kernel Mach-O (`kernel.flat`) | `main.c:135` | **OCCUPIED**: Kernel execution base |
| `0x83800000 - 0x8380ffff` | 64 KB | `struct boot_args` | `main.c:134` | **OCCUPIED**: Boot handoff parameters |
| `0x83810000 - 0x839fffff` | ~2 MB | Apple Device Tree (ADT) | `adt.h:11` | **OCCUPIED**: Device tree handoff |
| `0x83a00000 - 0x857fffff` | ~30 MB | Top of Initial Kernel Mapping | Free DRAM | **SAFE**: Free DRAM |

---

## 3. Qualcomm Secure World & Subsystem Reserved Memory (`0x85800000` - `0xa7ffffff`)

All regions below are defined in device trees with `no-map` attributes. XNU must never touch or map these areas.

| Physical Range | Size | Region Name | Owner / Subsystem | Source in DTSI | Safety Classification |
|---|---|---|---|---|---|
| `0x85800000 - 0x85dfffff` | 6 MB | `hyp_mem` | Qualcomm Hypervisor | `msm8996.dtsi:528` | **UNSAFE**: Hardware protected (`no-map`) |
| `0x85e00000 - 0x85ffffff` | 2 MB | `xbl_mem` | eXtensible Boot Loader | `msm8996.dtsi:533` | **UNSAFE**: Firmware reserved (`no-map`) |
| `0x86000000 - 0x861fffff` | 2 MB | `smem_mem` | Shared Memory (SMEM) | `msm8996.dtsi:538` | **UNSAFE**: Inter-processor IPC (`no-map`) |
| `0x86200000 - 0x887fffff` | 38 MB | `tz_mem` | TrustZone / QSEE | `msm8996.dtsi:543` | **UNSAFE**: Secure OS execution (`no-map`) |
| `0x88800000 - 0x8e9fffff` | 98 MB | `mpss_mem` | Cellular Modem Subsystem | `msm8996.dtsi:559` | **UNSAFE**: Baseband firmware (`no-map`) |
| `0x8ea00000 - 0x903fffff` | 26 MB | `adsp_mem` | Audio DSP Subsystem | `tone.dtsi:42` | **UNSAFE**: Hexagon DSP firmware (`no-map`) |
| `0x90400000 - 0x90401fff` | 8 KB | `gpu_mem` | Adreno GPU Shared Pool | `tone.dtsi:47` | **UNSAFE**: GPU hardware buffer (`no-map`) |
| `0x90500000 - 0x90efffff` | 10 MB | `slpi_mem` | Sensor Low-Power Island | `tone.dtsi:53` | **UNSAFE**: Sensor processor (`no-map`) |
| `0x90f00000 - 0x913fffff` | 5 MB | `venus_mem` | Video Hardware Decoder | `tone.dtsi:58` | **UNSAFE**: Video acceleration (`no-map`) |
| `0x91500000 - 0x916fffff` | 2 MB | `mba_mem` | Modem Boot Authenticator | `msm8996.dtsi:585` | **UNSAFE**: Modem security (`no-map`) |
| `0x8f600000 - 0x8fffffff` | ~10 MB | S1 LK Image | Sony Bootloader runtime | LK `target/msm8996` | **UNSAFE**: Bootloader heap/stack |
| `0xa0000000 - 0xa2000000` | 32 MB | `rmtfs_mem` | Remote Storage (Modem NV) | `msm8996.dtsi:548` | **UNSAFE**: Modem partition cache (`no-map`) |
| `0xa7f00000 - 0xa7ffffff` | 1 MB | `ramoops` | Sony Linux pstore / ramoops | `tone.dtsi:32` | **RESERVED**: Reset-persistent log buffer |

---

## 4. Verification Conclusion for Telemetry Buffer (`0x80060000`)

1. **Distance from Bootloader**: Bootshim loads at `0x80080000` (128 KB above the telemetry buffer).
2. **Distance from Kernel**: XNU kernel loads at `0x82000000` (>31 MB above the telemetry buffer).
3. **Zero Subsystem Conflict**: All modem, DSP, GPU, TrustZone, and SMEM memory regions reside strictly above `0x85800000`. The range `0x80060000 - 0x8006ffff` does not collide with any reserved-memory entry.
4. **TTBR0 Identity Mapping**: The buffer is mapped under TTBR0 Level 2 block entry 0 (`0x80000000 - 0x801fffff`, 2 MB) as normal memory (`ARM_TTE_BOOT_BLOCK_LOWER`), making it identical in physical and virtual address space.
5. **Alternative Candidate**: If empirical tests indicate that low DRAM `0x80060000` is cleared by XBL on fastboot reset, the dedicated `ramoops` region at `0xa7f00000` (1 MB, explicitly configured in Sony's `tone.dtsi` for kernel crash logs) serves as the primary fallback.
