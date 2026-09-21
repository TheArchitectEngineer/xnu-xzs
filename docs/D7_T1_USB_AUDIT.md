# D7-T1 MSM8996 USB-C Hardware and Controller Audit

## 1. Hardware Architecture & Identity

Based on decompiled device tree sources (`artifacts/builds/twrp-extracted.dtb`), reference device trees (`msm8996.dtsi`, `msm8996-sony-xperia-tone.dtsi`), and TWRP dmesg hardware logs:

```text
USB_CONTROLLER_TYPE=Synopsys DesignWare DWC3 (v2.80a / v2.90a) with Qualcomm MSM Glue
USB_CONTROLLER_MMIO_BASE=0x06a00000
USB_CONTROLLER_MMIO_SIZE=0x000fc000

DWC3_CORE_BASE=0x06a00000
DWC3_CORE_SIZE=0x0000cc00
QCOM_GLUE_BASE=0x06af8800
QCOM_GLUE_SIZE=0x00000400

USB2_PHY_TYPE=qcom,msm8996-qusb2-phy (High-Speed QUSB2 PHY)
USB2_PHY_BASE=0x07411000
USB2_PHY_SIZE=0x00000180

USB3_PHY_TYPE=qcom,msm8996-qmp-usb3-phy (SuperSpeed QMP USB3 PHY)
USB3_PHY_BASE=0x07410000
USB3_PHY_SIZE=0x00001000

USB_IRQ=GIC_SPI 131
USB_GIC_INTID=163 (32 + 131)
USB_IRQ_TRIGGER=LEVEL_HIGH
USB_SECONDARY_IRQS=pwr_event (SPI 180 / INTID 212), hs_phy (SPI 133 / INTID 165), ss_phy (SPI 243 / INTID 275)

USB_CLOCKS=GCC_USB30_MASTER_CLK (120 MHz), GCC_SYS_NOC_USB3_AXI_CLK, GCC_AGGRE2_USB3_AXI_CLK, GCC_USB30_SLEEP_CLK, GCC_USB30_MOCK_UTMI_CLK (19.2 MHz)
USB_POWER_DOMAINS=USB30_GDSC
USB_REGULATORS=USB3_GDSC-supply, vbus_dwc3-supply

USB_ROLE=peripheral (dr_mode = "peripheral", device mode)
USB_NEGOTIATED_SPEED_TARGET=high-speed (480 Mbps USB 2.0 High-Speed)
BOOTLOADER_INITIAL_USB_STATE=INITIALIZED_ACTIVE (Sony S1 bootloader fastboot runs on this exact controller in device mode)
```

---

## 2. Bootloader Handoff State

When `fastboot boot artifacts/builds/xzs-xnu-boot.img` is executed:
1. The host Mac communicates with the Xperia XZs via Fastboot protocol over USB-C.
2. The bootloader (Sony LK / `aboot`) has already configured:
   - `USB30_GDSC` power domain turned on.
   - Core and interface clocks (`GCC_USB30_MASTER_CLK`, `GCC_AGGRE2_USB3_AXI_CLK`) gated on and running.
   - `QUSB2` High-Speed PHY powered and calibrated.
   - DWC3 core in device mode (`GCTL.PRTCAPDIR = 2`).
3. When fastboot jumps to XNU via `bootshim`, the controller is already clocked and powered.
4. XNU performs a controlled soft-reset of the device controller (`DCTL.CSFTRST = 1`) or re-initializes endpoints without losing the PHY clock state.

---

## 3. DWC3 Register Map Summary

The Synopsys DesignWare DWC3 controller register layout:

| Offset | Register Name | Description |
|---|---|---|
| `0xC110` | `GCTL` | Global Core Control (PRTCAPDIR bits 13:12: 1=host, 2=device) |
| `0xC118` | `GSTS` | Global Status (CurPrd bits 21:20) |
| `0xC120` | `GSNPSID` | Synopsys ID (Magic `0x5533...`) |
| `0xC400` | `GEVNTADR(0)` | Event Buffer 0 Physical Address (64-bit) |
| `0xC408` | `GEVNTSIZ(0)` | Event Buffer 0 Size (bytes) |
| `0xC40C` | `GEVNTCNT(0)` | Event Buffer 0 Count (new events to process) |
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
- `EP2`: Physical EP 2 = Bulk OUT (Logical EP1 OUT, Mac → Phone)
- `EP3`: Physical EP 3 = Bulk IN (Logical EP1 IN, Phone → Mac)

---

## 4. Architectural Boundaries

- **Input bridge**: USB Bulk OUT packets drain to bounded buffer → deferred `thread_call` → `cons_cinput()` → sealed native tty → `read(0)` → `/bin/sh`.
- **Output mirror**: `xzs_console_write()` mirrors characters to non-blocking USB TX queue → Bulk IN transfer.
- **Safety**: `PSTORE_CONSOLE_PRESERVED=yes`; non-blocking USB writes prevent host disconnect stalls.
