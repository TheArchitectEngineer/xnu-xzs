# D2-M5 CMD17 Verified Physical LBA Read Report
## Final D2 Acceptance — Physical eMMC Block Read on Sony Xperia XZs (MSM8996 / Tone Keyaki / G8231)

- **Date:** 2026-09-19
- **Target Device:** Sony Xperia XZs (Tone Keyaki / G8231, Serial: `BH905SX976`)
- **Target Host Controller:** Qualcomm SDCC1 / SDHCI Host Controller (`sdhc_1` @ MMIO `0x07464900`)
- **Target Device Identity:** Samsung BJNB4R 32GB eMMC 5.1 (`CID: 150100424a4e4234520fdac7c0381400`)
- **Target Physical Sector:** `LBA = 1` (Primary GPT Header)
- **Status:** **PASS — D2_STORAGE_COMPLETE = yes**

---

## 1. Mandatory Git Verification

```text
PRE_TASK_GIT_HEAD:  28d6320213cf8441b8cfb36869173ecc48ffd4d5
LOCAL_HEAD:         28d6320213cf8441b8cfb36869173ecc48ffd4d5
REMOTE_HEAD:        28d6320213cf8441b8cfb36869173ecc48ffd4d5
BRANCH:             xzs-bringup
WORKTREE:           CLEAN
DIFF_CHECK:         PASS
```

---

## 2. D2-M4E Evidence Freeze

```text
CMD8_R1_RAW=0x00000900
CMD8_READY_FOR_DATA=1
CMD8_CURRENT_STATE=4
CMD8_CURRENT_STATE_NAME=TRAN

TRANSFER_STATE_DIRECTLY_OBSERVED=yes
TRANSFER_STATE_OPERATIONALLY_CONFIRMED=yes

STOCK-FIRMWARE-AUDITED:
Sony ABOOT CMD8 data path = ADMA
ABOOT_TRANSFER_MODE       = 0x0011

XZS SELFTEST:
D2-M4E/M5 data path       = PIO
XZS_TRANSFER_MODE         = 0x0010
DMA                       = disabled
```

---

## 3. TWRP Independent LBA1 Oracle

Before running the XNU implementation test on silicon, an independent physical block read was performed directly from TWRP recovery environment using `dd` from `/dev/block/mmcblk0`:

```bash
adb exec-out "dd if=/dev/block/mmcblk0 bs=512 skip=1 count=1 2>/dev/null" > artifacts/oracles/mmcblk0_lba1.bin
```

### Oracle Verification
```text
TWRP_ORACLE_LBA:                  1
TWRP_ORACLE_BYTES:                512
TWRP_ORACLE_SHA256:               e4b891b42fd57eb352ffbe0aa9098d04fe85f88e3425dcba529064cce72f862a
TWRP_LBA1_GPT_SIGNATURE_PRESENT:  yes ("EFI PART" at bytes 0..7)
```

---

## 4. Exact Sony ABOOT CMD17 Audit

The primary stock bootloader (`artifacts/firmware/stock/aboot.img`) was decompiled and reverse-engineered:
- `mmc_read` function located at `0xaa013bc8`:
  - If `num_blocks == 1`: sets `cmd.cmd_index = 17` (`0x11` / `MMC_CMD_READ_SINGLE_BLOCK`).
  - Sets `cmd.resp_type = 1` (`MMC_RESP_R1`).
  - Sets `cmd.data_present = 1`.
  - Checks card capacity type: for high-capacity eMMC / SDHC (`type != 4`), sets `cmd.argument = data_addr` directly without multiplying by sector size.
  - **No CMD16 (`SET_BLOCKLEN`)** is issued before `CMD17`.
- `sdhci_send_command` function located at `0xaa0106a8`:
  - Sets `TRANSFER_MODE = 0x0011` (`SDHCI_TRNS_READ | SDHCI_TRNS_DMA`).
  - Sets `SDHCI_COMMAND = 0x113A` (`(17 << 8) | RESP_48 | CRC | INDEX | DATA`).
  - Allocates and configures ADMA2 descriptor table.

```text
ABOOT_CMD17_OPCODE:                      17 (0x11)
ABOOT_CMD17_ARGUMENT_RULE:               sector index for high-capacity cards (no byte scaling)
ABOOT_CMD17_RESPONSE_TYPE:               MMC_RESP_R1 (resp_type = 1)
ABOOT_CMD17_BLOCK_SIZE:                  512 (0x0200)
ABOOT_CMD17_BLOCK_COUNT:                 1 (0x0001)
ABOOT_CMD17_TRANSFER_MODE:               0x0011 (READ | DMA)
ABOOT_CMD17_COMMAND_VALUE:               0x113A (CMD17 | RESP_48 | CRC | INDEX | DATA)
ABOOT_CMD17_DATA_PATH:                   ADMA
ABOOT_CMD17_DMA_MODE:                    ADMA2
CMD16_REQUIRED_BEFORE_CMD17:             no
ABOOT_HIGH_CAPACITY_ADDRESSING_PROVEN:   yes
```

---

## 5. High-Capacity Sector Addressing Audit

Physical device characteristics established in D2-M4C and D2-M4E:
- OCR Response: `FINAL_OCR = 0xC0FF8080` (bit 30 = 1, High Capacity / Sector Mode Addressing).
- EXT_CSD Geometry: `SEC_COUNT = 0x03A3E000` (61,071,360 sectors > 2 GiB).
- For all devices where sector count exceeds 2 GiB and OCR bit 30 is set, JEDEC JESD84-B51 specifies sector addressing:
  $$\text{CMD17 Argument} = \text{LBA Sector Index}$$
  $$\text{Target LBA} = 1 \implies \text{CMD17 Argument} = 0x00000001$$

---

## 6. User-Area Selection

- Established in D2-M4E: `PARTITION_CONFIG = 0x00` (Bits [2:0] `PARTITION_ACCESS = 0` / User Data Area).
- Zero partition switch commands (`CMD6`) were issued.
- Sector read targets `/dev/block/mmcblk0` user data area, matching the TWRP physical disk oracle.

---

## 7. CMD17 Encoding & PIO Transport Classification

```text
SDHCI Command Register:
  Command Index:   17 (0x11)
  Response Type:   48-bit (0x02)
  CRC Check:       Enabled (0x08)
  Index Check:     Enabled (0x10)
  Data Present:    Enabled (0x20)
  Result:          0x113A

CMD17_COMMAND_SOURCE_PROVEN=yes

Transport:
  ABOOT Transport:  ADMA (0x0011)
  XZS M5 Transport: PIO (0x0010)
  XZS_M5_INTENTIONAL_DMA_DEVIATION=yes
```

---

## 8. Fresh Initialization & Pre-CMD17 Gate

Every run executes the complete, verified prerequisite sequence:
1. 400-kHz RCG configuration (`F(400000, P_XO, 12, 1, 4)`).
2. Controlled Host Reset (`SDHCI_RESET_ALL`).
3. Vendor Register Setup (`0x0A1C` / `0x2001`, timeout `0x0F`, 1-bit bus).
4. `CMD0` (Go Idle).
5. `CMD1` polling -> `CARD_READY = yes` (`0xC0FF8080`).
6. `CMD2` -> `CID_MATCH = yes` (`150100424a4e4234520fdac7c0381400`).
7. `CMD3` -> `ASSIGNED_RCA = 2` (`0x0002`).
8. `CMD9` -> `CSD_MATCH = yes` (`d02701320f5903fff6dbffef8e404000`).
9. `CMD7` -> `CARD_SELECTION_CONFIRMED = yes` (Card in TRAN state).
10. `CMD8` -> `EXT_CSD_GEOMETRY_MATCH = yes`, `TRANSFER_STATE_DIRECTLY_OBSERVED = yes` (`R1 = 0x00000900`).

### Pre-CMD17 Host State
```text
PRE_CMD17_PRESENT_STATE:                 0x01f80000 (Host idle)
PRE_CMD17_INT_STATUS:                    0x00000000 (Clean)
```

---

## 9. CMD17 Silicon Execution & Event Telemetry

```text
CMD17_BLOCK_SIZE:                        0x0200 (512 bytes)
CMD17_BLOCK_COUNT:                       0x0001 (1 sector)
CMD17_ARGUMENT:                          0x00000001 (LBA 1)
CMD17_TRANSFER_MODE:                     0x0010 (PIO READ, DMA disabled)
CMD17_COMMAND:                           0x113A
CMD17_INT_ENABLE:                        0xFFFF8023
CMD17_SIGNAL_ENABLE:                     0x00000000 (Polling mode)
```

### Polling State Machine Transitions (W1C-Safe)
```text
RAW_INT_STATUS[0]:                       0x00000000
RAW_INT_STATUS[1]:                       0x00000001 (COMMAND_COMPLETE latched)
  -> R1 Response captured: 0x00000900
  -> W1C write: 0x00000001
RAW_INT_STATUS[2]:                       0x00000000 (Status after CC cleared)
RAW_INT_STATUS[3]:                       0x00000020 (BUFFER_READ_READY latched)
  -> Drained 128 x 32-bit words from SDHCI_BUFFER (0x07464920)
  -> W1C write: 0x00000020
RAW_INT_STATUS[4]:                       0x00000002 (TRANSFER_COMPLETE latched)
  -> Data transfer complete verified
  -> W1C write: 0x00000002
FINAL_INT_STATUS:                        0x00000000 (Clean idle)
POST_CMD17_PRESENT_STATE:                0x01f80000 (Clean idle)
```

### Event Flags & Error Separation
```text
CMD17_CMD_COMPLETE_SEEN:                 yes
CMD17_BUFFER_READ_READY_SEEN:            yes
CMD17_TRANSFER_COMPLETE_SEEN:            yes

XZS_SELFTEST_TIMEOUT_POLICY:             2000000 poll iterations (~500 ms)
CMD17_CMD_TIMEOUT:                       no
CMD17_BRR_TIMEOUT:                       no
CMD17_DATA_END_TIMEOUT:                  no

CMD17_R1_RAW:                            0x00000900
CMD17_R1_REJECT_BITS:                    0x00000000
CMD17_R1_READY_FOR_DATA:                 yes (1)
CMD17_R1_CURRENT_STATE:                  4 (TRAN - transfer state)

CMD17_COMMAND_ERROR_BITS:                0x00000000
CMD17_DATA_ERROR_BITS:                   0x00000000
CMD17_ALL_ERROR_BITS:                    0x00000000

PIO_READ_WIDTH:                          32
PIO_WORDS_READ:                          128 (0x80)
PIO_BYTES_READ:                          512 (0x200)
```

---

## 10. XNU Physical LBA 1 Payload

### Complete 512-Byte Raw Hex Dump (1024 Hex Characters)
```text
XNU_LBA1_RAW_HEX=4546492050415254000001005c0000001d74dfbf000000000100000000000000ffdfa303000000002200000000000000dedfa30300000000321b1098e2bbf24ba06e2bb33d000c2002000000000000008000000080000000f4e0ed64000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000
```

### Formatted 16-Byte Block Dump
```text
[0x000]: 45 46 49 20 50 41 52 54 00 00 01 00 5c 00 00 00
[0x010]: 1d 74 df bf 00 00 00 00 01 00 00 00 00 00 00 00
[0x020]: ff df a3 03 00 00 00 00 22 00 00 00 00 00 00 00
[0x030]: de df a3 03 00 00 00 00 32 1b 10 98 e2 bb f2 4b
[0x040]: a0 6e 2b b3 3d 00 0c 20 02 00 00 00 00 00 00 00
[0x050]: 80 00 00 00 80 00 00 00 f4 e0 ed 64 00 00 00 00
[0x060]: 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
[0x070]: 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
[0x080]: 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
[0x090]: 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
[0x0a0]: 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
[0x0b0]: 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
[0x0c0]: 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
[0x0d0]: 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
[0x0e0]: 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
[0x0f0]: 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
[0x100]: 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
[0x110]: 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
[0x120]: 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
[0x130]: 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
[0x140]: 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
[0x150]: 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
[0x160]: 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
[0x170]: 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
[0x180]: 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
[0x190]: 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
[0x1a0]: 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
[0x1b0]: 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
[0x1c0]: 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
[0x1d0]: 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
[0x1e0]: 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
[0x1f0]: 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
```

---

## 11. TWRP vs XNU Byte-for-Byte Comparison

```bash
$ cmp -l artifacts/oracles/mmcblk0_lba1.bin artifacts/builds/xnu_lba1.bin
(Exit code: 0 — Zero byte differences)

$ shasum -a 256 artifacts/oracles/mmcblk0_lba1.bin artifacts/builds/xnu_lba1.bin
e4b891b42fd57eb352ffbe0aa9098d04fe85f88e3425dcba529064cce72f862a  artifacts/oracles/mmcblk0_lba1.bin
e4b891b42fd57eb352ffbe0aa9098d04fe85f88e3425dcba529064cce72f862a  artifacts/builds/xnu_lba1.bin
```

```text
FILE_SIZE_TWRP:           512
FILE_SIZE_XNU:            512
TWRP_LBA1_SHA256:         e4b891b42fd57eb352ffbe0aa9098d04fe85f88e3425dcba529064cce72f862a
XNU_LBA1_SHA256:          e4b891b42fd57eb352ffbe0aa9098d04fe85f88e3425dcba529064cce72f862a
BYTE_FOR_BYTE_MATCH:      yes (100% 512/512 bytes identical)
```

---

## 12. Classification of Established Facts

### HARDWARE VERIFIED FACTS
- Qualcomm MSM8996 SDCC1 host controller (`0x07464900`) successfully issued physical `CMD17` with sector argument `0x00000001`.
- eMMC responded with `R1 = 0x00000900` (`READY_FOR_DATA = 1`, `CURRENT_STATE = 4` / TRAN).
- 512 bytes streamed over SDC1 DAT bus lines into host controller FIFO and drained via `SDHCI_BUFFER` MMIO reads.
- Physical payload extracted by XNU kernel matches independent TWRP read byte-for-byte (`SHA-256: e4b891b42fd57eb352ffbe0aa9098d04fe85f88e3425dcba529064cce72f862a`).

### STOCK-FIRMWARE-AUDITED FACTS
- Sony ABOOT LittleKernel `mmc_read @ 0xaa013bc8` uses `CMD17` for single-block reads.
- Stock firmware does not issue `CMD16` for high-capacity eMMC.
- High-capacity cards use sector addressing directly in `CMD17` argument.
- Stock ABOOT data transport uses ADMA (`TRANSFER_MODE = 0x0011`).

### XZS SELFTEST FACTS
- XZS uses standard SDHCI Buffer Port PIO (`TRANSFER_MODE = 0x0010`, DMA disabled).
- W1C status event state machine safely handles coalesced status events without destroying pending transfers.
- Single sector read verified without issuing `CMD18`, `CMD24`, or any storage writes.

---

## 13. D2 Final Acceptance & Milestone Closure

```text
PHYSICAL_BLOCK_READ_VERIFIED:        yes
LBA_ADDRESSING_VERIFIED:             yes
USER_AREA_READ_VERIFIED:             yes
CMD17_READ_SINGLE_BLOCK_VERIFIED:    yes

D2_STORAGE_COMPLETE:                 yes
```

Phase `D2` (Physical eMMC Storage Bringup) is **OFFICIALLY SEALED AND COMPLETE**.

---

## 14. Recommended Phase D3 GPT Implementation Plan

With physical single-block sector reads verified 100% against hardware ground truth, the foundation for Phase `D3` (Storage Subsystem Integration & Partition Table Discovery) is established:

1. **Phase D3-M1: GPT Header & Partition Array Discovery**:
   - Read LBA 1 (GPT Header) and validate CRC32 checksum.
   - Read LBA 2..33 (GPT Partition Entry Array, 128 entries $\times$ 128 bytes).
   - Enumerate partition names (`system`, `boot`, `userdata`, `oem`, `modem`, etc.) and establish LBA sector ranges.
2. **Phase D3-M2: Multi-Block Read Transport (CMD18 / READ_MULTIPLE_BLOCK)**:
   - Implement bounded multi-block transfers for efficient payload streaming.
   - Audit stock ABOOT ADMA descriptor table vs PIO burst throughput.
3. **Phase D3-M3: XNU I/O Kit Block Storage Driver (`com.apple.iokit.IOBlockStorageDevice`)**:
   - Instantiate native XNU I/O Kit storage driver attaching to BSD vfs root.
