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

## DEBT-004

| Field | Value |
|---|---|
| ID | DEBT-004 |
| AREA | reboot |
| TITLE | `xzs# reboot` should boot reliably into TWRP |
| STATUS | DEFERRED |
| FIRST_SEEN | D7 shell bring-up |
| LAST_KNOWN_COMMIT | shell `reboot` builtin still calls `SYS_REBOOT` |
| EVIDENCE | A shell reboot does not produce a usable recovery session. |
| IMPACT | There is no software path from the XNU shell into TWRP. |
| CURRENT_BYPASS | Sony manual force shutdown, then fastboot, then boot TWRP with `fastboot boot` when a pull is needed. |
| WHY_DEFERRED | Display bring-up uses the USB shell. Recovery pulls are not the current debugger. |
| RESUME_CONDITION | When a software reboot into TWRP is required for a failed boot that the host log cannot explain. |
| NEXT_INVESTIGATION | Define a reboot target that hands the phone to the recovery image without killing the shell first. |

## DEBT-005

| Field | Value |
|---|---|
| ID | DEBT-005 |
| AREA | recovery |
| TITLE | TWRP to fastboot transition |
| STATUS | DEFERRED |
| FIRST_SEEN | D7-T3 pstore pulls |
| LAST_KNOWN_COMMIT | not implemented in the XNU shell |
| EVIDENCE | Returning from TWRP to fastboot is a manual host step (`adb reboot bootloader` has been used). It is not a sealed XNU workflow. |
| IMPACT | A recovery pull is not yet a closed loop back to the next `fastboot boot`. |
| CURRENT_BYPASS | The host asks for fastboot explicitly. No flash. |
| WHY_DEFERRED | D8 does not need an automated recovery loop while each step returns to `xzs#`. |
| RESUME_CONDITION | When post-mortem pulls become part of the normal hardware loop. |
| NEXT_INVESTIGATION | Record one reliable TWRP-to-fastboot sequence and its failure signs. Do not implement it during D8-M2. |

## ACTIVE-D8-001

| Field | Value |
|---|---|
| ID | ACTIVE-D8-001 |
| AREA | display clocks |
| TITLE | `mdss_ahb` branch remains HALT after enable |
| STATUS | ACTIVE_BLOCKER |
| FIRST_SEEN | D8-M2 run `d8m2-0b0e429` |
| LAST_KNOWN_COMMIT | `0b0e4293b44285e866b1de26038548adb62330e2` |
| EVIDENCE | On `0b0e429`: before `0x80008000`, write `0x80008001`, after `0x80008001`. Enable bit accepted. Halt bit remains set. Poll timeout 2000 µs. Read-only `94a2c37`: AHB CMD `0x00000000` (`root_off=0`), CFG `0x00000513` (GPLL0), GCC NOC `0x20008001` running. Read-only `a5f47b5`, log `artifacts/hw/d8m2-a5f47b5/host.txt`: MDSS_BCR, MMAGIC_MDSS_BCR, MMAGICAHB_BCR, and MMAGIC_CFG_BCR all `0x00000000` (assert-control bit 0 clear). GPLL0 mode `0xc0118000` with `PLL_LOCK_DET` set. Vote `0x00000011`, enable bit set. AHB root still `root_off=0`. `mdss_ahb` on that fresh boot is `0x80008000` (enable clear). |
| IMPACT | Blocks AXI and MDP clock bring-up, completion of D8-M2, and D8-M3. |
| CURRENT_BYPASS | Stop after the timeout. Do not repeat the same branch write. Do not enable AXI or MDP until AHB is running. |
| WHY_DEFERRED | Not deferred. This is the active D8-M2 blocker. |
| RESUME_CONDITION | A new candidate must inspect the AHB root and its parents before another branch write. |
| NEXT_INVESTIGATION | Reset bit 0 is clear on the four audited BCRs, so a deassert write is not supported. GPLL0 lock is set. The unexplained fact is still the `0b0e429` readback `0x80008001`. The next candidate must observe the branch and the RCG in one boot, immediately before and after a single new action that is not a repeat of that enable. |

HYPOTHESIS, not established: an upstream clock was stopped. The `94a2c37` read does not support "AHB RCG root is off" or "GCC MMSS NOC config clock is off" on that boot. The earlier halt-with-enable result is still unexplained.
