# XZS D8-M5 Panel Power & Reset Sequence Source Audit

## 1. Objective & Target Device

- **Target:** Sony Xperia XZs (`G8231`, codename `keyaki` / `tone`, SoC Qualcomm MSM8996 v3.0)
- **Panel:** Sharp + Synaptics command-mode panel (`somc,sharp_synaptics_cmd_9_panel`, internal name `"9"`)
- **Panel Node Location:** `artifacts/display-audit/keyaki.dts:1740-1839`
- **Supply Entries Node:** `artifacts/display-audit/keyaki.dts:18628-18660` (`dsi_panel_pwr_supply_full_incell`, phandle `<0x28>`)
- **DSI0 Controller Node:** `artifacts/display-audit/keyaki.dts:1997-2030` (`qcom,mdss_dsi_ctrl0@994000`)

---

## 2. Source-Audited Parameters & Evidence

### A. Supply Rails & GPIO Mapping
1. **VDDIO (LCD Logic 1.8V):**
   - Controlled via `qcom,platform-vddio-gpio = <0x38 0x33 0x00>;` -> TLMM GPIO 51 (`0x33`), Active HIGH.
   - Supply entry: `qcom,panel-supply-entry@0` (`reg = <0x00>; qcom,supply-name = "vddio";`).
2. **LAB (Positive Bias Rail +5.60V, VSP):**
   - PMIC PMI8994/PMI8996 secondary arbiter SID 3, Base `0xDE00`, Type `0x24`.
   - Voltage register `0xDE41` = `0x8A` (+5.60V).
   - Supply entry: `qcom,panel-supply-entry@1` (`reg = <0x01>; qcom,supply-name = "lab";`).
3. **IBB (Negative Bias Rail -5.60V, VSN):**
   - PMIC PMI8994/PMI8996 secondary arbiter SID 3, Base `0xDC00`, Type `0x20`.
   - Voltage register `0xDC41` = `0xAA` (-5.60V).
   - Supply entry: `qcom,panel-supply-entry@2` (`reg = <0x02>; qcom,supply-name = "ibb";`).
4. **Panel Reset Pin:**
   - Controlled via `qcom,platform-reset-gpio = <0x38 0x08 0x00>;` -> TLMM GPIO 8 (`0x08`), Active LOW.
5. **Panel TE Pin (mdp_vsync):**
   - Controlled via `qcom,platform-te-gpio = <0x38 0x0a 0x00>;` -> TLMM GPIO 10 (`0x0a`), Function 1 (`mdp_vsync`).

---

## 3. Authoritative Power & Reset Sequences

### A. Power-On Sequence
- `somc,pw-wait-after-on-vdd = <0x00>;` (0 ms)
- `somc,pw-wait-after-on-vddio = <0x0a>;` (10 ms after VDDIO / GPIO51 enable)
- `somc,pw-wait-after-on-vsp = <0x0a>;` (10 ms after LAB / VSP enable + VREG_OK)
- `somc,pw-wait-after-on-vsn = <0x00>;` (0 ms after IBB / VSN enable + VREG_OK)
- `somc,pw-on-rst-seq = <0x00 0x0a 0x01 0x0a>;` (Reset LOW for 10 ms, then Reset HIGH for 10 ms)

Order:
```text
POWER_ON_SEQUENCE:
  1. GPIO51 HIGH (VDDIO ON) -> wait 10 ms (10,000 us)
  2. LAB enable (SID 3 0xDE46 = 0x80) -> poll VREG_OK == 1 -> wait 10 ms (10,000 us)
  3. IBB enable (SID 3 0xDC46 = 0x80) -> poll VREG_OK == 1 -> wait 0 ms
  4. Reset assert LOW (GPIO8 = 0) -> wait 10 ms (10,000 us)
  5. Reset release HIGH (GPIO8 = 1) -> wait 10 ms (10,000 us) stabilization
```

### B. Power-Off Sequence
- `somc,pw-off-rst-b-seq = <0x00 0x05>;` (Reset LOW for 5 ms)
- `somc,pw-wait-after-off-vsn = <0x0a>;` (10 ms after IBB / VSN disable + VREG_OK=0)
- `somc,pw-wait-after-off-vsp = <0x0a>;` (10 ms after LAB / VSP disable + VREG_OK=0)
- `somc,pw-wait-after-off-vddio = <0x00>;` (0 ms after VDDIO / GPIO51 disable)
- `somc,pw-wait-after-off-vdd = <0x00>;` (0 ms)
- `somc,pw-down-period = <0x12c>;` (300 ms panel power-down settling window)

Order:
```text
POWER_OFF_SEQUENCE:
  1. Reset assert LOW (GPIO8 = 0) -> wait 5 ms (5,000 us)
  2. IBB disable (SID 3 0xDC46 = 0x00) -> poll VREG_OK == 0 -> wait 10 ms (10,000 us)
  3. LAB disable (SID 3 0xDE46 = 0x00) -> poll VREG_OK == 0 -> wait 10 ms (10,000 us)
  4. GPIO51 LOW (VDDIO OFF) -> wait 0 ms
  5. Power-down settling window -> wait 300 ms (300,000 us) before re-enabling
```

### C. Reset Sequence
```text
RESET_SEQUENCE:
  ASSERT LOW:   GPIO8 = 0, duration = 10 ms
  RELEASE HIGH: GPIO8 = 1, duration = 10 ms stabilization
```

---

## 4. Required Source Audit Table

| Step | Signal / Rail | Action | Delay (us / ms) | Exact Source Property | Classification |
|---|---|---|---|---|---|
| **1** | GPIO 8 | Assert LOW (Safe State) | 0 ms | `keyaki.dts:2020` `qcom,platform-reset-gpio = <0x38 0x08 0x00>` | **SOURCE_PROVEN** |
| **2** | GPIO 51 (VDDIO) | Drive HIGH (Enable 1.8V) | 10 ms (10,000 us) | `keyaki.dts:1808` `somc,pw-wait-after-on-vddio = <0x0a>` | **SOURCE_PROVEN** |
| **3** | LAB (+5.60V) | Write 0xDE46=0x80, Poll VREG_OK | 10 ms (10,000 us) | `keyaki.dts:1809` `somc,pw-wait-after-on-vsp = <0x0a>` | **SOURCE_PROVEN** |
| **4** | IBB (-5.60V) | Write 0xDC46=0x80, Poll VREG_OK | 0 ms | `keyaki.dts:1810` `somc,pw-wait-after-on-vsn = <0x00>` | **SOURCE_PROVEN** |
| **5** | GPIO 8 (Reset) | Assert LOW | 10 ms (10,000 us) | `keyaki.dts:1805` `somc,pw-on-rst-seq = <0x00 0x0a 0x01 0x0a>` | **SOURCE_PROVEN** |
| **6** | GPIO 8 (Reset) | Release HIGH | 10 ms (10,000 us) | `keyaki.dts:1805` `somc,pw-on-rst-seq = <0x00 0x0a 0x01 0x0a>` | **SOURCE_PROVEN** |
| **7** | Powered-Idle Check | Readback GPIO8, 51, LAB, IBB, DSI0 host | N/A | D8-M5 Specification | **SOURCE_PROVEN** |
| **8** | GPIO 8 (Shutdown) | Assert LOW | 5 ms (5,000 us) | `keyaki.dts:1806` `somc,pw-off-rst-b-seq = <0x00 0x05>` | **SOURCE_PROVEN** |
| **9** | IBB (Shutdown) | Write 0xDC46=0x00, Poll VREG_OK=0 | 10 ms (10,000 us) | `keyaki.dts:1814` `somc,pw-wait-after-off-vsn = <0x0a>` | **SOURCE_PROVEN** |
| **10** | LAB (Shutdown) | Write 0xDE46=0x00, Poll VREG_OK=0 | 10 ms (10,000 us) | `keyaki.dts:1813` `somc,pw-wait-after-off-vsp = <0x0a>` | **SOURCE_PROVEN** |
| **11** | GPIO 51 (Shutdown)| Drive LOW (Disable 1.8V) | 0 ms | `keyaki.dts:1812` `somc,pw-wait-after-off-vddio = <0x00>` | **SOURCE_PROVEN** |
| **12** | Panel Settling | Settle before next power-on | 300 ms (300,000 us) | `keyaki.dts:1821` `somc,pw-down-period = <0x12c>` | **SOURCE_PROVEN** |

**Explicit Sequence Strings:**
```text
POWER_ON_SEQUENCE=VDDIO_10MS->LAB_VREG_OK_10MS->IBB_VREG_OK_0MS->RST_LOW_10MS->RST_HIGH_10MS
POWER_OFF_SEQUENCE=RST_LOW_5MS->IBB_OFF_10MS->LAB_OFF_10MS->VDDIO_OFF_0MS->SETTLE_300MS
RESET_SEQUENCE=ASSERT_LOW_10MS->RELEASE_HIGH_10MS
```
