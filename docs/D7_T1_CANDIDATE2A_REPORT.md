# D7-T1 Candidate-2A — Extended Read-Only USB Physical-Layer Snapshot

## Scope and immutable identity

| Field | Value |
|---|---|
| Target | Sony Xperia XZs G8231 / MSM8996 / `BH905SX976` |
| Source commit | `981bd0b5cd16dca5b8bf5c0e5f331e2f798bf7a7` |
| Parent candidate | `67338385144e6b4bbb2483814834c167dee32859` (Candidate-1) |
| Kernel SHA-256 | `c902fadb254a02396b98866b3473f2b18e445807ad5085dd94e0897e0ec03657` |
| Boot image SHA-256 | `6b8df9d45116b7e352007ac61a033ff84d0825766af6041242575bfa6cb92c91` |
| Recovery image SHA-256 | `575f50cf296685e302b7171b3e8a63a08c68bd1e9171c7d905772e0811d8da92` |
| Raw console SHA-256 | `9f4ee83975b5e7655c6ce8a2fc1f2ae49bfc6a0d9fb454adb3a0dc2d5df51153` |
| Evidence directory | `artifacts/logs/d7t1-candidate2a-981bd0b/pstore/` |

This candidate was booted only with `fastboot boot`. Neither kernel, system nor recovery partitions were flashed. The recovery image was not modified.

## Result

**HARDWARE VERIFIED — PASS.** The read-only code path completed all six Candidate-2A checkpoints, returned through the normal boot path, and preserved the D6, D7-M2, D7-M3 and D7-M4 regression prefix.

```text
D740/2A00 → /2A01 → /2A02 → /2A03 → /2A04 → /2A09
CANDIDATE2A_READ_ONLY=yes
DWC3_REGISTER_WRITES=0
QSCRATCH_REGISTER_WRITES=0
QUSB2_REGISTER_WRITES=0
GCC_REGISTER_WRITES=0
USB_STATE_MUTATED=no
```

The independent evidence gate passed:

```text
D7T1_CANDIDATE2A_CHECKPOINTS=PASS
D7T1_CANDIDATE2A_READ_ONLY_CONTRACT=PASS
D7T1_CANDIDATE2A_REGRESSION_PREFIX=PASS
D7T1_CANDIDATE2A_EVIDENCE_VERIFIER=PASS
```

## Hardware snapshot

| Block | Hardware-observed value | Decode / classification |
|---|---:|---|
| DWC3 `GSNPSID` | `0x5533270a` | **HARDWARE VERIFIED** Synopsys DWC3 revision 2.70a. |
| DWC3 `GCTL` | `0x00112000` | **HARDWARE VERIFIED** `PRTCAPDIR=2`, peripheral/device mode. |
| DWC3 `GSTS` | `0x3e800000` | **HARDWARE VERIFIED** raw global status captured. |
| DWC3 `GUSB2PHYCFG0` | `0x00002500` | **HARDWARE VERIFIED** raw USB2 PHY configuration captured. |
| DWC3 `GUSB3PIPECTL0` | `0x030c0002` | **HARDWARE VERIFIED** raw USB3 PIPE control captured; USB3 remains out of D7-T1 scope. |
| DWC3 `DCFG` | `0x0008080c` | **HARDWARE VERIFIED** `DEVSPD=4` (SuperSpeed encoding), address `1`, `NUMP=4`. |
| DWC3 `DCTL` | `0x00f00000` | **HARDWARE VERIFIED** `RUN_STOP=0`; Candidate-2A did not alter it. |
| DWC3 `DSTS` | `0x00d38f74` | **HARDWARE VERIFIED** `CONNECTSPD=4`, `USBLNKST=4` (SS.Disabled), `DEVCTRLHLT=1`, `COREIDLE=1`. |
| DWC3 `DEVTEN` / `OSTS` | `0x47` / `0x0` | **HARDWARE VERIFIED** raw event/OTG status captured. |
| QSCRATCH `GENERAL_CFG` | `0x0000000d` | **HARDWARE VERIFIED** raw wrapper state captured. |
| QSCRATCH `HS_PHY_CTRL` | `0x10100000` | **HARDWARE VERIFIED** `UTMI_OTG_VBUS_VALID=1`; `SW_SESSVLD_SEL=1`. |
| QSCRATCH `SS_PHY_CTRL` | `0x01000000` | **HARDWARE VERIFIED** raw SS wrapper state captured. |
| QSCRATCH `PWR_EVENT_IRQ_STAT` | `0x00040000` | **HARDWARE VERIFIED** raw power-event state captured. |
| QUSB2 `PLL_TEST` / `PLL_STATUS` | `0x80808080` / `0x30203020` | **HARDWARE VERIFIED** low-byte `CLK_REF_SEL=1` (single-ended); `PLL_LOCKED=1`. Repeated bytes are the observed 32-bit read representation of byte-addressed PHY state. |
| QUSB2 `PORT_POWERDOWN` | `0x22222222` | **HARDWARE VERIFIED** low-byte `POWER_DOWN=0`. |
| QUSB2 `UTMI_STATUS` | `0x10101010` | **HARDWARE VERIFIED** raw UTMI line state captured. |
| GCC `QUSB2PHY_PRIM_BCR` | `0x00000000` | **HARDWARE VERIFIED** raw primary-PHY reset BCR captured. |

## Source audit

**SOURCE-AUDITED FACT.** The local MSM8996 DT establishes the wrapper at `0x06af8800`, DWC3 at `0x06a00000`, QUSB2 at `0x07411000`, and GCC at `0x00300000`. The corresponding Linux/CAF sources establish:

- QSCRATCH: `GENERAL_CFG=0x08`, `HS_PHY_CTRL=0x10`, `SS_PHY_CTRL=0x30`, and primary `PWR_EVENT_IRQ_STAT=0x58`; HS bits 20 and 28 are `UTMI_OTG_VBUS_VALID` and `SW_SESSVLD_SEL`.
- QUSB2/MSM8996: `PLL_TEST=0x04`, `PLL_STATUS=0x38`, `PORT_POWERDOWN=0xb4`, `UTMI_STATUS=0xf4`; the lock bit is 5 and the power-down bit is 0.
- GCC/MSM8996: `GCC_QUSB2PHY_PRIM_BCR=0x12038` from GCC base.
- DWC3: global/device offsets including `GCTL`, `GSTS`, USB2/USB3 PHY controls, `DCFG`, `DCTL`, `DSTS`, `DEVTEN`, and `OSTS` are the standard DWC3 layout.

Candidate-2A only invokes volatile read helpers after mapping those audited apertures. It does not call the existing DWC3 write, endpoint, event-drain, reset, DMA, or run/stop paths. `scripts/verify_d7t1_candidate2a.py` statically checks that property.

## Regression and recovery evidence

**HARDWARE VERIFIED.** Existing independent verifiers passed against the fresh pstore console:

```text
D6_COMPLETE=yes / D6_SEALED=yes
D7_M2_REGRESSION_VERIFIER=PASS
D7_M3_ACCEPTANCE_VERIFIER=PASS
D7M4_INTERNAL_PIPELINE_VERIFIER=PASS
D7_M4_FINAL_ACCEPTANCE=PASS
```

The shell again reached `D720/91`; the sealed UARTDM native stdin pipeline reached `D730/70`. Candidate-2A therefore did not regress sealed EL0, syscall, stdout, or kernel-stdin behavior.

**SOURCE-AUDITED LIMITATION.** TWRP was booted only as a transient recovery path. Its `devmem` binary is present, but `/dev/mem` is absent, so a recovery-side MMIO snapshot was not available. No recovery MMIO read was performed and recovery was not patched.

## Interpretation and next candidate

**INFERENCE/HYPOTHESIS.** The measured QUSB2 session-valid bits, PLL lock and non-powered-down state make a dead or unpowered QUSB2 PHY less likely than before; they do not prove cable ownership, host enumeration, endpoint correctness, or a root cause. `DCTL.RUN_STOP=0` and `DSTS.DEVCTRLHLT=1` show that the inherited DWC3 device controller is halted at the time of the snapshot.

**Recommended Candidate-2B — do not execute automatically:** source-audit a DWC3-only, minimal device-takeover sequence starting from this frozen snapshot. It must retain the exact QSCRATCH, QUSB2 and GCC values as a before/after oracle; avoid PHY clocks, resets and wrapper writes; and separately prove each controlled DWC3 transition before endpoint or DMA work. Candidate-2A is complete evidence collection only, not enumeration.
