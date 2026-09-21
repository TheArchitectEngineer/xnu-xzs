# Phase D7-M4: P0 Readiness & Risk Reduction Report

## 1. Executive Summary

This document establishes the **Phase D7-M4 P0 Readiness & Risk Reduction** specification for Sony Xperia XZs (`G8231`, Qualcomm `MSM8996`) running Apple XNU natively.

Phase D7-M3 is completely **frozen, sealed, and verified** on physical hardware. The objective of P0 is to deconstruct the serial stdin pipeline into independently verifiable layers, audit all hardware registers, pinmux configurations, clocks, IRQ routing, XNU driver contracts, and tty/console read semantics, implement and unit-test the RX ring buffer, and establish the D730 telemetry schema and verifier scaffolding prior to implementing the production RX path.

---

## 2. Frozen D7-M3 Baseline

Before initiating P0 work, the D7-M3 verifier suite was executed on the sealed hardware console log, confirming 100% pass across all regression milestones:

```text
============================================================
D6_REGRESSION_VERIFIER: PASS
D7_M2_REGRESSION_VERIFIER: PASS
D7_M3_ACCEPTANCE_VERIFIER: PASS
/bin/sh userspace stdout banner + prompt verified in EL0.
============================================================
```

### Exact Sealed Baseline Artifacts
- **M3_MAIN_COMMIT**: `34a45dcf358f33e316f63376434f044c6b6d9ea2`
- **M3_TAG**: `xzs-d7m3-complete`
- **M3_KERNEL_SHA256**: `fe186dabe00f43b6089535dc4aefddeb9e3512d138afc0dd9a1a702d90c4ac4c`
- **M3_ROOTFS_SHA256**: `357c7e18e9a958c24320fe50afd7cd5225cae99cd540ecb967fe14131f847fdb`
- **M3_BOOT_IMAGE_SHA256**: `c2b66cea0a6475bc140e729b2827e41d636b855f2a29f7619a56c5d1cccbe68c`
- **M3_RAW_LOG_SHA256**: `156e4293d57fee058c3eb5dda7ccc4f832af8398347bcd7ad7987d38ddf32a59`

### Invariant Freeze Commitments
The following production components remain frozen:
- `/bin/sh` EL0 execution and entry point (`0x1000002f0`)
- Native Darwin `sysent[4]` `write()` syscall dispatch
- File descriptors fd 0, 1, 2 mapped to `/dev/console` (`cdev 0:0`, `VCHR`)
- `kmoutput()` and batched `xzs_console_write()`
- Persistent RAM pstore console output at `0xa7fbe000`
- D720 telemetry and breadcrumbs
- Post-prompt 64-round-trip `getpid` continuity loop

---

## 3. Dedicated Readiness Branch

- **Branch**: `xzs-d7m4-readiness`
- **Base**: `main` (`34a45dc`, tagged `xzs-d7m3-complete`)
- **Policy**: Clean separation of P0 audit, host tests, and diagnostic verifiers before modifying kernel source.

---

## 4. D7-M4 Layered Architecture

The complete end-to-end stdin path is decomposed into 7 independently verifiable layers:

```text
[Layer 1: Physical]   External Host Serial TX (115200 8N1) -> Xperia XZs RX Pin (GPIO 5)
                               ↓
[Layer 2: Hardware]   MSM8996 BLSP2 UART2 UARTDM RX Core (0x075b0000)
                               ↓
[Layer 3: Driver]     xnu/pexpert msm_uart_receive_ready() / msm_uart_receive_data()
                               ↓
[Layer 4: Buffering]  Lock-free SPSC RX Ring Buffer (xzs_uart_rx_buf)
                               ↓
[Layer 5: XNU TTY]    cons_cinput(ch) -> linesw[0].l_rint (ttyinput) -> tp->t_rawq -> tp->t_canq
                               ↓
[Layer 6: VFS/cdev]   read(0) -> sys_read -> fo_read -> vn_read -> kmread -> ttread (sleeps on &tp->t_rawq)
                               ↓
[Layer 7: EL0 Shell]  /bin/sh EL0 buffer receives "abc\n", parse and execute command
```

Each layer is independently validated before advancing to the next.

---

## 5. Qualcomm MSM8996 UARTDM Register Map

Based on Linux kernel (`drivers/tty/serial/msm_serial.c`), Code Aurora LK bootloader, and coreboot Qualcomm SoC drivers:

- **UART Controller**: Qualcomm MSM UART-DM v1.4 (`qcom,msm-uartdm-v1.4`)
- **MMIO Physical Base**: `0x075b0000` (`blsp2_uart2`)
- **MMIO Size**: `0x1000` (4 KB)

### Register Table

| Offset | Name | Type | Reset | Description & Bitfields |
|---|---|---|---|---|
| `0x0000` | `MSM_UART_MR1` | R/W | `0x0` | Mode Register 1: Flow control, RFR level, Bit 7: `RX_RDY_CTL`, Bit 6: `CTS_CTL` |
| `0x0004` | `MSM_UART_MR2` | R/W | `0x0` | Mode Register 2: Bit 6: `ERROR_MODE`, Bits 5:4: `BITS_PER_CHAR` (3 = 8 bits), Bits 3:2: `STOP_BIT_LEN` (1 = 1 stop bit), Bits 1:0: `PARITY_MODE` (0 = None). `0x34` = 8-N-1 |
| `0x0008` | `MSM_UART_SR` | RO | `0x08` | Status Register: Bit 0: `RX_READY` (data available in FIFO), Bit 1: `RX_FULL`, Bit 2: `TX_READY`, Bit 3: `TX_EMPTY`, Bit 4: `OVERRUN`, Bit 5: `PAR_FRAME_ERR`, Bit 6: `RX_BREAK`, Bit 7: `HUNT_CHAR` |
| `0x0010` | `MSM_UART_CR` | WO | - | Command Register: Bits 3:0: Enable/Disable (0x1: RX_EN, 0x2: RX_DIS, 0x4: TX_EN, 0x8: TX_DIS), Bits 7:4: Channel Commands (0x10: RESET_RX, 0x20: RESET_TX, 0x30: RESET_ERR, 0x80: RESET_STALE_INT), Bits 11:8: General Commands (0x0500: ENA_STALE_EVT, 0x0600: DIS_STALE_EVT) |
| `0x0010` | `MSM_UART_MISR` | RO | `0x0` | Masked Interrupt Status Register (same offset as CR on read): Bit 0: `TXLEV`, Bit 3: `RXSTALE`, Bit 4: `RXLEV`, Bit 7: `TX_READY` |
| `0x0014` | `MSM_UART_IMR` | WO | `0x0` | Interrupt Mask Register: Bit 0: `TXLEV`, Bit 3: `RXSTALE`, Bit 4: `RXLEV`, Bit 7: `TX_READY` |
| `0x0014` | `MSM_UART_ISR` | RO | `0x0` | Raw Interrupt Status Register (same offset as IMR on read) |
| `0x0018` | `MSM_UART_IPR` | R/W | `0x0` | Interrupt Programming Register: Bits 4:0: Stale timeout (LSB), set to `0x1F` for standard line timeout |
| `0x001C` | `MSM_UART_TFWR` | R/W | `0x0` | Transmit FIFO Watermark Register |
| `0x0020` | `MSM_UART_RFWR` | R/W | `0x0` | Receive FIFO Watermark Register: Word count trigger |
| `0x0024` | `MSM_UART_HCR` | R/W | `0x0` | Hunt Character Register (unused, kept 0) |
| `0x0034` | `UARTDM_DMRX` | R/W | `0x0` | RX Transfer Size: Programmed before receive transfer (e.g. `0x220` or buffer size) |
| `0x0038` | `UARTDM_RX_TOTAL_SNAP` | RO | `0x0` | Snapshot of actual number of bytes received in the current RX transfer |
| `0x003C` | `UARTDM_DMEN` | R/W | `0x0` | Data Mover / BAM Enable: Set to `0x0` for PIO mode |
| `0x0040` | `UARTDM_NCF_TX` | R/W | `0x0` | Number of characters to flush / transmit |
| `0x004C` | `UARTDM_TXFS` | RO | `0x0` | TX FIFO Status |
| `0x0050` | `UARTDM_RXFS` | RO | `0x0` | RX FIFO Status: Bits 9:7: Buffer state / word count in FIFO |
| `0x0070` | `UARTDM_TF` | WO | - | Transmit FIFO: Write 32-bit packed word |
| `0x0070` | `UARTDM_RF` | RO | - | Receive FIFO: Read 32-bit packed word (up to 4 characters, little-endian) |

---

## 6. RX Initialization Audit

### Current XZS State
In `src/xnu/pexpert/arm/pe_serial.c`:
```c
static unsigned int msm_uart_receive_ready(void) { return 0; }
static uint8_t msm_uart_receive_data(void) { return 0; }
```
- RX clock: **ENABLED** (`GCC_BLSP2_UART2_APPS_CLK` and `GCC_BLSP2_AHB_CLK` active from bootloader)
- RX GPIO: **CONFIGURED** (GPIO 5 mapped to `blsp_uart8` by bootloader)
- RX transfer: **UNINITIALIZED** (`UARTDM_DMRX = 0`, stale event disabled)
- RX FIFO: Not cleared
- Stale detection: Not configured (`IPR = 0`)
- `UARTDM_RX_AVAILABLE`: Currently reported as `no`

### Sony / Linux Reference Hardware Sequence
To transition UARTDM into active PIO reception:
1. Reset receiver: `write32(CR, MSM_UART_CMD_RESET_RX)` (`0x0010`)
2. Set mode: `write32(MR1, 0)`, `write32(MR2, 0x34)` (8-N-1)
3. Set stale timeout: `write32(IPR, 0x1F)`
4. Set watermark: `write32(RFWR, 0)`
5. Enable receiver: `write32(CR, MSM_UART_CR_RX_ENABLE)` (`0x0001`)
6. Arm transfer size: `write32(DMRX, 0x220)`
7. Reset stale interrupt: `write32(CR, MSM_UART_CMD_RES_STALE_INT)` (`0x0080`)
8. Enable stale event: `write32(CR, MSM_UART_GCMD_ENA_STALE_EVT)` (`0x0500`)

---

## 7. GPIO / Pinmux Audit

- **Controller**: Qualcomm MSM8996 TLMM (`pinctrl@1010000`)
- **TLMM MMIO Base**: `0x01010000`
- **UART Pins**:
  - `GPIO 4`: TX (`blsp2_uart2` TX, pinctrl function `blsp_uart8`)
  - `GPIO 5`: RX (`blsp2_uart2` RX, pinctrl function `blsp_uart8`)
- **Register Calculations**:
  - `TLMM_GPIO_CFG(4)` = `0x01010000 + 0x1000 * 4` = `0x01014000`
  - `TLMM_GPIO_CFG(5)` = `0x01010000 + 0x1000 * 5` = `0x01015000`
  - `TLMM_GPIO_IN_OUT(5)` = `0x01015004`
- **Bitfield Schema**:
  - `[1:0]` `pull`: `0` = no pull, `1` = pull-down, `2` = keeper, `3` = pull-up
  - `[5:2]` `func`: function selector (index for `blsp_uart8`)
  - `[8:6]` `drv`: drive strength (e.g. `16mA`)
  - `[9]` `oe`: output enable (`0` for input RX, `1` for output TX)
- **Bootloader Inheritance**: Sony S1 bootloader configures both GPIO 4 and GPIO 5 before kernel entry. Because TX operates reliably, pinmux is pre-initialized.

---

## 8. Physical Serial Configuration & Test Procedure

- **Baud Rate**: `115200`
- **Framing**: `8-N-1` (8 data bits, no parity, 1 stop bit)
- **Flow Control**: None (software/hardware flow control disabled)
- **Physical Channel**:
  - USB-UART adapter connected to host (`/dev/cu.debug-console`)
  - TX of host adapter -> Xperia XZs RX (GPIO 5)
  - Common GND connected
- **Test Ingestion Flow**:
  1. Operator / script opens host serial device: `stty -f /dev/cu.debug-console 115200 cs8 -cstopb -parenb`
  2. Single-byte test: `0x41` (`'A'`)
  3. Line test: `"abc\n"` (`0x61 0x62 0x63 0x0a`)
  4. Telemetry verifies:
     `EXTERNAL_TEST_BYTE == UARTDM_RX_BYTE == DRIVER_RX_BYTE == TTY_RX_BYTE == EL0_READ_BYTE`

---

## 9. IRQ Routing & Interrupt Semantics

- **IRQ Number**: `GIC_SPI 114` (Hardware ID: `114 + 32 = 146`)
- **Trigger Type**: Level High (`IRQ_TYPE_LEVEL_HIGH`)
- **Driver Model**:
  - Initial bring-up / probe: Bounded polling in driver primitive (zero IRQ complexity)
  - Production M4: GICv3 SPI 114 handler -> `xzs_uart_irq_handler`
- **Interrupt Status & Clearing**:
  - Read `MSM_UART_MISR` (`0x0010`)
  - If `RXSTALE` (`bit 3`):
    1. Read received count from `UARTDM_RX_TOTAL_SNAP` (`0x0038`)
    2. Read FIFO words from `UARTDM_RF` (`0x0070`) into ring buffer
    3. Clear stale interrupt: write `MSM_UART_CMD_RES_STALE_INT` (`0x0080`) to `CR`
    4. Re-arm next transfer: write `0x220` to `UARTDM_DMRX` (`0x0034`)
    5. Re-enable stale event: write `MSM_UART_GCMD_ENA_STALE_EVT` (`0x0500`) to `CR`

---

## 10. RX Driver Primitive Contract

In `src/xnu/pexpert/arm/pe_serial.c`:
- **`msm_uart_receive_ready(void)`**:
  - Returns `1` if bytes are available in unpack word or if `MISR & RXSTALE` / `SR & RX_READY` is set.
  - Returns `0` if no data is available.
- **`msm_uart_receive_data(void)`**:
  - Returns the next `uint8_t` byte.
  - Handles 32-bit word unpacking and automatic transfer re-arming.
- **`uart_getc(void)`**:
  - Standard XNU platform serial entrypoint calling `fns->receive_ready()` and `fns->receive_data()`.
  - Returns character as `int` or `-1` if no data available.

---

## 11. Lock-Free SPSC RX Ring Buffer

Implemented in `tests/test_xzs_rx_buf.c` and verified with 100% test coverage:
- **Size**: 256 bytes (power of 2, mask `0xFF`)
- **Producer**: UART RX handler (writes `data[head]`, memory barrier, advances `head`)
- **Consumer**: TTY input loop (reads `data[tail]`, memory barrier, advances `tail`)
- **Safety**: Single producer, single consumer requires zero locks. Memory barrier `dmb ish` ensures coherency across CPU clusters.
- **Overflow Policy**: On full buffer, `overflow_count` increments and incoming byte is dropped without corrupting existing FIFO data.

---

## 12. XNU TTY & Console Read Call Graphs

### TTY Input Injection
```text
UARTDM Hardware
      ↓
msm_uart_receive_data()
      ↓
xzs_uart_rx_buf_get()
      ↓
cons_cinput(ch)   [bsd/dev/arm/km.c:459]
      ↓
tty_lock(tp)
      ↓
linesw[0].l_rint(ch, tp) -> ttyinput(ch, tp)   [bsd/kern/tty.c]
      ↓
Enqueue to tp->t_rawq
      ↓
[If ICANON && ch == '\n']
Move line from rawq to tp->t_canq
      ↓
ttwakeup(tp) -> wakeup((caddr_t)&tp->t_rawq)
      ↓
tty_unlock(tp)
```

### Userspace `/dev/console` Read Path
```text
/bin/sh EL0: read(0, buf, n)
      ↓
XNU syscall: sys_read() (sysent[3])
      ↓
fo_read() -> vn_read()
      ↓
spec_read() -> cdevsw[0].d_read()
      ↓
kmread(dev, uio, ioflag)   [bsd/dev/arm/km.c:192]
      ↓
tty_lock(tp)
      ↓
linesw[0].l_read(tp, uio, ioflag) -> ttread(tp, uio, ioflag)   [bsd/kern/tty.c:2100]
      ↓
If tp->t_canq empty:
      ↓
ttysleep(tp, (caddr_t)&tp->t_rawq, TTIPRI | PCATCH, "ttyin", 0)
      [Thread sleeps, releases tty_lock]
      ... [Woken up by ttwakeup()] ...
[Thread re-acquires tty_lock]
      ↓
Copy bytes from tp->t_canq to user uio via q_to_b()
      ↓
tty_unlock(tp)
      ↓
Return byte count to userspace EL0
```

---

## 13. TTY Mode, Line Discipline & Echo Ownership

- **TTY Mode**: Canonical mode (`ICANON`) is the default native XNU termios setting on `/dev/console`.
- **Newline Mapping**: `ICRNL` maps incoming `\r` (CR) to `\n` (LF).
- **Echo Ownership**:
  - Character echo is owned exclusively by the BSD TTY line discipline (`ECHO` / `ECHOCTL`).
  - The UART hardware driver does **NOT** echo characters.
  - This prevents double-echo artifacts on serial terminals.

---

## 14. Phase D7-M4 Telemetry & Checkpoint Schema

Reserved family: **`D730`**

| Checkpoint | Identifier | Description |
|---|---|---|
| `D730/00` | `ENTER` | Phase D7-M4 entered |
| `D730/10` | `HW_AUDIT` | RX hardware register and pinmux audit complete |
| `D730/11` | `HW_CONFIG` | UARTDM RX receiver and stale event armed |
| `D730/20` | `BYTE_DETECT` | External RX byte detected in UARTDM hardware |
| `D730/21` | `RAW_VALID` | Raw UART byte value validated against expected test byte |
| `D730/30` | `DRV_READY` | Driver `receive_ready()` returned true |
| `D730/31` | `DRV_DATA` | Driver `receive_data()` returned valid character |
| `D730/40` | `BUF_ENQ` | Character enqueued into RX ring buffer |
| `D730/41` | `BUF_DEQ` | Character dequeued from RX ring buffer |
| `D730/50` | `TTY_IN` | Character accepted by `cons_cinput()` into tty raw queue |
| `D730/51` | `TTY_WAKE` | End-of-line detected; `ttwakeup()` issued to blocked reader |
| `D730/60` | `READ_ENTER` | Shell `read(0)` entered kernel via `sysent[3]` |
| `D730/61` | `READ_BLOCK` | Shell thread entered normal sleep on tty waitchannel |
| `D730/62` | `READ_WAKE` | Shell thread awakened by incoming character |
| `D730/63` | `READ_RET` | `read(0)` returned to EL0 with data |
| `D730/70` | `EL0_VALID` | Input bytes validated inside EL0 userspace memory |
| `D730/90` | `ACCEPT` | Phase D7-M4 acceptance invariants satisfied |
| `D730/91` | `PASS` | Phase D7-M4 complete and verified |
| `D730/01` | `TERMINAL` | Terminal milestone before D7-M5 |

---

## 15. Verifier & Negative Test Artifacts

- **Primary Verifier**: `scripts/verify_d7m4_acceptance.py` (executable, gated by D6, D7-M2, D7-M3 regressions, D730 sequence, and layer correlation)
- **Negative Test Baseline**: `artifacts/logs/d7m4_false_positive_console.log` (fails with exit code 1 as required)
- **Ring Buffer Host Tests**: `tests/test_xzs_rx_buf.c` (5/5 unit tests passing)

---

## 16. Hardware Implementation Ladder (M4-A through M4-F)

Implementation will proceed strictly along this ladder:
1. **M4-A**: Physical Hardware Byte (prove external byte reaches UARTDM register)
2. **M4-B**: Driver Primitive (`receive_ready` and `receive_data` operational)
3. **M4-C**: Buffering (SPSC ring buffer integrated)
4. **M4-D**: TTY Input (`cons_cinput` fed from ring buffer pump)
5. **M4-E**: Native Read (`read(0)` in EL0 blocks and returns data)
6. **M4-F**: Stdin Milestone (interactive line input validated)

---

## 17. Readiness Status Declaration

```text
M3_BASELINE_FROZEN=yes
UARTDM_RX_REGISTER_MAP_AUDITED=yes
UARTDM_RX_INIT_SEQUENCE_AUDITED=yes
UART_RX_GPIO_AUDITED=yes
UART_RX_CLOCK_AUDITED=yes
UARTDM_RX_IRQ_IDENTIFIED=yes
XNU_RX_DRIVER_CONTRACT_AUDITED=yes
XNU_TTY_INPUT_PATH_AUDITED=yes
DEV_CONSOLE_READ_PATH_AUDITED=yes
READ_BLOCK_WAKE_PATH_AUDITED=yes
RX_RING_BUFFER_DESIGN_READY=yes
D730_TELEMETRY_DEFINED=yes
D7M4_VERIFIER_SKELETON_READY=yes
D7M4_NEGATIVE_TESTS_READY=yes
EXTERNAL_INPUT_METHOD_DEFINED=yes
D7_M4_P0_READINESS_COMPLETE=yes
```
