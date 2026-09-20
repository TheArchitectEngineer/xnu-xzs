# Diagnostic & Bring-Up Branch Index

This index documents the historical diagnostic, forensic, and milestone branches created during the Sony Xperia XZs (MSM8996) native XNU bring-up.

In accordance with project policy, **none of these branches are rebased, squashed, or deleted**. They remain permanently frozen in the Git history as verifiable empirical records.

---

## 1. Branch Registry

| Branch Name | Purpose | Key Commit | Silicon Outcome | Final Interpretation / Superseded By |
| :--- | :--- | :--- | :--- | :--- |
| **`xzs-d44-autoconf-diag`** | Diagnostic instrumentation of `bsd_autoconf` / IOKit matching boundary | `58d7964` | Identified stall at `cfil_init` or during config thread matching | Isolated stall window to post-autoconf startup sequence |
| **`xzs-d44-m4-linked-diag`** | Static binary diff and Mach-O link-layout audit of M4-linked binary | `7ca806b` | Proved +2644-byte text increase was genuine XZSFS driver code | Ruled out compiler toolchain anomalies or corrupt linking |
| **`xzs-d44-layout-pad-diag`** | Exact 2644-byte early text-layout pad control without M4 code | `24ea29c` | Failed at `D510/53` despite zero M4 driver logic linked | Showed binary size/displacement can trigger existing runtime fragility |
| **`xzs-d44-late-pad-diag`** | Exact 2644-byte late text-layout pad preserving critical symbol addresses | `3dc4dd7` | Failed at `D510/53` despite critical symbols having delta=0 | Proved critical symbol displacement was NOT the causal factor |
| **`xzs-d44-abba-repro`** | Bounded ABBA reproducibility matrix (`A-B-B-A` runs on silicon) | `3fd8f8a` | Both Control (A) and Late-Pad (B) failed non-deterministically | Proved **runtime nondeterminism** dominates; binary layout non-causal |
| **`xzs-runtime-fix-memory-contract`** | Memory contract audit and 88 MiB `boot_args.memSize` bound | `d269a3d` | Immediate 100% reproducible PASS through D5-M4 (`D530/91`) | **Root cause identified & fixed**: resolved early physical memory exhaustion |
| **`xzs-d5m5-namespace`** | Implementation of namespace traversal and devfs `/dev/console` mount | `ac74409` | 100% in-order PASS (`D540/00` through `D540/91`) | Sealed D5-M5 namespace and devfs; handed off to D5-M6 |
| **`xzs-d5m6-seal`** | Final D5 integration, `/bin/sh` namei probe, and complete D5 seal | `789c68c` | 100% PASS (`D550/00` through `D550/91`); terminal halt at `D550/01` | **Authoritative D5 hardware seal** (tagged `xzs-d5-rootfs-complete`, merged into `main`) |

---

## 2. Invariants & Preservation Rules

1. **Immutable History**: All commits on these branches are permanent.
2. **Reproducible Proof**: Any archived boot image associated with these branches can be verified against its respective SHA-256 hash in `artifacts/reports/`.
3. **Traceability**: Future work on Phase D6 and subsequent milestones must branch exclusively from verified `main` releases.
