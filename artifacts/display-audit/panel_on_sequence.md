# KEYAKI / SHARP SYNAPTICS PANEL ON SEQUENCE AUDIT

## 1. Sequence Overview

Panel Node: `somc,sharp_synaptics_cmd_9_panel`  
Panel Name: "9"  
Mode: `dsi_cmd_mode` (DSI Command Mode)  
Transmission Mode: `dsi_lp_mode` for ON commands  

The complete power-on and initialization sequence proceeds in three distinct stages:
1. **Hardware Power & Reset Sequence** (Regulator rails + GPIO 8 reset)
2. **Post-Panel-On Command** (DCS Exit Sleep / Sleep Out + 120 ms delay)
3. **Panel On Command** (DCS Set Tear ON + DCS Display ON)

---

## 2. Hardware Power-On & Reset Sequence

From DTS properties:
- `somc,pw-on-rst-seq = <0x00 0x0a 0x01 0x0a>` (Low 10ms, High 10ms)
- `somc,pw-wait-after-on-vddio = <0x0a>` (10ms)
- `somc,pw-wait-after-on-vsp = <0x0a>` (10ms)
- `somc,pw-wait-after-on-vsn = <0x00>` (0ms)
- `qcom,mdss-dsi-lp11-init` (Drive DSI data and clock lanes to LP-11 state)

### Step-by-Step Power-Up Order:
1. Enable `vddio-supply` (GPIO 51 / PM8994 rail), wait 10 ms.
2. Enable `lab-supply` (VSP, +5.5V positive LCD bias), wait 10 ms.
3. Enable `ibb-supply` (VSN, -5.5V negative LCD bias), wait 0 ms.
4. Drive DSI lanes to LP-11 state (DSI bus idle high).
5. Assert Reset: Drive GPIO 8 LOW, delay 10 ms.
6. Deassert Reset: Drive GPIO 8 HIGH, delay 10 ms.

---

## 3. DCS Initialization Commands

### Stage 1: Post-Panel-On Command (`qcom,mdss-dsi-post-panel-on-command`)
Raw DTB: `<0x5010000 0x78000111>`  
Byte stream: `[05 01 00 00 78 00 01 11]`

| Field | Value | Meaning |
|---|:---:|---|
| DSI Packet Type | `0x05` | DCS Short Write (No Parameter) |
| Last Packet Flag| `0x01` | Last packet in batch |
| Virtual Channel | `0x00` | VC 0 |
| Delay After     | `0x78` | **120 milliseconds** |
| Payload Length  | `0x0001`| 1 byte payload |
| **Command**     | **`0x11`** | **Exit Sleep Mode (Sleep Out)** |

*Requirement: Standard MIPI DCS requires a minimum of 120 ms after Sleep Out before subsequent display commands.*

---

### Stage 2: Panel On Commands (`qcom,mdss-dsi-on-command`)
Raw DTB: `[39 01 00 00 00 00 02 35 00 05 01 00 00 00 00 01 29]`

#### Packet #1: Set Tearing Effect ON (DCS 0x35)
| Field | Value | Meaning |
|---|:---:|---|
| DSI Packet Type | `0x39` | DSI DCS Long Write |
| Last Packet Flag| `0x01` | Last packet in transaction |
| Virtual Channel | `0x00` | VC 0 |
| Delay After     | `0x00` | 0 ms |
| Payload Length  | `0x0002`| 2 bytes payload |
| Payload Byte 0  | **`0x35`** | **Set Tear ON (TE On)** |
| Payload Byte 1  | `0x00` | Mode 0: V-blanking only |

#### Packet #2: Set Display ON (DCS 0x29)
| Field | Value | Meaning |
|---|:---:|---|
| DSI Packet Type | `0x05` | DCS Short Write (No Parameter) |
| Last Packet Flag| `0x01` | Last packet in transaction |
| Virtual Channel | `0x00` | VC 0 |
| Delay After     | `0x00` | 0 ms |
| Payload Length  | `0x0001`| 1 byte payload |
| Payload Byte 0  | **`0x29`** | **Set Display ON (Display On)** |

---

## 4. Summary Checklist for D8-M5/M6

- [x] Power rails order: VDDIO -> LAB (VSP) -> IBB (VSN)
- [x] Reset sequence: GPIO 8 LOW (10ms) -> HIGH (10ms)
- [x] Sleep Out: DCS `0x11` + 120 ms delay
- [x] TE Enable: DCS `0x35 0x00` (on GPIO 10)
- [x] Display On: DCS `0x29`
- [x] Backlight: PMIC WLED enable
