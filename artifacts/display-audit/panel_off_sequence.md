# KEYAKI / SHARP SYNAPTICS PANEL OFF SEQUENCE AUDIT

## 1. Sequence Overview

Panel Node: `somc,sharp_synaptics_cmd_9_panel`  
Panel Name: "9"  
Mode: `dsi_cmd_mode`  
Transmission Mode: `dsi_hs_mode` for OFF commands  

The display power-down sequence proceeds in reverse:
1. **Backlight Disable** (WLED brightness = 0)
2. **DCS Display OFF** (DCS 0x28)
3. **DCS Sleep IN** (DCS 0x10 + 120 ms delay)
4. **Hardware Reset Assertion** (GPIO 8 LOW)
5. **Regulator Shutdown Order** (IBB -> LAB -> VDDIO)

---

## 2. DCS Shutdown Commands (`qcom,mdss-dsi-off-command`)

Raw DTB: `<0x5010000 0x128 0x5010000 0x78000110>`

### Packet #1: Set Display OFF (DCS 0x28)
Byte stream: `[05 01 00 00 00 00 01 28]`

| Field | Value | Meaning |
|---|:---:|---|
| DSI Packet Type | `0x05` | DCS Short Write (No Parameter) |
| Last Packet Flag| `0x01` | Last packet in transaction |
| Virtual Channel | `0x00` | VC 0 |
| Delay After     | `0x00` | 0 ms |
| Payload Length  | `0x0001`| 1 byte payload |
| **Command**     | **`0x28`** | **Set Display OFF (Display Off)** |

### Packet #2: Enter Sleep Mode (DCS 0x10)
Byte stream: `[05 01 00 00 78 00 01 10]`

| Field | Value | Meaning |
|---|:---:|---|
| DSI Packet Type | `0x05` | DCS Short Write (No Parameter) |
| Last Packet Flag| `0x01` | Last packet in transaction |
| Virtual Channel | `0x00` | VC 0 |
| Delay After     | `0x78` | **120 milliseconds** |
| Payload Length  | `0x0001`| 1 byte payload |
| **Command**     | **`0x10`** | **Enter Sleep Mode (Sleep In)** |

---

## 3. Hardware Power-Down Sequence

From DTS properties:
- `somc,pw-off-rst-b-seq = <0x00 0x05>` (GPIO 8 LOW, wait 5 ms)
- `somc,pw-wait-after-off-vsn = <0x0a>` (10 ms)
- `somc,pw-wait-after-off-vsp = <0x0a>` (10 ms)
- `somc,pw-wait-after-off-vddio = <0x00>` (0 ms)
- `somc,pw-down-period = <0x12c>` (300 ms power-down stabilization window)

### Step-by-Step Power-Down Order:
1. Drive GPIO 8 LOW (assert reset), wait 5 ms.
2. Disable `ibb-supply` (VSN, negative bias), wait 10 ms.
3. Disable `lab-supply` (VSP, positive bias), wait 10 ms.
4. Disable `vddio-supply` (GPIO 51 / PM8994 rail), wait 0 ms.
5. Hold unpowered for at least 300 ms (`somc,pw-down-period`) before any subsequent power-on cycle.
