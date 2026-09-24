# XNU Display M3 Expected Status Gates & Diagnostics

## 1. Overview
This specification establishes the deterministic checkpoints, polling criteria, timeout policies, and diagnostic commands for the XNU D8-M3 display bring-up milestone.

---

## 2. Checkpoint Status Gates Matrix

| Gate ID | Subsystem | Monitored Address | Mask | Expected Value | Max Timeout | Failure Consequence | Recovery Action |
|---|---|---|---|---|---|---|---|
| `D8M3-10` | MMCC GDSC | `0x008c2304` | `0x80000000` | `0x80000000` | 100 us | Fatal | Abort before touching PLL/PHY |
| `D8M3-20` | DSI PHY | `0x0099444c` | `0x000000ff` | `0x00000001` | Immediate | Fatal | Halt divider setup |
| `D8M3-30` | DSI PLL | `0x00994800` | `0x000000ff` | `0x00000004` | Immediate | Fatal | PLL db commit incomplete |
| `D8M3-40` | DSI PLL | `0x00994448` | `0x000000ff` | `0x00000001` | Immediate | Fatal | Clock kick not asserted |
| `D8M3-50` | DSI PLL | `0x009948cc` | `0x00000020` | `0x00000020` | 1,000 us | Fatal | PLL lock timeout, soft reset |
| `D8M3-60` | DSI PLL | `0x009948cc` | `0x00000001` | `0x00000001` | 1,000 us | Fatal | Reset state machine stall |
| `D8M3-70` | MMCC | `0x008c233c` | `0x80000001` | `0x00000001` | 100 us | Fatal | Byte0 clock stuck halted |
| `D8M3-80` | MMCC | `0x008c2314` | `0x80000001` | `0x00000001` | 100 us | Fatal | Pclk0 clock stuck halted |
| `D8M3-90` | MMCC | `0x008c2344` | `0x80000001` | `0x00000001` | 100 us | Fatal | Esc0 clock stuck halted |
| `D8M3-A0` | DSI PHY | `0x00994564` | `0x000000ff` | `0x0000001d` | Immediate | Fatal | Regulator bias incorrect |
| `D8M3-B0` | DSI PHY | `0x00994440` | `0x0000ffff` | `0x000006ff` | Immediate | Fatal | Drive strength mismatch |
| `D8M3-C0` | Integration | All above | `0xffffffff` | Golden Match | Immediate | Gate Block | Block real M3 start |

---

## 3. Golden-vs-XNU Sequence Verification
Comparing the planned XNU trace-only write sequence against the Linux Golden sequence (`artifacts/display-audit-a1/linux_golden_display_writes.csv`):

| Evaluation Criteria | Linux Golden Count | XNU Planned Count | Alignment Status |
|---|---|---|---|
| **PHY Regulator Writes** | 5 | 5 | **100% IDENTICAL** |
| **PHY Soft Reset** | 2 | 2 | **100% IDENTICAL** |
| **PLL DB Commit Writes** | 46 | 46 | **100% IDENTICAL** |
| **PLL Common Config & Kick**| 2 | 2 | **100% IDENTICAL** |
| **PLL Lock & Ready Poll** | 2 | 2 | **100% IDENTICAL** |
| **MMCC Branch Clock Unhalting**| 9 | 9 | **100% IDENTICAL** |
| **PHY Strength Configuration**| 5 | 5 | **100% IDENTICAL** |
| **Total M3 Scope Writes** | **68** | **68** | **PERFECT MATCH** |

*(Note: Linux writes 69-72 cover DSI controller host timing and lane enables which belong to Milestone D8-M4 and are strictly deferred from M3).*

---

## 4. XNU Diagnostics Shell Command Proposal
For D8-M3 runtime verification within XNU, the following subcommands will be exposed under `xzs_diag`:

### 1. `xzs_diag display-status`
Prints current display state machine enum, GDSC power domain status, PLL lock bit, and MMCC CBCR halt states.
```text
[XZS-DIAG] Display State: M3_PHY_ACCEPTED
[XZS-DIAG] MDSS GDSC: ON (0x008c2304 = 0x80000000)
[XZS-DIAG] DSI0 PLL: LOCKED (0x009948cc = 0x00000021)
[XZS-DIAG] Clocks: BYTE0=ON PCLK0=ON ESC0=ON
```

### 2. `xzs_diag display-dryrun`
Executes full display bring-up state machine purely in trace/dry-run mode without writing any hardware MMIO registers. Emits JSON trace buffer and validates checkpoints `D8M3-10` through `D8M3-C0`.

### 3. `xzs_diag display-regs`
Safely dumps the read-only / config registers across DSI0 CTRL, PHY, PLL, and MMCC into key-value format for direct ingestion by `compare_display_state.py`.
