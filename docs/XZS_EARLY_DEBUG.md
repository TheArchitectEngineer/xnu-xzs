# Xperia XZs (MSM8996) Early Debug Architecture & Telemetry Specification

## 1. Hardware UART Identification (MSM8996 / Tone Keyaki)

Based on thorough verification of Sony Open Devices source code, device tree sources (`msm8996.dtsi`, `msm8996-sony-xperia-tone.dtsi`, `minimal-keyaki.dts`), Linux mainline, and Qualcomm MSM8996 hardware documentation:

### Target Debug UART: BLSP2 UART2 (`serial@75b0000`)
- **Controller Type:** Qualcomm MSM UART-DM (UART Data Mover) v1.4 (`qcom,msm-uartdm-v1.4`, `qcom,msm-uartdm`).
- **Physical MMIO Base Address:** `0x075b0000`
- **MMIO Size:** `0x1000` (4 KB)
- **Interrupt (GICv3):** `GIC_SPI 114` (Level High, active in GICD/GICR)
- **Hardware Clocks:**
  - Core / Apps clock: `GCC_BLSP2_UART2_APPS_CLK`
  - Bus / Interface clock: `GCC_BLSP2_AHB_CLK`
- **Pin Configuration & Mux:**
  - TX Pin: `GPIO 4` (Pinctrl function `blsp_uart8`, drive-strength: 16mA, bias-disable)
  - RX Pin: `GPIO 5` (Pinctrl function `blsp_uart8`, drive-strength: 16mA, bias-disable)
- **Baud Rate & Framing:** `115200 8-N-1` (No parity, 8 data bits, 1 stop bit, no flow control)
- **Bootloader Pre-initialization:**
  - Sony S1 bootloader (`aboot` / LK) initializes this UART controller and sets pinmux before launching the kernel.
  - Stock / reference kernel command line: `console=ttyMSM0,115200 earlycon=msm_serial_dm,0x075b0000`

*(Note: `blsp1_uart2` at `0x07570000` is assigned to Bluetooth BCM4359 on Tone/Keyaki and is NOT the debug console).*

### Qualcomm MSM UART-DM v1.4 Register Map

| Offset | Register Name | Description |
|---|---|---|
| `0x0008` | `MSM_UART_SR` | Status Register (Bit 2: `TX_READY`, Bit 3: `TX_EMPTY`) |
| `0x0010` | `MSM_UART_CR` | Command Register |
| `0x0014` | `MSM_UART_IMR` | Interrupt Mask Register (write 0 to disable UART interrupts) |
| `0x0040` | `UARTDM_NCF_TX` | Number of Characters to Flush/Transmit |
| `0x0070` | `UARTDM_TF` | Transmit FIFO (32-bit packed word) |

To transmit a single byte `c` via raw MMIO without OS drivers:
1. Write `0` to `MSM_UART_IMR` (`0x075b0014`).
2. Poll `MSM_UART_SR` (`0x075b0008`) until `TX_EMPTY` (bit 3) is set.
3. Write `1` to `UARTDM_NCF_TX` (`0x075b0040`) and read back to commit.
4. Poll `MSM_UART_SR` until `TX_READY` (bit 2) is set.
5. Write `(uint32_t)(uint8_t)c` to `UARTDM_TF` (`0x075b0070`).

---

## 2. Hardware Watchdog Control (Qualcomm APCS WDT)

The MSM8996 APCS (Application Processor Subsystem) Watchdog is located at:
- **Base Address:** `0x09830000`
- **WDT_RST (Reset / Pet):** `0x09830038`
- **WDT_EN (Enable):** `0x09830040`
- **Default Bark Time:** 20 seconds (`0x4e20`)
- **Default Bite Time (Hard Reset):** 30 seconds (`0x7530` = 30,000 ms)

**Action Required:**
Before handing off execution from bootshim or during early XNU startup, the APCS WDT must be disabled:
```c
*(volatile uint32_t *)0x09830040 = 0; // Disable watchdog
*(volatile uint32_t *)0x09830038 = 1; // Pet/reset counter
```
This eliminates the 30-second watchdog hardware reset during kernel bringup.

---

## 3. Persistent Reserved DRAM Debug Log Buffer

To survive unexpected resets and guarantee log retrieval even when physical serial cables are not hooked up:
- **Physical Address:** `0x80060000`
- **Buffer Size:** `0x10000` (64 KB)
- **Placement:** Low DRAM between Bootloader/Bootshim staging (`0x80080000`) and Kernel load address (`0x82000000`). This memory is preserved across warm CPU resets and is outside the XNU kernel image and bootstrap page tables.

### Buffer Header (`struct xzs_debug_log`)
```c
struct xzs_debug_log {
    uint32_t magic;         /* 0x585a5344 ("XZSD") */
    uint32_t version;       /* 1 */
    uint32_t write_offset;  /* Offset into data[] for next write */
    uint32_t boot_count;    /* Incremented on each boot */
    char data[65520];       /* Circular or linear log text */
};
```

---

## 4. Boot Checkpoint Checklists

### Bootshim Telemetry Checkpoints
- `[XZS-SHIM] A: entry` - Bootshim C entry reached (`bootshim_main`)
- `[XZS-SHIM] B: DTB located` - S1 LK device tree verified
- `[XZS-SHIM] C: Mach-O parsed` - Kernel Mach-O header checked (`0xfeedfacf`)
- `[XZS-SHIM] D: Mach-O verified` - Kernel entry point extracted
- `[XZS-SHIM] E: ADT constructed` - Apple Device Tree built in memory
- `[XZS-SHIM] F: boot args ready` - `struct boot_args` populated with memory/commandline
- `[XZS-SHIM] G: jumping to XNU` - Final CPU register state dump before `jump_to_xnu`

### XNU Early Kernel Checkpoints
- `[XNU-XZS] K0: kernel entry` - `start_first_cpu` reached in `start.s`
- `[XNU-XZS] K1: exception vectors installed` - Diagnostic `LowExceptionVectorBase` loaded into `VBAR_EL1`
- `[XNU-XZS] K2: early mappings prepared` - Bootstrap page tables populated
- `[XNU-XZS] K3: TCR_EL1 configured` - `TCR_EL1`, `TTBR0_EL1`, `TTBR1_EL1`, `MAIR_EL1` set
- `[XNU-XZS] K4: before SCTLR.M` - Immediately prior to enabling MMU in `SCTLR_EL1`
- `[XNU-XZS] K5: after SCTLR.M` - Immediately after `isb` following MMU enable
- `[XNU-XZS] K6: arm_init entered` - High-level C entry in `osfmk/arm/arm_init.c`
- `[XNU-XZS] K7: pmap initialized` - VM system initialized in `pmap_bootstrap`

---

## 5. Early Exception Telemetry & Register Dump

If any CPU fault occurs during early boot, `LowExceptionVectorBase` catches the exception and outputs:
```text
*** XNU EARLY EXCEPTION ***
TYPE      = <SYNC / IRQ / FIQ / SERROR>
CurrentEL = 0x...
ESR_EL1   = 0x...
ELR_EL1   = 0x...
FAR_EL1   = 0x...
SPSR_EL1  = 0x...
SCTLR_EL1 = 0x...
TCR_EL1   = 0x...
TTBR0_EL1 = 0x...
TTBR1_EL1 = 0x...
MAIR_EL1  = 0x...
VBAR_EL1  = 0x...
SP        = 0x...
x0  = ...  x1  = ...  x2  = ...  x3  = ...
x4  = ...  x5  = ...  x6  = ...  x7  = ...
...
x28 = ...  x29 = ...  x30 = ...
```
Followed by a controlled spin `1: wfe; b 1b` preserving registers for JTAG/RAM dump inspection.
