# Hardware Map: Sony Xperia XZs (MSM8996 / Tone Keyaki / G8231)

## SoC & Platform
- **SoC:** Qualcomm MSM8996 (Snapdragon 820)
- **CPUs:** 4x Qualcomm Kryo 64-bit ARMv8-A cores
  - Cluster 0: CPU0 (MPIDR 0x000), CPU1 (MPIDR 0x001) ~1.6 GHz
  - Cluster 1: CPU2 (MPIDR 0x100), CPU3 (MPIDR 0x101) ~2.15 GHz
- **SMP Enable Method:** ARM PSCI 1.0 via SMC (`arm,psci-1.0`, method: `smc`)
- **RAM Base:** `0x80000000` (4 GB LPDDR4: `0x80000000` - `0x17fffffff`)

## Core Peripherals & Base Addresses
- **Interrupt Controller (GIC-v3):**
  - Distributor (GICD): `0x09bc0000`, size `0x10000`
  - Redistributors (GICR): `0x09c00000`, size `0x100000` (stride `0x40000` per CPU)
  - Maintenance IRQ: `GIC_PPI 9`
- **Architectural Timer:**
  - ARMv8 Generic Timer (EL1 physical: PPI 14, EL1 virtual: PPI 11, EL2: PPI 10, EL3/Sec: PPI 13)
- **UART / Debug Console:**
  - `blsp2_uart2`: `0x075b0000`, size `0x1000`, IRQ: SPI 114 (Qualcomm MSM UART-DM v1.4)
  - `blsp1_uart2`: `0x07570000`, size `0x1000`, IRQ: SPI 108
  - Ramoops console buffer: physical `0xa7f00000`, size `0x100000` (1 MB)
- **Storage:**
  - UFS Host Controller: `0x00624000`, size `0x2500`, IRQ: SPI 265
  - UFS PHY: `0x00627000`, size `0x1000`
- **Pin Controller / TLMM GPIO:**
  - `0x01010000`, size `0x300000`, IRQ: SPI 208
- **Display Subsystem (MDSS):**
  - MDSS Phys: `0x00900000`, size `0x1000`, IRQ: SPI 83
  - VBIF: `0x009b0000`, size `0x1040`
  - DSI 0: `0x00994000`
  - DSI PHY 0: `0x00994400`

## Reserved Memory Layout
- `hyp_mem`: `0x85800000`
- `xbl_mem`: `0x85e00000`
- `tz_mem`:  `0x86200000`
- `adsp_mem`: `0x8ea00000`
- `gpu_mem`:  `0x90400000`
- `slpi_mem`: `0x90500000`
- `venus_mem`: `0x90f00000`
- `ramoops`:  `0xa7f00000` (size 1 MB)
