# Contributing & Branch Strategy

This document outlines branch management, hardware verification principles, and development workflows for the XNU on Sony Xperia XZs project.

---

## 1. Branch Strategy

The repository maintains three designated branches with distinct purposes:

```text
main
  Stable public milestones.
  Contains certified, public-ready releases with complete documentation and proven hardware verification.

xzs-bringup
  Active hardware bring-up laboratory.
  Contains diagnostic instrumentation, checkpoint logging, selftests, and temporary bring-up workarounds.
  All experimental code must prove hardware feasibility here before being cleaned up.

xzs-port
  Clean hardware-verified Xperia XZs port changes.
  Contains architecturally clean adaptations intended to remain as close to canonical upstream Apple XNU
  as possible, with verbose telemetry stripped and temporary workarounds canonicalized.
```

> [!NOTE]
> During bring-up phases, daily engineering occurs on `xzs-bringup`. Completed milestones are selectively canonicalized into `xzs-port` and published to `main`.

---

## 2. Engineering Discipline & Verification Rules

Every code change submitted to this repository must respect the following core principles:

1. **Hardware Truth Over Speculation**:
   - Physical hardware behavior on the Sony Xperia XZs is the single source of truth.
   - Code changes that compile or pass host unit tests are not considered functional until proven on physical silicon.
2. **Workaround Taxonomy**:
   - Never introduce an undocumented workaround.
   - Every modification outside canonical XNU behavior must be documented in [`docs/XZS_WORKAROUNDS.md`](XZS_WORKAROUNDS.md) and classified as `XZS-PORT`, `XZS-COMPAT`, `XZS-WORKAROUND`, or `XZS-SELFTEST`.
3. **Hardware Watchdog Discipline**:
   - The Qualcomm APCS hardware watchdog (`0x09830000`) enforces execution deadlines.
   - Do **not** sprinkle unconditional watchdog pets inside polling loops to mask deadlocks.
   - Watchdogs may only be reset on genuine forward milestone progress.
4. **Git Hygiene & Secret Prevention**:
   - Never commit private credentials, local absolute paths, device serial numbers, or binary compiler object dumps (`.o`, `.pyc`).
   - Maintain the Pointer Authentication (`scripts/check-no-pac.sh`) guardrail on all builds.

---

## 3. Commit Message Style

Commit messages should be concise, structured, and clearly indicate the subsystem affected:

```text
xzs: <concise summary of change>

[Optional detailed context describing:]
- Hardware evidence or observed anomaly
- Subsystem / file affected
- Workaround classification (if applicable)
```

Example:
```text
xzs: defer IOFindBSDRoot matching debug serialization to achieve reproducible D1 baseline

Defers OSSerialize::ensureCapacity in IOFindBSDRoot under CONFIG_XZS_BRINGUP.
This eliminates early kmem_realloc_guard page allocation stalls without altering
service matching semantics, achieving reproducible D1 terminal execution.
```
