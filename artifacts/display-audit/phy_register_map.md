# QUALCOMM MSM8996 14NM DSI PHY REGISTER MAP & TIMING AUDIT

## 1. Architectural Summary

| Parameter | Value | Evidence Class | Source |
|---|---|---|---|
| **PHY Architecture** | Qualcomm 14nm DSI PHY | SOURCE_PROVEN | `msm8996.dtsi` (`compatible = "qcom,dsi-phy-14nm"`) |
| **DSI0 PHY Common Base** | `0x00994400` (length `0x100`) | SOURCE_PROVEN | `msm8996.dtsi:1107`, `xzs_diag.c:199` |
| **DSI0 PHY Lane Base** | `0x00994500` (length `0x300`) | SOURCE_PROVEN | `msm8996.dtsi:1108`, `xzs_diag.c:199` |
| **PHY Revision** | 0x2 (14nm v2) | HARDWARE_PROVEN | `twrp-dmesg-full.log:876` (`PHY rev:0x2`) |
| **Lane Stride** | `0x80` bytes per lane | SOURCE_PROVEN | Qualcomm 14nm PHY hardware spec |
| **Total Lanes** | 5 (4 Data lanes + 1 Clock lane) | SOURCE_PROVEN | DTB `somc,sharp_synaptics_cmd_9_panel` |

---

## 2. Lane Base Address Mapping

| Lane | Base Address | Offset Range | Role |
|---|---|---|---|
| **Data Lane 0** | `0x00994500` | `0x00994500 - 0x0099457f` | High-speed data lane 0 (bidirectional / LP) |
| **Data Lane 1** | `0x00994580` | `0x00994580 - 0x009945ff` | High-speed data lane 1 |
| **Data Lane 2** | `0x00994600` | `0x00994600 - 0x0099467f` | High-speed data lane 2 |
| **Data Lane 3** | `0x00994680` | `0x00994680 - 0x009946ff` | High-speed data lane 3 |
| **Clock Lane**  | `0x00994700` | `0x00994700 - 0x0099477f` | High-speed differential clock lane |

---

## 3. Keyaki Panel "9" PHY Timing Blob Decoding

Property: `qcom,mdss-dsi-panel-timings-8996` (40 bytes / 10 32-bit words):
```text
Raw DTB hex:
<0x241f0809 0x050304a0 0x241f0809 0x050304a0 0x241f0809 0x050304a0 0x241f0809 0x050304a0 0x241b0809 0x050304a0>
```

### Lane-by-Lane Breakdown (8 bytes per lane):

| Register / Parameter | Offset in Lane | Lane 0 (0x994500) | Lane 1 (0x994580) | Lane 2 (0x994600) | Lane 3 (0x994680) | Clock Lane (0x994700) |
|---|---|:---:|:---:|:---:|:---:|:---:|
| `TIMING_CTRL_0` | `+ 0x18` | `0x24` | `0x24` | `0x24` | `0x24` | `0x24` |
| `TIMING_CTRL_1` | `+ 0x1c` | `0x1f` | `0x1f` | `0x1f` | `0x1f` | **`0x1b`** |
| `TIMING_CTRL_2` | `+ 0x20` | `0x08` | `0x08` | `0x08` | `0x08` | `0x08` |
| `TIMING_CTRL_3` | `+ 0x24` | `0x09` | `0x09` | `0x09` | `0x09` | `0x09` |
| `TIMING_CTRL_4` | `+ 0x28` | `0x05` | `0x05` | `0x05` | `0x05` | `0x05` |
| `TIMING_CTRL_5` | `+ 0x2c` | `0x03` | `0x03` | `0x03` | `0x03` | `0x03` |
| `TIMING_CTRL_6` | `+ 0x30` | `0x04` | `0x04` | `0x04` | `0x04` | `0x04` |
| `TIMING_CTRL_7` | `+ 0x34` | `0xa0` | `0xa0` | `0xa0` | `0xa0` | `0xa0` |

*Note: In `TIMING_CTRL_1`, the clock lane has `0x1b` (matching `t-clk-post = 0x1b` = 27), while data lanes have `0x1f`.*

---

## 4. Hardware Parameters from Device Tree

- **Strength Control** (`qcom,platform-strength-ctrl`):
  `[ff 06 ff 06 ff 06 ff 06 ff 00]`
  - Lanes 0 to 3: Drive strength `0xff 0x06`
  - Clock lane: Drive strength `0xff 0x00`
- **Regulator Settings** (`qcom,platform-regulator-settings`):
  `[1d 1d 1d 1d 1d]` (0x1d = 29 for each lane)
- **Lane Config** (`qcom,platform-lane-config`):
  - Lanes 0 to 3: `0x100f`
  - Clock lane: `0x108f`
- **Clock Timing Parameters**:
  - `t-clk-pre` = `0x2b` (43)
  - `t-clk-post` = `0x1b` (27)
