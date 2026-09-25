# XZS D8-M6 Panel Vendor / DCS Initialization Source Audit

## 1. Objective & Target Device

- **Target:** Sony Xperia XZs (`G8231`, codename `keyaki` / `tone`, SoC Qualcomm MSM8996 v3.0, Serial: `BH905SX976`)
- **Panel:** Sharp + Synaptics command-mode panel (`somc,sharp_synaptics_cmd_9_panel`, internal name `"9"`)
- **Panel Node Location:** `artifacts/display-audit/keyaki.dts:1740-1839`
- **DSI0 Controller Node:** `artifacts/display-audit/keyaki.dts:1997-2030` (`qcom,mdss_dsi_ctrl0@994000`)
- **Resolution & Bus:** 1080x1920, MIPI DSI0 Command Mode, 4 data lanes + 1 clock lane, RGB888 (24 bpp).

---

## 2. Source-Audited Command Tables from `keyaki.dts`

The device tree defines three exact command properties for the `"9"` panel:
1. `qcom,mdss-dsi-post-panel-on-command` (Sleep Out sequence)
2. `qcom,mdss-dsi-on-command` (Tear On + Display On sequence)
3. `qcom,mdss-dsi-off-command` (Display Off + Sleep In sequence)

### A. Transmission State
- `qcom,mdss-dsi-on-command-state = "dsi_lp_mode";` (`keyaki.dts:1774`)
- Transmission mode is Low Power Escape Mode (`dsi_lp_mode`).
- `DSI_COMMAND_MODE_DMA_CTRL` bit 26 (`DSI_CMD_DMA_CTRL_LOW_POWER = 0x04000000`) must be set.

### B. ON Command Sequence

#### 1. `post-panel-on-command` (`keyaki.dts:1772`)
Raw DT Property: `<0x05010000 0x78000111>`
- **Word 0 (`0x05010000`):**
  - Byte 0 (`0x05`): DCS Short Write (0 param) Data Type (`DSI_DCS_SHORT_WRITE`)
  - Byte 1 (`0x01`): Last command flag (`last = 1`)
  - Byte 2 (`0x00`): Virtual Channel (`vc = 0`)
  - Byte 3 (`0x00`): Acknowledge request (`ack = 0`)
- **Word 1 (`0x78000111`):**
  - Byte 4-5 (`0x0078` = 120): Post-transmission delay = 120 ms (`0x78`)
  - Byte 6 (`0x01`): Payload length = 1 byte
  - Byte 7 (`0x11`): Payload byte 0 = `0x11` (DCS `SLPOUT` — Exit Sleep Mode)
- **Interpretation:** Sends DCS `0x11` (Sleep Out) to initialize internal panel charge pumps, followed by mandatory 120 ms stabilization delay.

#### 2. `on-command` (`keyaki.dts:1771`)
Raw DT Property: `[39 01 00 00 00 00 02 35 00 05 01 00 00 00 00 01 29]`
- **Packet 1: Tear On (`39 01 00 00 00 00 02 35 00`):**
  - Byte 0 (`0x39`): DCS Long Write Data Type (`DSI_DCS_LONG_WRITE`)
  - Byte 1 (`0x01`): `last = 1`
  - Byte 2 (`0x00`): `vc = 0`
  - Byte 3 (`0x00`): `ack = 0`
  - Byte 4-5 (`0x0000`): Post-wait delay = 0 ms
  - Byte 6 (`0x02`): Payload length = 2 bytes
  - Byte 7 (`0x35`): Payload byte 0 = `0x35` (DCS `TEON` — Set Tear On)
  - Byte 8 (`0x00`): Payload byte 1 = `0x00` (Mode 0: V-blanking information only)
- **Packet 2: Display On (`05 01 00 00 00 00 01 29`):**
  - Byte 9 (`0x05`): DCS Short Write (0 param) Data Type (`DSI_DCS_SHORT_WRITE`)
  - Byte 10 (`0x01`): `last = 1`
  - Byte 11 (`0x00`): `vc = 0`
  - Byte 12 (`0x00`): `ack = 0`
  - Byte 13-14 (`0x0000`): Post-wait delay = 0 ms
  - Byte 15 (`0x01`): Payload length = 1 byte
  - Byte 16 (`0x29`): Payload byte 0 = `0x29` (DCS `DISPON` — Set Display On)

### C. OFF Command Sequence

#### `off-command` (`keyaki.dts:1773`)
Raw DT Property: `<0x05010000 0x00000128 0x05010000 0x78000110>`
- **Packet 1: Display Off (`0x05010000 0x00000128`):**
  - Word 0 (`0x05010000`): DataType `0x05`, `last = 1`, `vc = 0`, `ack = 0`
  - Word 1 (`0x00000128`): Post-wait = 0 ms, Length = 1 byte, Payload = `0x28` (DCS `DISPOFF` — Set Display Off)
- **Packet 2: Sleep In (`0x05010000 0x78000110`):**
  - Word 2 (`0x05010000`): DataType `0x05`, `last = 1`, `vc = 0`, `ack = 0`
  - Word 3 (`0x78000110`): Post-wait = 120 ms (`0x78`), Length = 1 byte, Payload = `0x10` (DCS `SLPIN` — Enter Sleep Mode)

Followed immediately by proven M5 physical shutdown:
1. Assert Reset LOW (GPIO8 = 0), wait 5 ms
2. Disable IBB (-5.60V), poll `VREG_OK == 0`, wait 10 ms
3. Disable LAB (+5.60V), poll `VREG_OK == 0`, wait 10 ms
4. Disable VDDIO (GPIO51 = 0)
5. Power-down settling window: wait 300 ms (`somc,pw-down-period = <0x12c>`)

---

## 3. MSM8996 DSI Command Mode DMA Architecture

### A. Register Map (Base: `0x00994000`)
| Offset | Name | Type | Reset/Mask | Proven Role in DSI DMA |
|---|---|---|---|---|
| `0x004` | `DSI_CTRL` | RW | `0x000001f5` | Host enable, command mode enable, lane enables |
| `0x008` | `DSI_STATUS` | RO | `0x00000000` | Host status flags (Bit 0 = CMD_MODE_DMA_BUSY) |
| `0x00c` | `DSI_FIFO_STATUS` | RO | `0x11111000` | FIFO watermarks & empty indicators |
| `0x03c` | `DSI_COMMAND_MODE_DMA_CTRL` | RW | `0x00000000` | Bit 28: Embedded mode (`0x10000000`), Bit 26: LPM (`0x04000000`) |
| `0x048` | `DSI_DMA_CMD_OFFSET` | RW | `0x00000000` | Physical address (`paddr`) of DMA memory buffer |
| `0x04c` | `DSI_DMA_CMD_LENGTH` | RW | `0x00ffffff` | Total byte length of DMA buffer (4-byte aligned) |
| `0x068` | `DSI_ACK_ERR_STATUS` | RO | `0x00000000` | Acknowledge error flags from panel |
| `0x084` | `DSI_TRIG_CTRL` | RW | `0x00000000` | Trigger source multiplexer (`0 = SW Trigger`) |
| `0x090` | `DSI_CMD_MODE_DMA_SW_TRIGGER` | WO | `0x00000001` | Write 1 triggers DMA command transmission (self-clearing) |
| `0x0c0` | `DSI_TIMEOUT_STATUS` | RO | `0x00000000` | DSI timeout status flags |
| `0x110` | `DSI_INT_CTRL` | RW | `0x00000001` | Bit 0: `DSI_IRQ_CMD_DMA_DONE = 0x1` (W1C to acknowledge/clear) |

### B. Memory Buffer Layout (MSM Specific In-Memory Packet Format)
In downstream Qualcomm MSM8996 Linux (`drivers/video/fbdev/msm/mdss/mdss_dsi_host.c`) and upstream DRM (`drivers/gpu/drm/msm/dsi/dsi_host.c`):
The DMA engine expects in-memory DSI packets structured with a 4-byte header:
```text
  data[0] = packet.header[1];  // Short: param0 (or 0)      | Long: wc & 0xFF
  data[1] = packet.header[2];  // Short: param1 (or 0)      | Long: (wc >> 8) & 0xFF
  data[2] = packet.header[0];  // (vc << 6) | dtype
  data[3] = BIT(7);            // Last packet flag (0x80)
                               // | BIT(6) (0x40) if Long Packet
                               // | BIT(5) (0x20) if Read/BTA
```
- **Short Write Packet (dtype 0x05, 1 byte cmd):**
  - `data[0]` = `cmd` (e.g. `0x11`)
  - `data[1]` = `0x00`
  - `data[2]` = `0x05`
  - `data[3]` = `0x80` (`BIT(7)`)
  - Length = 4 bytes.
  - 32-bit LE Word = `0x80050011`
- **Long Write Packet (dtype 0x39, length 2 bytes):**
  - `data[0]` = `0x02` (Word Count low)
  - `data[1]` = `0x00` (Word Count high)
  - `data[2]` = `0x39` (DCS Long Write)
  - `data[3]` = `0xC0` (`BIT(7) | BIT(6)`)
  - `data[4]` = `0x35` (DCS `TEON`)
  - `data[5]` = `0x00` (Mode 0)
  - `data[6..7]` = `0xFF 0xFF` (Padding to 4-byte alignment)
  - Length = 8 bytes.
  - 32-bit LE Words: Word 0 = `0xC0390002`, Word 1 = `0xFFFF0035`

### C. Cache Maintenance & Physical Mapping
- Buffer allocation: Statically allocated in kernel BSS, aligned to 64 bytes (cache line).
- Physical address lookup: `(uint32_t)ml_static_vtop((vm_offset_t)&buf[0])`.
- Cache Clean: ARM64 `dc cvac` across buffer lines + `dsb sy; isb` to push memory writes to physical RAM before triggering DMA.

---

## 4. Master Command Truth Table for D8-M6

| Step | Command Name | DCS Opcode | DSI Data Type | In-Memory Word 0 | In-Memory Word 1 | Packet Bytes | Mode | Post-Delay | Exact Source |
|---|---|---|---|---|---|---|---|---|---|
| **1** | Sleep Out (`SLPOUT`) | `0x11` | `0x05` | `0x80050011` | N/A | 4 | LP | 120 ms (120,000 us) | `keyaki.dts:1772` |
| **2** | Set Tear On (`TEON`) | `0x35 0x00` | `0x39` | `0xC0390002` | `0xFFFF0035` | 8 | LP | 0 ms | `keyaki.dts:1771` |
| **3** | Display On (`DISPON`) | `0x29` | `0x05` | `0x80050029` | N/A | 4 | LP | 0 ms | `keyaki.dts:1771` |
| **4** | Display Off (`DISPOFF`) | `0x28` | `0x05` | `0x80050028` | N/A | 4 | LP | 0 ms | `keyaki.dts:1773` |
| **5** | Sleep In (`SLPIN`) | `0x10` | `0x05` | `0x80050010` | N/A | 4 | LP | 120 ms (120,000 us) | `keyaki.dts:1773` |

---

## 5. Strict Milestone Boundaries

- **Zero WLED Backlight Writes:** `WLED_WRITES = 0`. Backlight remains strictly disabled. Screen visually dark is expected.
- **Zero MDP Scanout:** `MDP_KICKOFF_COUNT = 0`. INTF_1 and layer mixer remain dormant.
- **Cycle-Accurate Timing:** Post-delays enforced via `cntvct_el0` @ 19.2 MHz with active watchdog petting (`xzs_watchdog_pet()`).
- **Completion Guarantee:** Polled on `DSI_INT_CTRL` bit 0 (`DMA_CMD_DONE`) with bounded 200 ms timeout.
