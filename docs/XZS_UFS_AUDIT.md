# Qualcomm MSM8996 UFS Hardware & Software Audit (Phase D2)

**Document Version:** 1.1.0  
**Phase:** D2 — Qualcomm UFS Controller Bring-up  
**Platform:** Sony Xperia XZs (`G8231` / `tone` / `keyaki`)  
**SoC:** Qualcomm Snapdragon 820 (`MSM8996` / `MSM8996 Pro`)  
**Date:** 2026-09-18  
**Classification:** `SOURCE AUDIT` / `CANONICAL SPEC` (Hardware Verification Pending)

---

## 1. Executive Summary

Phase D1 verified that Apple XNU successfully boots through BSD initialization, mounts VFS, configures IOKit, executes root device discovery, and halts cleanly at the storage boundary (`bdevvp()` returning canonical `ENODEV 0x13`).

Phase D2 implements communication with the physical Universal Flash Storage (UFS) subsystem on the Xperia XZs. The ultimate goal of Phase D2 is to initialize the host controller and PHY, issue a read command to physical storage, and dump/verify a real physical sector (LBA 0).

This document audits the hardware registers, interconnect paths, clocks, resets, power rails, DMA architecture, SMMU interaction, and operating system driver flows required for Phase D2.

---

## 2. Hardware Resource & Address Map

The primary hardware definitions are derived from the audited device tree sources (`device/reference/msm8996.dtsi`, upstream Linux `arch/arm64/boot/dts/qcom/msm8996.dtsi`, and Sony Tone/Keyaki board files).

### 2.1 Audited Register Apertures

| Subsystem / Block | Physical Address | Size | Description | Source Reference | Confidence |
| :--- | :--- | :--- | :--- | :--- | :--- |
| **UFS Host Controller (UFSHCI)** | `0x00624000` | `0x2500` (9.25 KB) | JEDEC UFS 2.0 Host Controller with extension/vendor registers | `device/reference/msm8996.dtsi:2144` | `SOURCE AUDIT` |
| **UFS QMP M-PHY** | `0x00627000` | `0x1000` (4 KB) | Qualcomm Multi-PHY (QMP) M-PHY v2 (`QSERDES_V2`) | `device/reference/msm8996.dtsi:2194` | `SOURCE AUDIT` |
| **GIC Interrupt** | SPI 265 (`INTID 297`) | 1 Line | Level-sensitive interrupt routed to GICv3 distributor (`0x09bc0000`) | `device/reference/msm8996.dtsi:2145` | `SOURCE AUDIT` |

### 2.2 Register Aperture Partitioning

* **`0x000` – `0x09F` (Core UFSHCI Region):** Standard JEDEC UFSHCI registers (`CAP`, `VER`, `HCPID`, `HCMID`, `AHIT`, `IS`, `IE`, `HCS`, `HCE`, `UICCMD`, v.v.).
* **`>= 0x0A0` (Extension / Implementation-Defined Region):** May contain implementation-defined, extension, or Qualcomm-specific registers depending on controller revision. Phase D2-A only accesses audited read-only offsets (`REG_UFS_HW_VERSION = 0xE4`).

---

## 3. UFSHCI Register Specifications (JEDEC JESD223B / JESD220B)

The host controller implements standard JEDEC Universal Flash Storage Host Controller Interface (UFSHCI) registers at offsets `0x000` – `0x09F`.

### 3.1 Standard UFSHCI Registers (Core Region: 0x000 – 0x09F)

| Offset | Name | Description | Read / Write | D2-A Access |
| :--- | :--- | :--- | :--- | :--- |
| `0x00` | `CAP` | Controller Capabilities (Slots, 64-bit addressing, etc.) | RO | **READ-ONLY** |
| `0x08` | `VER` | UFSHCI Version (`major[15:8]`, `minor[7:4]`) | RO | **READ-ONLY** |
| `0x10` | `HCPID` | Host Controller Product ID | RO | **READ-ONLY** |
| `0x14` | `HCMID` | Host Controller Manufacturer ID | RO | **READ-ONLY** |
| `0x18` | `AHIT` | Auto-Hibernate Idle Timer | RW | **READ-ONLY** (no write in D2-A) |
| `0x20` | `IS` | Interrupt Status (cleared by writing 1) | R/W1C | **READ-ONLY** (no clear in D2-A) |
| `0x24` | `IE` | Interrupt Enable | RW | **READ-ONLY** (no enable in D2-A) |
| `0x30` | `HCS` | Host Controller Status (`DP`, `UTRLRDY`, `UTMRLRDY`, `UCRDY`) | RO | **READ-ONLY** |
| `0x34` | `HCE` | Host Controller Enable (Bit 0 = 1: enable, 0: disable/reset) | RW | **READ-ONLY** (no toggle in D2-A) |
| `0x38` | `UECPA` | UIC Error Code: PHY Adapter Layer | RO | Available for diagnostics |
| `0x3C` | `UECDL` | UIC Error Code: Data Link Layer | RO | Available for diagnostics |
| `0x40` | `UECN` | UIC Error Code: Network Layer | RO | Available for diagnostics |
| `0x44` | `UECT` | UIC Error Code: Transport Layer | RO | Available for diagnostics |
| `0x48` | `UECDME`| UIC Error Code: DME | RO | Available for diagnostics |
| `0x4C` | `UTRIACR`| UTP Transfer Request Interrupt Aggregation Control | RW | Untouched in D2-A |
| `0x50` | `UTRLBA` | UTP Transfer Request List Base Address (Lower 32 bits) | RW | Untouched in D2-A |
| `0x54` | `UTRLBAU`| UTP Transfer Request List Base Address (Upper 32 bits) | RW | Untouched in D2-A |
| `0x58` | `UTRLDBR`| UTP Transfer Request Doorbell Register | RW | Untouched in D2-A |
| `0x5C` | `UTRLCLR`| UTP Transfer Request List Clear Register | RW | Untouched in D2-A |
| `0x60` | `UTRLRSR`| UTP Transfer Request List Run/Stop Register | RW | Untouched in D2-A |
| `0x70` | `UTMRLBA`| UTP Task Management Request List Base Address (Lower 32) | RW | Untouched in D2-A |
| `0x74` | `UTMRLBAU`| UTP Task Management Request List Base Address (Upper 32) | RW | Untouched in D2-A |
| `0x78` | `UTMRLDBR`| UTP Task Management Request Doorbell Register | RW | Untouched in D2-A |
| `0x7C` | `UTMRLCLR`| UTP Task Management Request List Clear Register | RW | Untouched in D2-A |
| `0x80` | `UTMRLRSR`| UTP Task Management Request List Run/Stop Register | RW | Untouched in D2-A |
| `0x90` | `UICCMD` | UIC Command Opcode (triggers DME command) | RW | Untouched in D2-A |
| `0x94` | `UCMDARG1`| UIC Command Argument 1 | RW | Untouched in D2-A |
| `0x98` | `UCMDARG2`| UIC Command Argument 2 | RW | Untouched in D2-A |
| `0x9C` | `UCMDARG3`| UIC Command Argument 3 | RW | Untouched in D2-A |

### 3.2 Audited Extension / Qualcomm Registers (Region: >= 0x0A0)

| Offset | Name | Purpose | D2-A Access |
| :--- | :--- | :--- | :--- |
| `0xE4` | `REG_UFS_HW_VERSION` | Qualcomm hardware version (`major[31:28]`, `minor[27:16]`, `step[15:0]`) | **READ-ONLY** |
| `0xC0` | `REG_UFS_SYS1CLK_1US` | Core clock cycles per 1 microsecond (calibration) | Untouched in D2-A |
| `0xDC` | `REG_UFS_CFG1` | UniPro mode, debug rams config | Untouched in D2-A |

---

## 4. Clock and Power Domain Audit & Classification

### 4.1 Controller Clocks (`msm8996.dtsi:2152-2181`)

| Clock Name | Provider & Signal | Freq Range | Linux Stage | Initial Boot Status Classification |
| :--- | :--- | :--- | :--- | :--- |
| `core_clk` | `gcc GCC_UFS_AXI_CLK` | 100 MHz – 200 MHz | Controller bus interface | `HYPOTHESIS / NOT HARDWARE VERIFIED` |
| `bus_clk` | `gcc GCC_SYS_NOC_UFS_AXI_CLK` | Auto / NoC rate | System NoC interconnect | `HYPOTHESIS / NOT HARDWARE VERIFIED` |
| `bus_aggr_clk` | `gcc GCC_AGGRE2_UFS_AXI_CLK` | Auto / NoC rate | Aggre2 NoC interface | `HYPOTHESIS / NOT HARDWARE VERIFIED` |
| `iface_clk` | `gcc GCC_UFS_AHB_CLK` | Auto / AHB rate | Register MMIO access | `HYPOTHESIS / NOT HARDWARE VERIFIED` |
| `core_clk_unipro` | `gcc GCC_UFS_UNIPRO_CORE_CLK` | 75 MHz – 150 MHz | UniPro link protocol layer | `HYPOTHESIS / NOT HARDWARE VERIFIED` |
| `core_clk_ice` | `gcc GCC_UFS_ICE_CORE_CLK` | 150 MHz – 300 MHz | Inline Crypto Engine (ICE) | `HYPOTHESIS / NOT HARDWARE VERIFIED` |
| `ref_clk` | `rpmcc RPM_SMD_LN_BB_CLK` | 19.2 MHz | Reference clock | `HYPOTHESIS / NOT HARDWARE VERIFIED` |
| `tx_lane0_sync_clk` | `gcc GCC_UFS_TX_SYMBOL_0_CLK` | Symbol rate | PHY TX Lane 0 sync | `HYPOTHESIS / NOT HARDWARE VERIFIED` |
| `rx_lane0_sync_clk` | `gcc GCC_UFS_RX_SYMBOL_0_CLK` | Symbol rate | PHY RX Lane 0 sync | `HYPOTHESIS / NOT HARDWARE VERIFIED` |

### 4.2 Power Rails & GDSC Classification

* **Global Distributed Switch Controller (GDSC):** `&gcc UFS_GDSC`
* **Regulator Supplies:** `vcc` (3.0V flash core), `vccq` (1.2V core logic), `vccq2` (1.8V I/O).
* **Bootloader Handoff Classification:**
  * `BOOTLOADER-INITIALIZED UFS`: **`HIGH-CONFIDENCE INFERENCE`** (Sony S1 ABOOT reads boot image from UFS prior to kernel handoff).
  * `BOOTLOADER-LEFT-ON CLOCKS/PHY/LINK`: **`HYPOTHESIS / NOT HARDWARE VERIFIED`** (We do not assume clocks, PHY, or link remain powered or unclocked until hardware probe confirms register readability).
  * `POWER SEQUENCE NOT YET OWNED BY XNU`: XNU will not write or cycle power/regulators during Phase D2-A.

---

## 5. Qualcomm UFS PHY (QMP M-PHY v2)

* **Linux Upstream Driver:** `drivers/phy/qualcomm/phy-qcom-qmp-ufs.c`.
* **PHY Generation:** MSM8996 implements QMP v2 (`QSERDES_V2`).
* **Registers & Configuration Tables:**
  1. `msm8996_ufsphy_serdes`: 47 register writes to `QSERDES_V2_COM_*` registers (`0x000` – `0x1FC`).
  2. `msm8996_ufsphy_tx`: Register writes to `QSERDES_V2_TX_*`.
  3. `msm8996_ufsphy_rx`: Register writes to `QSERDES_V2_RX_*`.
* **Phase D2-A Constraint:** **NO PHY ACCESS.** PHY programming is deferred to Phase D2-C after controller MMIO sanity is hardware-verified.

---

## 6. Interconnect, DMA Architecture & SMMU Relationship

### 6.1 Interconnect Topology (`msm8996.dtsi:2183-2185`)

```text
+-----------------------+
|  UFS Host Controller  | (0x00624000)
+-----------------------+
           │
           │ MASTER_UFS
           ▼
+-----------------------+
|       a2noc           | (Aggre2 NoC interconnect @ 0x00583000)
+-----------------------+
           │
           │ SLAVE_EBI_CH0
           ▼
+-----------------------+
|       bimc            | (Bus Interface / Memory Controller @ 0x00408000)
+-----------------------+
           │
           ▼
+-----------------------+
|    Physical LPDDR4    | (0x80000000 - 0x17fffffff)
+-----------------------+
```

### 6.2 SMMU Audit Findings & Classification

* **Audited Device Tree Finding:** In `device/reference/msm8996.dtsi`, the `ufshc` node contains **NO** `iommus = <...>` property.
* **Classification:** **`NO DT IOMMU ASSOCIATION OBSERVED`**.
* **DMA Coherency Classification:** **`UNKNOWN / NOT REQUIRED FOR D2-A`**.
* **D2-A Constraint:** Phase D2-A does **not** allocate DMA descriptors, program DMA base registers, or issue DMA requests.
* **Cache Instruction Policy:** **Zero cache maintenance instructions** (`dc cvac`, `dc civac`, `dc civau`). No speculative cache invalidation or flushes are permitted.

---

## 7. MMIO Mapping Source Audit in XNU ARM64

A source audit of `ml_io_map()` was performed to trace the exact translation from physical MMIO address to ARM64 Translation Table descriptors:

```text
ml_io_map(phys_addr, size)
    │   [src/xnu/osfmk/arm64/machine_routines.c:1747]
    ▼
io_map(phys_addr, size, VM_WIMG_IO, VM_PROT_DEFAULT, false)
    │   [src/xnu/osfmk/arm/io_map.c:88]
    ├── kmem_alloc(io_submap, &start, alloc_size, KMA_NOFAIL | KMA_PAGEABLE | KMA_PERMANENT, VM_KERN_MEMORY_IOKIT)
    │   [Allocates KVA space from io_submap]
    ▼
pmap_map(start, phys_addr, phys_addr + alloc_size, VM_PROT_DEFAULT, VM_WIMG_IO)
    │   [src/xnu/osfmk/arm/pmap/pmap.c:1865]
    ▼
pmap_enter(kernel_pmap, virt, atop(phys), VM_PROT_DEFAULT, VM_PROT_NONE, VM_WIMG_IO, FALSE, PMAP_MAPPING_TYPE_INFER)
    │   [src/xnu/osfmk/arm/pmap/pmap.c]
    ▼
ARM64 Stage-1 PTE Attribute Programming:
    - AttrIndx: MAIR index for VM_WIMG_IO (Guarded, Non-Cacheable)
    - Type: Device-nGnRnE (Device non-Gathering, non-Reordering, Early Write Acknowledgment)
    - Memory Barriers: dsb sy / isb sy executed around mapping creation
```

Result: Accessing `ml_io_map(0x00624000, 0x2500)` guarantees strict `Device-nGnRnE` semantics without speculative reads or out-of-order writes.

---

## 8. Current XNU Storage Architecture Audit

* `src/xnu/`: Zero hardware UFS or UFSHCI driver files exist.
* `IOStorageFamily` is external to XNU.
* Phase D2-A will use a modular C implementation under `pexpert/arm/`:
  * `pexpert/pexpert/arm/xzs_ufs.h`: Audited register definitions and macros.
  * `pexpert/arm/xzs_ufs.c`: Low-level read-only probe harness.
* Gated by `#if CONFIG_XZS_BRINGUP` and invoked before `[D50]` in `bsd_init.c`.

---

## 9. Telemetry & Automation Pipeline Protection Invariants

The automated test loop:
```text
CODE -> BUILD -> FASTBOOT -> RUN -> PERSIST LOG -> AUTO RESET -> FASTBOOT -> AUTO RECOVERY -> EXTRACT LOG
```
is a hard engineering invariant.

* **Persistent Memory Regions:** `0x80060000` (DRAM log), `0xa7f00000` (dmesg ramoops), `0xa7fbe000` (console ramoops), `0x066bf65c` (IMEM cookie) must remain **completely untouched and reserved**.
* **Terminal Recovery:** Both success (`[U04]`) and failure paths must call `xzs_spin_halt()` to trigger the APCS Watchdog Bite and automated reboot to Fastboot.
* **Watchdog Policy:** No petting watchdog inside blocking/polling loops.

---

## 10. Conclusion

Phase D2-A is strictly defined as a **Read-Only MMIO Sanity Probe**. It verifies whether the UFS host controller aperture is responsive and readable, captures raw `CAP`, `VER`, `QCOM_HW_VER`, and status registers, and halts cleanly back to Fastboot for automated log extraction.

---

## 11. Phase D2-A.1 — UFS Prerequisite-State Audit (GCC / GDSC / Resets)

Hardware testing in Phase D2-A revealed that reading `0x00624000` (`CAP`) triggers an immediate ARM64 Asynchronous System Error (SError / Bus Abort at `sleh.c:2862`). This proves that the UFS host controller is unclocked, power-gated, or held in reset by platform controllers after the Sony bootloader handoff.

Phase D2-A.1 inspects the exact state of these prerequisites via the Global Clock Controller (GCC) aperture (`0x00300000`, size `0x90000`).

### 11.1 Audited Prerequisite Register Table

| Logical resource | Register name | Physical base | Offset | Absolute address | Relevant bits | Source file | Source line/reference | Confidence |
| :--- | :--- | :--- | :--- | :--- | :--- | :--- | :--- | :--- |
| **GCC Aperture Base** | `GCC` | `0x00300000` | `+0x00000` | `0x00300000` | Size: `0x90000` (576 KB) | `msm8996.dtsi:788` | `reg = <0x00300000 0x90000>` | `SOURCE AUDIT` |
| **UFS Block Reset (BCR)** | `GCC_UFS_BCR` | `0x00300000` | `+0x75000` | `0x00375000` | `bit 0`: `BLK_ARES` (1 = in reset, 0 = released) | `gcc-msm8996.c:3575` | `[GCC_UFS_BCR] = { 0x75000 }` | `SOURCE AUDIT` |
| **UFS Power Domain (GDSC)** | `UFS_GDSC` | `0x00300000` | `+0x75004` | `0x00375004` | `bit 0`: `SW_COLLAPSE_REQ` (1 = collapse, 0 = power on req)<br>`bit 31`: `PWR_ON_STATUS` (1 = powered on, 0 = off) | `gcc-msm8996.c:3270`<br>`gdsc.h:40` | `.gdscr = 0x75004`<br>`SW_COLLAPSE_MASK=BIT(0), PWR_ON_MASK=BIT(31)` | `SOURCE AUDIT` |
| **UFS AXI Core Clock** | `GCC_UFS_AXI_CBCR` | `0x00300000` | `+0x75008` | `0x00375008` | `bit 0`: `CLK_ENABLE` (1 = SW enabled, 0 = disabled)<br>`bit 31`: `CLK_OFF` (1 = gated/inactive, 0 = active) | `gcc-msm8996.c:2643`<br>`clk-branch.h:71` | `.halt_reg = 0x75008, .enable_reg = 0x75008`<br>`CBCR_CLK_OFF=BIT(31), CBCR_CLOCK_ENABLE=BIT(0)` | `SOURCE AUDIT` |
| **UFS AHB Bus Clock** | `GCC_UFS_AHB_CBCR` | `0x00300000` | `+0x7500c` | `0x0037500c` | `bit 0`: `CLK_ENABLE` (1 = SW enabled, 0 = disabled)<br>`bit 31`: `CLK_OFF` (1 = gated/inactive, 0 = active) | `gcc-msm8996.c:2660`<br>`clk-branch.h:71` | `.halt_reg = 0x7500c, .enable_reg = 0x7500c`<br>`CBCR_CLK_OFF=BIT(31), CBCR_CLOCK_ENABLE=BIT(0)` | `SOURCE AUDIT` |
| **System NoC UFS AXI Clock** | `GCC_SYS_NOC_UFS_AXI_CBCR` | `0x00300000` | `+0x75038` | `0x00375038` | `bit 0`: `CLK_ENABLE` (1 = SW enabled, 0 = disabled)<br>`bit 31`: `CLK_OFF` (1 = gated/inactive, 0 = active) | `gcc-msm8996.c:1203`<br>`clk-branch.h:71` | `.halt_reg = 0x75038, .enable_reg = 0x75038`<br>`CBCR_CLK_OFF=BIT(31), CBCR_CLOCK_ENABLE=BIT(0)` | `SOURCE AUDIT` |
| **Aggre2 NoC UFS AXI Clock** | `GCC_AGGRE2_UFS_AXI_CBCR` | `0x00300000` | `+0x83014` | `0x00383014` | `bit 0`: `CLK_ENABLE` (1 = SW enabled, 0 = disabled)<br>`bit 31`: `CLK_OFF` (1 = gated/inactive, 0 = active) | `gcc-msm8996.c:2938`<br>`clk-branch.h:71` | `.halt_reg = 0x83014, .enable_reg = 0x83014`<br>`CBCR_CLK_OFF=BIT(31), CBCR_CLOCK_ENABLE=BIT(0)` | `SOURCE AUDIT` |
| **Aggre2 NoC Reset (BCR)** | `GCC_AGGRE2_NOC_BCR` | `0x00300000` | `+0x83000` | `0x00383000` | `bit 0`: `BLK_ARES` (1 = in reset, 0 = released) | `gcc-msm8996.c:3580` | `[GCC_AGGRE2_NOC_BCR] = { 0x83000 }` | `SOURCE AUDIT` |

### 11.2 Bit Field Interpretation Rules

1. **CBCR (Clock Branch Control Registers):**
   - `CLK_OFF` (`bit 31`): `1` = Clock is halted / gated / inactive; `0` = Clock is actively running.
   - `CLK_ENABLE` (`bit 0`): `1` = Software enable requested; `0` = Disabled.
   - A healthy running branch requires: `CLK_ENABLE == 1` AND `CLK_OFF == 0`.
2. **GDSCR (Global Distributed Switch Controller Registers):**
   - `PWR_ON_STATUS` (`bit 31`): `1` = Power domain is energized (ON); `0` = Power domain is collapsed (OFF).
   - `SW_COLLAPSE_REQ` (`bit 0`): `1` = Software collapse active; `0` = Software power-on active.
   - A powered domain requires: `PWR_ON_STATUS == 1` AND `SW_COLLAPSE_REQ == 0`.
3. **BCR (Block Control Registers):**
   - `BLK_ARES` (`bit 0`): `1` = Block reset asserted; `0` = Reset deasserted (normal operation).

---

## 12. Phase D2-B — Clock Ownership Audit & Topology

Phase D2-A.1 established via direct hardware measurement that:
* `UFS_GDSC` is already **ON** (`PWR_ON_STATUS = 1`, `SW_COLLAPSE_REQ = 0`).
* `UFS_BCR` is already **RELEASED** (`BLK_ARES = 0`).
* All UFS CBCR branches are **OFF / HALTED** (`ENABLE = 0`, `CLK_OFF = 1`).

Phase D2-B takes minimal ownership of the clock path to ungate the MMIO aperture, split into two isolated runs:
- **D2-B1**: Clock path enable and bit transition verification only (no UFS HCI read).
- **D2-B2**: Reproduce verified clock path and read `CAP` (`0x00624000`).

### 12.1 D2-B Clock Ownership Audit Table

| Clock | CBCR absolute address | Parent | Parent source register | Parent current state | Enable semantics | CLK_OFF semantics | Required for first HCI MMIO access? | Source reference |
| :--- | :--- | :--- | :--- | :--- | :--- | :--- | :--- | :--- |
| **`GCC_UFS_AHB_CLK`** | `0x0037500c` | AHB bus root (CNOC/PNOC) | N/A (bus root) | `RUNNING` (GCC accessible) | `bit 0`: `1` (`CBCR_CLOCK_ENABLE`) | `bit 31`: `0` (`ACTIVE`) | **YES (PRIMARY)** — Clock for register bus slave | `gcc-msm8996.c:2660`<br>`msm8996.dtsi:2166` |
| **`GCC_SYS_NOC_UFS_AXI_CLK`** | `0x00375038` | `ufs_axi_clk_src` | `CMD_RCGR` @ `0x00375024`<br>`CFG_RCGR` @ `0x00375028` | Measured in D2-B1 | `bit 0`: `1` (`CBCR_CLOCK_ENABLE`) | `bit 31`: `0` (`ACTIVE`) | **LIKELY** — System NoC to UFS bus bridge | `gcc-msm8996.c:1203`<br>`msm8996.dtsi:2164` |
| **`GCC_AGGRE2_UFS_AXI_CLK`** | `0x00383014` | `ufs_axi_clk_src` | `CMD_RCGR` @ `0x00375024`<br>`CFG_RCGR` @ `0x00375028` | Measured in D2-B1 | `bit 0`: `1` (`CBCR_CLOCK_ENABLE`) | `bit 31`: `0` (`ACTIVE`) | **POSSIBLE** — Aggre2 NoC interconnect bridge | `gcc-msm8996.c:2938`<br>`msm8996.dtsi:2165` |
| **`GCC_UFS_AXI_CLK`** | `0x00375008` | `ufs_axi_clk_src` | `CMD_RCGR` @ `0x00375024`<br>`CFG_RCGR` @ `0x00375028` | Measured in D2-B1 | `bit 0`: `1` (`CBCR_CLOCK_ENABLE`) | `bit 31`: `0` (`ACTIVE`) | **POSSIBLE** — Controller core clock | `gcc-msm8996.c:2643`<br>`msm8996.dtsi:2163` |

### 12.2 Enabling Strategy & Isolation Rules

1. **Order of Operation:**
   - Interconnect bus bridges first (`GCC_SYS_NOC_UFS_AXI_CLK`, `GCC_AGGRE2_UFS_AXI_CLK`)
   - Core controller clock second (`GCC_UFS_AXI_CLK`)
   - AHB register interface clock third (`GCC_UFS_AHB_CLK`)
2. **Controlled Write Protocol:**
   `READ old` -> `WRITE (old | BIT(0))` -> `dsb sy / isb sy` -> `POLL CLK_OFF (bit 31) == 0 (bounded timeout)` -> `VERIFY`.
3. **No GDSC / Reset modifications:** `UFS_GDSC`, `UFS_BCR`, `AGGRE2_NOC_BCR` remain untouched.
4. **No UFS MMIO read in D2-B1:** `0x00624000` is strictly NOT read in D2-B1.

---

## 13. Phase D2-B1 — Hardware Verification Results

Hardware run executed on Sony Xperia XZs (`BH905SX976`) with automated telemetry pipeline.

### 13.1 Hardware-Measured Register Transitions

| Resource | CBCR Address | PRE Raw Value | PRE State | Controlled Write (`old \| BIT(0)`) | POST Raw Value | POST State | CLK_OFF Transition | Verification Result |
| :--- | :--- | :--- | :--- | :--- | :--- | :--- | :--- | :--- |
| **`GCC_SYS_NOC_UFS_AXI_CBCR`** | `0x00375038` | `0x80000000` | Gated (`ENABLE=0`, `CLK_OFF=1`) | `0x80000001` | `0x00000001` | Running (`ENABLE=1`, `CLK_OFF=0`) | `1 -> 0` | **PASS / RUNNING** (CP=0xEB12) |
| **`GCC_AGGRE2_UFS_AXI_CBCR`** | `0x00383014` | `0x80000000` | Gated (`ENABLE=0`, `CLK_OFF=1`) | `0x80000001` | `0x00000001` | Running (`ENABLE=1`, `CLK_OFF=0`) | `1 -> 0` | **PASS / RUNNING** (CP=0xEB13) |
| **`GCC_UFS_AXI_CBCR`** | `0x00375008` | `0x80004220` | Gated (`ENABLE=0`, `CLK_OFF=1`) | `0x80004221` | `0x00004221` | Running (`ENABLE=1`, `CLK_OFF=0`) | `1 -> 0` | **PASS / RUNNING** (CP=0xEB14) |
| **`GCC_UFS_AHB_CBCR`** | `0x0037500c` | `0x80008000` | Gated (`ENABLE=0`, `CLK_OFF=1`) | `0x80008001` | `0x00008001` / `0x20008001` | Running (`ENABLE=1`, `CLK_OFF=0`) | `1 -> 0` | **PASS / RUNNING** (CP=0xEB15) |

### 13.2 Companion & Invariant Registers (Verified Unchanged)

| Resource | Address | PRE Value | POST Value | Invariant Status |
| :--- | :--- | :--- | :--- | :--- |
| **`UFS_GDSC`** | `0x00375004` | `0xa0222000` | `0xa0222000` | **UNCHANGED** (`PWR_ON_STATUS=1`, `SW_COLLAPSE_REQ=0`) |
| **`UFS_BCR`** | `0x00375000` | `0x00000000` | `0x00000000` | **UNCHANGED** (`BLK_ARES=0` / RELEASED) |
| **`AGGRE2_NOC_BCR`** | `0x00383000` | `0x00000000` | `0x00000000` | **UNCHANGED** (`BLK_ARES=0` / RELEASED) |
| **`UFS_AXI_CMD_RCGR`** | `0x00375024` | `0x80000000` | N/A (read-only) | Captured: `ROOT_OFF=1` |
| **`UFS_AXI_CFG_RCGR`** | `0x00375028` | `0x00000000` | N/A (read-only) | Captured |

### 13.3 Key Hardware Findings

1. **Root Clock Reprogramming Not Required:** All 4 branches transitioned from halted (`CLK_OFF=1`) to actively running (`CLK_OFF=0`) immediately upon setting `CBCR_CLOCK_ENABLE` (`bit 0`), without requiring any manual PLL or RCG re-configuration.
2. **Internal Register Bits Preserved:** Bit patterns in `UFS_AXI_CBCR` (`0x4220`) and `UFS_AHB_CBCR` (`0x8000`) were successfully preserved through controlled read-modify-write.
3. **No Timeout Occurred:** All branches unhalted within the first poll iteration, confirming that parent clock sources and power rail `UFS_GDSC` were already fully functional.
4. **Preserved Pipeline:** The test completed with terminal checkpoint `[B17]` (breadcrumb `0xEB17`), followed by `xzs_spin_halt()`, automated watchdog reset (+4s), and automatic TWRP pstore extraction with zero manual button presses.

---

## 14. Phase D2-B2 — UFS HCI Accessibility Hardware Verification Results

Hardware run executed on Sony Xperia XZs (`BH905SX976`) with automated telemetry pipeline.

### 14.1 Hardware-Measured UFS HCI Registers (@ 0x00624000)

| Register | Offset | Raw Silicon Value | Checkpoint | Breadcrumb | Decoded Status / Meaning |
| :--- | :--- | :--- | :--- | :--- | :--- |
| **`CAP`** | `+0x00` | `0x0107000F` | `[B29]` | `CP=0xEB29, ERR=0x0107000F` | `NUTRS=16` (0xF), `NUTMRS=8` (0x7), `64AS=1` (64-bit addressing supported) |
| **`VER`** | `+0x08` | `0x00000100` | `[B30]` | `CP=0xEB30, ERR=0x00000100` | UFSHCI Version 1.0 / 1.1 compliant |
| **`QCOM_HW_VER`** | `+0xE4` | `0x20020000` | `[B31]` | `CP=0xEB31, ERR=0x20020000` | Qualcomm Controller v2.2.0 (Major=2, Minor=2, Step=0) |
| **`HCPID`** | `+0x10` | `0x01000000` | `[B32]` | `CP=0xEB32, ERR=0x01000000` | Host Controller Product ID |
| **`HCMID`** | `+0x14` | `0x00010217` | `[B33]` | `CP=0xEB33, ERR=0x00010217` | JEDEC Manufacturer ID: Bank 2, 0x17 = **Qualcomm** |
| **`AHIT`** | `+0x18` | `0x00000000` | `[B34]` | `CP=0xEB34, ERR=0x00000000` | Auto-Hibernate Idle Timer disabled |
| **`IS`** | `+0x20` | `0x00000000` | `[B35]` | `CP=0xEB35, ERR=0x00000000` | Interrupt Status clear |
| **`IE`** | `+0x24` | `0x00000000` | `[B36]` | `CP=0xEB36, ERR=0x00000000` | Interrupt Enable clear |
| **`HCS`** | `+0x30` | `0x00000000` | `[B37]` | `CP=0xEB37, ERR=0x00000000` | `DP=0`, `UTRLRDY=0`, `UTMRLRDY=0`, `UCRDY=0` |
| **`HCE`** | `+0x34` | `0x00000000` | `[B38]` | `CP=0xEB38, ERR=0x00000000` | Controller disabled (`HCE_DISABLE=0`) |

### 14.2 RCG State Transition (Observed)

| Register | Address | PRE Value | POST Value | Transition |
| :--- | :--- | :--- | :--- | :--- |
| **`UFS_AXI_CMD_RCGR`** | `0x00375024` | `0x80000000` (`ROOT_OFF=1`) | `0x00000000` (`ROOT_OFF=0`) | `ROOT_OFF` automatically cleared when branches enabled! |
| **`UFS_AXI_CFG_RCGR`** | `0x00375028` | `0x00000000` | `0x00000000` | Unchanged |

### 14.3 Causal Proof Established

* **Before clock branch un-gating (Phase D2-A):** `CAP` read @ `0x00624000` produced immediate `ARM64 SError / External Abort`.
* **After controlled clock branch un-gating (Phase D2-B2):** `CAP` read @ `0x00624000` succeeded immediately without fault, returning valid silicon value `0x0107000F`. All standard UFSHCI core and Qualcomm extension registers read cleanly.
* **Causal Result:** **HARDWARE VERIFIED PROVEN.** Clock gating was the sole direct cause of the previous UFS HCI register inaccessibility and SError exception in Phase D2-A.



