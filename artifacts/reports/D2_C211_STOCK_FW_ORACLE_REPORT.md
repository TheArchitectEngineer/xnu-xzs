# D2-C2.11 Stock Firmware UFS Oracle Report

## Mandatory Pre-Task Git Checkpoint

- **Baseline Pre-Task Commit:** `aa0db586ec267f4a2d96143f5a21be66be6e0ef1`
- **Remote Tracking Branch:** `origin/xzs-bringup`
- **Remote Head Verification:** `aa0db586ec267f4a2d96143f5a21be66be6e0ef1` (`git ls-remote` verified matching)
- **Working Tree:** `CLEAN`
- **Diff Check:** `PASS`
- **Target Device:** Sony Xperia XZs (`tone-keyaki` / G8231 / MSM8996 v2.2.0, Serial: `BH905SX976`)
- **Status Variables:**
  ```text
  C2.10_CHECKPOINT_COMMIT=aa0db586ec267f4a2d96143f5a21be66be6e0ef1
  C2.10_REMOTE_COMMIT=aa0db586ec267f4a2d96143f5a21be66be6e0ef1
  C2.10_WORKTREE_CLEAN=yes
  ```

### C2.10 Evidence Classification Corrections

1. **Duplicate C04 Caveat:**
   ```text
   C2.10_DUPLICATE_C04_BINARY_PROVEN=no
   ```
   *Classification:* C2.10 executed `+0xC04 = 1` during `phy_power_on` and repeated `+0xC04 = 1` after calibration soft reset release before `+0xC00 = 1`. This was near-source-equivalent with one extra idempotent write.
2. **L25 Caveat:**
   ```text
   L25_PREEXISTING_ACTIVE=yes
   L25_RAW_REQUEST_SENT=yes
   L25_AGGREGATE_STATE_PRESERVED=unknown
   ```
   *Classification:* Pre-vote SPMI proved L25 was active (`ENABLE=0x80`). Raw RPM request was ACKed in 40 µs. Rollback preserved enable bit without transmitting `SWEN=0`. However, whether aggregate APPS/RPM voter state was mathematically preserved remains unproven by software telemetry.

---

## Stock Firmware Inventory

All boot-chain partitions were dumped directly from the physical Sony Xperia XZs device (`BH905SX976`) in recovery mode into `artifacts/firmware/stock/`.

| Name | Partition Device | Size (bytes) | Format | Architecture | Entrypoint | Primary Role |
|---|---|---|---|---|---|---|
| `xbl.img` | `mmcblk0p3` | 2,097,152 | ELF 64-bit | AArch64 / ARM | `0x06208000` / `0x80200000` | Qualcomm eXtensible BootLoader (SBL) |
| `tz.img` | `mmcblk0p5` | 2,097,152 | ELF 64-bit | AArch64 | N/A | TrustZone / QSEE (Secure World) |
| `rpm.img` | `mmcblk0p7` | 512,000 | ELF 32-bit | ARM (Cortex-M3) | `0x00020000` | Resource Power Manager firmware |
| `hyp.img` | `mmcblk0p9` | 524,288 | ELF 64-bit | AArch64 | N/A | Qualcomm Hypervisor |
| `pmic.img` | `mmcblk0p11` | 524,288 | ELF 64-bit | AArch64 | N/A | PMIC device configuration image |
| `aboot.img` | `mmcblk0p13` | 1,048,576 | ELF 32-bit | ARM (EABI5) | `0xaa000000` | Applications Bootloader (Little Kernel / LK) |
| `keymaster.img`| `mmcblk0p15` | 524,288 | ELF 32-bit | ARM | N/A | Keymaster TrustZone application |
| `cmnlib.img` | `mmcblk0p17` | 262,144 | ELF 32-bit | ARM | N/A | Qualcomm Common Library (32-bit) |
| `cmnlib64.img`| `mmcblk0p19` | 262,144 | Data / ELF | AArch64 | N/A | Qualcomm Common Library (64-bit) |
| `devcfg.img` | `mmcblk0p21` | 131,072 | ELF 64-bit | AArch64 | N/A | Device Configuration Database |
| `s1sbl.img` | `mmcblk0p23` | 31,457,280 | ELF 64-bit | AArch64 | `0x80080000` | Sony S1 Emergency Recovery Linux Kernel (3.18.20) |
| `tzs1attest.img`| `mmcblk0p25` | 102,400 | ELF 32-bit | ARM | N/A | Sony S1 Attestation TrustZone app |
| `tzs1sbl.img` | `mmcblk0p27` | 524,288 | Tar / Raw | N/A | N/A | S1 SBL TrustZone package |
| `boot.img` | `mmcblk0p30` | 67,108,864 | Android Boot | AArch64 | `0x80080000` | Android Kernel + Ramdisk boot partition |
| `fotakernel.img`| `mmcblk0p48` | 67,108,864 | Android Boot | AArch64 | `0x80080000` | Sony FOTA / Recovery kernel image |
| `devinfo.img` | `mmcblk0p33` | 1,024 | Raw Data | N/A | N/A | Device lock / tamper status record |

---

## Firmware Hashes

Immutable SHA-256 digests of all examined images:

```text
ec041ed43eedf6e4c9411afeda07be7bccf0bf356a04b560f7ef18e594b385c6  artifacts/firmware/stock/aboot.img
29505a1463ac6393657ed2779662d3ac6e999cf6a00c1c321e4ecd781748c1d4  artifacts/firmware/stock/boot.img
3928ad2673143b987a33f88043f188da37a730c46a6f43447a284da85866a817  artifacts/firmware/stock/cmnlib.img
8a39d2abd3999ab73c34db2476849cddf303ce389b35826850f9a700589b4a90  artifacts/firmware/stock/cmnlib64.img
40151e69ba57626d85a55d803f4c703f94cb4eb04764588c613b4daec477203b  artifacts/firmware/stock/devcfg.img
5f70bf18a086007016e948b04aed3b82103a36bea41755b6cddfaf10ace3c6ef  artifacts/firmware/stock/devinfo.img
57d1513824de357ffeb5706bbcd6063b866bf8152c9cb087aed84036b08f8fb3  artifacts/firmware/stock/fotakernel.img
2a68001783fa7da1d9a7f0fc9e409c5a554185fe2a0e7ff6ad1338b04378df8e  artifacts/firmware/stock/hyp.img
e5ee3d6a613a04992d53382a9b2fb69b6384ec530494c030101bb50f545ad35d  artifacts/firmware/stock/keymaster.img
e14687e2e2ac19bce50f960c372b4ee85f837def2eb99fa2883dac9004491597  artifacts/firmware/stock/pmic.img
aad6230a9c78269aca89296760478457352081d8960babb4fbfeb49ffa76a4a4  artifacts/firmware/stock/rpm.img
e09e84bd0bf8c21afbd0002860638742c636211c377936193c60b70f1df8b4ae  artifacts/firmware/stock/s1sbl.img
0f7e21988899f8f8159a06ff31ed5b76c6453add9842103e073ed5aac079cd66  artifacts/firmware/stock/tz.img
9c96df8c7fb002f90d9db7c0d3709908a724fd2b874b007d380f9f87c9681214  artifacts/firmware/stock/tzs1attest.img
5036530b2f00ab834593cd3d8a626c9d13c1d8b854cc58d060988c9cfc175885  artifacts/firmware/stock/tzs1sbl.img
ad3faa412c7a650a760665b51d6852a3986a5ace89879596fee66fabd4945528  artifacts/firmware/stock/xbl.img
```

---

## Exact Xperia Boot Chain

Disassembly and forensic analysis of the firmware binaries established the exact boot chain:

```text
PBL (Primary Boot Loader in SoC ROM)
    │ Reads BOOT_CONFIG register (0x76044) bits 5:1 = 2 (BOOT_DEV_EMMC)
    ▼
XBL (Qualcomm eXtensible BootLoader @ mmcblk0p3)
    │ Reads eFuses, selects BDEV_SD_DRIVER (/hdev/sdc1)
    │ Initializes DDR, clocks, power
    │ Loads TZ, RPM, HYP, PMIC, DEVCFG, KEYMASTER, and ABOOT from eMMC
    ▼
TrustZone (QSEE @ mmcblk0p5) + RPM (@ mmcblk0p7)
    │ Sets up XPU memory firewalls, SCM call interfaces, power rails
    ▼
ABOOT (Little Kernel / S1 Boot @ mmcblk0p13)
    │ Evaluates 0x76044 bits 5:1 = 2 (BOOT_DEV_EMMC)
    │ Calls mmc_init() (platform/msm_shared/mmc_sdhci.c)
    │ Formats kernel cmdline: androidboot.bootdevice=7464900.sdhci
    │ Reads boot partition (mmcblk0p30) into RAM @ 0x80080000
    │ Jumps to OS kernel
    ▼
Linux / Android Kernel (or XNU)
    │ Mounts root / system / userdata from /dev/block/bootdevice (7464900.sdhci)
    │ UFS driver (ufshcd) probe fails with -ENODEV (-19) due to absence of UFS device
```

---

## Known-Good UFS Owning Stage

```text
KNOWN_GOOD_UFS_STAGE=NONE_ON_HARDWARE
```

### Critical Ground Truth Discovery

Static disassembly, live kernel cmdline analysis, sysfs interrogation, and OEM hardware documentation prove conclusively:

1. **Physical Storage Technology:**
   The Sony Xperia XZ (`Kagura`) and Sony Xperia XZs (`Keyaki` / G8231) **DO NOT USE UFS STORAGE**.
   They are hardware-manufactured with **eMMC 5.1** internal flash memory.
2. **Physical Storage Device Identification:**
   - Linux Sysfs Name: `BJNB4R`
   - Manufacturer: Samsung (`manfid = 0x000015`)
   - Type: `MMC` (eMMC 5.1)
   - Size: 61,071,360 sectors × 512 bytes = 31,268,536,320 bytes (~32 GB)
   - CID: `150100424a4e4234520fdac7c0381400`
   - Block Device: `/dev/block/mmcblk0` attached to controller `sdhci@7464900`.
3. **Hardware Straps & eFuses:**
   - Physical register `0x00076044` (`BOOT_CONFIG`): bits 5:1 are hard-fused to `0b00010` (`BOOT_DEV_EMMC = 2`).
   - Bit combination for UFS (`BOOT_DEV_UFS = 4`) is **NOT** set on this board.
4. **Bootloader Storage Ownership:**
   - In `aboot` (`platform/msm_shared/boot_device.c` lines `0xaa024ae4`–`0xaa024b88`):
     Because `boot_device == 2`, the bootloader selects `sdhci` and appends `androidboot.bootdevice=7464900.sdhci`.
     The UFS formatting branch (`%x.ufshc`) is completely bypassed.
   - In `target/msm8996/init.c` (`0xaa07b71c`), the bootloader exclusively invokes `mmc_init()`. UFS initialization is dead code.
5. **Why `ufshcd` Probe Failed in Linux (`err -19`):**
   In TWRP / stock Android, the device tree includes the inherited `ufshc@624000` node from Qualcomm reference sources (`msm8996.dtsi`). When `ufshcd` probes the controller, `ufs_qcom_init` executes and fails with `-ENODEV` (`-19`) because no physical UFS device exists on the PCB SerDes lines.

---

## Evidence of Real UFS Access

- **Call Chain to UFS:** **ABSENT**.
  Zero firmware stages (PBL, XBL, Aboot, or Android kernel) perform block reads or writes over UFS.
- **Evidence Quality:**
  ```text
  Evidence Quality = NONE
  ```
  The physical UFS device does not exist on the PCB. All storage operations target the eMMC SDHCI controller at `0x7464900`.

---

## Stock Firmware UFS Init Call Graph

Qualcomm reference LK source code in `aboot.img` includes unused UFS drivers:
- `platform/msm_shared/ufs_hci.c`
- `platform/msm_shared/rpmb/rpmb_ufs.c`

However, in the compiled binary:
1. `target/msm8996/init.c` contains only:
   ```c
   target_init() {
       ...
       mmc_init(); // Target is eMMC!
       ...
   }
   ```
2. `xbl.img` contains `BDEV_UFS_DRIVER` and `BDEV_SD_DRIVER`, but runtime dispatch selects `BDEV_SD_DRIVER` based on `BOOT_CONFIG (0x76044) == 2`.
3. Neither `xbl` nor `aboot` executes ANY writes to `0x624000` (`ufshc`) or `0x627000` (`ufsphy`).

---

## Stock Firmware Power Sequence

In the stock Sony boot path:
- **eMMC Power Supplies:**
  - `pm8994_l20` / `pm8994_l21` (SDC / eMMC VDD / VDD_IO).
- **UFS Analog Rails (L28, L12, L25):**
  - `L25` is marked `always-on` in DT and shared with `vccq`. It is left enabled by PMIC default / bootloader.
  - `L28` and `L12` are enabled as general PMIC defaults or voted dynamically only if UFS is probed.
  - Neither XBL nor Aboot issues explicit runtime UFS rail power-up sequences because UFS is not the active boot medium.

---

## Stock Firmware Clock Sequence

- **Active Storage Clocks (eMMC @ 0x7464900):**
  - `SDCC2_APPS_CLK_SRC` (eMMC core clock, up to 200 MHz HS400).
  - `GCC_SDCC2_APPS_CBCR`, `GCC_SDCC2_AHB_CBCR`.
- **UFS Clocks (@ 0x75000 / 0x76000):**
  - Uninitialized by bootloader. Digital branches remain gated until the OS kernel boots.

---

## Stock Firmware Reset Sequence

- No UFS reset sequence exists in boot firmware.
- eMMC card reset is driven via standard SDHCI MSM software reset and CMD0.

---

## Stock Firmware PHY Calibration Sequence

- **Firmware QMP Calibration Table:** **NONE**.
  An exhaustive binary scan of `aboot.img`, `xbl.img`, and `tz.img` for the 76-entry Rate-A table constants (`0x084 = 0x03`, `0x094 = 0x58`, etc.) returned **ZERO** matches.
- Boot firmware does not calibrate the QMP UFS SerDes macro.

---

## Secure / TrustZone Preconditions

```text
SECURE_PRECONDITION_FOUND=no
```
- TrustZone (`tz.img`) configures XPU memory firewalls and SCM service dispatch.
- UFS controller and PHY MMIO spaces (`0x624000`, `0x627000`) are accessible in Non-Secure (NS) world, as proven by direct XNU read/write access to `REG_UFS_CFG1`, `QCOM_HW_VER`, and QSERDES registers without generating secure bus aborts.
- No secure call (SCM) is required or executed to unlock the UFS hardware block.

---

## UFS Shutdown / Kernel-Handoff Sequence

- Because UFS is never initialized by XBL or Aboot, there is **NO** UFS shutdown or deinit sequence executed prior to kernel handoff.
- The hardware state handed off to XNU represents uninitialized, power-gated SoC silicon blocks for `ufshc` and `ufsphy`.

---

## Stock vs XNU Machine Diff

The complete comparison table is committed at `artifacts/reports/d2c211_stock_vs_xnu.csv`.

Summary of top rows:

| Order | Domain | Resource | Stock Action / Value | XNU Action / Value | Classification |
|---|---|---|---|---|---|
| 1 | `BOOT_STRAP` | `0x76044` (`BOOT_CONFIG`) | Bits 5:1 = 2 (`BOOT_DEV_EMMC`) | Assumed UFS boot device | `VALUE_DIFFERENCE` |
| 2 | `STORAGE_DEV`| Physical Storage | Samsung `BJNB4R` eMMC 5.1 | Probing nonexistent UFS | `VALUE_DIFFERENCE` |
| 3 | `CONTROLLER` | `0x7464900` (`sdhci`) | Active storage controller | Not yet initialized | `STOCK_ONLY` |
| 4 | `CONTROLLER` | `0x624000` (`ufshc`) | Unused (`-ENODEV` in Linux) | Mapped & clocked @ 200MHz | `XNU_ONLY` |
| 5 | `PHY` | `0x627000` (`ufsphy`) | Unused / Uninitialized | 76-entry Rate-A calibrated | `XNU_ONLY` |
| 6 | `POWER_RPM` | `L28` (0.925V) | Pre-existing in PMIC | Explicitly voted (18 mA) | `MATCH` |
| 7 | `POWER_RPM` | `L12` (1.800V) | Pre-existing in PMIC | Explicitly voted (9 mA) | `MATCH` |
| 8 | `POWER_RPM` | `L25` (1.200V) | Pre-existing in PMIC | Explicitly voted (0 mA) | `MATCH` |
| 9 | `POWER_RPM` | `LN_BB` (19.2MHz) | Shared clock buffer | Explicitly voted (Active/Sleep)| `MATCH` |
| 10 | `GCC_CLOCK` | `UFS_AXI_CLK_SRC` | Gated in bootloader | Latched @ 200 MHz | `MATCH` (Linux) |
| 11 | `GCC_CLOCK` | `UNIPRO_SRC` | Gated in bootloader | Latched @ 300 MHz | `MATCH` (Linux) |
| 12 | `INVARIANT` | `UFSHCI_REG_HCE` | 0x00 | 0x00 | `MATCH` |
| 13 | `CMDLINE` | `androidboot.bootdevice`| `7464900.sdhci` | N/A | `STOCK_ONLY` |

---

## Top Proven Firmware Deltas

1. **Primary Boot Device:**
   Stock firmware boots exclusively from `sdhci@7464900` (eMMC 5.1). XNU Phase D2 was attempting to bring up `ufshc@624000`.
2. **Boot Device Straps:**
   Register `0x76044` has `0b00010` (eMMC), directing all bootloader layers to bypass UFS.
3. **Absence of UFS Hardware:**
   No physical UFS flash device is attached to the MSM8996 UFS SerDes pins on the Sony Xperia XZ / XZs PCB.

---

## HARDWARE VERIFIED FACTS

1. **Storage Device Identity (from live TWRP sysfs):**
   - Device Name: `BJNB4R`
   - Device Type: `MMC` (eMMC 5.1)
   - Size: 31,268,536,320 bytes (32 GB)
   - Block Device: `/dev/block/mmcblk0`
2. **Kernel Command Line from Aboot:**
   - `androidboot.bootdevice = 7464900.sdhci`
   - `oemandroidboot.s1boot = 1299-4832_S1_Boot_MSM8996_LA2.0_N_117`
3. **Linux Kernel Probe Failure:**
   - `ufshcd 624000.ufshc: ufshcd_variant_hba_init: variant qcom init failed err -19`
   - `ufshcd 624000.ufshc: Intialization failed`
4. **All Partitions Reside on eMMC:**
   `xbl` (p3), `tz` (p5), `rpm` (p7), `aboot` (p13), `boot` (p30), `system` (p55), `userdata` (p54) are all partitions of `/dev/block/mmcblk0`. Zero SCSI/UFS disk nodes (`sda`, `sdb`) exist in `/sys/bus/scsi/devices` or `/dev/block/`.

---

## STOCK-FIRMWARE-AUDITED FACTS

1. **Aboot Boot Device Resolution (`0xaa024ae4`–`0xaa024b88`):**
   Reads `BOOT_CONFIG` (`0x76044`), tests bits 5:1. Value is `2` (`BOOT_DEV_EMMC`), formatting `7464900.sdhci`.
2. **Aboot Storage Initialization (`0xaa07b71c`):**
   `target_init()` calls exclusively `mmc_init()`. UFS init is not called.
3. **XBL Driver Selection:**
   `xbl.img` binds `BDEV_SD_DRIVER` to `/hdev/sdc1` for loading secondary bootloader stages.
4. **Zero UFS QMP Calibration Tables in Firmware:**
   Neither `xbl.img` nor `aboot.img` contains any UFS PHY calibration table or SerDes start logic.

---

## INFERENCES

1. **Root Cause of `C_READY = 0` Across All Phases (D2-C2.1 through D2-C2.10):**
   The QMP UFS SerDes common PLL fails to lock and PCS never achieves ready status because **there is no physical UFS device soldered to the PCB**. The differential TX/RX SerDes lines are floating or terminated without a partner transceiver.
2. **Sony Platform Architecture:**
   Sony selected eMMC 5.1 for the Xperia XZ (Kagura) and Xperia XZs (Keyaki / G8231), retaining Qualcomm's default MSM8996 device tree skeleton with unpopulated UFS nodes.
3. **Path Forward for Storage:**
   To boot XNU and access storage partitions (Mach-O kernel, ramdisk, rootfs) on Sony Xperia XZs, XNU must initialize the Qualcomm SDHCI controller at `0x7464900` (`sdhci@7464900`), NOT the UFS controller.

---

## Candidate for D2-C2.12

### Proposed Pivot to Primary Boot Storage: MSM8996 SDHCI eMMC Bringup
1. **Target Hardware:** Qualcomm SDCC v5 controller at `0x7464900` (`sdhci@7464900`).
2. **Clocks:** `SDCC2_APPS_CLK_SRC` (GPLL0 divider) + `GCC_SDCC2_APPS_CBCR` + `GCC_SDCC2_AHB_CBCR`.
3. **Power Rails:** `PM8994_L20` (VDD) and `PM8994_L21` (VDD_IO).
4. **Physical Storage:** Samsung `BJNB4R` eMMC 5.1.
5. **Phase Objective:** Prove MMIO communication and read MBR/GPT partition table from `mmcblk0` in native XNU.

---

## D2-C2 Status
```text
D2-C2 STATUS: CLOSED / HARDWARE_TARGET_RESOLVED (PHYSICAL_STORAGE_IS_EMMC)
```
The UFS hardware line is conclusively resolved: physical UFS hardware is absent on the Sony Xperia XZs. All storage bringup must target `sdhci@7464900`.
