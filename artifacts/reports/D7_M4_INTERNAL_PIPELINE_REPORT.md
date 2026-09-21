# D7-M4 Internal UART RX Pipeline Report

## Classification

```text
D7_M4_INTERNAL_PIPELINE=HARDWARE VERIFIED
D7_M4_EXTERNAL_PIPELINE=NOT VERIFIED
D7_M4_COMPLETE=no
D7_M4_SEALED=no
```

Internal loopback is used only as the deterministic byte source. It does not
satisfy the external physical RX acceptance gate.

## Immutable tested identity

```text
SOURCE_COMMIT=639fe1c42478c8ca07aa0019cbe59bfd846d9866
KERNEL_SHA256=3cc027c7057a7ba9aec1fc0f7e94c7a9d6a7fc1ecc78f81bb299253be05bee75
ROOTFS_SHA256=0d14a1dfa3726abb85ddbbc10f2aeb111fd632be42df9d2ba9a728e94eace264
BOOT_IMAGE_SHA256=329f869a55e7185bda99d14f38575d13c693298b04c7fa0b87f5d3439b34044a
RAW_CONSOLE_LOG_SHA256=49422168deee99ad9c8fa3b5d07adb70edf6c367b185174e44cb8e3ff769d469
```

The target was deployed only with `fastboot boot`. No partition was flashed.

## Hardware and software path proven

```text
UARTDM internal TX loopback
  -> UARTDM RX FIFO / RXSTALE
  -> GICv3 SPI 114 (architectural INTID 146, level-high, CPU0)
  -> hard-IRQ FIFO drain
  -> bounded SPSC RX ring
  -> deferred XNU thread_call
  -> cons_cinput
  -> tty canonical line discipline
  -> native tty wakeup
  -> /dev/console fd0 read(0)
  -> exact 41 42 43 0a in EL0
  -> post-read EL0 getpid execution
```

The first IRQ candidate correctly exposed that `cons_cinput()` cannot be called
while hard-IRQ preemption is disabled because it takes the tty mutex. The
minimal correction kept FIFO/ring work in hard IRQ and moved tty delivery to a
deferred thread call. The corrected silicon run recorded one UART IRQ and four
bytes delivered.

## Acceptance telemetry

```text
UARTDM_RX_IRQ=146
GIC_INTERRUPT_TYPE=SPI_114
GIC_TRIGGER_TYPE=LEVEL_HIGH
UARTDM_RX_IRQ_CONFIGURED=yes
UARTDM_RX_IRQ_WORKING=yes
UARTDM_RX_IRQ_COUNT=0x0000000000000001
UARTDM_RX_IRQ_BYTE_COUNT=0x0000000000000004

RX_BUFFER_WORKING=yes
TTY_INPUT_WORKING=yes
TTY_INPUT_WAKEUP_WORKING=yes

SHELL_READ_SYSCALL_ENTERED=yes
SHELL_READ_NATIVE=yes
SHELL_READ_BLOCKED=yes
SHELL_READ_AWAKENED=yes
SHELL_READ_RETURNED_TO_EL0=yes

EL0_READ_HEX=41 42 43 0a
EL0_READ_EXACT_BYTES=yes
POST_READ_EL0_EXECUTION=yes
D7M4_INPUT_BYPASS=no
D7M4_INTERNAL_PIPELINE_COMPLETE=yes
```

## Independent verification

```text
D6_REGRESSION_VERIFIER=PASS
D7_M2_REGRESSION_VERIFIER=PASS
D7_M3_ACCEPTANCE_VERIFIER=PASS
D7M4_INTERNAL_PIPELINE_VERIFIER=PASS
```

The D7-M4 verifier requires explicit internal or external mode. External mode
cannot accept internal-loopback evidence. Six verifier unit tests cover the
positive internal contract and negative missing-tty, missing-wakeup, wrong-byte,
zero-IRQ, and internal-as-external cases.

## Remaining final gate

Send `ABC\n` (`41 42 43 0a`) from the host USB-UART at 115200 8N1 through the
physical Xperia RX pin. The same common IRQ/ring/tty/read path must produce an
external-source telemetry block and pass `verify_d7m4_acceptance.py --external`.
Only then may D7-M4 be marked complete or sealed.
