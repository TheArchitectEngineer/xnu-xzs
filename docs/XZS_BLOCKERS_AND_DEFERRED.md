# Blockers and deferred work

This register is the list of problems that were deliberately left unfixed. Chat history is not the record. When an entry is fixed, set `STATUS=RESOLVED` and add `FIX_COMMIT` and `HARDWARE_PROOF`. Do not delete an entry.

Status values: `ACTIVE_BLOCKER`, `DEFERRED`, `BYPASSED`, `RESOLVED`, `OBSOLETE`.

## DEBT-001

| Field | Value |
|---|---|
| ID | DEBT-001 |
| AREA | userspace exec |
| TITLE | Generic external Mach-O execution loses VM mappings |
| STATUS | DEFERRED |
| FIRST_SEEN | D7-T2 on `xzs-d7t2-shell` |
| LAST_KNOWN_COMMIT | `3f335e65572592d36d76ee718caf7979b3cfad5d` |
| EVIDENCE | Fork child path proven. EL0 child `x0=0` proven. SVC 59 proven. `execve` entered. Mach-O loader reached. Entry point `0x1000002f0` identified. `load_machfile()` reports success. Final task `vm_map` has `min=0x100000000`, `max=0x00007ffffe000000`, `nentries=0`. `__TEXT` is missing. `vm_fault(0x1000002f0)` returns `KERN_INVALID_ADDRESS`. Frozen branch `xzs-d7t2-shell`. Tag `xzs-d7t2-deferred`. Notes: `docs/XZS_D7_T2_DEFERRED.md`. |
| IMPACT | A general external executable cannot be started. |
| CURRENT_BYPASS | Interactive `/bin/sh` stays usable. Shell builtins provide diagnostics. Generic external exec is not required for D8 display bring-up. |
| WHY_DEFERRED | Display bring-up does not need `/bin/hello`. |
| RESUME_CONDITION | Before full Darwin userland, launchd/service spawning, or general external executable support is required. |
| NEXT_INVESTIGATION | Trace where Mach-O segment `vm_map` entries are created and where they disappear before the final task map becomes active. |

## DEBT-002

| Field | Value |
|---|---|
| ID | DEBT-002 |
| AREA | persistent telemetry |
| TITLE | Persistent XNU ramoops telemetry not recoverable through TWRP |
| STATUS | DEFERRED |
| FIRST_SEEN | D7-T3-PERSIST, commits `94c1c84` and `7a57645` |
| LAST_KNOWN_COMMIT | `0c108c1` records the failed write-combine pull. Runtime experiments are not active on the clean D8-M1 tree. |
| EVIDENCE | Live XNU sees a DBGC header (`sig 0x43474244`). TWRP maps the expected physical region (`console 0xa7fbe000`). Pstore files change between runs. `MAGIC=XZSP` is not recovered. Changing cacheability did not make the marker recoverable. Raw DRAM cannot currently be independently inspected in this TWRP image (`/dev/mem` is absent). Notes: `docs/XZS_PERSISTENT_TELEMETRY.md`. |
| IMPACT | A disappearance that happens after the last USB line cannot be reconstructed from pstore. |
| CURRENT_BYPASS | The USB shell is the primary debugger. One PRE/APPLY/POST transaction per hardware step localizes the D8 frontier. |
| WHY_DEFERRED | The shell transcript is sufficient while each display step returns to `xzs#`. |
| RESUME_CONDITION | Non-deterministic crashes, crashes after POST, or failures that cannot be localized from the host log. |
| NEXT_INVESTIGATION | Determine whether the marker never reaches the DRAM payload Linux saves, or whether the ramoops driver replaces it before the pull. Do not add new cache-maintenance experiments without a new hypothesis. |

## DEBT-003

| Field | Value |
|---|---|
| ID | DEBT-003 |
| AREA | reboot |
| TITLE | `xzs# reboot` is unreliable |
| STATUS | BYPASSED |
| FIRST_SEEN | D7 shell bring-up |
| LAST_KNOWN_COMMIT | shell builtin still calls `SYS_REBOOT`. Not used for recovery. |
| EVIDENCE | The USB gadget can remain present while the shell is dead. CPU0 can keep petting the watchdog after the shell thread stops. |
| IMPACT | A shell reboot is not a way back to fastboot. |
| CURRENT_BYPASS | Sony manual force shutdown, then fastboot, then `fastboot boot`. No flash. |
| WHY_DEFERRED | Manual recovery is reliable enough for single-step display bring-up. |
| RESUME_CONDITION | When a software-controlled reboot is operationally useful. |
| NEXT_INVESTIGATION | Separate gadget teardown from the shell thread and the CPU0 watchdog pet. |

## ACTIVE-D8-001

| Field | Value |
|---|---|
| ID | ACTIVE-D8-001 |
| AREA | display clocks |
| TITLE | `mdss_ahb` branch remains HALT after enable |
| STATUS | ACTIVE_BLOCKER |
| FIRST_SEEN | D8-M2 run `d8m2-0b0e429` |
| LAST_KNOWN_COMMIT | `0b0e4293b44285e866b1de26038548adb62330e2` |
| EVIDENCE | Before `0x80008000`. Write `0x80008001`. After `0x80008001`. Enable bit accepted. Halt bit remains set. Poll timeout 2000 µs. Shell stayed up. Host log `artifacts/hw/d8m2-0b0e429/host.txt`. |
| IMPACT | Blocks AXI and MDP clock bring-up, completion of D8-M2, and D8-M3. |
| CURRENT_BYPASS | Stop after the timeout. Do not repeat the same branch write. Do not enable AXI or MDP until AHB is running. |
| WHY_DEFERRED | Not deferred. This is the active D8-M2 blocker. |
| RESUME_CONDITION | A new candidate must inspect the AHB root and its parents before another branch write. |
| NEXT_INVESTIGATION | Read `ahb_clk_src` CMD/CFG, `mmss_mmagic_ahb`, `mmss_mmagic_cfg_ahb`, `mmagic_mdss_noc_cfg_ahb`, and `gcc_mmss_noc_cfg_ahb`. Hypothesis, not a finding: an upstream or config/NOC parent is not running. |

HYPOTHESIS: the upstream or root clock, or the config/NOC parent, is not running. This is not an established root cause.
