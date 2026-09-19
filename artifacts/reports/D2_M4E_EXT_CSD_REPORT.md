# PHASE D2-M4E: First Physical eMMC Data Transfer Report
## MMC CMD8 / SEND_EXT_CSD — Sony Xperia XZs (MSM8996 / Tone Keyaki / G8231)

- **Date:** 2026-09-19
- **Target Device:** Sony Xperia XZs (Tone Keyaki / G8231, Serial: `BH905SX976`)
- **Target Host Controller:** Qualcomm SDCC1 / SDHCI Host Controller (`sdhc_1` @ MMIO `0x07464900`)
- **Target Device Identity:** Samsung BJNB4R 32GB eMMC 5.1 (`CID: 150100424a4e4234520fdac7c0381400`)
- **Assigned RCA:** `2` (`0x0002`)
- **Status:** **PASS** (`FIRST_PHYSICAL_DATA_TRANSFER_CONFIRMED = yes`)

---

## 1. Executive Summary

Phase `D2-M4E` successfully executed the **first physical 512-byte data transfer** over the SDC1 DAT bus lines from the internal Samsung BJNB4R eMMC device to host memory.

Using the source-audited single-block MMC `CMD8` (`SEND_EXT_CSD`) protocol semantics combined with standard SDHCI PIO transport (`TRANSFER_MODE = 0x0010`, DMA disabled), the kernel successfully:
1. Replayed clean prerequisite initialization (400-kHz RCG, reset, vendor registers `0x0A1C` / `0x2001`, timeout `0x0F`, 1-bit bus) and command sequence (`CMD0` -> `CMD1` -> `CMD2` -> `CMD3` -> `CMD9` -> `CMD7`).
2. Configured SDHCI transfer parameters (`BLOCK_SIZE = 0x0200`, `BLOCK_COUNT = 0x0001`, `TRANSFER_MODE = 0x0010`, `INT_ENABLE = 0xFFFF8023`, `SIGNAL_ENABLE = 0x00000000`).
3. Issued `CMD8` (`COMMAND = 0x083A`, `ARGUMENT = 0x00000000`).
4. Decoded and cleared interrupt events independently via W1C without coalescing destruction:
   - `COMMAND_COMPLETE` observed with zero command errors.
   - `BUFFER_READ_READY` observed; drained exactly 128 $\times$ 32-bit words (512 bytes) from `SDHCI_BUFFER` (`0x07464920`) into an aligned buffer in little-endian byte order.
   - `TRANSFER_COMPLETE` observed with zero data errors.
5. Preserved and verified the raw 512-byte `EXT_CSD` payload (`artifacts/builds/ext_csd.bin`, SHA-256 `e9fac06592092cc4f12ee2705c4fcfb9a839e4a9cce539a02a2208f8cc2ea29c`).
6. Verified device geometry: `EXT_CSD_REV = 0x08` (eMMC 5.1), `SEC_COUNT = 0x03A3E000` (61,071,360 sectors / 29.12109375 GiB user capacity).
7. Enforced immediate hard stop (zero storage writes, zero block reads, no CMD17).

---

## 2. Evidence Classification: ABOOT ADMA vs XZS PIO

```text
STOCK-FIRMWARE-AUDITED:
Sony ABOOT CMD8 data path = ADMA
ABOOT_TRANSFER_MODE       = 0x0011

XZS SELFTEST:
D2-M4E data path          = PIO
XZS_TRANSFER_MODE         = 0x0010
DMA                       = disabled

CMD8_PROTOCOL_SOURCE_PROVEN=yes
XZS_M4E_INTENTIONAL_DMA_DEVIATION=yes
PIO_PATH_SDHC_STANDARD_BASED=yes
```

The single intentional deviation from stock Sony ABOOT LittleKernel is the transport mechanism:
- **ABOOT:** `READ | DMA` (`0x0011`) using a 128-byte aligned ADMA2 descriptor table.
- **XZS Phase D2-M4E:** `READ` (`0x0010`) using SDHCI Buffer Port PIO (`SDHCI_BUFFER` @ `0x20`), eliminating DMA and IOMMU complexity before storage bringup.

---

## 3. Transfer Configuration & Semantics

| Parameter | Value | Description / Spec Reference |
|---|---|---|
| `BLOCK_SIZE` | `0x0200` (512) | Transfer block length in bytes |
| `BLOCK_COUNT` | `0x0001` (1) | Single block transfer |
| `ARGUMENT` | `0x00000000` | EXT_CSD argument is stuff bits (all 0) |
| `TRANSFER_MODE` | `0x0010` | `SDHCI_TRNS_READ`; no DMA, no multiblock, no autocmd |
| `COMMAND` | `0x083A` | `(8 << 8) \| RESP_48 \| CRC \| INDEX \| DATA` |
| `SDHCI_INT_ENABLE` | `0xFFFF8023` | CC (`0x1`), TC (`0x2`), BRR (`0x20`), Err Summary (`0x8000`), Details (`0xFFFF0000`) |
| `SDHCI_SIGNAL_ENABLE` | `0x00000000` | Polling mode (no GIC interrupt generation) |

---

## 4. Silicon Execution Telemetry

### A. Pre-CMD8 Controller State
```text
PRE_CMD8_PRESENT_STATE:      0x01f80000
  -> CMD_INHIBIT:            0 (Idle)
  -> DATA_INHIBIT:           0 (Idle)
PRE_CMD8_INT_STATUS:         0x00000000 (Clean)
CARD_SELECTION_CONFIRMED:    yes (RCA=2)
```

### B. Polling State Machine & W1C Transitions
The state machine independently sampled and handled all coalesced status bits:
```text
RAW_INT_STATUS[0]:           0x00000000 (Initial sample)
RAW_INT_STATUS[1]:           0x00000001 (COMMAND_COMPLETE asserted)
  -> Action: Record RESPONSE_0 (0x00000900), W1C write 0x0001 to INT_STATUS
RAW_INT_STATUS[2]:           0x00000000 (Status after CC cleared)
RAW_INT_STATUS[3]:           0x00000020 (BUFFER_READ_READY asserted)
  -> Action: Drain 128 words from SDHCI_BUFFER, W1C write 0x0020 to INT_STATUS
RAW_INT_STATUS[4]:           0x00000002 (TRANSFER_COMPLETE asserted)
  -> Action: Transfer complete confirmed, W1C write 0x0002 to INT_STATUS
```

### C. State Machine Verification Flags
```text
CMD_COMPLETE_SEEN:           yes
BUFFER_READ_READY_SEEN:      yes
TRANSFER_COMPLETE_SEEN:      yes

XZS_SELFTEST_TIMEOUT_POLICY: 2000000 poll iterations (~500 ms)
CMD8_CMD_TIMEOUT:            no
CMD8_BRR_TIMEOUT:            no
CMD8_DATA_END_TIMEOUT:       no
```

### D. R1 Response & Error Decode
```text
CMD8_R1_RAW:                 0x00000900
  -> CURRENT_STATE:          4 (TRAN / Transfer State)
  -> READY_FOR_DATA:         1 (Ready)
CMD8_R1_REJECT_BITS:         0x00000000 (Zero reject bits)

CMD8_COMMAND_ERROR_BITS:     0x00000000
CMD8_DATA_ERROR_BITS:        0x00000000
CMD8_ALL_ERROR_BITS:         0x00000000
```

### E. PIO Data Port Telemetry
```text
PIO_READ_WIDTH:              32
PIO_WORDS_READ:              128 (0x80)
PIO_BYTES_READ:              512 (0x200)
```

### F. Post-CMD8 Controller State
```text
POST_CMD8_PRESENT_STATE:     0x01f80000 (Returned to complete idle)
FINAL_INT_STATUS:            0x00000000 (Clean)
```

---

## 5. EXT_CSD Acceptance & Geometry Verification

### Complete Raw Hex Payload (512 Bytes / 1024 Hex Characters)
```text
EXT_CSD_RAW_HEX=000000000000000000000000000000003900000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000f0000c7c700000000000000000000000000000000000000000000000000000001000000000500000000000000000000000000000000000000000000000000000000010000000000000000000000000000000000000000000000000000470700070001020000141f20000000000000000000000000000000010000000000000008000200571f050100000000000000000000000100e0a30307110007071001010107200007111b550200000000000000001e00000000003c0a00000100000f000000000000000000011000010606000000000000000000000000000000000000000000000000000000000000000000000000000f01000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000081c70000010307050002013f3f01010100000000000000
```

### Formatted EXT_CSD Dump (16 Bytes / Row)
```text
[0x000]: 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
[0x010]: 39 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
[0x020]: 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
[0x030]: 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
[0x040]: 0f 00 00 c7 c7 00 00 00 00 00 00 00 00 00 00 00
[0x050]: 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
[0x060]: 01 00 00 00 00 05 00 00 00 00 00 00 00 00 00 00
[0x070]: 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
[0x080]: 00 00 01 00 00 00 00 00 00 00 00 00 00 00 00 00
[0x090]: 00 00 00 00 00 00 00 00 00 00 00 00 00 47 07 00
[0x0a0]: 07 00 01 02 00 00 14 1f 20 00 00 00 00 00 00 00
[0x0b0]: 00 00 00 00 00 00 00 00 01 00 00 00 00 00 00 00
[0x0c0]: 08 00 02 00 57 1f 05 01 00 00 00 00 00 00 00 00
[0x0d0]: 00 00 00 01 00 e0 a3 03 07 11 00 07 07 10 01 01
[0x0e0]: 01 07 20 00 07 11 1b 55 02 00 00 00 00 00 00 00
[0x0f0]: 00 1e 00 00 00 00 00 3c 0a 00 00 01 00 00 0f 00
[0x100]: 00 00 00 00 00 00 00 00 01 10 00 01 06 06 00 00
[0x110]: 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
[0x120]: 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
[0x130]: 00 00 00 0f 01 00 00 00 00 00 00 00 00 00 00 00
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
[0x1e0]: 00 00 00 00 00 00 00 00 00 81 c7 00 00 01 03 07
[0x1f0]: 05 00 02 01 3f 3f 01 01 01 00 00 00 00 00 00 00
```

### Field Decodes
| Field Name | Byte Index | Raw Value | Decoded Value / Interpretation | Gate |
|---|---|---|---|---|
| `EXT_CSD_REV` | `192` | `0x08` | eMMC 5.1 / 5.1-or-later compliant revision | **PASS** |
| `SEC_COUNT` | `212..215` | `00 E0 A3 03` | `0x03A3E000` = 61,071,360 sectors (31,268,536,320 bytes / 29.12109375 GiB) | **PASS** |
| `CARD_TYPE` | `196` | `0x57` | HS200 (SDR 200MHz @ 1.8V), HS400, DDR52, HS52 | AUDITED |
| `BOOT_SIZE_MULT` | `226` | `0x20` | $32 \times 128\text{ KiB} = 4096\text{ KiB} = 4\text{ MiB}$ via JEDEC | **PASS** |
| `RPMB_SIZE_MULT` | `168` | `0x20` | $32 \times 128\text{ KiB} = 4096\text{ KiB} = 4\text{ MiB}$ via JEDEC | **PASS** |
| `PARTITION_CONFIG` | `179` | `0x00` | Default user data area, no boot partition enabled | AUDITED |
| `BUS_WIDTH` | `183` | `0x00` | 1-bit data bus mode (current power-on default) | AUDITED |
| `HS_TIMING` | `185` | `0x00` | Selecting default timing interface | AUDITED |
| `HC_ERASE_GRP_SIZE`| `224` | `0x01` | $1 \times 512\text{ KiB} = 512\text{ KiB}$ erase group size | AUDITED |
| `HC_WP_GRP_SIZE` | `221` | `0x10` | $16 \times 512\text{ KiB} = 8\text{ MiB}$ write-protect group size | AUDITED |
| `CACHE_SIZE` | `249..252` | `0x00010000` | 65,536 KiB = 64 MiB internal device cache | AUDITED |

---

## 6. Verification & Gate Summary

```text
CMD8_PROTOCOL_SOURCE_PROVEN=yes
XZS_M4E_INTENTIONAL_DMA_DEVIATION=yes
PIO_PATH_SDHC_STANDARD_BASED=yes

CMD8_BLOCK_SIZE=0x0200
CMD8_BLOCK_COUNT=0x0001
CMD8_TRANSFER_MODE=0x0010
CMD8_COMMAND=0x083A

CMD8_INT_ENABLE=0xFFFF8023
CMD8_SIGNAL_ENABLE=0x00000000

CMD_COMPLETE_SEEN=yes
BUFFER_READ_READY_SEEN=yes
TRANSFER_COMPLETE_SEEN=yes

CMD8_CMD_TIMEOUT=no
CMD8_BRR_TIMEOUT=no
CMD8_DATA_END_TIMEOUT=no

CMD8_R1_RAW=0x00000900
CMD8_READY_FOR_DATA=1
CMD8_CURRENT_STATE=4
CMD8_CURRENT_STATE_NAME=TRAN
CMD8_R1_REJECT_BITS=0x00000000

CMD8_COMMAND_ERROR_BITS=0x00000000
CMD8_DATA_ERROR_BITS=0x00000000
CMD8_ALL_ERROR_BITS=0x00000000

PIO_READ_WIDTH=32
PIO_WORDS_READ=128
PIO_BYTES_READ=512

EXT_CSD_REV=0x08
SEC_COUNT=61071360
BOOT_SIZE_MULT=0x20
RPMB_SIZE_MULT=0x20

EXT_CSD_GEOMETRY_MATCH=yes
FIRST_PHYSICAL_DATA_TRANSFER_CONFIRMED=yes
TRANSFER_DATA_PATH_CONFIRMED=yes
TRANSFER_STATE_DIRECTLY_OBSERVED=yes
TRANSFER_STATE_OPERATIONALLY_CONFIRMED=yes
CMD17_ISSUED=no
```

---

## 7. Hard Stop & Safety Invariants Preserved

Immediately after successful `EXT_CSD` capture and field validation, the kernel executed an unconditional hard stop and warm reset back to Fastboot:
- **NO CMD6** (no bus width switch, no high-speed timing change).
- **NO CMD13** (no polled status commands).
- **NO CMD17 / CMD18** (zero block data reads).
- **NO CMD24 / CMD25** (zero block data writes).
- **NO EXT_CSD modifications** (all fields untouched).
- **NO DMA / ADMA / CQE / ICE** (controller remained in standard PIO polling mode).
- **NO storage writes** (pure read-only inspection).
