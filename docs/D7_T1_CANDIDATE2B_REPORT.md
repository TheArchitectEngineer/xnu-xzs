# D7-T1 Candidate-2B — Halted DWC3 `DCFG` Normalization

## Immutable test identity

| Field | Value |
|---|---|
| Target | Sony Xperia XZs G8231 / MSM8996 / `BH905SX976` |
| Source commit | `da548da5f22e0c1a8c45156aedab2a2d26072963` |
| Kernel SHA-256 | `7e29f7527d51d7bbb55afdc56907179f9c276b652e5f49ee333f1cec9be5a40d` |
| Boot image SHA-256 | `e5fd82e9a4ffec69cde8cbc8520aef85ce107b26edd73e538a7e1ba0db52709f` |
| Recovery image SHA-256 | `575f50cf296685e302b7171b3e8a63a08c68bd1e9171c7d905772e0811d8da92` |
| Raw pstore SHA-256 | `4aa1cf14177b2aad000e9db05663b61062f07e47986338e5d9c0f75cf988f742` |
| Evidence | `artifacts/logs/d7t1-candidate2b-da548da/console-ramoops` |

The physical run used `fastboot boot` only. No persistent partition was flashed.

## Result

**HARDWARE VERIFIED — PASS.** Candidate-2B completed the ordered `2B00` through `2B91` path, then continued normal XNU boot. The sole logical register mutation was a masked runtime read-modify-write of DWC3 `DCFG`; it was performed only while `RUN_STOP=0` and `DEVCTRLHLT=1`.

| Oracle | Before | Written | Readback / after |
|---|---:|---:|---:|
| DWC3 `DCFG` raw | `0x0008080c` | `0x00080800` | `0x00080800` |
| `DEVSPD` | SuperSpeed encoding | — | High Speed |
| `DEVADDR` | `1` | — | `0` |
| `NUMP` | `4` | — | `4` |
| `DCTL.RUN_STOP` | `0` | no write | `0` |
| `DSTS.DEVCTRLHLT` | `1` | no write | `1` |

```text
CANDIDATE2B_DWC3_WRITE_COUNT=1
CANDIDATE2B_DWC3_WRITE_TARGET=DCFG
DCFG_WRITE_MATCH=yes
GCTL_UNCHANGED=yes
GUSB2PHYCFG0_UNCHANGED=yes
QSCRATCH_UNCHANGED=yes
QUSB2_UNCHANGED=yes
GCC_UNCHANGED=yes
EVENT_BUFFER_UNCHANGED=yes
EP0_UNTOUCHED=yes
CANDIDATE2B_COMPLETE=yes
D7_T1_COMPLETE=no
D7_T1_SEALED=no
```

The independent evidence gate passed:

```text
D7T1_CANDIDATE2B_CHECKPOINTS=PASS
D7T1_CANDIDATE2B_DCFG_ORACLE=PASS
D7T1_CANDIDATE2B_HALTED_SAFETY=PASS
D7T1_CANDIDATE2B_IMMUTABLE_DOMAINS=PASS
D7T1_CANDIDATE2B_EVIDENCE_VERIFIER=PASS
```

## Scope and source audit

**SOURCE-AUDITED FACT.** DWC3 `DCFG` uses bits 2:0 for device speed and bits 9:3 for the device address. Candidate-2B computes the value from the hardware read, clearing exactly those masks and preserving the rest, including `NUMP`. It does not write a magic full-register value.

**SOURCE-AUDITED FACT.** The MSM8996 QUSB2 driver identifies `PLL_STATUS` at byte offset `0x38`, tests `PLL_LOCKED` at bit 5, and reads that status as a byte. Candidate-2B therefore captures the raw byte before/after and defines its QUSB2 state oracle as `PLL_LOCKED` plus full `PORT_POWERDOWN`; it makes no QUSB2 write.

The static gate proves the source contains exactly one `dwc3_write32(DWC3_DCFG, ...)` call in the Candidate-2B path and excludes GCTL, DCTL, DEVTEN, PHY configuration, event buffer, endpoint, transfer, QSCRATCH, QUSB2, and GCC mutation routes.

## Raw QUSB2 observation

| Field | Before | After | Classification |
|---|---:|---:|---|
| `PLL_STATUS` byte | `0x30` | `0x20` | **HARDWARE VERIFIED** raw sample |
| `PLL_LOCKED` (bit 5) | yes | yes | **HARDWARE VERIFIED**, source-audited state bit |
| `PORT_POWERDOWN` | `0x22222222` | `0x22222222` | **HARDWARE VERIFIED** unchanged |

**INFERENCE, not a write claim.** Bit 4 changed in the raw status sample, but it has no source-audited semantic in this work. With zero QUSB2 register writes and the audited lock bit/powerdown oracle preserved, that variation is treated as volatile telemetry rather than evidence of a PHY mutation.

The first Candidate-2B run (`a9caf8707b87ea13ca686136e68ef8b3dc2d021f`) and its byte-width correction (`9525f099ed4378acfb6a352ae683dde774fc1e33`) both established the same DCFG behavior; they did not satisfy the earlier overly broad raw-QUSB2 equality contract. Commit `da548da` narrows only that oracle to source-audited stable state and is the hardware-tested acceptance source above.

## Regression evidence

**HARDWARE VERIFIED.** The same pstore console passed all independent pre-existing gates:

```text
D6_COMPLETE=yes / D6_SEALED=yes
D7_M2_REGRESSION_VERIFIER=PASS
D7_M3_ACCEPTANCE_VERIFIER=PASS
D7M4_INTERNAL_PIPELINE_VERIFIER=PASS
D7_M4_FINAL_ACCEPTANCE=PASS
```

The `/bin/sh` EL0 stdout path reached `D720/01`, so this Candidate-2B change did not regress sealed EL0, syscall, console output, or internal stdin behavior.

## Next action

`RECOMMENDED_NEXT=CANDIDATE-2C_XNU_OWNED_EVENT_BUFFER`

`NEXT_ACTION=STOP_FOR_REVIEW`

Candidate-2C is planning only: audit an XNU-owned DWC3 event buffer and its ownership/initialization boundary. It has not been implemented or hardware-executed. Candidate-2B does not enumerate USB, touch EP0, activate `RUN_STOP`, or make D7-T1 complete/sealed.
