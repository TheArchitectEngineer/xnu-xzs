# D8-M10 Panel 9 source-regression audit

## Finding

`somc,sharp_synaptics_cmd_9_panel` in
`artifacts/display-audit/keyaki.dts` is the selected Sony Panel 9 node.  Its
command properties are:

1. `qcom,mdss-dsi-on-command`: `TEON (0x35 0x00)`, then `DISPON (0x29)`.
2. `qcom,mdss-dsi-post-panel-on-command`: `SLPOUT (0x11)` with a 120 ms wait.

The former D8-M10 code labelled a 14-command sequence as Panel 9 vendor
initialisation.  Those commands (`0x51`, `0x53`, `0x55`, `0xb0`, `0xd6`,
`0xc9`, `0xd5`, `0xd0`, `0xd3`, `0xd7`, `0xc1`, `0xc7`, `0xc8`) do not occur
in that selected node's on-command property.  It also inserted `SLPOUT`,
`COLMOD`, `CASET`, `PASET`, and `MADCTL` before the documented on-command.

This was a source-fidelity regression introduced by the D8-M10 console work;
it was not evidence of an additional Panel 9 requirement.

## Correction

`xzs_d8m8_panel_prepare()` now sends only the source-selected `TEON` and
`DISPON` commands, checks both the DMA result and DSI ACK/timeout registers,
and leaves `SLPOUT` to the existing post-on path in kickoff.  The unsourced
payload table was removed from `xzs_d8m6.h` so a later caller cannot silently
restore it.

## What this does and does not prove

This correction makes the current D8-M10 code match the Sony Panel 9 DTB.
It does **not** establish physical scanout: the earlier F15 RAM-boot evidence
already exercised the same `TEON -> DISPON -> post-on SLPOUT` order and showed
zero PP line count.  The correction therefore removes a regression and makes
future physical tests interpretable; it is not claimed as a display fix.

The source-audit phase itself made no device, flash, or MMIO change.

## Follow-up RAM-boot validation

The correction was built, PAC-audited (zero executable PAC instructions),
packaged, and RAM-booted on `BH905SX976`; no partition was flashed.

The first runner invocation stopped before panel work because its obsolete M4
guard required a disabled host although bootshim had already enabled
`DSI_CTRL=0x000001f5`.  The runner now accepts only that explicit active-host
admission and rejects other M4 failures.

The retry reached Panel 9 preparation and recorded:

```text
DCS_SEQUENCE=SONY_PANEL9_ON_TEON_DISPON_POSTON_SLPOUT
TEON_ACK=PASS       (DMA completion, ACK_ERR=0, TIMEOUT=0)
DISPON_ACK=PASS     (DMA completion, ACK_ERR=0, TIMEOUT=0)
PREPARE_GPIO10_TRANSITIONS=0
PREPARE_GPIO10_HIGH_SAMPLES=0
PANEL_READY=yes
```

`display m8-fb-init` then did not return a shell prompt within the runner's
60-second bound, so the runner stopped before CTL/PP kickoff and returned the
device to fastboot.  This run therefore supplies no pixel or scanout claim.
The artifacts are under
`artifacts/hw/d8m10/panel9-source-regression-rerun/`; the target was confirmed
back in fastboot after the run.
