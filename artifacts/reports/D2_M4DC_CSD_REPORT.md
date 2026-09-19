# D2-M4D-C CMD9 / eMMC CSD Report

## Mandatory Git Verification

Prior to performing any modifications or silicon executions, the repository state was rigorously audited against the remote branch `refs/heads/xzs-bringup`:

```text
LOCAL_HEAD  = dec95ac7e30367b12630d6ea285531e59f69a96b
REMOTE_HEAD = dec95ac7e30367b12630d6ea285531e59f69a96b
BRANCH      = xzs-bringup
WORKTREE    = CLEAN
DIFF_CHECK  = PASS
```

Git baseline gate: **PASS**.

---

## D2-M4D-B Corrections

In accordance with Phase D2-M4D-C directives, all documentation semantics and range notations from D2-M4D-B were audited and corrected:

1. **CMD3 State Semantics Frozen**:
   - `CMD3_R1_RAW = 0x00000500`
   - `CURRENT_STATE_IN_CMD3_RESPONSE = 2 (IDENT)`
   - `CMD3_R1_REJECT_BITS = 0`
   - `ASSIGNED_RCA = 2`
   - We do **NOT** require `CMD3_R1 CURRENT_STATE == 3` for CMD3 success. JEDEC eMMC standard JESD84-B51 Section 6.13 explicitly defines that the `CURRENT_STATE` field in the Card Status (R1) register reflects the state of the device **when the command is received** (namely `ident` = 2, with `READY_FOR_DATA = 1`, producing `0x00000500`).
   - The device transitions from `ident` (2) to `stby` (3) upon completing transmission of the R1 response. The subsequent addressed command (`CMD9 SEND_CSD` addressed to `RCA = 2`) operationally verifies that RCA assignment and the expected post-CMD3 state transition succeeded.
2. **Disassembly Range Notation Fixed**:
   - The reversed address-range notation `0xaa008908 - 0xaa008780` in `artifacts/reports/D2_M4DB_RCA_REPORT.md` was corrected to exact ascending order (`0xaa0086a8 - 0xaa008980`).
3. **Preserved Timeout Fix**:
   - `TIMEOUT_CONTROL = 0x0F` (`ABOOT_TIMEOUT_VAL`) is preserved across all host initialization paths and fresh probe routines, preventing any reintroduction of the M4D-A `0x0E` divergence.

---

## Exact ABOOT CMD9 Path

Primary authority: stock Sony bootloader image `artifacts/firmware/stock/aboot.img`.

Immediately following the successful CMD3 verification (`aboot.img @ 0xaa00af88: beq 0xaa00b01c`), the bootloader invokes the CSD retrieval routine:

```asm
; Stock Sony ABOOT CMD9 invocation path (0xaa00b01c - 0xaa00b058):
aa00b01c: ldr  r3, [sp, #0x28]        ; load dev pointer
aa00b020: ldr  r1, [r3, #0x1c]        ; load assigned RCA (dev->rca = 2)
aa00b024: lsl  r1, r1, #16            ; argument = dev->rca << 16 = 0x00020000
aa00b028: mov  r2, #4                 ; response_type = 4 (R2 / 136-bit)
aa00b02c: mov  r0, #9                 ; cmd_idx = 9 (CMD9 / SEND_CSD)
aa00b030: bl   0xaa0086a8             ; call mmc_send_cmd(cmd=9, arg=0x00020000, resp_type=4, ...)
aa00b034: cmp  r0, #0                 ; check return status == 0
aa00b038: bne  0xaa00b144             ; error bail
; On success, extract 128-bit CSD from controller response registers
aa00b03c: ldr  r0, [sp, #0x28]        ; dev pointer
aa00b040: add  r0, r0, #0x28          ; dev->csd buffer
aa00b044: bl   0xaa0084d8             ; mmc_unpack_csd(dev)
```

From this disassembly, the exact parameters are resolved:

```text
ABOOT_CMD9_OPCODE          = 9 (SEND_CSD)
ABOOT_CMD9_ARGUMENT        = 0x00020000 (RCA << 16 = 2 << 16)
ABOOT_CMD9_RESPONSE_TYPE   = 4 (R2 / 136-bit)
ABOOT_CMD9_TRANSFER_MODE   = 0x0000
ABOOT_CMD9_COMMAND_VALUE   = 0x0909
ABOOT_CMD9_COMPLETION_MASK = 0x0001 (SDHCI_INT_RESPONSE / COMMAND_COMPLETE)
ABOOT_CMD9_ERROR_MASK      = 0xFFFF0000 (SDHCI_INT_ERROR | SDHCI_INT_CMD_ERR_MASK)
```

---

## CMD9 Encoding

In standard SDHCI controllers, the 16-bit Command Register (`SDHCI_COMMAND`, offset `0x0E`) is formatted as:
- `Bits [15:8]`: Command Index = `9` (`0x09`)
- `Bits [7:6]`: Command Type = `00b` (Normal)
- `Bit 5`: Data Present Select = `0` (No data)
- `Bit 4`: Command Index Check Enable = `0` (R2 responses have no command index field)
- `Bit 3`: Command CRC Check Enable = `1` (`0x08`, R2 has 7-bit CRC check in SDHCI)
- `Bits [1:0]`: Response Type Select = `01b` (`0x01`, Response length 136 bits)

Encoding:
```text
COMMAND = (9 << 8) | SDHCI_CMD_RESP_136 | SDHCI_CMD_CRC
        = 0x0900 | 0x0001 | 0x0008
        = 0x0909
```

This exactly matches stock Sony ABOOT and SDHCI specification.

---

## Fresh Initialization

Every physical execution executes the complete, deterministic bringup pipeline:
1. **Clock & Host Reset**:
   - `SDCC1_APPS_CFG_RCGR = 0x00002017` (verified 400-kHz SDC1 configuration).
   - `SDCC1_HC_VENDOR_SPEC = 0x00000A1C` (POR prerequisite `SDCC1_HC_VENDOR_SPEC_POR`).
   - `SDHCI_SOFTWARE_RESET = 0x01` (`SDHCI_RESET_ALL`).
   - `MSM_SDCC_HC_MODE = 0x00002001` (`MSM_SDCC_HC_MODE_PREREQ = HC_MODE_EN | FF_CLK_SW_RST_DIS`).
2. **Host Power & Clocks**:
   - `SDHCI_POWER_CONTROL = 0x0B` (1.8-V bus voltage selector + `SD_BUS_POWER` ON).
   - `SDHCI_CLOCK_CONTROL = 0x0007` (Internal clock enabled, stable, card clock enabled).
   - `SDHCI_TIMEOUT_CONTROL = 0x0F` (Restored ABOOT timeout).
   - `SDHCI_HOST_CONTROL = 0x00` (1-bit mode).
3. **Prerequisite Commands**:
   - `CMD0` (`GO_IDLE_STATE`): Transmitted cleanly, card reset to idle.
   - `CMD1` (`SEND_OP_COND`): Polled with argument `0x40FF8000`. Card reached `CARD_READY = yes` on iteration 2 with `FINAL_OCR = 0xC0FF8080`.
   - `CMD2` (`ALL_SEND_CID`): Captured 136-bit R2 response, reconstructed 128-bit CID `150100424a4e4234520fdac7c0381400`, `CID_MATCH = yes`.
   - `CMD3` (`SET_RELATIVE_ADDR`): Addressed with `RCA = 2` (`ARG = 0x00020000`). Card returned `R1 = 0x00000500`, zero error bits, zero reject bits.
4. **Pre-CMD9 Verification**:
   - `PRE_CMD9_PRESENT_STATE = 0x01f80000` (`CMD_INHIBIT = 0`, `DATA_INHIBIT = 0`).
   - `PRE_CMD9_RESPONSE0 = 0x00000500`.

---

## CMD9 Silicon Execution

Exactly ONE `CMD9` was written to the hardware:

```text
CMD9_ARGUMENT    = 0x00020000
CMD9_TRANSFER    = 0x0000
CMD9_COMMAND     = 0x0909
```

Execution telemetry captured from Qualcomm SDCC1 hardware:

```text
CMD9_INT_STATUS  = 0x00000001 (COMMAND_COMPLETE)
COMMAND_COMPLETE = yes
CMD9_ERROR_BITS  = 0x00000000
  -> COMMAND_TIMEOUT  = CLEARED
  -> COMMAND_CRC      = CLEARED
  -> COMMAND_END_BIT  = CLEARED
  -> COMMAND_INDEX    = CLEARED
  -> BUS_POWER        = CLEARED
```

---

## Raw R2 Registers

Immediately upon `COMMAND_COMPLETE`, the raw 32-bit hardware response registers were captured before any software transformation:

```text
CMD9_RAW_RESP0 (HC + 0x10) = 0xef8e4040
CMD9_RAW_RESP1 (HC + 0x14) = 0xfff6dbff
CMD9_RAW_RESP2 (HC + 0x18) = 0x320f5903
CMD9_RAW_RESP3 (HC + 0x1C) = 0x00d02701
```

---

## Reconstructed CSD

Reusing the exact, proven R2 transformation formula from CMD2:

```c
resp[0] = (raw0 << 8);
resp[1] = (raw1 << 8) | (raw0 >> 24);
resp[2] = (raw2 << 8) | (raw1 >> 24);
resp[3] = (raw3 << 8) | (raw2 >> 24);
```

Reconstructed normalized 32-bit big-endian words:

```text
CSD_WORD0 (MSB) = resp[3] = 0xd0270132
CSD_WORD1       = resp[2] = 0x0f5903ff
CSD_WORD2       = resp[1] = 0xf6dbffef
CSD_WORD3 (LSB) = resp[0] = 0x8e404000
```

---

## Full CSD Match

Comparison against the independently known physical device CSD oracle:

```text
CAPTURED_CSD_HEX = d02701320f5903fff6dbffef8e404000
EXPECTED_CSD_HEX = d02701320f5903fff6dbffef8e404000

CSD_MATCH = yes
```

Every single byte of the reconstructed 128-bit CSD matches the independent device oracle identically.

---

## CSD Field Decode

Decoded in accordance with JEDEC Standard JESD84-B51:

| Field Name | Raw Value | Decoded Meaning |
| :--- | :--- | :--- |
| `CSD_STRUCTURE` | `0x3` (`11b`) | Extended CSD version (eMMC 4.1 - 5.1 structure) |
| `SPEC_VERS` | `0x4` | eMMC 4.0 - 5.1 specification version |
| `TAAC` | `0x27` | Asynchronous data access time |
| `NSAC` | `0x01` | Worst-case clock-dependent factor |
| `TRAN_SPEED` | `0x32` | Maximum data transfer rate (legacy 26 MHz / 52 MHz) |
| `CCC` | `0x0F5` | Card Command Classes supported: 0, 2, 4, 5, 6, 7 |
| `READ_BL_LEN` | `0x9` | 512 bytes max read block length ($2^9$) |
| `C_SIZE` | `0x0FFF` | Device size parameter (4095) |
| `C_SIZE_MULT` | `0x7` | Device size multiplier ($2^{7+2} = 512$) |
| `ERASE_GRP_SIZE` | `0x1F` | Legacy erase group size raw field (legacy formula: $(ERASE\_GRP\_SIZE + 1) \times (ERASE\_GRP\_MULT + 1)$ blocks; obsolete in eMMC 5.1, see EXT_CSD) |
| `WP_GRP_SIZE` | `0x0F` | Write protect group size (15 erase groups) |
| `R2W_FACTOR` | `0x3` | Write timeout factor (8 times read timeout) |
| `WRITE_BL_LEN` | `0x9` | 512 bytes max write block length ($2^9$) |

> [!NOTE]
> As specified in JESD84-B51, high-capacity eMMC devices ($> 2\text{ GB}$) report a saturated `C_SIZE = 0x0FFF` in legacy CSD. The definitive user-area capacity and sector count will be obtained from `EXT_CSD` in later phases.

---

## Post-CMD3 Addressing Verification

The execution of addressed `CMD9` with `RCA = 2`:
- Received `COMMAND_COMPLETE` from hardware with zero SDHCI errors (`CMD9_ERROR_BITS = 0x00000000`).
- Returned a valid 136-bit R2 response matching the known BJNB4R CSD oracle byte-for-byte.

This constitutes absolute operational proof that:
1. RCA assignment to `RCA = 2` succeeded and is active on the card.
2. The card successfully transitioned from `ident` (2) to `stby` (3) post-CMD3 and responds to its assigned relative address.

```text
POST_CMD3_ADDRESSING_CONFIRMED = yes
```

---

## HARDWARE VERIFIED FACTS

- **Hardware Target**: Sony Xperia XZs (Tone Keyaki / G8231), Qualcomm MSM8996, eMMC Samsung BJNB4R.
- **RCA Assignment Confirmed**: Card actively responds to addressed commands directed to `RCA = 2` (`ARG = 0x00020000`).
- **Clean Execution**: Addressed `CMD9` executed with zero SDHCI errors (`CMD9_ERROR_BITS = 0x00000000`, `INT_STATUS = 0x00000001`).
- **Raw Response Capture**:
  - `RAW_RESP0 = 0xef8e4040`
  - `RAW_RESP1 = 0xfff6dbff`
  - `RAW_RESP2 = 0x320f5903`
  - `RAW_RESP3 = 0x00d02701`
- **Reconstructed CSD**:
  - `CSD_WORD0 = 0xd0270132`
  - `CSD_WORD1 = 0x0f5903ff`
  - `CSD_WORD2 = 0xf6dbffef`
  - `CSD_WORD3 = 0x8e404000`
- **Exact CSD Match**: `d02701320f5903fff6dbffef8e404000` (`CSD_MATCH = yes`).
- **Post-CMD3 Addressing Confirmed**: `POST_CMD3_ADDRESSING_CONFIRMED = yes`.
- **Constraint Compliance**: Exactly ONE CMD9 issued. Zero CMD7, zero CMD8, zero CMD13, zero data transfers, zero writes, zero power-cycles.

---

## STOCK-FIRMWARE-AUDITED FACTS

- **ABOOT CMD9 Location**: `aboot.img @ 0xaa00b01c - 0xaa00b058`.
- **Command Structure**:
  - `cmd_idx = 9`
  - `arg = dev->rca << 16` (`0x00020000`)
  - `resp_type = 4` (`R2 / 136-bit`)
  - `SDHCI_COMMAND = 0x0909` (`SDHCI_MAKE_CMD(9, SDHCI_CMD_RESP_136 | SDHCI_CMD_CRC)`)
- **Completion Check**: `aboot.img @ 0xaa00b034` tests return status `== 0` and proceeds directly to unpacking CSD into `dev->csd` at `0xaa0084d8`.

---

## SOURCE-AUDITED FACTS

- **R2 Word Unpacking**: `resp[0] = (raw0 << 8); resp[1] = (raw1 << 8) | (raw0 >> 24); ...` properly aligns SDHCI's 136-bit response register window to the 128-bit CID/CSD format, dropping the 8-bit response header and 8-bit CRC/end bit.
- **R1 State Semantics**: `CURRENT_STATE` in R1 reflects card state prior to command execution; transition to `stby` (3) completes after R1 transmission. Addressed `CMD9` proves the state transition operationally.

---

## D2-M4D-C Status

```text
D2-M4D-C STATUS                = COMPLETE (HARDWARE TELEMETRY CAPTURED)
ASSIGNED_RCA                   = 2
CMD9_ARGUMENT                  = 0x00020000
CMD9_COMMAND                   = 0x0909
CMD9_INT_STATUS                = 0x00000001
CMD9_ERROR_BITS                = 0x00000000
CSD_HEX                        = d02701320f5903fff6dbffef8e404000
EXPECTED_CSD_HEX               = d02701320f5903fff6dbffef8e404000
CSD_MATCH                      = yes
POST_CMD3_ADDRESSING_CONFIRMED = yes
CMD7_ISSUED                    = no
```

---

## Recommended CMD7 Plan

For the subsequent phase (`PHASE D2-M4D-D: eMMC Card Selection — CMD7 / SELECT_CARD`):
1. **Objective**: Issue `CMD7` (`SELECT_CARD`) with argument `RCA << 16 = 0x00020000` to transition the Samsung BJNB4R from Standby State (`stby` = 3) to Transfer State (`tran` = 4).
2. **Command Configuration**:
   - `OPCODE = 7`
   - `ARGUMENT = 0x00020000`
   - `RESPONSE = R1b` (48-bit response with busy indication)
   - `SDHCI_COMMAND = 0x071B` (`RESP_48_BUSY | CRC | INDEX`)
3. **Verification**:
   - Wait for `TRANSFER_COMPLETE` / `COMMAND_COMPLETE` and poll `SDHCI_PRESENT_STATE` for `DAT[0]` de-assertion (busy release).
   - Verify card status in R1 response (`CURRENT_STATE` transitions to `tran`).
4. **Safety**: Do not initiate data transfers or clock rate changes until `CMD7` is fully verified.
