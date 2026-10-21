# XZS display: DDIC lifecycle handoff

## Mission

Make the Xperia XZs Panel 9 display real pixels from XNU.  Completion means
the physical LCD renders the XNU test pattern or framebuffer console, not
merely that WLED/backlight is on or that a source-address register latched.

Work on branch:

```text
codex/fix-display-ddic-lifecycle
```

Base commit:

```text
5e45b63 fix(display): correct panel9 one-shot scanout setup
```

## Current, hardware-proven state

The target is `BH905SX976` (MSM8996, Panel 9).  Persistent partitions must
not be flashed; `fastboot boot` is the only permitted boot operation.

| Area | Result | Evidence |
|---|---|---|
| Framebuffer mapping and fill | PASS | XNU M8-1 maps PA `0x98000000`, reads/writes it, and fills the test pattern. |
| RGB source handoff | PASS | `CTL_START` is consumed and `RGB0_CURRENT_SRC0_ADDR` latches `0x98000000`. |
| Static command-mode route | PASS | `CTL_TOP=0x00020020`, selecting command mode and `MDSS_MDP_INTF1`. |
| Panel 9 autorefresh | Corrected | Sony LK uses one-shot `PP0_AUTOREFRESH=0`; XNU now matches. |
| DSI/clock/power baseline | PASS | PLL/lane/MDSS clocks are live; no DSI ACK or generic timeout during normal setup. |
| Backlight | PASS | WLED is enabled. |
| Physical scanout | FAIL | `PP_LINE=0`, `PP_OUT=0`, no PP done, no DSI MDP busy/done. |
| Physical TE in XNU | FAIL | GPIO10 stays low; XNU sees no physical TE transition. |
| DDIC readback in XNU | FAIL | BTA tests time out (`DSI_TIMEOUT_STATUS=0x00000100`). |

The latest XNU run is:

```text
artifacts/hw/d8m10/ctl-top-intf1-routing/transcript.txt
```

Its decisive lines are:

```text
CTL_TOP=0x00020020
RGB0_CURRENT_SRC0_ADDR=0x98000000
PP_LINE_NONZERO=NO
PP_OUT_NONZERO=NO
DSI_MDP_BUSY_SEEN=NO
CMD_MDP_DONE_SEEN=NO
SCANOUT_BOUNDARY=RGB0_OR_CTL_ONLY_NO_PINGPONG_SCANLINES
```

## What was fixed, and why not to undo it

1. `xzs_d8m8_fb_init()` no longer uses the persistent breadcrumb transport.
   Those IMEM/fixed-DRAM writes made framebuffer initialization appear hung.
   M8-1 now reaches a real read/write/fill PASS.
2. `PP0_AUTOREFRESH_CONFIG` is zero before and through `CTL_START`.  The prior
   `0x80000001` was a mistaken attribution to Panel 9.  Do not restore it
   without new primary-source evidence.
3. `CTL_TOP` is `0x00020020`; `INTF1` is enum value 2 shifted by four.  Do not
   revert to `0x00020010`.

Relevant implementation:

```text
src/xnu/pexpert/arm/xzs_d8m8.h
```

## Golden reference and the real mismatch

RAM-booted TWRP on the same handset proves that the panel and physical display
path work.  Its panel properties are:

```text
panel_name=9
type=mipi dsi cmd panel
RGB0 active
hw_vsync_mode=1
te_tear_check_en=1
wr_mem_start=0x2c
wr_mem_continue=0x3c
insert_dcs_cmd=1
DSI_CMD_MDP_CTRL=0x06100006
DSI_CMD_DCS_CTRL readback=0x00003c2c
```

Capture/reference files:

```text
artifacts/display-audit/m8/golden_live_hw_dump.json
artifacts/hw/d8m8/f17-twrp-ddic/twrp-live-mdss-dump.txt
scripts/display/capture_twrp_golden.py
scripts/display/f17_twrp_readback.py
```

`0x00013c2c` is the correct XNU write to `DSI_CMD_DCS_CTRL`; bit 16 is the
insert-DCS control and it reads back clear as `0x00003c2c`.  This is not a
remaining defect.

The remaining hypothesis is not an MDP register typo: XNU does not yet own the
full Panel 9 / in-cell touch / DDIC lifecycle that causes physical TE and DSI
bus-turnaround acknowledgement.  The direct DCS commands `TEON`, `DISPON`, and
`SLPOUT` return cleanly but do not establish those two physical conditions.

## Work plan

### Phase 1 — capture a frame-synchronous TWRP baseline

The existing TWRP dump can be idle/gated.  Improve or add a capture tool that
records a short time series while TWRP is actively committing a frame:

- MDP: PP0 `INT_COUNT`, `LINE`, `OUT_LINE`, `INTR_STATUS`, `INTR_EN`, CTL0,
  RGB0 current source, and the relevant clock state.
- DSI: status, interrupt, FIFO, command MDP controls and lane state.
- Panel: standard DCS status registers (`0x0A` through `0x0F`, ID registers),
  only when TWRP debugfs exposes safe reads.
- GPIO10 / TE state where the runtime permits it.
- Driver trace or counters that correlate a redraw with the samples.

Do not mistake an idle snapshot with zeroed runtime registers for the active
frame configuration.  Save raw samples and metadata under a new uniquely named
directory in `artifacts/hw/d8m8/`.

### Phase 2 — reconstruct and port the lifecycle

Audit only the pinned Sony downstream source and the Panel 9 DTS:

```text
scratch/panel9.dtsi
scratch/cmd_9_panel.dts
scratch/mdss_dsi.c
scratch/mdss_dsi_host.c
scratch/mdss_dsi_panel.c
scratch/mdss_mdp_intf_cmd.c
```

Trace the exact sequence from power/reset through first valid command frame:

1. LAB/IBB and panel/touch supply ownership.
2. GPIO8 panel reset plus GPIO50/GPIO89/GPIO125 in-cell-touch sequencing.
3. DSI host/PHY clock and lane state.
4. Panel on/post-on commands, timings and any required deferred work.
5. TE routing (`hw_vsync_mode`) and command-mode first-frame admission.
6. Any real driver operation required before DCS read/BTA works.

Port the smallest complete lifecycle to XNU.  Preserve ordering and delay
requirements.  Document every added action with the source file/function/line
that justifies it.

### Phase 3 — validate physical admission, then scanout

For each RAM boot, alter one source-backed lifecycle variable or one coherent
source-backed sequence.  Before `CTL_START`, record:

```text
panel rails/reset/touch GPIO state
GPIO10 TE transitions
DSI BTA/read result and timeout state
PP sync configuration and interrupt state
```

After one `CTL_START`, require all of:

```text
PP_LINE_NONZERO=YES
PP_OUT_NONZERO=YES
DSI_MDP_BUSY_SEEN=YES
PP0_DONE_SEEN=YES
CMD_MDP_DONE_SEEN=YES
visible physical pattern confirmed
```

Only once those are true should the work move to continuous framebuffer console
updates, brightness behavior, and recovery.

## Explicit non-goals / guardrails

- Never use `fastboot flash`, erase, unlock, or modify a persistent partition.
- Never add a raw write to `0x00994094` (DSI software trigger): the Sony path
  does not use it for MDP scanout.
- Do not claim success from WLED, DCS transmission, a latched RGB address,
  `CTL_START`, or a PP timing counter alone.
- Do not make a random MDP/DSI register sweep.  Every hardware-changing test
  needs source evidence, a hypothesis, and a single-variable comparison.
- Return the device to fastboot in `finally`/cleanup after each XNU experiment.
- Keep existing user changes intact; inspect `git status` before editing.

## Required final report

Create `docs/DISPLAY_DDIC_LIFECYCLE_RESULT.md` containing:

1. Branch, base commit, final commit(s), device serial, and exact boot image
   SHA-256.
2. TWRP frame-synchronous baseline with raw artifact paths.
3. A source-to-XNU lifecycle mapping table with citations.
4. One row per XNU RAM-boot: changed variable/sequence, preconditions,
   readback, `PP_LINE`, `PP_OUT`, DSI busy/done, BTA/TE results, and visible
   outcome.
5. The final verdict.  “Display fixed” is allowed only with the Phase 3
   success criteria plus a visible test pattern.
6. Tests run, known limitations, and the exact next blocker if unsuccessful.

## Completion contract

The task is complete only when a RAM-booted XNU image visibly renders the
color-bar/console pattern and the hardware trace proves PP0-to-DSI transfer.
If the evidence instead proves a remaining board-level dependency outside the
available XNU code, stop after documenting that dependency precisely; do not
invent a register fix.
