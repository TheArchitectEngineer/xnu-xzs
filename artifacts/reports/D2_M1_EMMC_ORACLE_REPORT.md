# D2-M1 MSM8996 eMMC Hardware Oracle Report

## Mandatory Pre-Task Git Verification

Prior to performing any D2-M1 modifications, the mandatory pre-task commit and push gate was verified:

```text
LOCAL_HEAD  = 13f63cd046dc248fb488acb089bbad3993dc23a5
REMOTE_HEAD = 13f63cd046dc248fb488acb089bbad3993dc23a5
WORKTREE    = CLEAN
DIFF_CHECK  = PASS
BRANCH      = xzs-bringup
```

All previous D2-C2.11 artifacts and documentation were pushed to the remote repository.

---

## C2.11 Documentation Corrections

Historical documentation statements regarding UFS and controller numbering were updated across `artifacts/reports/D2_C211_STOCK_FW_ORACLE_REPORT.md` and `docs/XZS_UFS_PHY_AUDIT.md`:

```text
HARDWARE VERIFIED:
Physical primary storage is eMMC.

HARDWARE VERIFIED:
No UFS block device participates in the boot/storage path.

PROJECT CONCLUSION:
UFS is an invalid storage target for XZs.

UNRESOLVED / NO LONGER RELEVANT:
Reason unused QMP UFS C_READY remained 0.
```

### Controller Numbering Freeze
- `sdhc_1 @ 0x07464900` = `SDC1` = Internal eMMC flash (`/dev/block/mmcblk0`).
- `sdhc_2 @ 0x074A4900` = `SDC2` = Removable external MicroSD card (`/dev/block/mmcblk1`).
- All references mapping `0x07464900` to `SDCC2` have been eliminated. SDC1 strictly uses `GCC_SDCC1_*`.

---

## Physical eMMC Identity

Queried directly from physical sysfs and debugfs on Sony Xperia XZs (G8231 / Tone Keyaki):

```text
Device Path:           /sys/block/mmcblk0/device
Device Name:           BJNB4R
Manufacturer:          Samsung Electronics (manfid = 0x000015)
OEM ID:                0x0100
Serial Number:         0xdac7c038
Hardware Revision:     0x0
Firmware Revision:     0x0f00000000000000 (0x0F)
Manufacturing Date:    01/2017
Sector Size:           512 bytes
Sector Count:          61,071,360 sectors
Capacity:              31,268,536,320 bytes (29.12 GiB)
CID:                   150100424a4e4234520fdac7c0381400
CSD:                   d02701320f5903fff6dbffef8e404000
EXT_CSD Revision:      0x08 (JEDEC eMMC Standard Version 5.1)
Operating Mode:        HS400 (Enhanced Strobe supported, byte 184 = 0x1)
Cache Size:            65,536 KB (64 MB)
Boot Partitions:       mmcblk0boot0 (4096 KB), mmcblk0boot1 (4096 KB)
RPMB Partition:        mmcblk0rpmb (4096 KB)
```

---

## Exact Sony SDC1 DT

Extracted from live board device tree (`twrp-extracted.dts` line 5546):

```dts
sdhci@7464900 {
    compatible = "qcom,sdhci-msm";
    reg = <0x7464900 0x500 0x7464000 0x800 0x7464e00 0x19c>;
    reg-names = "hc_mem", "core_mem", "cmdq_mem";
    interrupts = <0x00 0x8d 0x00 0x00 0x86 0x00>;
    interrupt-names = "hc_irq", "pwr_irq";
    clock-names = "iface_clk", "core_clk", "ice_core_clk";
    clocks = <0x44 0x691e0caa 0x44 0x9ad6fb96 0x44 0xfd5680a>;
    sdhc-msm-crypto = <0x10e>;
    qcom,large-address-bus;
    qcom,bus-width = <0x08>;
    qcom,devfreq,freq-table = <0x1312d00 0xbebc200>;
    vdd-supply = <0x10f>;
    qcom,vdd-voltage-level = <0x2d0370 0x2d0370>;
    qcom,vdd-current-level = <0xc8 0x8b290>;
    vdd-io-supply = <0x2e>;
    qcom,vdd-io-always-on;
    qcom,vdd-io-voltage-level = <0x1b7740 0x1b7740>;
    qcom,vdd-io-current-level = <0x6e 0x4f588>;
    pinctrl-names = "active", "sleep";
    pinctrl-0 = <0x110 0x111 0x112 0x113>;
    pinctrl-1 = <0x114 0x115 0x116 0x117>;
    qcom,clk-rates = <0x61a80 0x1312d00 0x17d7840 0x2faf080 0x5b8d800 0xb71b000 0x16e36000>;
    qcom,ice-clk-rates = <0x11e1a300 0x8f0d180>;
    qcom,nonremovable;
    qcom,bus-speed-mode = "HS400_1p8v", "HS200_1p8v", "DDR_1p8v";
};
```

---

## Controller MMIO Map

| Aperture Name | Physical Address | Size | Function |
|:---|:---|:---|:---|
| `hc_mem` | `0x07464900` | `0x500` (1280 B) | Standard SDHCI Host Controller Specification 3.00 Registers |
| `core_mem` | `0x07464000` | `0x800` (2048 B) | Qualcomm Vendor Specific Extensions (HC Mode, DLL, FIFO) |
| `cmdq_mem` | `0x07464E00` | `0x19C` (412 B) | Qualcomm Command Queueing Hardware Engine |

---

## Exact Clock Tree

Audited directly from LittleKernel `aboot.img` clock tables and Linux debugfs:

```text
GCC Base = 0x00300000

SDC1 Core Clock (core_clk):
  Branch CBCR:          GCC_SDCC1_APPS_CBCR  @ 0x00313004
  RCG Command:          SDCC1_APPS_CMD_RCGR  @ 0x00313010
  RCG Configuration:    SDCC1_APPS_CFG_RCGR  @ 0x00313014
  RCG M/N/D:            M=0x00313018, N=0x0031301C, D=0x00313020
  Parent PLL:           gpll4_out_main
  Operating Rates:      400 KHz (init), 20 MHz, 25 MHz, 50 MHz, 96 MHz, 192 MHz, 384 MHz (HS400)

SDC1 Bus Interface Clock (iface_clk):
  Branch CBCR:          GCC_SDCC1_AHB_CBCR   @ 0x00313008
  Parent:               AHB / System NoC

SDC1 ICE Clock (ice_core_clk):
  Branch CBCR:          GCC_SDCC1_ICE_CORE_CBCR @ 0x00313038
  RCG Command:          SDCC1_ICE_CORE_CMD_RCGR @ 0x00313040
  Parent:               gpll0_out_main (300 MHz)
```

---

## Exact Reset

```text
Reset Register:         GCC_SDCC1_BCR @ 0x00313000 (GCC Base + 0x13000)
Reset Bit:              Bit 0 (BLK_ARES)
Status:                 0x00000000 (Deasserted / Not in reset)
```

---

## Exact Power Rails

Audited from Sony DTS phandles `0x10f` and `0x2e`:

| Rail Role | DT Property | PMIC Regulator | Resource | Target Voltage | Current Range | Always-On | Boot State |
|:---|:---|:---|:---|:---|:---|:---|:---|
| `vdd` | `vdd-supply = <0x10f>` | `pm8994_l20` | `ldoa` ID 20 | 2.950 V (`0x2d0370`) | 200 uA - 570 mA | No (Dynamic IO) | Enabled on demand |
| `vdd_io` | `vdd-io-supply = <0x2e>` | `pm8994_s4` | `smpa` ID 4 | 1.800 V (`0x1b7740`) | 110 uA - 325 mA | **YES** | Active (9 users) |

---

## Exact Pinctrl

Audited from Sony DTS phandles:
- Active (`pinctrl-0`):
  - `0x110`: `sdc1_clk_on` -> pins = "sdc1_clk", bias-disable, drive-strength = 16 mA (`0x10`)
  - `0x111`: `sdc1_cmd_on` -> pins = "sdc1_cmd", bias-pull-up, drive-strength = 10 mA (`0x0a`)
  - `0x112`: `sdc1_data_on` -> pins = "sdc1_data", bias-pull-up, drive-strength = 10 mA (`0x0a`)
  - `0x113`: `sdc1_rclk_on` -> pins = "sdc1_rclk", bias-pull-down
- Sleep (`pinctrl-1`):
  - `0x114`..`0x117`: drive-strength = 2 mA

---

## XBL eMMC Init Call Graph

From disassembly of stock `xbl.img`:
```text
XBL Cold Boot
  ↓
Detect Boot Device: BOOT_CONFIG = 0x00076044 (Device = 2: eMMC)
  ↓
Initialize SDC1 Clocks (GCC_SDCC1_AHB_CLK, GCC_SDCC1_APPS_CLK)
  ↓
Configure TLMM GPIOs: Sdc1GpioConfigOn = 0x1E92
  ↓
Instantiate Block Driver: BDEV_SD_DRIVER (/hdev/sdc1)
  ↓
Read Secondary Firmware Stages (tz, hyp, rpm, devcfg, aboot)
```

---

## Aboot eMMC Init Call Graph

From disassembly of stock `aboot.img` (`0xaa000000`):
```text
kmain() @ 0xaa02cf94
  ↓
bootstrap2() @ 0xaa02cf4c
  ↓
target_init() @ 0xaa000b98
  ↓
target_is_emmc_boot() @ 0xaa024a74 -> returns 1
  ↓
target_mmc_init() @ 0xaa0003ac:
  Populates slot 1 config:
    HC base   = 0x07464900
    Core base = 0x07464000
    pwr_irq   = 166 (0xa6)
    max_clk   = 192,000,000 Hz (0x0b71b000)
  ↓
mmc_init(&config) @ 0xaa00ac14
  ↓
sdhci_init() @ 0xaa00949c
  - Sets HC_MODE_EN (bit 0) in MSM_SDCC_HC_MODE (0x78)
  - Invokes sdhci_reset() @ 0xaa0083a0 (polls reg 0x2F bit 0)
  - Configures INT_ENABLE (0x34) and SIGNAL_ENABLE (0x38)
  ↓
sdhci_msm_init() @ 0xaa008ee0
  - Reads SDHCI_CAPABILITIES (0x40) & SDHCI_CAPABILITIES_1 (0x44)
  - Writes SDHCI_POWER_CONTROL (0x29) to enable 1.8V bus power
  - Writes SDHCI_HOST_CONTROL (0x28)
```

---

## Known-Good Block Read Path

```text
partition_parser:
  ↓
mmc_read() @ 0xaa00c784
  ↓
target_is_emmc_boot() branch @ 0xaa00c7e4
  ↓
mmc_sdhci_read() @ 0xaa00bbc8
  - Formats struct mmc_command:
    If num_blocks == 1 -> CMD17 (READ_SINGLE_BLOCK, opcode 0x11)
    If num_blocks >  1 -> CMD18 (READ_MULTIPLE_BLOCK, opcode 0x12)
  - Invokes sdhci_send_command()
  ↓
Reads LBA0 (MBR) and LBA1 (Primary GPT Header)
  ↓
Parses boot partition offset and loads boot.img into RAM (0x80008000)
```

```text
KNOWN_GOOD_EMMC_STAGE=ABOOT
KNOWN_GOOD_CONTROLLER=0x07464900 (sdhc_1 / SDC1)
KNOWN_GOOD_FIRST_BLOCK_READ_FUNCTION=mmc_sdhci_read @ 0xaa00bbc8 (invoked via mmc_read @ 0xaa00c784)
```

---

## SDHCI Register Whitelist

| Offset | Register Name | Size | Access | Allowed in D2-M1 | Reason |
|:---|:---|:---|:---|:---|:---|
| `0xFE` | `SDHCI_HOST_VERSION` | 16-bit | RO | YES | Silicon specification version probe |
| `0x40` | `SDHCI_CAPABILITIES` | 32-bit | RO | YES | Controller hardware capabilities probe |
| `0x44` | `SDHCI_CAPABILITIES_1` | 32-bit | RO | YES | High-speed timing modes probe |
| `0x24` | `SDHCI_PRESENT_STATE` | 32-bit | RO | YES | Card presence and signal line levels |
| `0x78` | `MSM_SDCC_HC_MODE` (Core) | 32-bit | RO | YES | Qualcomm HC mode enable bit verification |
| `0x100` | `MSM_SDCC_DLL_CONFIG` (Core) | 32-bit | RO | YES | Qualcomm DLL configuration state |
| `0x108` | `MSM_SDCC_DLL_STATUS` (Core) | 32-bit | RO | YES | Qualcomm DLL lock status |
| `0x2F` | `SDHCI_SOFTWARE_RESET` | 8-bit | RW | **NO** | State-mutating (Deferred to D2-M3) |
| `0x0C` | `SDHCI_TRANSFER_MODE` | 16-bit | RW | **NO** | Command execution (Deferred to D2-M4) |
| `0x0E` | `SDHCI_COMMAND` | 16-bit | RW | **NO** | Command execution (Deferred to D2-M4) |
| `0x58` | `SDHCI_ADMA_ADDRESS` | 64-bit | RW | **NO** | DMA setup (Deferred to D2-M5) |

---

## TWRP/Linux Reference State

The complete comparison table is committed as `artifacts/reports/d2m1_linux_vs_xnu.csv`. Key parameters match 100%:
- Host Controller node: `sdhci@7464900`
- Base addresses: `0x07464900` (HC), `0x07464000` (Core), `0x07464E00` (CMDQ)
- Interrupts: GIC 173 (`hc_irq`), GIC 166 (`pwr_irq`)
- Physical device: Samsung `BJNB4R` eMMC 5.1 (31,268,536,320 bytes)
- Power rails: `pm8994_l20` (2.95V) and `pm8994_s4` (1.80V always-on)

---

## XNU Read-Only MMIO Probe

Executed live on Sony Xperia XZs physical silicon via automated `full-test-and-extract.sh`:

```text
[BREADCRUMB] CP=0x000000000000d300 ERR=0x0000000000000000
================================================================================
[XZS-SDHCI] PHASE D2-M1: MSM8996 SDC1 / eMMC HARDWARE IDENTITY PROBE
[XZS-SDHCI] Target Device: Sony Xperia XZs (Tone Keyaki / G8231)
[XZS-SDHCI] Target Controller: sdhc_1 (SDC1) @ 0x07464900 (internal eMMC)
================================================================================

[BREADCRUMB] CP=0x000000000000d300 ERR=0x0000000000000010
[XZS-SDHCI] 1. SONY DEVICE TREE AUDIT PROFILE (sdhci@7464900):
  Node:          sdhci@7464900
  Compatible:    qcom,sdhci-msm
  hc_mem:        0x07464900 [size 0x500]
  core_mem:      0x07464000 [size 0x800]
  cmdq_mem:      0x07464E00 [size 0x19C]
  Interrupts:    hc_irq=SPI 141 (GIC 173), pwr_irq=SPI 134 (GIC 166)
  Clocks:        iface_clk (gcc_sdcc1_ahb_clk), core_clk (gcc_sdcc1_apps_clk)
  Power Rails:   vdd = pm8994_l20 (2.95V), vdd-io = pm8994_s4 (1.80V always-on)
  Bus Width:     8-bit (qcom,bus-width = <0x08>), non-removable
  Speed Modes:   HS400_1p8v, HS200_1p8v, DDR_1p8v

[BREADCRUMB] CP=0x000000000000d300 ERR=0x0000000000000020
[XZS-SDHCI] 2. CLOCK TOPOLOGY AUDIT (GCC Base 0x00300000):
  GCC_SDCC1_AHB_CBCR   (0x313008): 0x20008001 [ENABLED]
  GCC_SDCC1_APPS_CBCR  (0x313004): 0x00004221 [ENABLED]
  SDCC1_APPS_CMD_RCGR  (0x313010): 0x00000003
  SDCC1_APPS_CFG_RCGR  (0x313014): 0x0000050e
  GCC_SDCC1_BCR        (0x313000): 0x00000000

[BREADCRUMB] CP=0x000000000000d300 ERR=0x0000000000000030
[XZS-SDHCI] 3. KNOWN-GOOD BOOTLOADER ORACLE (XBL / ABOOT):
  XBL Driver:    BDEV_SD_DRIVER (/hdev/sdc1)
  ABOOT Trigger: target_is_emmc_boot() == 1
  ABOOT Init:    target_mmc_init() @ 0xaa0003ac -> mmc_init() @ 0xaa00ac14
  Slot 1 MMIO:   HC=0x07464900, Core=0x07464000, pwr_irq=166 (SPI 134)
  Controller:    sdhci_init() @ 0xaa00949c, sdhci_msm_init() @ 0xaa008ee0
  Block Read:    mmc_read() @ 0xaa00c784 -> mmc_sdhci_read() @ 0xaa00bbc8
  Proven Path:   CMD17/CMD18 -> parses MBR & primary GPT -> loads boot.img

[BREADCRUMB] CP=0x000000000000d300 ERR=0x0000000000000040
[XZS-SDHCI] 4. SDHCI REGISTER WHITELIST:
  Permitted HC:   HOST_VERSION(0xFE), CAPABILITIES(0x40/0x44), PRESENT_STATE(0x24)
  Permitted CORE: HC_MODE(0x78), DLL_CONFIG(0x100), DLL_STATUS(0x108)
  Forbidden:      Software Reset, Command Reg, Transfer Mode, ADMA Regs

[BREADCRUMB] CP=0x000000000000d300 ERR=0x0000000000000050
[XZS-SDHCI] 5. MAPPING SDHCI MMIO REGIONS:
  HC Virtual Base:   0xfffffecfff58c900
  Core Virtual Base: 0xfffffecfff590000
  CMDQ Virtual Base: 0xfffffecfff594e00

[BREADCRUMB] CP=0x000000000000d300 ERR=0x0000000000000051
[XZS-SDHCI] 6. EXECUTING READ-ONLY SILICON IDENTITY PROBE:
  [0x51] SDHCI_HOST_VERSION (0x74649FE): 0x00004902
    -> Spec Version:   0x00000002 (SDHCI 3.00)
    -> Vendor Version: 0x00000049 (Vendor-defined version field 0x49, not a standardized vendor ID)

[BREADCRUMB] CP=0x000000000000d300 ERR=0x0000000000000052
  [0x52] SDHCI_CAPABILITIES   (0x7464940): 0x742dc8b2
    -> Timeout Clk Freq:  50 MHz
    -> Base Clk Freq:     200 MHz
    -> Max Block Length:  1024 bytes
    -> 8-bit Bus Support: YES
    -> ADMA2 Support:     YES
    -> High Speed Supp:   YES
    -> 3.3V Voltage Supp: NO
    -> 3.0V Voltage Supp: NO
    -> 1.8V Voltage Supp: YES
    -> 64-bit Bus (ADMA): YES
  [0x52] SDHCI_CAPABILITIES_1 (0x7464944): 0x00008007
    -> SDR50 Support:     YES
    -> SDR104 Support:    YES
    -> DDR50 Support:     YES
    -> Driver Type A:     NO
    -> Driver Type C:     NO
    -> Driver Type D:     NO
  [0x52] SDHCI_PRESENT_STATE  (0x7464924): 0x01f80000
    -> Native SDHCI card-detect bits are not authoritative for soldered non-removable eMMC (qcom,nonremovable)
    -> Card Inserted:     NO (expected for non-removable eMMC with disconnected CD pin)
    -> Card State Stable: NO
    -> Card Detect Pin:   DEASSERTED
    -> Write Protect Pin: PROTECTED
    -> CMD Line Level:    HIGH
    -> DAT[3:0] Level:    0x0F (Pull-ups active)
    -> DAT[7:4] Level:    0x00

[BREADCRUMB] CP=0x000000000000d300 ERR=0x0000000000000053
  [0x53] MSM_SDCC_HC_MODE     (0x7464078): 0x00000001 [HC_MODE_EN ACTIVE]
  [0x53] MSM_SDCC_DLL_CONFIG  (0x7464100): 0x00000000
  [0x53] MSM_SDCC_DLL_STATUS  (0x7464108): 0x00000000 [DLL_LOCK NOT ASSERTED]

[BREADCRUMB] CP=0x000000000000d300 ERR=0x0000000000000060
================================================================================
[XZS-SDHCI] READ-ONLY SILICON PROBE COMPLETED SUCCESSFULLY (PASS)
[XZS-SDHCI] Qualcomm SDC1 SDHCI Host Controller is ACTIVE and RESPONDING!
[XZS-SDHCI] NO BUS ABORT, NO SERROR, NO CARD MUTATION OCCURRED.
================================================================================

[BREADCRUMB] CP=0x000000000000d300 ERR=0x0000000000000080
[XZS-SDHCI] 7. CLEANUP & TEARDOWN COMPLETE

[BREADCRUMB] CP=0x000000000000d300 ERR=0x0000000000000001
[XZS-SDHCI] 8. TERMINAL STATE — TRIGGERING WARM RESET TO FASTBOOT
```

---

## HARDWARE VERIFIED FACTS

1. **Active Storage Controller Response:** The Qualcomm SDCC v5 Host Controller at physical address `0x07464900` responded cleanly to 16-bit and 32-bit MMIO reads without triggering any asynchronous SError or synchronous data abort.
2. **SDHCI Version:** `SDHCI_HOST_VERSION` returned `0x4902`, confirming SDHCI Specification 3.00 compliance (`spec_version = 0x02`) and vendor-defined version `0x49` (not a standardized vendor ID).
3. **Hardware Capabilities:** `SDHCI_CAPABILITIES` (`0x742dc8b2`) proves hardware support for:
   - 8-bit bus width (`bit 18 = 1`)
   - 64-bit ADMA (`bit 28 = 1` and `bit 19 = 1`)
   - 1.8V bus operation (`bit 26 = 1`)
   - 200 MHz base clock frequency
4. **Qualcomm Core Mode:** `MSM_SDCC_HC_MODE` at `0x07464078` returned `0x00000001`, confirming `HC_MODE_EN` is active from the bootloader.
5. **Pin Levels:** `SDHCI_PRESENT_STATE` (`0x01f80000`) confirmed `CMD` line is high and `DAT[3:0]` lines are high (`0x0F`), confirming active pull-ups.
6. **Clock Branches Active:** `GCC_SDCC1_AHB_CBCR` (`0x20008001`) and `GCC_SDCC1_APPS_CBCR` (`0x00004221`) are actively clocked.
7. **Telemetry and Pipeline Integrity:** The entire sequence from entry (`0x00`) through probe (`0x51`..`0x53`), success (`0x60`), cleanup (`0x80`), and warm reset (`0x01`) executed without interruption, returning automatically to Fastboot and preserving ramoops logs.

---

## STOCK-FIRMWARE-AUDITED FACTS

1. **Boot Configuration Fuse:** Register `0x00076044` has bits 5:1 set to `0b00010` (`BOOT_DEV_EMMC = 2`).
2. **XBL Bootloader:** Uses `BDEV_SD_DRIVER` bound to `/hdev/sdc1` for loading secondary boot stages.
3. **LittleKernel (ABOOT):**
   - Detects eMMC boot via `target_is_emmc_boot()` (`0xaa024a74`).
   - Initializes SDC1 via `target_mmc_init()` (`0xaa0003ac`) and `mmc_init()` (`0xaa00ac14`).
   - Configures slot 1 with HC aperture `0x07464900`, Core aperture `0x07464000`, `pwr_irq = 166`, and max clock `192 MHz`.
   - Reads storage blocks via `mmc_read()` (`0xaa00c784`) and `mmc_sdhci_read()` (`0xaa00bbc8`), issuing CMD17 for single block and CMD18 for multiblock transfers.

---

## SOURCE-AUDITED FACTS

1. **Sony Device Tree:** Live DT node `sdhci@7464900` declares `hc_mem` (`0x7464900`), `core_mem` (`0x7464000`), and `cmdq_mem` (`0x7464e00`).
2. **Power Supplies:** Live DT specifies `vdd-supply = <0x10f>` (`pm8994_l20` at 2.95V) and `vdd-io-supply = <0x2e>` (`pm8994_s4` at 1.80V always-on).
3. **GCC Offsets:** Verified in LittleKernel clock tables at `0xaa0a7600`:
   - `GCC_SDCC1_BCR`: `0x13000`
   - `GCC_SDCC1_APPS_CBCR`: `0x13004`
   - `GCC_SDCC1_AHB_CBCR`: `0x13008`
   - `SDCC1_APPS_CMD_RCGR`: `0x13010`
   - `SDCC1_APPS_CFG_RCGR`: `0x13014`

---

## INFERENCES

1. **Bootloader Handover State:** The LittleKernel bootloader leaves `GCC_SDCC1_AHB_CBCR` and `GCC_SDCC1_APPS_CBCR` enabled, and `MSM_SDCC_HC_MODE` set to `1` when jumping to the kernel image.
2. **Readiness for Software Reset:** Because the controller MMIO window is active and responsive, XNU can proceed in D2-M2/M3 to reproduce the bootloader's clock/power/reset sequence and perform SDHCI controller software reset.

---

## D2-M1 Status

```text
D2-M1 STATUS: COMPLETE (PASS)
```

All 10 required stopping conditions were fulfilled:
- [x] Exact SDC1 controller node resolved (`sdhci@7464900`)
- [x] Exact clocks resolved (`GCC_SDCC1_AHB_CLK`, `GCC_SDCC1_APPS_CLK`, RCG 0x13010)
- [x] Exact regulators resolved (`pm8994_l20` 2.95V, `pm8994_s4` 1.80V)
- [x] Exact reset resolved (`GCC_SDCC1_BCR` 0x13000)
- [x] Exact pinctrl resolved (`sdc1_clk_on`, `sdc1_cmd_on`, `sdc1_data_on`, `sdc1_rclk_on`)
- [x] Bootloader init path reconstructed (`target_mmc_init` -> `mmc_init` -> `sdhci_init`)
- [x] Real bootloader block read path proven (`mmc_read` -> `mmc_sdhci_read` CMD17/18)
- [x] Safe register whitelist generated
- [x] XNU read-only SDHCI identity probe succeeds (`SDHCI_HOST_VERSION = 0x4902`, `CAPABILITIES = 0x742dc8b2`)
- [x] Zero bus aborts / SErrors

---

## Recommended D2-M2 Plan

### Goal: Clock, Power, Reset, and Pinctrl Reproduction
1. **Clock Configuration:** Program `SDCC1_APPS_CMD_RCGR` / `CFG_RCGR` to 400 KHz initial frequency (XO-derived, not GPLL0) matching LittleKernel init state.
2. **Regulator Management:** Verify `pm8994_s4` (1.80V) and evaluate explicit `pm8994_l20` (2.95V) vote if needed during card power-up.
3. **Controller Software Reset:** Issue `SDHCI_SOFTWARE_RESET` (write `0x01` to HC offset `0x2F`) and poll until self-clearing.
4. **Breadcrumbs:** Use namespace `CP = 0xD310`.
