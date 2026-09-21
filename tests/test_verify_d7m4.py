#!/usr/bin/env python3
"""Negative tests for the D7-M4 internal/external acceptance boundary."""

import importlib.util
import pathlib
import unittest


ROOT = pathlib.Path(__file__).resolve().parents[1]
SPEC = importlib.util.spec_from_file_location(
    "verify_d7m4", ROOT / "scripts" / "verify_d7m4_acceptance.py"
)
VERIFY = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(VERIFY)


def internal_log(**overrides):
    telemetry = dict(VERIFY.INTERNAL_REQUIRED)
    telemetry.update({"UARTDM_RX_IRQ_COUNT": "0x1", "UARTDM_RX_IRQ_BYTE_COUNT": "0x4"})
    telemetry.update(overrides)
    crumbs = "\n".join(
        f"CP=0x0000d730, ERR=0x{checkpoint:02x}"
        for checkpoint in VERIFY.INTERNAL_SEQUENCE
    )
    fields = "\n".join(f"{key}={value}" for key, value in telemetry.items())
    return (
        f"{crumbs}\n"
        "=== D7-M4 INTERNAL ACCEPTANCE TELEMETRY BEGIN ===\n"
        f"{fields}\n"
        "=== D7-M4 INTERNAL ACCEPTANCE TELEMETRY END ===\n"
    )


class D7M4VerifierTests(unittest.TestCase):
    def test_internal_positive(self):
        self.assertTrue(VERIFY.validate_mode(internal_log(), "internal")[0])

    def test_uart_without_tty_fails(self):
        passed, _ = VERIFY.validate_mode(
            internal_log(TTY_INPUT_WORKING="no"), "internal"
        )
        self.assertFalse(passed)

    def test_tty_without_read_wakeup_fails(self):
        passed, _ = VERIFY.validate_mode(
            internal_log(SHELL_READ_AWAKENED="no"), "internal"
        )
        self.assertFalse(passed)

    def test_wrong_el0_bytes_fail(self):
        passed, _ = VERIFY.validate_mode(
            internal_log(EL0_READ_HEX="41 42 44 0a"), "internal"
        )
        self.assertFalse(passed)

    def test_zero_irq_count_fails(self):
        passed, _ = VERIFY.validate_mode(
            internal_log(UARTDM_RX_IRQ_COUNT="0x0"), "internal"
        )
        self.assertFalse(passed)

    def test_internal_evidence_cannot_pass_external_mode(self):
        passed, _ = VERIFY.validate_mode(internal_log(), "external")
        self.assertFalse(passed)


if __name__ == "__main__":
    unittest.main()
