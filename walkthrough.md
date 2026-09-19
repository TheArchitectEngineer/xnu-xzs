# Walkthrough: Phase D2-M1 & D2-M2 — MSM8996 SDC1 / eMMC Bringup

## 1. Phase D2-M1 Summary: SDC1 / eMMC Identity + Known-Good Bootloader Oracle Audit

Phase D2-M1 established that the Sony Xperia XZs persistent storage is eMMC 5.1 (Samsung BJNB4R) attached to Qualcomm SDCC v5 Host Controller SDC1 (`sdhc_1` @ `0x07464900`):
- Traced LittleKernel (`aboot.img`) and `xbl.img` storage driver call chains.
- Read-only identity probe verified on silicon: `SDHCI_HOST_VERSION = 0x4902`, `SDHCI_CAPABILITIES = 0x742dc8b2`, `MSM_SDCC_HC_MODE = 0x00000001` (`CP = 0xD300`).

---

## 2. Phase D2-M2: SDC1 Prerequisites Replay + Controlled Host Reset

### A. Objectives & Accomplishments
1. **D2-M1 Documentation Corrections**:
   - `SDHCI_HOST_VERSION` (`0x4902`): `0x49` is vendor-defined version, `0x02` is SDHCI 3.00 (not standardized Vendor ID).
   - `PRESENT_STATE` (`CARD_PRESENT=0`): Non-authoritative for soldered `qcom,nonremovable` eMMC.
   - 400-kHz clock: Proven to be XO-derived (`P_XO = 19.2 MHz`), NOT GPLL0.
2. **Inherited Handoff Snapshot (`D2M2_HANDOFF_SNAPSHOT`)**:
   - GCC, SDHCI HC, and CORE registers captured read-only before mutation.
3. **Exact ABOOT Prerequisite Ordering Reconstructed**:
   - Decompiled LittleKernel `target_mmc_init @ 0xaa0003ac`, `mmc_init @ 0xaa00ac14`, `sdhci_init @ 0xaa00949c`, and `sdhci_reset @ 0xaa0083a0`.
   - Identified critical Qualcomm prerequisite: `MSM_SDCC_HC_MODE` bit 13 (`FF_CLK_SW_RST_DIS` / `0x2000`) and `SDCC1_HC_VENDOR_SPEC` (`0x07464A0C = 0x00000A1C` / `CORE_VENDOR_SPEC_POR_VAL`).
4. **400-kHz RCG Replay**:
   - Programmed `F(400000, P_XO, 12, 1, 4)`: `CFG_RCGR = 0x00002017`, `M = 0x1`, `N = 0xFFFFFFFC`, `D = 0xFFFFFFFB`.
   - Update handshake verified: `ROOT_OFF = 0`, APPS branch and AHB branch running.
5. **Controlled SDHCI Host Controller Reset**:
   - Issued `SDHCI_RESET_ALL` (`0x01`) to `SDHCI_SOFTWARE_RESET` (`0x0746492F`).
   - Self-cleared on physical silicon in **10 microseconds**!
6. **Differential & Post-Reset Identity**:
   - Host controller remains responsive (`HOST_VERSION = 0x4902`, `CAPABILITIES = 0x742dc8b2`).
   - Standard SDHCI registers verified reset (`HOST_CONTROL = 0x00`, `TIMEOUT_CONTROL = 0x00`, `INT_ENABLE = 0x00`).
   - ZERO MMC commands sent, ZERO card power-cycles.

---

### B. Live Hardware Verification Telemetry (Breadcrumbs CP = 0xD310)

```text
[BREADCRUMB] CP=0x000000000000d310 ERR=0x0000000000000000 (Enter Phase D2-M2)
[BREADCRUMB] CP=0x000000000000d310 ERR=0x0000000000000010 (D2M2_HANDOFF_SNAPSHOT Captured)
[BREADCRUMB] CP=0x000000000000d310 ERR=0x0000000000000020 (ABOOT_SDC1_PREREQ_SEQUENCE Audited)
[BREADCRUMB] CP=0x000000000000d310 ERR=0x0000000000000030 (Stage A Prerequisite Verification: PASS)
  TLMM SDC1 Pad Register (0x0113C000): 0x00009fe4 [PINCTRL_ALREADY_ACTIVE=yes]
  GCC_SDCC1_AHB_CBCR   (0x313008): 0x20008001 [ENABLED]
  GCC_SDCC1_APPS_CBCR  (0x313004): 0x00004221 [ENABLED]
  GCC_SDCC1_BCR        (0x313000): 0x00000000 [DEASSERTED]
  MSM_SDCC_HC_MODE     (0x7464078): 0x00002001 (HC_MODE_EN=1, FF_CLK_SW_RST_DIS=1)
[BREADCRUMB] CP=0x000000000000d310 ERR=0x0000000000000040 (400k RCG Programmed)
[BREADCRUMB] CP=0x000000000000d310 ERR=0x0000000000000041 (Stage B 400-kHz Clock Replay: PASS)
  SDCC1_APPS_CMD_RCGR (0x313010): 0x00000000 [UPDATE CLEARED / SUCCESS]
    -> ROOT_OFF: CLEARED (root clock running)
  SDCC1_APPS_CFG_RCGR (0x313014): 0x00002017
[BREADCRUMB] CP=0x000000000000d310 ERR=0x0000000000000050 (SDHCI_RESET_ALL Issued)
[BREADCRUMB] CP=0x000000000000d310 ERR=0x0000000000000051 (Reset Self-Cleared)
  RESET_WRITE:             0x01
  RESET_CLEAR_LATENCY_US:  10 us (0x0a us)
  RESET_FINAL:             0x00 [SELF-CLEARED / PASS]
[BREADCRUMB] CP=0x000000000000d310 ERR=0x0000000000000052 (Post-Reset Identity PASS)
  Post-Reset SDHCI_HOST_VERSION (0x74649FE): 0x4902
  Post-Reset SDHCI_CAPABILITIES (0x7464940): 0x742dc8b2
  Post-Reset PRESENT_STATE      (0x7464924): 0x01f80000
[BREADCRUMB] CP=0x000000000000d310 ERR=0x0000000000000060 (PHASE D2-M2 PASS)
[BREADCRUMB] CP=0x000000000000d310 ERR=0x0000000000000080 (Cleanup Complete)
[BREADCRUMB] CP=0x000000000000d310 ERR=0x0000000000000001 (Terminal State -> Warm Reset to Fastboot)
```

---

## 3. Artifacts & Deliverables

- **Phase D2-M2 Report:** [D2_M2_HOST_RESET_REPORT.md](file:///Users/lechaukha12/Desktop/xnu-xzs/artifacts/reports/D2_M2_HOST_RESET_REPORT.md)
- **Phase D2-M1 Report (Corrected):** [D2_M1_EMMC_ORACLE_REPORT.md](file:///Users/lechaukha12/Desktop/xnu-xzs/artifacts/reports/D2_M1_EMMC_ORACLE_REPORT.md)
- **SDHCI Header:** [xzs_sdhci.h](file:///Users/lechaukha12/Desktop/xnu-xzs/src/xnu/pexpert/pexpert/arm/xzs_sdhci.h)
- **SDHCI Driver:** [xzs_sdhci.c](file:///Users/lechaukha12/Desktop/xnu-xzs/src/xnu/pexpert/arm/xzs_sdhci.c)
- **Extracted Silicon Ramoops Log:** `artifacts/logs/console-ramoops.log`

---

## 4. Phase Status & Next Step

```text
D2-M2 STATUS: COMPLETE (PASS)
```

---

## 5. Phase D2-M3: SDC1 Host Power + Internal/Card Clock Activation

### A. Objectives & Accomplishments
1. **D2-M2 Evidence Corrections**:
   - `FF_CLK_SW_RST_DIS` causality in isolation classified as `INFERENCE`. Combined vendor configuration (`CORE_VENDOR_SPEC = 0x0A1C` with `HC_MODE_EN=1`) verified on silicon.
2. **Snapshot Transition Root Cause Accounted For**:
   - `D2M2_POWER_0B_TO_00_CAUSE`: Hardware power monitor de-asserts `SD_BUS_POWER` upon clock frequency reconfiguration.
   - `D2M2_CLOCK_07_TO_03_CAUSE`: Standard SDHCI §2.2.14 hardware safety gating of `SD_CLOCK_ENABLE` (bit 2) during source clock switch.
   - `SNAPSHOT_TRANSITION_ACCOUNTED_FOR=yes`.
3. **Exact Stock ABOOT Pre-Command Sequence Audited**:
   - Decompiled `sdhci_msm_init` and `sdhci_set_clock` in `aboot.img`:
     - Power sequence: 1.8V bus voltage selector (`0x0A`), then assert power (`0x0B`).
     - Internal clock: write `0x0001` (`INT_EN`), poll stable (`0x0002`), write `0x0007` (`CARD_EN`).
     - Timeout: `0x0F` (TMCLK x $2^{27}$).
     - Host control: `0x00` (1-bit initial bus width).
     - Qualcomm power status: direct write safe, no `pwr_irq` wait.
4. **Physical Silicon Replay & Verification**:
   - Restored 400 kHz RCG2 source clock parented by `P_XO`.
   - Performed clean `SDHCI_RESET_ALL` (self-cleared in 10 us).
   - Executed Stage A: Host power reached `0x0B`; `CORE_PWRCTL_STATUS = 0x02` (`BUS_ON`).
   - Executed Stage B: Internal clock stabilized in 10 us (`CLOCK_CONTROL = 0x0003`).
   - Executed Stage C: Card clock enabled (`CLOCK_CONTROL = 0x0007` exact match).
   - Executed Stage D: `TIMEOUT_CONTROL = 0x0F`, `HOST_CONTROL = 0x00`.
5. **Command Engine Readiness & Safety**:
   - `PRESENT_STATE = 0x01F80000`: `CMD_INHIBIT = 0`, `DATA_INHIBIT = 0`, CMD=1, DAT=1111b.
   - ZERO MMC commands transmitted (`SDHCI_COMMAND = 0x0000`).
   - ZERO eMMC writes, zero power rail cycles.

### B. Live Hardware Verification Telemetry (Breadcrumbs CP = 0xD320)

```text
[BREADCRUMB] CP=0x000000000000d320 ERR=0x0000000000000000 (Enter Phase D2-M3)
[BREADCRUMB] CP=0x000000000000d320 ERR=0x0000000000000010 (D2-M2 Snapshot Root Cause Logged)
[BREADCRUMB] CP=0x000000000000d320 ERR=0x0000000000000020 (ABOOT Sequence Logged)
[BREADCRUMB] CP=0x000000000000d320 ERR=0x0000000000000030 (Prerequisite Restored: 400k RCG + Reset Self-Clear PASS)
[BREADCRUMB] CP=0x000000000000d320 ERR=0x0000000000000040 (Host Power Sequence Begin: 0x0A -> 0x0B)
[BREADCRUMB] CP=0x000000000000d320 ERR=0x0000000000000041 (Stage A Host Power PASS: 0x0B, PWRCTL_STATUS=0x02)
[BREADCRUMB] CP=0x000000000000d320 ERR=0x0000000000000050 (Internal Clock Enable: 0x0001)
[BREADCRUMB] CP=0x000000000000d320 ERR=0x0000000000000051 (Stage B Internal Clock Stable PASS: 10 us, 0x0003)
[BREADCRUMB] CP=0x000000000000d320 ERR=0x0000000000000060 (Stage C Card Clock Enabled PASS: 0x0007)
[BREADCRUMB] CP=0x000000000000d320 ERR=0x0000000000000061 (Stage D Timeout 0x0F + Host Control 0x00 PASS)
[BREADCRUMB] CP=0x000000000000d320 ERR=0x0000000000000070 (D2M3_PRE_COMMAND_STATE Snapshot Captured)
[BREADCRUMB] CP=0x000000000000d320 ERR=0x0000000000000071 (Inhibit Check PASS: CMD_INHIBIT=0, DATA_INHIBIT=0)
[BREADCRUMB] CP=0x000000000000d320 ERR=0x0000000000000080 (PHASE D2-M3 PASS)
[BREADCRUMB] CP=0x000000000000d320 ERR=0x0000000000000090 (Cleanup Complete)
[BREADCRUMB] CP=0x000000000000d320 ERR=0x0000000000000001 (Terminal State -> Warm Reset to Fastboot)
```

---

## 6. Phase D2-M3 Status & Next Step

```text
D2-M3 STATUS: COMPLETE (PASS)
```

---

## 7. Phase D2-M4A: First MMC Command / CMD0 GO_IDLE_STATE

### A. Objectives & Accomplishments
1. **D2-M3 Documentation Corrections**:
   - Explicitly clarified: card clock enable accepted (`CLOCK_CONTROL = 0x0007`) verified on hardware, but external pad waveform was not physically probed with an oscilloscope.
   - Normalized vendor registers: `SDCC1_HC_VENDOR_SPEC` (HC offset `0x10C` = `0x0A1C`) vs `MSM_SDCC_HC_MODE` (CORE offset `0x078` = `0x2001`).
2. **Exact Stock ABOOT CMD0 Sequence Audited**:
   - Traced `target_mmc_init` -> `mmc_init @ 0xaa00ac14` -> `mmc_send_cmd @ 0xaa0086a8`.
   - Determined exact parameters: `CMD0_ARGUMENT = 0x00000000`, `CMD0_TRANSFER_MODE = 0x0000`, `CMD0_COMMAND_VALUE = 0x0000`.
   - Verified that ABOOT clears `COMMAND_COMPLETE` via W1C write of `0x0001` to `SDHCI_INT_STATUS`.
3. **Polling-Mode Interrupt Safety**:
   - Applied XZS bringup workaround: `INT_ENABLE = 0xFFFF800B`, `SIGNAL_ENABLE = 0x00000000` to prevent unhandled GIC interrupts while enabling status register polling.
4. **Physical Silicon Execution & Verification**:
   - Replayed all D2-M3 prerequisites (400 kHz RCG2, reset, 1.8V power, internal clock, card clock, timeout, 1-bit bus mode).
   - Applied 1000 us pre-CMD0 delay (eMMC 5.1 specification requires $\ge 74$ clock cycles / 185 us).
   - Transmitted exactly ONE `CMD0` (`0x0000`).
   - Observed immediate `CMD_INHIBIT = 1` in `PRESENT_STATE` (`0x01F80001`), confirming hardware command engine engagement.
   - Observed `COMMAND_COMPLETE` (bit 0 = 1) within 10 us latency.
   - Decoded error status: strictly 0 (`CTO = 0`, `CCRC = 0`, `CEND = 0`, `CINDEX = 0`, `POWER = 0`).
   - Cleared `COMMAND_COMPLETE` via W1C; readback verified `INT_STATUS = 0x00000000`.
   - Applied 1000 us settling delay; verified `PRESENT_STATE = 0x01F80000` (`CMD_INHIBIT = 0`, `DATA_INHIBIT = 0`).
5. **Card Communication & Scope Boundaries**:
   - `CMD0_HOST_TRANSMISSION = PASS`.
   - `CMD0_CARD_RESPONSE_EXPECTED = no` (CMD0 expects no response).
   - `CARD_COMMUNICATION_CONFIRMED = no` (eMMC response verification begins in D2-M4B / CMD1).

### B. Live Hardware Verification Telemetry (Breadcrumbs CP = 0xD330)

```text
[BREADCRUMB] CP=0x000000000000d330 ERR=0x0000000000000000 (Enter Phase D2-M4A)
[BREADCRUMB] CP=0x000000000000d330 ERR=0x0000000000000010 (Git Baseline & Evidence Corrections Logged)
[BREADCRUMB] CP=0x000000000000d330 ERR=0x0000000000000020 (ABOOT CMD0 Sequence Audited)
[BREADCRUMB] CP=0x000000000000d330 ERR=0x0000000000000030 (Prerequisite Replay: Power 0x0B, Clock 0x0007 PASS)
[BREADCRUMB] CP=0x000000000000d330 ERR=0x0000000000000031 (Polling IRQ Mode: SIGNAL_ENABLE=0 PASS)
[BREADCRUMB] CP=0x000000000000d330 ERR=0x0000000000000040 (Stale INT_STATUS Cleared: 0x00000000 PASS)
[BREADCRUMB] CP=0x000000000000d330 ERR=0x0000000000000041 (Inhibit Check PASS: CMD=0, DATA=0)
[BREADCRUMB] CP=0x000000000000d330 ERR=0x0000000000000050 (Pre-CMD0 Delay 1000 us Complete)
[BREADCRUMB] CP=0x000000000000d330 ERR=0x0000000000000051 (CMD0 Written: PRESENT_STATE=0x01f80001)
[BREADCRUMB] CP=0x000000000000d330 ERR=0x0000000000000052 (Command Complete Observed: <10 us, INT_STATUS=0x01)
[BREADCRUMB] CP=0x000000000000d330 ERR=0x0000000000000053 (Error Decode PASS: Zero Errors)
[BREADCRUMB] CP=0x000000000000d330 ERR=0x0000000000000060 (W1C Clear & Post-Delay Complete: Engine Idle)
[BREADCRUMB] CP=0x000000000000d330 ERR=0x0000000000000061 (CMD0_HOST_TRANSMISSION: PASS)
[BREADCRUMB] CP=0x000000000000d330 ERR=0x0000000000000080 (D2M4A_POST_CMD0_STATE Captured)
[BREADCRUMB] CP=0x000000000000d330 ERR=0x0000000000000090 (Cleanup Complete)
[BREADCRUMB] CP=0x000000000000d330 ERR=0x0000000000000001 (Terminal State -> Warm Reset to Fastboot)
```

---

## 8. Phase D2-M4A Status & Next Step

```text
D2-M4A STATUS: COMPLETE (PASS)
```

---

## 9. Phase D2-M4B: First Physical eMMC Response / CMD1 SEND_OP_COND

### A. Objectives & Accomplishments
1. **D2-M4A Timing Anomaly Audit**:
   - Resolved why CMD0 reported 0 us latency: measurement loop executed after UART logging; command completed during print overhead before loop entry.
   - Replaced loop counter with high-precision ARM64 architectural virtual counter (`cntvct_el0` @ 19.2 MHz `cntfrq_el0`, ~52 ns/tick).
2. **Exact Stock ABOOT CMD1 Path Audited**:
   - Decompiled `mmc_init` @ `0xaa00ac14` and `mmc_send_cmd` @ `0xaa0086a8`.
   - Verified exact parameters: `CMD1_ARGUMENT = 0x40FF8000`, `CMD1_RESPONSE_TYPE = R3` (48-bit short, no CRC, no index), `CMD1_COMMAND_ENCODING = 0x0102`.
3. **Physical Silicon Execution & First eMMC Response**:
   - Replayed all D2-M3 host prerequisites (400 kHz RCG2, reset, 1.8V power, internal/card clock, timeout, 1-bit bus mode).
   - Replayed prerequisite CMD0 cleanly.
   - Transmitted exactly ONE `CMD1` (`0x0102`) with argument `0x40FF8000`.
   - Captured architectural timestamps without intervening console print overhead.
   - Observed `COMMAND_COMPLETE` (bit 0 = 1) with zero host errors.
   - Read `SDHCI_RESPONSE_0`: **`0x40FF8080`**!
4. **OCR Response Structural Validation**:
   - Bit 31: `0` (`CARD_POWER_UP_STATUS`: busy, initialization in progress).
   - Bit 30: `1` (`ACCESS_MODE`: sector mode / high capacity > 2GB).
   - Bits 23..15: `0xFF` (voltage window 2.7 - 3.6 V supported).
   - Bit 7: `1` (dual voltage 1.70 - 1.95 V supported, matching 1.8V VDDIO PMIC rail S4).
   - All reserved bits strictly 0.
   - Structural validation: **PASS**.
5. **Milestone Claim**:
   - **`CARD_COMMUNICATION_CONFIRMED: yes`**!
   - Physical Samsung BJNB4R eMMC 5.1 storage chip successfully communicated with XNU!

### B. Live Hardware Verification Telemetry (Breadcrumbs CP = 0xD340)

```text
[BREADCRUMB] CP=0x000000000000d340 ERR=0x0000000000000000 (Enter Phase D2-M4B)
[BREADCRUMB] CP=0x000000000000d340 ERR=0x0000000000000010 (Git Baseline & Evidence Corrections Logged)
[BREADCRUMB] CP=0x000000000000d340 ERR=0x0000000000000020 (CMD0 Latency Audit: CMD0_LT10US_EXPLAINED=yes)
[BREADCRUMB] CP=0x000000000000d340 ERR=0x0000000000000021 (Timer Source: cntvct_el0 @ 19.2 MHz verified)
[BREADCRUMB] CP=0x000000000000d340 ERR=0x0000000000000030 (Host Prerequisite Replay PASS)
[BREADCRUMB] CP=0x000000000000d340 ERR=0x0000000000000031 (Source Clock Config 400K Verified)
[BREADCRUMB] CP=0x000000000000d340 ERR=0x0000000000000040 (Prerequisite CMD0 Replay PASS)
[BREADCRUMB] CP=0x000000000000d340 ERR=0x0000000000000041 (Post-CMD0 Delay 1000 us Complete)
[BREADCRUMB] CP=0x000000000000d340 ERR=0x0000000000000050 (CMD1 Prepared: Arg=0x40FF8000, Cmd=0x0102)
[BREADCRUMB] CP=0x000000000000d340 ERR=0x0000000000000051 (CMD1 Written with Timestamps)
[BREADCRUMB] CP=0x000000000000d340 ERR=0x0000000000000052 (CMD1 Command Complete Observed)
[BREADCRUMB] CP=0x000000000000d340 ERR=0x0000000000000053 (Response Captured: RESPONSE_0=0x40FF8080)
[BREADCRUMB] CP=0x000000000000d340 ERR=0x0000000000000054 (OCR Validated: Sector Mode + 1.8V/3.0V PASS)
[BREADCRUMB] CP=0x000000000000d340 ERR=0x0000000000000060 (CARD_COMMUNICATION_CONFIRMED: yes)
[BREADCRUMB] CP=0x000000000000d340 ERR=0x0000000000000061 (CARD_READY: no - Busy during initial query)
[BREADCRUMB] CP=0x000000000000d340 ERR=0x0000000000000080 (D2M4B_POST_CMD1_STATE Captured)
[BREADCRUMB] CP=0x000000000000d340 ERR=0x0000000000000090 (Cleanup Complete)
[BREADCRUMB] CP=0x000000000000d340 ERR=0x0000000000000001 (Terminal State -> Warm Reset to Fastboot)
```

---

## 10. Phase D2-M4B Status

```text
D2-M4B STATUS: COMPLETE (PASS)
CARD_COMMUNICATION_CONFIRMED: yes
```

---

## 11. Phase D2-M4C: Complete eMMC Power-Up Negotiation via CMD1 Polling

### A. Objectives & Accomplishments
1. **D2-M4B Corrections Applied**:
   - D2-M4B timer anomaly reclassified: `D2M4B_TIMER_ROOT_CAUSE = UNRESOLVED`.
   - Low-voltage OCR range wording corrected: `OCR bit7 = MMC_VDD_165_195 = 1.65 V – 1.95 V`.
   - XNU `delay(usec)` audited from `src/xnu/osfmk/kern/clock.c`: `delay(1000) = 1 ms` verified.
2. **Architectural Timer Instrumentation Verified**:
   - Implemented `xzs_read_cntvct()` with `"isb\n\tmrs %0, cntvct_el0"` and dedicated `uint64_t` variables.
   - Binary inspection verified both `isb` and `mrs x?, CNTVCT_EL0` present in compiled kernel (`CNTVCT_MRS_BINARY_VERIFIED = yes`).
   - Monotonicity gate `t_before_cmd < t_after_complete <= t_after_response` passed on physical silicon (`TIMER_VALID = yes`).
3. **Exact ABOOT CMD1 Retry Loop Audited from `aboot.img`**:
   - Audited `0xaa00ad50` - `0xaa00adec`: 1000 max iterations (`mov sb, #0x3e8`), 1 ms delay (`mov r0, #1; bl #0xaa011844`), break condition `OCR bit 31 == 1` (`cmp r3, #0; blt #0xaa00adec`).
4. **Physical Silicon Execution & eMMC Power-Up Complete**:
   - Replayed fresh host initialization (400 kHz RCG2, reset, power, clock, timeout, 1-bit mode) and prerequisite CMD0.
   - Executed CMD1 polling loop with argument `0x40FF8000` and in-RAM compact telemetry:
     - **Iteration 1**: `OCR = 0x40FF8080` (`OCR_POWER_UP_STATUS = 0`, card initializing / busy).
     - **Iteration 2**: `OCR = 0xC0FF8080` (`OCR_POWER_UP_STATUS = 1`, card power-up complete!).
     - Immediate STOP! No further CMD1 transmitted.
   - Total iterations: **2**.
   - Total elapsed time: **1 ms**.
   - Zero controller errors: `CMD1_ERROR_BITS = 0x00000000`.
   - **`CARD_READY = yes`**!
5. **Milestone Claim**:
   - **`HARDWARE VERIFIED: eMMC Power-Up Negotiation Complete!`**
   - Samsung BJNB4R eMMC 5.1 is powered up and ready to transition to identification state.
   - HARD STOP enforced: zero CMD2, zero writes, zero power cycle.

### B. Live Hardware Verification Telemetry (Breadcrumbs CP = 0xD350)

```text
[BREADCRUMB] CP=0x000000000000d350 ERR=0x0000000000000000 (Enter Phase D2-M4C)
[BREADCRUMB] CP=0x000000000000d350 ERR=0x0000000000000010 (Git Baseline & Evidence Baseline Logged)
[BREADCRUMB] CP=0x000000000000d350 ERR=0x0000000000000020 (Timer Bug Audited: D2M4B_TIMER_ROOT_CAUSE=UNRESOLVED)
[BREADCRUMB] CP=0x000000000000d350 ERR=0x0000000000000021 (Timer Instrumentation: MONOTONIC_SANITY_PASS=yes)
[BREADCRUMB] CP=0x000000000000d350 ERR=0x0000000000000030 (Fresh Host Initialization Replayed)
[BREADCRUMB] CP=0x000000000000d350 ERR=0x0000000000000031 (CMD0 Result: PASS)
[BREADCRUMB] CP=0x000000000000d350 ERR=0x0000000000000040 (CMD1 Polling Loop Begin)
[BREADCRUMB] CP=0x000000000000d350 ERR=0x0000000000000041 (First OCR Captured: 0x40FF8080)
[BREADCRUMB] CP=0x000000000000d350 ERR=0x0000000000000050 (Iter 1: OCR_POWER_UP_STATUS=0)
[BREADCRUMB] CP=0x000000000000d350 ERR=0x0000000000000060 (Iter 2: OCR_POWER_UP_STATUS=1)
[BREADCRUMB] CP=0x000000000000d350 ERR=0x0000000000000061 (CARD_READY: yes)
[BREADCRUMB] CP=0x000000000000d350 ERR=0x0000000000000070 (Timing Summary: 1 ms elapsed)
[BREADCRUMB] CP=0x000000000000d350 ERR=0x0000000000000080 (Final Snapshot Captured)
[BREADCRUMB] CP=0x000000000000d350 ERR=0x0000000000000090 (Cleanup Complete)
[BREADCRUMB] CP=0x000000000000d350 ERR=0x0000000000000001 (Terminal State -> Warm Reset to Fastboot)
```

---

## 12. Phase D2-M4C Status & Next Step

```text
D2-M4C STATUS: COMPLETE (PASS)
CARD_READY: yes
FINAL_OCR: 0xC0FF8080
READY_ITERATION: 2
```

- **Next Phase (D2-M4D):** eMMC Card Identification (CMD2 / `ALL_SEND_CID`) — Read 128-bit CID register from Samsung BJNB4R, parse manufacturer ID, product name (`BJNB4R`), revision, serial number, and manufacturing date.

---

## 13. Phase D2-M4D-A: eMMC Identification — CMD2 / ALL_SEND_CID

### A. Objectives & Accomplishments
1. **Stock Sony ABOOT CMD2 Audit**:
   - Audited `artifacts/firmware/stock/aboot.img` (`@ 0xaa00ae00` and `@ 0xaa008aa0`):
     - `ABOOT_CMD2_OPCODE = 2`
     - `ABOOT_CMD2_ARGUMENT = 0x00000000`
     - `ABOOT_CMD2_RESPONSE_TYPE = 4` (R2 / 136-bit)
     - `ABOOT_CMD2_COMMAND_VALUE = 0x0209` (`SDHCI_MAKE_CMD(2, SDHCI_CMD_RESP_136 | SDHCI_CMD_CRC)`)
     - `ABOOT_CMD2_COMPLETION_MASK = 0x0001` (`COMMAND_COMPLETE`)
     - `ABOOT_CMD2_ERROR_MASK = 0xFFFF0000`
2. **R2 136-bit Response Reconstruction Proven**:
   - Audited extraction formula: `resp[i] = (r[i] << 8) | (r[i-1] >> 24)`.
   - Proved `R2_RECONSTRUCTION_SOURCE_PROVEN = yes`.
3. **Silicon Execution on Sony Xperia XZs (Tone Keyaki / G8231)**:
   - Fresh initialization and verified CMD1 negotiation (`CARD_READY = yes` on iteration 2).
   - Issued exactly ONE CMD2 (`COMMAND = 0x0209`).
   - `COMMAND_COMPLETE` observed, zero command errors (`CMD2_ERROR_BITS = 0x00000000`).
4. **Physical CID Verification**:
   - Raw registers:
     - `RAW_RESP0 = 0xC7C03814`
     - `RAW_RESP1 = 0x34520FDA`
     - `RAW_RESP2 = 0x424A4E42`
     - `RAW_RESP3 = 0x00150100`
   - Reconstructed normalized CID words:
     - `CID_WORD0 = 0x15010042`
     - `CID_WORD1 = 0x4A4E4234`
     - `CID_WORD2 = 0x520FDAC7`
     - `CID_WORD3 = 0xC0381400`
   - Serialized CID: `150100424a4e4234520fdac7c0381400`
   - **`CID_MATCH = yes`**
   - **`DEVICE_IDENTITY_CONFIRMED = yes`**
5. **Decoded Physical Identity (JEDEC JESD84-B51)**:
   - Manufacturer ID (MID): `0x15` (Samsung)
   - Device Type (CBX): `0x01` (BGA / Discrete eMMC)
   - Product Name (PNM): `BJNB4R`
   - Product Revision: `CID_PRV_RAW = 0x0F` (Hardware verified: raw PRV field = `0x0F`; vendor-facing semantic revision string is not established)
   - Product Serial Number (PSN): `0xDAC7C038`
   - Manufacturing Date: `CID_MDT_RAW = 0x14` (Raw hardware field: `0x14`; decoded using eMMC revision-aware MDT rules: `month = 1` / January, `year index = 4`; with `EXT_CSD_REV = 0x08` / eMMC 5.1, corresponds to January 2017)

### B. Live Hardware Verification Telemetry (Breadcrumbs CP = 0xD360)

```text
[BREADCRUMB] CP=0x000000000000d360 ERR=0x0000000000000000 (Enter Phase D2-M4D-A)
[BREADCRUMB] CP=0x000000000000d360 ERR=0x0000000000000010 (Git Baseline Logged)
[BREADCRUMB] CP=0x000000000000d360 ERR=0x0000000000000020 (ABOOT CMD2 Audited)
[BREADCRUMB] CP=0x000000000000d360 ERR=0x0000000000000021 (R2 Reconstruction Audited)
[BREADCRUMB] CP=0x000000000000d360 ERR=0x0000000000000030 (Fresh Initialization)
[BREADCRUMB] CP=0x000000000000d360 ERR=0x0000000000000031 (CARD_READY: yes, FINAL_OCR: 0xC0FF8080)
[BREADCRUMB] CP=0x000000000000d360 ERR=0x0000000000000040 (CMD2 Prepared)
[BREADCRUMB] CP=0x000000000000d360 ERR=0x0000000000000041 (CMD2 Written)
[BREADCRUMB] CP=0x000000000000d360 ERR=0x0000000000000042 (COMMAND_COMPLETE Observed)
[BREADCRUMB] CP=0x000000000000d360 ERR=0x0000000000000043 (Zero Errors: CMD2_ERROR_BITS = 0)
[BREADCRUMB] CP=0x000000000000d360 ERR=0x0000000000000050 (RAW_RESP0..3 Captured)
[BREADCRUMB] CP=0x000000000000d360 ERR=0x0000000000000051 (Normalized CID Reconstructed)
[BREADCRUMB] CP=0x000000000000d360 ERR=0x0000000000000052 (CID_MATCH: yes)
[BREADCRUMB] CP=0x000000000000d360 ERR=0x0000000000000060 (DEVICE_IDENTITY_CONFIRMED: yes)
[BREADCRUMB] CP=0x000000000000d360 ERR=0x0000000000000080 (Final Snapshot Captured)
[BREADCRUMB] CP=0x000000000000d360 ERR=0x0000000000000090 (Cleanup Complete)
[BREADCRUMB] CP=0x000000000000d360 ERR=0x0000000000000001 (Terminal State -> Warm Reset to Fastboot)
```

---

## 14. Phase D2-M4D-A Status & Next Step

```text
D2-M4D-A STATUS: COMPLETE (PASS)
CID_MATCH: yes
DEVICE_IDENTITY_CONFIRMED: yes
CID_HEX: 150100424a4e4234520fdac7c0381400
```

- **Next Phase (D2-M4D-B):** eMMC Address Assignment (CMD3 / `SET_RELATIVE_ADDR`) — Assign Relative Card Address (RCA) and transition card to Standby State (`stby`).

---

## 15. Phase D2-M4D-B: eMMC RCA Assignment — CMD3 / SET_RELATIVE_ADDR

### A. Objectives & Accomplishments
1. **Stock Sony ABOOT CMD3 Disassembly Audit**:
   - Disassembled `aboot.img @ 0xaa00af48 - 0xaa00af88` and `aboot.img @ 0xaa0086a8 - 0xaa008980`:
     - `ABOOT_CMD3_OPCODE = 3` (`SET_RELATIVE_ADDR`)
     - `ASSIGNED_RCA = 2` (`0x0002`): Sony ABOOT explicitly assigns RCA = 2 to eMMC device.
     - `ABOOT_CMD3_ARGUMENT = 0x00020000` (`2 << 16`).
     - `ABOOT_INTERNAL_RESP_TYPE = 0x40` (mapped to SDHCI flags `0x1A`: `RESP_48 | CRC | INDEX`).
     - `ABOOT_CMD3_COMMAND_VALUE = 0x031A`.
2. **TIMEOUT_CONTROL Discrepancy Resolved**:
   - `M4DA_TIMEOUT_0E_ROOT_CAUSE`: Source divergence at line 2533.
   - Restored `SDHCI_TIMEOUT_CONTROL = 0x0F` (`ABOOT_TIMEOUT_VAL`).
3. **Physical Silicon Execution (Sony Xperia XZs Tone Keyaki / G8231)**:
   - Fresh initialization -> CMD0 -> CMD1 polling (`CARD_READY = yes`) -> CMD2 (`CID_MATCH = yes`).
   - Issued exactly ONE CMD3 (`COMMAND = 0x031A`, `ARG = 0x00020000`).
   - `COMMAND_COMPLETE` observed, zero SDHCI command errors (`CMD3_ERROR_BITS = 0x00000000`).
4. **R1 Card Status Capture & Error Decode**:
   - `CMD3_R1_RAW = 0x00000500`.
   - All 15 named reject error bits are zero: `CMD3_R1_REJECT_BITS = 0x00000000`.
   - `READY_FOR_DATA = 1` (bit 8).
   - `CURRENT_STATE = 2` (`IDENT`): Per JEDEC JESD84-B51 Section 6.13, `CURRENT_STATE` in R1 reflects the state of the card when the command was received (`ident` = 2), prior to transitioning to `stby` (3).
5. **Hard Stop Enforced**:
   - Exactly ONE CMD3 issued. Zero CMD9, zero CMD7, zero storage writes, zero power-cycle.

### B. Live Hardware Verification Telemetry (Breadcrumbs CP = 0xD370)

```text
[BREADCRUMB] CP=0x000000000000d370 ERR=0x0000000000000000 (Enter Phase D2-M4D-B)
[BREADCRUMB] CP=0x000000000000d370 ERR=0x0000000000000010 (Git Baseline Logged)
[BREADCRUMB] CP=0x000000000000d370 ERR=0x0000000000000020 (M4D-A Corrections Applied)
[BREADCRUMB] CP=0x000000000000d370 ERR=0x0000000000000021 (Timeout Discrepancy Resolved)
[BREADCRUMB] CP=0x000000000000d370 ERR=0x0000000000000030 (ABOOT CMD3 Audited)
[BREADCRUMB] CP=0x000000000000d370 ERR=0x0000000000000031 (RCA Frozen: 2)
[BREADCRUMB] CP=0x000000000000d370 ERR=0x0000000000000040 (Fresh Initialization Replayed)
[BREADCRUMB] CP=0x000000000000d370 ERR=0x0000000000000041 (CARD_READY: yes, FINAL_OCR: 0xC0FF8080)
[BREADCRUMB] CP=0x000000000000d370 ERR=0x0000000000000042 (CID_MATCH: yes)
[BREADCRUMB] CP=0x000000000000d370 ERR=0x0000000000000050 (CMD3 Prepared)
[BREADCRUMB] CP=0x000000000000d370 ERR=0x0000000000000051 (CMD3 Written: 0x031A, ARG: 0x00020000)
[BREADCRUMB] CP=0x000000000000d370 ERR=0x0000000000000052 (COMMAND_COMPLETE Observed)
[BREADCRUMB] CP=0x000000000000d370 ERR=0x0000000000000053 (Zero Errors: CMD3_ERROR_BITS = 0)
[BREADCRUMB] CP=0x000000000000d370 ERR=0x0000000000000060 (R1 Captured: 0x00000500)
[BREADCRUMB] CP=0x000000000000d370 ERR=0x0000000000000080 (Final Snapshot Captured)
[BREADCRUMB] CP=0x000000000000d370 ERR=0x0000000000000090 (Cleanup Complete)
[BREADCRUMB] CP=0x000000000000d370 ERR=0x0000000000000001 (Terminal State -> Warm Reset to Fastboot)
```

---

## 16. Phase D2-M4D-B Status & Next Step

```text
D2-M4D-B STATUS: COMPLETE (HARDWARE TELEMETRY CAPTURED)
ASSIGNED_RCA: 2
CMD3_ARGUMENT: 0x00020000
CMD3_COMMAND: 0x031A
CMD3_R1_RAW: 0x00000500
CMD3_R1_REJECT_BITS: 0x00000000
CURRENT_STATE: 2 (IDENT)
```

- **Next Phase (D2-M4D-C):** eMMC Card-Specific Data (CMD9 / `SEND_CSD`) — Query CSD register addressed to `RCA = 2` (`ARG = 0x00020000`), verify card in Standby State (`stby`), and decode device capacity / timing characteristics.

---

## 17. Phase D2-M4D-C: eMMC CSD Identification — CMD9 / SEND_CSD

### A. Objectives & Accomplishments
1. **D2-M4D-B Documentation Corrections**:
   - Frozen CMD3 R1 state semantics: `CURRENT_STATE = 2` (`IDENT`) in R1 is expected per JEDEC JESD84-B51 Section 6.13 (reflects state when command was received). Subsequent addressed CMD9 operationally verifies transition to `stby` (3).
   - Fixed reversed disassembly range notation to ascending order (`0xaa0086a8 - 0xaa008980`).
   - Preserved `TIMEOUT_CONTROL = 0x0F` (`ABOOT_TIMEOUT_VAL`).
2. **Stock Sony ABOOT CMD9 Disassembly Audit**:
   - Decompiled `aboot.img @ 0xaa00b01c - 0xaa00b058`:
     - `ABOOT_CMD9_OPCODE = 9` (`SEND_CSD`)
     - `ABOOT_CMD9_ARGUMENT = 0x00020000` (`dev->rca << 16 = 2 << 16`)
     - `ABOOT_CMD9_RESPONSE_TYPE = 4` (R2 / 136-bit)
     - `ABOOT_CMD9_COMMAND_VALUE = 0x0909` (`SDHCI_MAKE_CMD(9, SDHCI_CMD_RESP_136 | SDHCI_CMD_CRC)`)
3. **Physical Silicon Execution (Sony Xperia XZs Tone Keyaki / G8231)**:
   - Fresh initialization -> CMD0 -> CMD1 polling (`CARD_READY = yes`) -> CMD2 (`CID_MATCH = yes`) -> CMD3 (`RCA = 2`, zero errors).
   - Issued exactly ONE CMD9 (`COMMAND = 0x0909`, `ARG = 0x00020000`).
   - `COMMAND_COMPLETE` observed, zero SDHCI command errors (`CMD9_ERROR_BITS = 0x00000000`).
4. **Raw Response Capture & Proven R2 Reconstruction**:
   - Captured raw SDHCI response registers:
     - `RAW_RESP0 = 0xef8e4040`
     - `RAW_RESP1 = 0xfff6dbff`
     - `RAW_RESP2 = 0x320f5903`
     - `RAW_RESP3 = 0x00d02701`
   - Reconstructed normalized 128-bit CSD using proven R2 algorithm:
     - `CSD_WORD0 = 0xd0270132`
     - `CSD_WORD1 = 0x0f5903ff`
     - `CSD_WORD2 = 0xf6dbffef`
     - `CSD_WORD3 = 0x8e404000`
5. **Exact 128-bit CSD Match**:
   - `CSD_HEX = d02701320f5903fff6dbffef8e404000`
   - `EXPECTED_CSD_HEX = d02701320f5903fff6dbffef8e404000`
   - `CSD_MATCH = yes` (byte-for-byte exact equality).
6. **Operational Proof of Post-CMD3 Addressing**:
   - `POST_CMD3_ADDRESSING_CONFIRMED = yes`: Card successfully assigned RCA=2 and operates in `stby` state.
7. **CSD Field Decode**:
   - Decoded `CSD_STRUCTURE = 0x3`, `SPEC_VERS = 0x4`, `READ_BL_LEN = 512`, `WRITE_BL_LEN = 512`, `TRAN_SPEED = 0x32` (26/52 MHz legacy), `CCC = 0xF5`.
8. **Hard Stop Enforced**:
   - Exactly ONE CMD9 issued. Zero CMD7, zero data transfers, zero storage writes, zero power-cycle.

### B. Live Hardware Verification Telemetry (Breadcrumbs CP = 0xD380)

```text
[BREADCRUMB] CP=0x000000000000d380 ERR=0x0000000000000000 (Enter Phase D2-M4D-C)
[BREADCRUMB] CP=0x000000000000d380 ERR=0x0000000000000010 (Git Baseline Logged)
[BREADCRUMB] CP=0x000000000000d380 ERR=0x0000000000000020 (M4D-B Corrections Applied)
[BREADCRUMB] CP=0x000000000000d380 ERR=0x0000000000000021 (Exact CMD9 Audit Logged)
[BREADCRUMB] CP=0x000000000000d380 ERR=0x0000000000000030 (Fresh Initialization Replayed)
[BREADCRUMB] CP=0x000000000000d380 ERR=0x0000000000000031 (CARD_READY: yes, FINAL_OCR: 0xC0FF8080)
[BREADCRUMB] CP=0x000000000000d380 ERR=0x0000000000000032 (CID_MATCH: yes)
[BREADCRUMB] CP=0x000000000000d380 ERR=0x0000000000000033 (RCA_ASSIGNED: 2)
[BREADCRUMB] CP=0x000000000000d380 ERR=0x0000000000000040 (CMD9 Prepared)
[BREADCRUMB] CP=0x000000000000d380 ERR=0x0000000000000041 (CMD9 Written: 0x0909, ARG: 0x00020000)
[BREADCRUMB] CP=0x000000000000d380 ERR=0x0000000000000042 (COMMAND_COMPLETE Observed)
[BREADCRUMB] CP=0x000000000000d380 ERR=0x0000000000000043 (Zero Errors: CMD9_ERROR_BITS = 0)
[BREADCRUMB] CP=0x000000000000d380 ERR=0x0000000000000050 (Raw CSD Registers Captured)
[BREADCRUMB] CP=0x000000000000d380 ERR=0x0000000000000051 (CSD Reconstructed)
[BREADCRUMB] CP=0x000000000000d380 ERR=0x0000000000000052 (CSD_MATCH: yes)
[BREADCRUMB] CP=0x000000000000d380 ERR=0x0000000000000060 (POST_CMD3_ADDRESSING_CONFIRMED: yes)
[BREADCRUMB] CP=0x000000000000d380 ERR=0x0000000000000080 (Final Snapshot Captured)
[BREADCRUMB] CP=0x000000000000d380 ERR=0x0000000000000090 (Cleanup Complete)
[BREADCRUMB] CP=0x000000000000d380 ERR=0x0000000000000001 (Terminal State -> Warm Reset to Fastboot)
```

---

## 18. Phase D2-M4D-C Status & Next Step

```text
D2-M4D-C STATUS: COMPLETE (HARDWARE TELEMETRY CAPTURED)
ASSIGNED_RCA: 2
CMD9_ARGUMENT: 0x00020000
CMD9_COMMAND: 0x0909
CMD9_INT_STATUS: 0x00000001
CMD9_ERROR_BITS: 0x00000000
CSD_HEX: d02701320f5903fff6dbffef8e404000
EXPECTED_CSD_HEX: d02701320f5903fff6dbffef8e404000
CSD_MATCH: yes
POST_CMD3_ADDRESSING_CONFIRMED: yes
CMD7_ISSUED: no
```

- **Next Phase (D2-M4D-D):** eMMC Card Selection (CMD7 / `SELECT_CARD`) — Issue `CMD7` addressed to `RCA = 2` (`ARG = 0x00020000`) with response `R1` to transition Samsung BJNB4R into Transfer State (`tran`).

---

## 19. Phase D2-M4D-D: eMMC Card Selection — CMD7 / SELECT_CARD

### A. Objectives & Accomplishments
1. **Vendor Register Discrepancy Resolved**:
   - Machine code disassembly and live silicon MMIO readback proved `SDCC1_HC_VENDOR_SPEC = 0x00000A1C` and `SDHCI_TIMEOUT_CONTROL = 0x0F`.
   - Source/binary verified: `MSM_SDCC_HC_MODE` write requested = `0x00002001`, but later MMIO readback reflects `HC_MODE_EN = 1` (`0x00000001`) with bit 13 reading 0. Cause of bit 13 readback clearing is classified as UNRESOLVED.
   - Prior documentation stating `0x00000100` and `0x00000001` was corrected as a report-text transcription typo.
2. **Stock Sony LittleKernel (`aboot.img`) CMD7 Audit**:
   - Decompiled `aboot.img @ 0xaa00b16c - 0xaa00b1ac`:
     - `OPCODE = 7`
     - `ARGUMENT = 0x00020000` (`dev->rca << 16`)
     - For eMMC (`card_type > 1`), ABOOT explicitly sets `resp_type = 1` (`MMC_RESP_R1`), mapping to `SDHCI_COMMAND = 0x071A` (`RESP_48 | CRC | INDEX`).
     - NO busy wait (`CMD7_RSP_BUSY_EXPECTED = no`, `CMD7_DAT0_WAIT_REQUIRED = no`).
3. **Physical Silicon Execution (Sony Xperia XZs Tone Keyaki / G8231)**:
   - Fresh initialization -> CMD0 -> CMD1 polling (`CARD_READY = yes`) -> CMD2 (`CID_MATCH = yes`) -> CMD3 (`RCA = 2`, zero errors) -> CMD9 (`CSD_MATCH = yes`).
   - Issued exactly ONE CMD7 (`COMMAND = 0x071A`, `ARG = 0x00020000`).
   - `COMMAND_COMPLETE` observed with zero SDHCI errors (`CMD7_ERROR_BITS = 0x00000000`).
4. **R1 Response Validation**:
   - Captured `CMD7_R1_RAW = 0x00000700`.
   - All 15 named reject bits are zero: `CMD7_R1_REJECT_BITS = 0x00000000`.
   - `READY_FOR_DATA = 1` (bit 8).
   - `CURRENT_STATE = 3` (`STBY`): Per JEDEC JESD84-B51 Section 6.13, card reflects the state when the command was received (`stby` = 3), transitioning to `tran` (4) after R1 transmission.
5. **Card Selection Confirmed**:
   - `CARD_SELECTION_CONFIRMED = yes`.
   - `TRANSFER_STATE_CONFIRMED = not_directly_observed` (following ABOOT behavior without unneeded CMD13; operational proof in CMD8).
6. **Hard Stop Enforced**:
   - Exactly ONE CMD7 issued. Zero CMD8, zero CMD13, zero data transfers, zero writes, zero power-cycles.

### B. Live Hardware Verification Telemetry (Breadcrumbs CP = 0xD390)

```text
[BREADCRUMB] CP=0x000000000000d390 ERR=0x0000000000000000 (Enter Phase D2-M4D-D)
[BREADCRUMB] CP=0x000000000000d390 ERR=0x0000000000000010 (Git Baseline Logged)
[BREADCRUMB] CP=0x000000000000d390 ERR=0x0000000000000020 (Vendor-Reg Audit Logged)
[BREADCRUMB] CP=0x000000000000d390 ERR=0x0000000000000030 (ABOOT CMD7 Audit Logged)
[BREADCRUMB] CP=0x000000000000d390 ERR=0x0000000000000031 (CMD7 Encoding Frozen: 0x071A)
[BREADCRUMB] CP=0x000000000000d390 ERR=0x0000000000000040 (Fresh Initialization Replayed)
[BREADCRUMB] CP=0x000000000000d390 ERR=0x0000000000000021 (Vendor Register Gate PASS)
[BREADCRUMB] CP=0x000000000000d390 ERR=0x0000000000000041 (CARD_READY: yes, FINAL_OCR: 0xC0FF8080)
[BREADCRUMB] CP=0x000000000000d390 ERR=0x0000000000000042 (CID_MATCH: yes)
[BREADCRUMB] CP=0x000000000000d390 ERR=0x0000000000000043 (RCA_ASSIGNED: 2)
[BREADCRUMB] CP=0x000000000000d390 ERR=0x0000000000000044 (CSD_MATCH: yes)
[BREADCRUMB] CP=0x000000000000d390 ERR=0x0000000000000050 (CMD7 Prepared)
[BREADCRUMB] CP=0x000000000000d390 ERR=0x0000000000000051 (CMD7 Written: 0x071A, ARG: 0x00020000)
[BREADCRUMB] CP=0x000000000000d390 ERR=0x0000000000000052 (COMMAND_COMPLETE Observed)
[BREADCRUMB] CP=0x000000000000d390 ERR=0x0000000000000053 (Zero Errors: CMD7_ERROR_BITS = 0)
[BREADCRUMB] CP=0x000000000000d390 ERR=0x0000000000000060 (R1 Captured: 0x00000700)
[BREADCRUMB] CP=0x000000000000d390 ERR=0x0000000000000061 (Zero R1 Reject Bits: 0x00000000)
[BREADCRUMB] CP=0x000000000000d390 ERR=0x0000000000000062 (CARD_SELECTION_CONFIRMED: yes)
[BREADCRUMB] CP=0x000000000000d390 ERR=0x0000000000000070 (Transfer-State Evidence Logged)
[BREADCRUMB] CP=0x000000000000d390 ERR=0x0000000000000080 (Final Snapshot Captured)
[BREADCRUMB] CP=0x000000000000d390 ERR=0x0000000000000090 (Cleanup Complete)
[BREADCRUMB] CP=0x000000000000d390 ERR=0x0000000000000001 (Terminal State -> Warm Reset to Fastboot)
```

---

## 20. Phase D2-M4D-D Status & Next Step

```text
D2-M4D-D STATUS: COMPLETE (HARDWARE TELEMETRY CAPTURED)
LIVE_HC_VENDOR_SPEC: 0x00000A1C
LIVE_HC_MODE: 0x00000001
LIVE_TIMEOUT_CONTROL: 0x0F
CMD7_ARGUMENT: 0x00020000
CMD7_COMMAND: 0x071A
CMD7_INT_STATUS: 0x00000001
CMD7_ERROR_BITS: 0x00000000
CMD7_R1_RAW: 0x00000700
CMD7_R1_REJECT_BITS: 0x00000000
CMD7_R1_READY_FOR_DATA: yes (1)
CMD7_R1_CURRENT_STATE: 3 (STBY)
CARD_SELECTION_CONFIRMED: yes
TRANSFER_STATE_CONFIRMED: not_directly_observed
CMD8_ISSUED: no
```

- **Next Phase (D2-M4E):** eMMC Extended CSD (CMD8 / `SEND_EXT_CSD`) — Stream 512-byte `EXT_CSD` block over DAT lines to decode true user-area sector capacity, supported bus widths, and high-speed timing modes.

---

## 21. Phase D2-M4E: First Physical eMMC Data Transfer — CMD8 / SEND_EXT_CSD

### A. Objectives & Accomplishments
1. **Evidence Classification & Transport Choice**:
   - Audited stock Sony ABOOT LittleKernel `mmc_get_ext_csd`: uses ADMA (`0x0011`).
   - XZS Phase D2-M4E intentional deviation: standard SDHCI Buffer Port PIO (`TRANSFER_MODE = 0x0010`, DMA disabled).
   - Classified: `CMD8_PROTOCOL_SOURCE_PROVEN = yes`, `XZS_M4E_INTENTIONAL_DMA_DEVIATION = yes`, `PIO_PATH_SDHC_STANDARD_BASED = yes`.
2. **Transfer Configuration Frozen**:
   - `BLOCK_SIZE = 0x0200` (512 bytes), `BLOCK_COUNT = 0x0001` (1 block).
   - `ARGUMENT = 0x00000000`, `TRANSFER_MODE = 0x0010`, `COMMAND = 0x083A`.
   - `SDHCI_INT_ENABLE = 0xFFFF8023`, `SDHCI_SIGNAL_ENABLE = 0x00000000`.
3. **W1C-Safe Event Handling & State Machine**:
   - Successfully avoided destructive status clearing; cleared only consumed events.
   - Decomposed coalesced interrupts cleanly:
     - `COMMAND_COMPLETE` (`0x0001`): captured R1 response (`0x00000900`), W1C cleared `0x0001`.
     - `BUFFER_READ_READY` (`0x0020`): drained exactly 128 $\times$ 32-bit words (512 bytes) from `SDHCI_BUFFER` (`0x20`), W1C cleared `0x0020`.
     - `TRANSFER_COMPLETE` (`0x0002`): verified transaction end, W1C cleared `0x0002`.
   - Zero command errors (`0x00000000`), zero data errors (`0x00000000`), zero timeouts.
4. **EXT_CSD Payload & Geometry Verified**:
   - Captured complete 512-byte raw EXT_CSD (`artifacts/builds/ext_csd.bin`, SHA-256 `e9fac06592092cc4f12ee2705c4fcfb9a839e4a9cce539a02a2208f8cc2ea29c`).
   - `EXT_CSD_REV = 0x08` (eMMC 5.1 compliant).
   - `SEC_COUNT = 0x03A3E000` (61,071,360 sectors / 31,268,536,320 bytes / 29.12109375 GiB user capacity).
   - `BOOT_SIZE_MULT = 0x20` (4 MiB via JEDEC).
   - `RPMB_SIZE_MULT = 0x20` (4 MiB via JEDEC).
   - `CARD_TYPE = 0x57` (HS200, HS400, DDR52, HS52).
   - `EXT_CSD_GEOMETRY_MATCH = yes`.
5. **Operational Confirmation of Transfer State**:
   - `FIRST_PHYSICAL_DATA_TRANSFER_CONFIRMED = yes`.
   - `TRANSFER_DATA_PATH_CONFIRMED = yes`.
   - `TRANSFER_STATE_OPERATIONALLY_CONFIRMED = yes` (R1 returned by CMD8 directly reported `CURRENT_STATE = 4` / TRAN and `READY_FOR_DATA = 1`).
6. **Hard Stop Enforced**:
   - Exactly ONE CMD8 executed. Zero CMD6, zero CMD13, zero CMD17, zero storage writes, zero power cycles.

### B. Live Hardware Verification Telemetry (Breadcrumbs CP = 0xD3A0)

```text
[BREADCRUMB] CP=0x000000000000d3a0 ERR=0x0000000000000000 (Enter Phase D2-M4E)
[BREADCRUMB] CP=0x000000000000d3a0 ERR=0x0000000000000010 (Git Baseline Logged)
[BREADCRUMB] CP=0x000000000000d3a0 ERR=0x0000000000000020 (Evidence Classification Logged)
[BREADCRUMB] CP=0x000000000000d3a0 ERR=0x0000000000000021 (ABOOT CMD8 Audit Logged)
[BREADCRUMB] CP=0x000000000000d3a0 ERR=0x0000000000000030 (Fresh Hardware Initialization Replayed)
[BREADCRUMB] CP=0x000000000000d3a0 ERR=0x0000000000000031 (CARD_READY: yes, FINAL_OCR: 0xC0FF8080)
[BREADCRUMB] CP=0x000000000000d3a0 ERR=0x0000000000000032 (CID_MATCH: yes)
[BREADCRUMB] CP=0x000000000000d3a0 ERR=0x0000000000000033 (ASSIGNED_RCA: 2)
[BREADCRUMB] CP=0x000000000000d3a0 ERR=0x0000000000000034 (CSD_MATCH: yes)
[BREADCRUMB] CP=0x000000000000d3a0 ERR=0x0000000000000035 (CARD_SELECTION_CONFIRMED: yes)
[BREADCRUMB] CP=0x000000000000d3a0 ERR=0x0000000000000040 (CMD8 Registers Programmed)
[BREADCRUMB] CP=0x000000000000d3a0 ERR=0x0000000000000041 (CMD8 Issued: 0x083A)
[BREADCRUMB] CP=0x000000000000d3a0 ERR=0x0000000000000042 (COMMAND_COMPLETE Observed & W1C Cleared)
[BREADCRUMB] CP=0x000000000000d3a0 ERR=0x0000000000000050 (BUFFER_READ_READY Observed)
[BREADCRUMB] CP=0x000000000000d3a0 ERR=0x0000000000000051 (PIO Read Begin)
[BREADCRUMB] CP=0x000000000000d3a0 ERR=0x0000000000000052 (512 Bytes Captured from SDHCI_BUFFER)
[BREADCRUMB] CP=0x000000000000d3a0 ERR=0x0000000000000053 (TRANSFER_COMPLETE Observed & W1C Cleared)
[BREADCRUMB] CP=0x000000000000d3a0 ERR=0x0000000000000060 (EXT_CSD Raw Preserved)
[BREADCRUMB] CP=0x000000000000d3a0 ERR=0x0000000000000061 (EXT_CSD_REV Valid: 0x08)
[BREADCRUMB] CP=0x000000000000d3a0 ERR=0x0000000000000062 (SEC_COUNT Valid: 61,071,360)
[BREADCRUMB] CP=0x000000000000d3a0 ERR=0x0000000000000063 (EXT_CSD Geometry Match: PASS)
[BREADCRUMB] CP=0x000000000000d3a0 ERR=0x0000000000000070 (FIRST_PHYSICAL_DATA_TRANSFER_CONFIRMED: yes)
[BREADCRUMB] CP=0x000000000000d3a0 ERR=0x0000000000000080 (Final Controller Snapshot Captured)
[BREADCRUMB] CP=0x000000000000d3a0 ERR=0x0000000000000090 (Cleanup Complete)
[BREADCRUMB] CP=0x000000000000d3a0 ERR=0x0000000000000001 (Terminal State -> Warm Reset to Fastboot)
```

---

## 22. Phase D2-M4E Status & Deliverables

```text
D2-M4E STATUS: COMPLETE (PASS)
ABOOT_DATA_PATH=ADMA
XZS_M4E_DATA_PATH=PIO
CMD8_BLOCK_SIZE=0x0200
CMD8_BLOCK_COUNT=0x0001
CMD8_TRANSFER_MODE=0x0010
CMD8_COMMAND=0x083A
CMD8_INT_ENABLE=0xFFFF8023
CMD8_SIGNAL_ENABLE=0x00000000
CMD_COMPLETE_SEEN=yes
BUFFER_READ_READY_SEEN=yes
TRANSFER_COMPLETE_SEEN=yes
CMD8_R1_RAW=0x00000900
CMD8_R1_REJECT_BITS=0x00000000
CMD8_COMMAND_ERROR_BITS=0x00000000
CMD8_DATA_ERROR_BITS=0x00000000
CMD8_ALL_ERROR_BITS=0x00000000
PIO_READ_WIDTH=32
PIO_WORDS_READ=128
PIO_BYTES_READ=512
EXT_CSD_REV=0x08
SEC_COUNT=61071360
BOOT_SIZE_MULT=0x20
RPMB_SIZE_MULT=0x20
FIRST_PHYSICAL_DATA_TRANSFER_CONFIRMED=yes
TRANSFER_DATA_PATH_CONFIRMED=yes
TRANSFER_STATE_OPERATIONALLY_CONFIRMED=yes
CMD17_ISSUED=no
```

- **Report:** [D2_M4E_EXT_CSD_REPORT.md](file:///Users/lechaukha12/Desktop/xnu-xzs/artifacts/reports/D2_M4E_EXT_CSD_REPORT.md)
- **Binary Artifact:** `artifacts/builds/ext_csd.bin` (512 bytes, SHA-256: `e9fac06592092cc4f12ee2705c4fcfb9a839e4a9cce539a02a2208f8cc2ea29c`)
- **SDHCI Implementation:** [xzs_sdhci.c](file:///Users/lechaukha12/Desktop/xnu-xzs/src/xnu/pexpert/arm/xzs_sdhci.c) & [xzs_sdhci.h](file:///Users/lechaukha12/Desktop/xnu-xzs/src/xnu/pexpert/pexpert/arm/xzs_sdhci.h)

---

## 23. Phase D2-M5: Final D2 Acceptance — Physical eMMC Block Read (CMD17 / LBA 1)

### A. Objectives & Accomplishments
1. **TWRP Independent Oracle Captured First**:
   - Host-side capture directly from physical disk: `adb exec-out "dd if=/dev/block/mmcblk0 bs=512 skip=1 count=1 2>/dev/null" > artifacts/oracles/mmcblk0_lba1.bin`.
   - Size: 512 bytes.
   - SHA-256: `e4b891b42fd57eb352ffbe0aa9098d04fe85f88e3425dcba529064cce72f862a`.
   - GPT signature verified at offset 0: `45 46 49 20 50 41 52 54` (`"EFI PART"`).
2. **Stock Sony ABOOT LittleKernel Audit**:
   - Reverse-engineered `aboot.img` (`mmc_read @ 0xaa013bc8` and `sdhci_send_command @ 0xaa0106a8`):
     - Single block read uses `CMD17` (`0x11` / `MMC_CMD_READ_SINGLE_BLOCK`).
     - Sector addressing rule: for high-capacity eMMC cards (`type != 4`), argument is sector index directly (no multiplication by 512).
     - `CMD16` (`SET_BLOCKLEN`) is NOT issued before `CMD17`.
     - Stock command flags: `0x113A` (`(17 << 8) | RESP_48 | CRC | INDEX | DATA`).
     - Data path: ADMA2 (`TRANSFER_MODE = 0x0011`).
3. **High-Capacity Addressing Semantics Proven**:
   - Card OCR bit 30 = 1 (`FINAL_OCR = 0xC0FF8080`, Sector Mode).
   - `SEC_COUNT = 61071360` (> 2 GiB).
   - Target sector LBA = 1 $\implies$ `CMD17_ARGUMENT = 0x00000001` (NOT byte addressing `0x00000200`).
4. **User-Area Selection Confirmed**:
   - `PARTITION_CONFIG = 0x00` (`PARTITION_ACCESS = 0` / User Data Area).
   - Zero partition switch commands (`CMD6`). Targets `/dev/block/mmcblk0` user data area matching TWRP.
5. **Standard SDHCI PIO Transport Engine Reused**:
   - Reused the hardware-proven PIO engine from M4E:
     - `BLOCK_SIZE = 0x0200` (512 bytes).
     - `BLOCK_COUNT = 0x0001` (1 sector).
     - `TRANSFER_MODE = 0x0010` (PIO READ, DMA disabled).
     - `COMMAND = 0x113A`.
     - `INT_ENABLE = 0xFFFF8023`, `SIGNAL_ENABLE = 0`.
     - Non-destructive W1C event handling.
6. **Fresh Initialization & Prerequisite Replay**:
   - Complete initialization sequence replayed: 400-kHz RCG -> Reset -> Vendor Regs -> CMD0 -> CMD1 (`CARD_READY=yes`) -> CMD2 (`CID_MATCH=yes`) -> CMD3 (`RCA=2`) -> CMD9 (`CSD_MATCH=yes`) -> CMD7 (`selected=yes`) -> CMD8 (`EXT_CSD_GEOMETRY_MATCH=yes`, `TRAN` observed: `R1 = 0x00000900`).
   - Pre-CMD17 state: `PRESENT_STATE = 0x01f80000`, `INT_STATUS = 0x00000000`.
7. **Silicon Execution of CMD17**:
   - Exactly ONE `CMD17` issued to silicon.
   - Response: `CMD17_R1_RAW = 0x00000900` (`READY_FOR_DATA = 1`, `CURRENT_STATE = 4` / TRAN).
   - Reject bits: `0x00000000`, Command error bits: `0x00000000`, Data error bits: `0x00000000`.
   - Drained exactly 128 words (512 bytes) from `SDHCI_BUFFER` into dedicated 64-byte aligned buffer `g_xzs_lba1`.
   - Extracted binary: `artifacts/builds/xnu_lba1.bin` (512 bytes).
8. **Byte-for-Byte Comparison with TWRP Oracle**:
   - `cmp -l artifacts/oracles/mmcblk0_lba1.bin artifacts/builds/xnu_lba1.bin` returned exit code 0 (zero byte differences).
   - SHA-256 TWRP: `e4b891b42fd57eb352ffbe0aa9098d04fe85f88e3425dcba529064cce72f862a`.
   - SHA-256 XNU:  `e4b891b42fd57eb352ffbe0aa9098d04fe85f88e3425dcba529064cce72f862a`.
   - `BYTE_FOR_BYTE_MATCH = yes` (100% exact equality).
9. **Hard Stop Enforced**:
   - Zero CMD18, zero CMD24/25, zero writes, zero partition switches, zero clock escalation.

### B. Live Hardware Verification Telemetry (Breadcrumbs CP = 0xD3B0)

```text
[BREADCRUMB] CP=0x000000000000d3b0 ERR=0x0000000000000000 (Enter Phase D2-M5)
[BREADCRUMB] CP=0x000000000000d3b0 ERR=0x0000000000000010 (Git Baseline Logged)
[BREADCRUMB] CP=0x000000000000d3b0 ERR=0x0000000000000020 (CMD17 ABOOT & Addressing Audit Logged)
[BREADCRUMB] CP=0x000000000000d3b0 ERR=0x0000000000000030 (Fresh Hardware Initialization Replayed)
[BREADCRUMB] CP=0x000000000000d3b0 ERR=0x0000000000000031 (CARD_READY: yes, FINAL_OCR: 0xC0FF8080)
[BREADCRUMB] CP=0x000000000000d3b0 ERR=0x0000000000000032 (CID_MATCH: yes)
[BREADCRUMB] CP=0x000000000000d3b0 ERR=0x0000000000000033 (ASSIGNED_RCA: 2)
[BREADCRUMB] CP=0x000000000000d3b0 ERR=0x0000000000000034 (CSD_MATCH: yes)
[BREADCRUMB] CP=0x000000000000d3b0 ERR=0x0000000000000035 (CARD_SELECTION_CONFIRMED: yes)
[BREADCRUMB] CP=0x000000000000d3b0 ERR=0x0000000000000036 (EXT_CSD / TRAN State PASS: R1=0x00000900)
[BREADCRUMB] CP=0x000000000000d3b0 ERR=0x0000000000000040 (CMD17 Registers Programmed)
[BREADCRUMB] CP=0x000000000000d3b0 ERR=0x0000000000000041 (CMD17 Issued: 0x113A, ARG: 0x00000001)
[BREADCRUMB] CP=0x000000000000d3b0 ERR=0x0000000000000042 (COMMAND_COMPLETE Observed & W1C Cleared)
[BREADCRUMB] CP=0x000000000000d3b0 ERR=0x0000000000000050 (BUFFER_READ_READY Observed)
[BREADCRUMB] CP=0x000000000000d3b0 ERR=0x0000000000000051 (PIO Read Begin)
[BREADCRUMB] CP=0x000000000000d3b0 ERR=0x0000000000000052 (512 Bytes Captured from SDHCI_BUFFER)
[BREADCRUMB] CP=0x000000000000d3b0 ERR=0x0000000000000053 (TRANSFER_COMPLETE Observed & W1C Cleared)
[BREADCRUMB] CP=0x000000000000d3b0 ERR=0x0000000000000060 (LBA1 Raw Preserved: 512 bytes)
[BREADCRUMB] CP=0x000000000000d3b0 ERR=0x0000000000000061 (XNU SHA256 Produced)
[BREADCRUMB] CP=0x000000000000d3b0 ERR=0x0000000000000070 (Oracle Comparison Executed)
[BREADCRUMB] CP=0x000000000000d3b0 ERR=0x0000000000000071 (BYTE_FOR_BYTE_MATCH: yes)
[BREADCRUMB] CP=0x000000000000d3b0 ERR=0x0000000000000080 (D2_STORAGE_COMPLETE: yes)
[BREADCRUMB] CP=0x000000000000d3b0 ERR=0x0000000000000090 (Cleanup Complete)
[BREADCRUMB] CP=0x000000000000d3b0 ERR=0x0000000000000001 (Terminal State -> Warm Reset to Fastboot)
```

---

## 24. Phase D2 Final Acceptance & Milestone Closure

```text
PHYSICAL_BLOCK_READ_VERIFIED:        yes
LBA_ADDRESSING_VERIFIED:             yes
USER_AREA_READ_VERIFIED:             yes
CMD17_READ_SINGLE_BLOCK_VERIFIED:    yes

D2_STORAGE_COMPLETE:                 yes
```

Phase `D2` (Physical eMMC Storage Bringup) is **OFFICIALLY COMPLETE AND SEALED**.

### Deliverables
- **Phase D2-M5 Report:** [D2_M5_BLOCK_READ_REPORT.md](file:///Users/lechaukha12/Desktop/xnu-xzs/artifacts/reports/D2_M5_BLOCK_READ_REPORT.md)
- **TWRP LBA1 Oracle:** `artifacts/oracles/mmcblk0_lba1.bin` (512 bytes, SHA-256: `e4b891b42fd57eb352ffbe0aa9098d04fe85f88e3425dcba529064cce72f862a`)
- **XNU LBA1 Physical Read:** `artifacts/builds/xnu_lba1.bin` (512 bytes, SHA-256: `e4b891b42fd57eb352ffbe0aa9098d04fe85f88e3425dcba529064cce72f862a`)
- **SDHCI Implementation:** [xzs_sdhci.c](file:///Users/lechaukha12/Desktop/xnu-xzs/src/xnu/pexpert/arm/xzs_sdhci.c) & [xzs_sdhci.h](file:///Users/lechaukha12/Desktop/xnu-xzs/src/xnu/pexpert/pexpert/arm/xzs_sdhci.h)

---

## 25. Pre-D3 Freeze & Integration Checkpoint

### A. Repository Integration Baseline
```text
D2 FINAL CHECKPOINT

Pre-merge xzs-bringup:  421b884a7724a6236713949a4db4562b6ee8aedb
Integrated main merge:  ca127a41295c5a40bc23a8fc4a243f84c7364dc4
Milestone Tag:          xzs-d2-storage-complete
```

### B. Hardware Verification & Byte Equality
```text
PHYSICAL_BLOCK_READ_VERIFIED=yes
LBA_ADDRESSING_VERIFIED=yes
USER_AREA_READ_VERIFIED=yes
CMD17_READ_SINGLE_BLOCK_VERIFIED=yes

D2_STORAGE_COMPLETE=yes

TWRP LBA1 SHA256:  e4b891b42fd57eb352ffbe0aa9098d04fe85f88e3425dcba529064cce72f862a
XNU LBA1 SHA256:   e4b891b42fd57eb352ffbe0aa9098d04fe85f88e3425dcba529064cce72f862a
cmp exit code:     0 (100% exact 512/512 byte match)
```

### C. Strict Architectural Boundary
> [!IMPORTANT]
> The EFI PART GPT signature (`45 46 49 20 50 41 52 54`) was observed at LBA 1 byte offsets 0..7 as expected on a GUID-partitioned eMMC disk. However, in accordance with strict phase discipline, **zero GPT parsing, zero CRC32 verification, and zero partition enumeration have occurred in Phase D2**. All GPT discovery logic belongs exclusively to Phase D3.
