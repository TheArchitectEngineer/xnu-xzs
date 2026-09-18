# Porting Status: XNU on Sony Xperia XZs (MSM8996)

## Device Specification
- **Device**: Sony Xperia XZs (`G8231` / `tone` / `keyaki`)
- **Serial**: `BH905SX976`
- **SoC**: Qualcomm MSM8996 Snapdragon 820 (Kryo quad-core, ARMv8.0-A)
- **Constraint**: Sealed Device (no hardware disassembly, no soldering, telemetry via USB / fastboot / persistent DRAM only)

---

## Telemetry & Checkpoint Status

| Checkpoint ID | Description | Code Status | Observed on Device | Notes |
|:---|:---|:---|:---|:---|
| **0x100 (Shim A)** | Bootshim entry from LK (`0x80080000`) | IMPLEMENTED | PENDING_EMPIRICAL_RUN | LK jumped to bootshim |
| **0x101 (Shim B)** | DRAM scratchpad verified (`0x80060000`) | IMPLEMENTED | PENDING_EMPIRICAL_RUN | Log buffer initialized |
| **0x102 (Shim C)** | UART MMIO mapped / accessible | IMPLEMENTED | PENDING_EMPIRICAL_RUN | BLSP2 UART2 (`0x075b0000`) |
| **0x103 (Shim D)** | DTB parsed & tags verified | IMPLEMENTED | PENDING_EMPIRICAL_RUN | DTB tag check |
| **0x104 (Shim E)** | Boot arguments prepared | IMPLEMENTED | PENDING_EMPIRICAL_RUN | `boot_args` assembled |
| **0x105 (Shim F)** | Memory map verified & caches cleaned | IMPLEMENTED | PENDING_EMPIRICAL_RUN | D-cache clean to PoC |
| **0x106 (Shim G)** | Jump to XNU kernel entry (`0x82000000`) | IMPLEMENTED | PENDING_EMPIRICAL_RUN | Direct branch to `start.s` |
| **0x200 (XNU K0)** | XNU early entry in `start.s` | IMPLEMENTED | PENDING_EMPIRICAL_RUN | Physical entry `start_first_cpu` |
| **0x201 (XNU K1)** | Exception vectors installed (`VBAR_EL1`) | IMPLEMENTED | PENDING_EMPIRICAL_RUN | Early vector installed |
| **0x202 (XNU K2)** | Page tables constructed in RAM | IMPLEMENTED | PENDING_EMPIRICAL_RUN | Identity + KVA mappings |
| **0x203 (XNU K3)** | TTBR0 / TTBR1 configured | IMPLEMENTED | PENDING_EMPIRICAL_RUN | TCR, MAIR, TTBR0/1 set |
| **0x204 (XNU K4)** | MMU enabled (`SCTLR_EL1.M = 1`) | IMPLEMENTED | PENDING_EMPIRICAL_RUN | `isb` after MMU enable |
| **0x205 (XNU K5)** | High-virtual jump to KVA | IMPLEMENTED | PENDING_EMPIRICAL_RUN | Branch to `arm_init` in KVA |
| **0x206 (XNU K6)** | Entry into `arm_init()` | IMPLEMENTED | PENDING_EMPIRICAL_RUN | C runtime environment active |
| **0x207 (XNU K7)** | `pe_arm_init()` / early console | IMPLEMENTED | PENDING_EMPIRICAL_RUN | Serial / pexpert init |
| **Visual Markers** | Framebuffer color screen markers | BLOCKED | BLOCKED (UNVERIFIED_FRAMEBUFFER) | Framebuffer address unverified; speculative writes prohibited per Rule 5 |

---

## Infrastructure & Tooling Status

| Tool / Component | Path | Status | Description |
|:---|:---|:---|:---|
| **RAM Persistence Tester** | `artifacts/builds/xzs-ram-test-writer.img` | READY | Writes magic pattern `0x585a535f52414d54` to `0x80060000` |
| **Debug Dumper Image** | `artifacts/builds/xzs-debug-dumper.img` | READY | High-Speed USB CDC ACM dumper (`0x06a00000`) + UART TX |
| **XNU Boot Image** | `artifacts/builds/xzs-xnu-boot.img` | READY | Complete XNU + Bootshim image with Stage 0-2 exception capture |
| **Host Log Reader** | `scripts/read-debug-log.sh` | READY | Polls `/dev/cu.usbmodem*` & `fastboot oem getlog` |
| **Persistence Verifier** | `scripts/test-ram-persistence.sh` | READY | Automates two-step RAM persistence verification |
| **ELR Symbolicator** | `scripts/symbolicate_elr.py` | READY | Maps `ELR_EL1` to exact symbol + source line |
| **ESR Decoder** | `scripts/decode_esr.py` | READY | Decodes `ESR_EL1` class, syndrome, ISS, and abort details |
| **Feature Decoder** | `scripts/decode_features.py` | READY | Analyzes CPU feature registers |

