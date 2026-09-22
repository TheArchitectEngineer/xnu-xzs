# Failed D7-T1 candidate — DEPCMD telemetry correction

Preserved source: `28e41947880e0a4d49053242cd4168c673b1cc61` (boot SHA-256 `b76e012f5367`).
Preserved consoles: `artifacts/logs/console-ramoops-run2.bin` and `console-ramoops-run3.bin` on the desktop tree.

## What the console actually printed

```text
DEPSTARTCFG rc=0x9
SETEPCONFIG rc=0x1
SETTRANSXFR rc=0x2
```

`dwc3_ep_cmd()` returned `DEPCMD & 0x0F` after `CMDACT` cleared. Those values are the command opcodes:

```text
DEPSTARTCFG / STARTNEWCFG = 0x9
SETEPCONFIG               = 0x1
SETTRANSFRESOURCE         = 0x2
```

They are not `DEPCMD.STATUS`. Status lives in bits [15:12]. The preserved consoles do not contain `DEPCMD_RAW_BEFORE`, `DEPCMD_RAW_AFTER`, `DEPCMD_CMDACT_AFTER`, or `DEPCMD_STATUS`.

`dwc3_start_transfer()` treated `DEPCMD & 0x0F` as an error status and returned bits [22:16] as a resource index. `STARTTRANSFER` opcode `0x6` occupies that low nibble, so the check cannot mean completion success. The preserved consoles do not print a `STARTTRANSFER` completion line. `EP0_STARTTRANSFER_ACCEPTED` is withdrawn.

The same boot did show the EP0 command path was reached, no exception was printed, and the log then continued into recovery without a host bus-reset/connect event or enumeration. That is the whole claim those logs support.

## Later images

Candidate-1 and Candidate-2A did not issue endpoint commands. The live command helpers now log raw before/after, opcode, `CMDACT` after completion, status bits [15:12], and, for `STARTTRANSFER`, the transfer-resource index. A command is accepted only when `DEPCMD_CMDACT_AFTER=0` and `DEPCMD_STATUS=0`. That logging is source-only until a later candidate is authorized to issue a command.
