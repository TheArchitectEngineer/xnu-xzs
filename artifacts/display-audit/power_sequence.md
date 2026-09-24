# KEYAKI PANEL POWER RAILS & SEQUENCE AUDIT

## 1. Supply Rails Inventory

| Rail Name | Node Consumer | Source / Controller | Voltage | Role | Evidence Class |
|---|---|---|:---:|---|---|
| `gdsc` | `qcom,mdss_dsi@0` | MMCC GDSC `0x2304` | 0 V / SoC | MDSS core power domain gate | HARDWARE_PROVEN |
| `vdda` | `qcom,mdss_dsi@0` | PM8994 regulator | 1.25 V (`1250000 uV`) | DSI analog controller supply | SOURCE_PROVEN |
| `vcca` | `qcom,mdss_dsi@0` | PM8994 regulator | 0.925 V (`925000 uV`) | DSI 14nm PHY core supply | SOURCE_PROVEN |
| `vddio` | `somc,sharp_synaptics_cmd_9_panel` | GPIO 51 + PM8994 | 1.8 V | LCD logic & digital I/O rail | SOURCE_PROVEN |
| `lab` (VSP)| `somc,sharp_synaptics_cmd_9_panel` | PMI8994 QPNP LAB | +5.5 V | Positive LCD bias voltage | SOURCE_PROVEN |
| `ibb` (VSN)| `somc,sharp_synaptics_cmd_9_panel` | PMI8994 QPNP IBB | -5.5 V | Negative LCD bias voltage | SOURCE_PROVEN |
| `wled` | `qcom,mdss-dsi-bl-pmic-control-type`| PMI8994 WLED | Variable | Backlight LED string driver | SOURCE_PROVEN |
| `touch-vddio` | Touch controller | GPIO 50 | 1.8 V | Synaptics touch I/O supply | SOURCE_PROVEN |
| `touch-avdd` | Touch controller | PM8994 regulator | 3.0 V | Synaptics touch analog supply | SOURCE_PROVEN |

---

## 2. GPIO Pin Assignments

| Pin | DTB Property | Polarity | Role | Evidence Class |
|---|---|---|---|---|
| **GPIO 8** | `qcom,platform-reset-gpio` | Active Low | Panel Hardware Reset (`disp_reset_n`) | SOURCE_PROVEN |
| **GPIO 10** | `qcom,platform-te-gpio` | Active Low | Tearing Effect Sync (`mdp_vsync` / TE) | SOURCE_PROVEN |
| **GPIO 51** | `qcom,platform-vddio-gpio` | Active High | LCD VDDIO Rail Enable (`lcd_vddio_en`) | SOURCE_PROVEN |
| **GPIO 50** | `qcom,platform-touch-vddio-gpio` | Active High | Touch VDDIO Enable (`panel_tvdd`) | SOURCE_PROVEN |
| **GPIO 89** | `qcom,platform-touch-reset-gpio` | Active Low | Touch Controller Reset | SOURCE_PROVEN |
| **GPIO 125**| `qcom,platform-touch-int-gpio` | Active Low | Touch Controller Interrupt | SOURCE_PROVEN |

---

## 3. Power-Up Sequence Order

```text
1. Assert VDDIO:
   Set GPIO 51 = 1 (Enable LCD 1.8V VDDIO)
   Wait 10 ms (somc,pw-wait-after-on-vddio = 10)

2. Enable Positive Bias:
   Enable PMI8994 QPNP LAB (+5.5V VSP)
   Wait 10 ms (somc,pw-wait-after-on-vsp = 10)

3. Enable Negative Bias:
   Enable PMI8994 QPNP IBB (-5.5V VSN)
   Wait 0 ms (somc,pw-wait-after-on-vsn = 0)

4. DSI Bus State:
   Force DSI Data Lanes (0-3) and Clock Lane to LP-11 state (high)

5. Reset Cycle:
   Assert GPIO 8 = 0 (Reset LOW)
   Wait 10 ms
   Deassert GPIO 8 = 1 (Reset HIGH)
   Wait 10 ms

6. DSI DCS Initialization:
   Send Sleep Out (DCS 0x11), wait 120 ms
   Send Tear ON (DCS 0x35 0x00)
   Send Display ON (DCS 0x29)

7. Backlight:
   Set PMI8994 WLED current / brightness level (1 to 4095)
```

---

## 4. Power-Down Sequence Order

```text
1. Disable Backlight:
   Set PMI8994 WLED brightness = 0

2. DCS Display Shutdown:
   Send Display OFF (DCS 0x28)
   Send Sleep IN (DCS 0x10), wait 120 ms

3. Assert Reset:
   Set GPIO 8 = 0 (Reset LOW)
   Wait 5 ms (somc,pw-off-rst-b-seq = 5)

4. Disable Bias Rails:
   Disable IBB (-5.5V VSN), wait 10 ms
   Disable LAB (+5.5V VSP), wait 10 ms

5. Disable VDDIO:
   Set GPIO 51 = 0 (Disable LCD 1.8V VDDIO)
   Wait 0 ms

6. Stabilization Window:
   Maintain power-off condition for at least 300 ms (somc,pw-down-period = 300)
```
