# D7-T1 MSM8996 USB-C Hardware and Controller Audit

## 1. Hardware Architecture & Hierarchy

Based on decompiled device tree sources (`artifacts/builds/twrp-extracted.dtb`), reference device trees (`msm8996.dtsi`, `msm8996-sony-xperia-tone.dtsi`), and TWRP dmesg hardware logs:

```text
Qualcomm USB3 Wrapper (0x06af8800)
  ├── Power Domain: GCC USB30_GDSC
  ├── Clocks: GCC_USB30_MASTER_CLK (120MHz), GCC_SYS_NOC_USB3_AXI_CLK,
  │           GCC_AGGRE2_USB3_AXI_CLK, GCC_USB30_SLEEP_CLK,
  │           GCC_USB30_MOCK_UTMI_CLK (19.2MHz)
  ├── QUSB2 Primary High-Speed PHY (0x07411000, size 0x180)
  │     ├── Clocks: GCC_USB_PHY_CFG_AHB2PHY_CLK, GCC_RX1_USB2_CLKREF_CLK
  │     └── Reset: GCC_QUSB2PHY_PRIM_BCR
  └── Synopsys DesignWare DWC3 Core (0x06a00000, size 0xcc00)
        ├── Interrupt: GIC_SPI 131 -> Architectural INTID 163 (Level-High)
        └── Role: Peripheral / Device mode (USB2 High-Speed 480 Mbps)
```

### Exact Hardware Attributes
```text
USB_CONTROLLER_TYPE=Synopsys DesignWare DWC3 (v2.80a / v2.90a) with Qualcomm MSM Glue
QCOM_GLUE_BASE=0x06af8800
QCOM_GLUE_SIZE=0x00000400

DWC3_CORE_BASE=0x06a00000
DWC3_CORE_SIZE=0x0000cc00

USB2_PHY_TYPE=qcom,msm8996-qusb2-phy (High-Speed QUSB2 PHY)
USB2_PHY_BASE=0x07411000
USB2_PHY_SIZE=0x00000180

USB3_PHY_TYPE=qcom,msm8996-qmp-usb3-phy (SuperSpeed QMP USB3 PHY)
USB3_PHY_BASE=0x07410000
USB3_PHY_SIZE=0x00001000
USB3_SUPERSPEED=OUT_OF_SCOPE_D7_T1

USB_DT_SPI=131
USB_ARCH_GIC_INTID=163
USB_IRQ_TRIGGER=LEVEL_HIGH

USB_SECONDARY_IRQS=pwr_event (SPI 180 / INTID 212), hs_phy (SPI 133 / INTID 165), ss_phy (SPI 243 / INTID 275)

USB_CLOCKS=GCC_USB30_MASTER_CLK (120 MHz), GCC_SYS_NOC_USB3_AXI_CLK, GCC_AGGRE2_USB3_AXI_CLK, GCC_USB30_SLEEP_CLK, GCC_USB30_MOCK_UTMI_CLK (19.2 MHz)
USB_POWER_DOMAINS=USB30_GDSC
USB_REGULATORS=USB3_GDSC-supply, vbus_dwc3-supply

TARGET_USB_MODE=USB2_DEVICE
TARGET_MAX_SPEED=HIGH_SPEED
BOOTLOADER_INITIAL_USB_STATE=INITIALIZED_ACTIVE (Sony S1 bootloader fastboot runs on this exact controller in device mode)
BOOTLOADER_USB_STATE_AUDITED=yes
```

---

## 2. Bootloader Handoff State

When `fastboot boot artifacts/builds/xzs-xnu-boot.img` is executed:
1. The host Mac communicates with the Xperia XZs via Fastboot protocol over USB-C.
2. The bootloader (Sony LK / `aboot`) has already configured:
   - `USB30_GDSC` power domain turned on.
   - Core and interface clocks (`GCC_USB30_MASTER_CLK`, `GCC_AGGRE2_USB3_AXI_CLK`) gated on and running at 120 MHz.
   - `QUSB2` High-Speed PHY powered and calibrated.
   - Qualcomm wrapper at `0x06af8800` active.
   - DWC3 core in peripheral/device mode (`GCTL.PRTCAPDIR = 2`).
3. When fastboot jumps to XNU via `bootshim`, the controller is already clocked, powered, and attached to the physical cable.
4. XNU performs controlled re-initialization of DWC3 device mode, preserving the inherited PHY and clock states without resetting the PHY into an unclocked state.

---

## 3. DWC3 Register Map Summary

The Synopsys DesignWare DWC3 controller register layout (`0x06a00000`):

| Offset | Register Name | Description |
|---|---|---|
| `0xC110` | `GCTL` | Global Core Control (PRTCAPDIR bits 13:12: 1=host, 2=device) |
| `0xC118` | `GSTS` | Global Status (CurPrd bits 21:20) |
| `0xC120` | `GSNPSID` | Synopsys ID (Magic `0x5533...`) |
| `0xC400` | `GEVNTADR(0)` | Event Buffer 0 Physical Address (Low 32 bits) |
| `0xC404` | `GEVNTADR_HI(0)` | Event Buffer 0 Physical Address (High 32 bits) |
| `0xC408` | `GEVNTSIZ(0)` | Event Buffer 0 Size (bytes, bit 31=mask) |
| `0xC40C` | `GEVNTCNT(0)` | Event Buffer 0 Count (new events to process; write N to decrement) |
| `0xC700` | `DCFG` | Device Configuration (DEVSPD bits 2:0: 0=High-Speed 480M) |
| `0xC704` | `DCTL` | Device Control (Bit 31: Run/Stop `RUN_STOP`, Bit 30: `CSFTRST`) |
| `0xC708` | `DEVTEN` | Device Event Enable (Disconnect, USB Reset, Connection Done, Suspend) |
| `0xC70C` | `DSTS` | Device Status (CONNECTSPD bits 2:0, SUSPSTS bit 3) |
| `0xC714` | `DALEPENA` | Device Active Logical Endpoint Enable (Bitmask) |
| `0xC800 + n * 0x10` | `DEPCMDPAR2(n)` | Endpoint Command Parameter 2 |
| `0xC804 + n * 0x10` | `DEPCMDPAR1(n)` | Endpoint Command Parameter 1 |
| `0xC808 + n * 0x10` | `DEPCMDPAR0(n)` | Endpoint Command Parameter 0 |
| `0xC80C + n * 0x10` | `DEPCMD(n)` | Endpoint Command (Start Transfer, Update, Complete, etc.) |

Physical Endpoint Numbering:
- `EP0`: Physical EP 0 = Control OUT (EP0 OUT)
- `EP1`: Physical EP 1 = Control IN (EP0 IN)
- `EP2`: Physical EP 2 = Bulk OUT (Logical EP1 OUT, Mac -> Phone, 0x01)
- `EP3`: Physical EP 3 = Bulk IN (Logical EP1 IN, Phone -> Mac, 0x81)

---

## 4. Endpoint Command & Lifecycle Protocol

DWC3 endpoints follow strict command sequences:
1. `DEPSTARTCFG` (cmd 9): Issued on EP0 after USB Reset or Set Configuration.
2. `SETEPCONFIG` (cmd 1): Configure EP type (Control/Bulk), max packet size (64 for EP0, 512 for Bulk HS), FIFO numbers.
3. `SETTRANSFRESOURCE` (cmd 2): Allocate transfer resources (1 resource for EP0 and Bulk).
4. `DALEPENA`: Enable active endpoints in mask (`0x3` for EP0, `0xF` for EP0 + Bulk).
5. `STARTTRANSFER` (cmd 6): Submit TRBs to hardware.
6. Re-arm TRBs upon completion (`XferComplete`).

---

## 5. Architectural Boundaries

- **Input bridge**: USB Bulk OUT packets drain to bounded buffer -> deferred `thread_call` -> `cons_cinput()` -> sealed native tty -> `read(0)` -> `/bin/sh`.
- **Output mirror**: Single C-level console output point mirrors characters to non-blocking USB TX queue only when `USB_CONSOLE_READY=yes`.
- **Safety**: `PSTORE_CONSOLE_PRESERVED=yes`; non-blocking USB writes prevent host disconnect stalls.
- **DMA / Cache Correctness**: All TRB structures and DMA buffers are aligned to 64 bytes and flushed using `flush_dcache()` / `dsb sy` barriers before controller execution.

