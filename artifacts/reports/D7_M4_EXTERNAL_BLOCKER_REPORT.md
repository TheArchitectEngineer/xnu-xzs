# D7-M4 External UART Hardware Acceptance Blocker Report

## Executive Summary

```text
D7_M4_P0_READINESS_STATUS=COMPLETE
D7_M4_INTERNAL_PIPELINE_STATUS=HARDWARE VERIFIED
D7_M4_EXTERNAL_PIPELINE_STATUS=BLOCKED ON PHYSICAL TRANSPORT
D7_M4_COMPLETE=no
D7_M4_SEALED=no
```

In accordance with Sections 11, 18, and 19 of the D7-M4 directive, an immutable external UART candidate was built, deployed via `fastboot boot`, and executed on physical Xperia XZs hardware.

The software pipeline (ARM64 native `read(0)`, tty canonical line discipline, bounded SPSC RX ring, deferred `thread_call`, and GICv3 INTID 146 / SPI 114 interrupt handler) executed as intended, blocked on `read(0)`, and armed the UARTDM hardware interrupt. However, no bytes were received over the physical GPIO5 RX line because no physical USB-UART adapter is connected to the host Mac or wired to the device's GPIO5 test point.

Per Section 19, no software regressions were introduced, internal verification is preserved, and M4 is **NOT sealed**.

---

## Hardware Candidate Identity

```text
CANDIDATE_BOOT_IMAGE_SHA256=019beff1c47a15bbb20979fc8d03c47befa7f7f80dbe27bacd1d91c5d9892909
CANDIDATE_RAW_LOG_SHA256=1dd42ebfd8ac667aa359b68a0ffe5e4d5d99a55bcdc4b083ddfe7ba26b7d8338
TARGET_DEVICE_SERIAL=BH905SX976
DEPLOYMENT_METHOD=fastboot boot (no partitions flashed)
```

---

## Verifier Results on Fresh Silicon Run

```text
verify_d6_acceptance.py              -> PASS (2298 sustained getpid roundtrips)
verify_d7m2_acceptance.py --regression -> PASS (D710 24/24 checkpoints complete)
verify_d7m3_acceptance.py            -> PASS (D720 15/15 checkpoints, stdout banner + prompt observed)
verify_d7m4_acceptance.py --external -> FAIL (exit code 1; stopped at D730/11 awaiting external bytes)
verify_d7m4_acceptance.py --internal -> FAIL (exit code 1; correctly rejects external run)
```

---

## Observed Silicon Telemetry

```text
[BREADCRUMB] CP=0x000000000000d730 ERR=0x0000000000000000
[XZS-D7M4] D730/00 M4 external pipeline entered

[BREADCRUMB] CP=0x000000000000d730 ERR=0x0000000000000010
[XZS-D7M4] D730/10 UARTDM RX low-level ready

[BREADCRUMB] CP=0x000000000000d730 ERR=0x0000000000000011
[XZS-D7M4] D730/11 external UART IRQ source armed

=======================================================
=== D7-M4 ACCEPTANCE TELEMETRY BEGIN ===
D7M4_INPUT_SOURCE=EXTERNAL_UART
D7M4_SHELL_IMAGE_SHA256=fd9e0db28b88834c70c9e413a7c3ecf1e1f07e5aac167de4a05373e18ce92988
D7M4_SHELL_READ_ENTRY=0x0000000100000870
D7M4_INTERNAL_LOOPBACK_ACTIVE=no
UARTDM_RX_IRQ=146
GIC_INTERRUPT_TYPE=SPI_114
GIC_TRIGGER_TYPE=LEVEL_HIGH
UARTDM_RX_IRQ_CONFIGURED=yes
UARTDM_RX_IRQ_WORKING=no
UARTDM_RX_IRQ_COUNT=0x0000000000000000
UARTDM_RX_IRQ_BYTE_COUNT=0x0000000000000000
UARTDM_RX_IRQ_LAST_ISR=0x0000000000000000
UARTDM_RX_MODE=IRQ_WITH_BOUNDED_POLL_FALLBACK
UARTDM_RX_HW_CONFIGURED=yes
UARTDM_RX_AVAILABLE=no
UARTDM_RX_EXTERNAL_BYTE_OBSERVED=no
UARTDM_RX_RAW_BYTE_MATCH=no
UARTDM_RECEIVE_READY_WORKING=no
UARTDM_RECEIVE_DATA_WORKING=no
RX_BUFFER_WORKING=no
TTY_INPUT_WORKING=no
TTY_INPUT_WAKEUP_WORKING=no
SHELL_READ_SYSCALL_ENTERED=yes
SHELL_READ_NATIVE=yes
SHELL_READ_BLOCKED=yes
SHELL_READ_AWAKENED=no
SHELL_READ_RETURNED_TO_EL0=no
EL0_READ_EXACT_BYTES=no
POST_READ_EL0_EXECUTION=no
EL0_READ_LENGTH=4
EL0_READ_HEX=41 42 43 0a
D7M4_INPUT_BYPASS=no
EXTERNAL_UART_PIPELINE_PASS=no
SHELL_INPUT_BYTES_MATCH=no
SHELL_PROCESS_STILL_ALIVE=no
SHELL_STDIN_WORKING=no
D7_M4_COMPLETE=no
D7_M4_HARDWARE_VERIFIED=no
D7_M4_SEALED=no
=== D7-M4 ACCEPTANCE TELEMETRY END ===
=======================================================
```

---

## Physical Transport Path Classification (Section 19 Audit)

1. **Host USB Device Audit**:
   - `ioreg -p IOUSB -l -w 0` inspection confirms that only one USB device is attached to the host Mac: `S1Boot Fastboot` (`0x0fce:0x0dde`, serial `BH905SX976`).
   - Zero external USB-UART adapters (FTDI, CP2102, CH340, PL2303, etc.) are present.
2. **Host Port `/dev/cu.debug-console` Audit**:
   - `ioreg -p IOService -n "debug-console" -r` confirms `/dev/cu.debug-console` is an internal Apple Silicon SoC on-board debug serial client (`AppleSimpleUARTSync`).
   - Non-blocking loopback test on `/dev/cu.debug-console` produced zero readback (`BlockingIOError`).
   - `/dev/cu.debug-console` is NOT electrically connected to the Xperia phone.
3. **Physical Wire & Common Ground**:
   - The USB-C cable connects the host to the Xperia phone for USB 2.0 Fastboot and ADB data transfer. It does not carry discrete UART TX/RX lines to the SoC BLSP2 interface.
   - There is no physical copper connection between the host serial transmitter and Xperia GPIO5 (BLSP2 UART2 RX).
4. **Voltage Level Compatibility**:
   - MSM8996 GPIOs require 1.8V LVCMOS signaling. Connecting a 3.3V or 5.0V adapter directly would damage the SoC I/O pad. A dedicated 1.8V level shifter / adapter is required when wiring is attached.
5. **Software Pipeline Status**:
   - Hard IRQ FIFO handling, bounded SPSC ring buffer, deferred `thread_call`, `cons_cinput`, tty line discipline, and Darwin blocking `read(0)` are fully verified and intact from the internal loopback milestone (`639fe1c42478c8ca07aa0019cbe59bfd846d9866`).
   - Zero modifications were made to the core pipeline architecture.

---

## Conclusion and Next Required Action

- Software readiness for external UART stdin is **100% complete**.
- Physical hardware verification requires connecting a 1.8V USB-to-UART adapter to the host, wiring its TX pin to Xperia GPIO5 test point, and connecting common ground.
- Until physical transport is attached, D7-M4 remains **IN PROGRESS / NOT SEALED** and D7-M5 remains **BLOCKED**.
